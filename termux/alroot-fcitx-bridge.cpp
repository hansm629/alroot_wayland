/*
 * Alroot Fcitx5 native composition frontend. GPL-3.0-or-later.
 * Runs only as an Alroot-private Fcitx5 addon in the :90 session bus.
 * Receives Unix socket C/T/F/R operations; replies 1 only when a GTK/Qt
 * input context explicitly supports native client-side preedit.
 * Unsupported clients (and committed-text DEL) reply 0, for fallback
 * to the existing XTEST daemon. No clipboard or user text logging.
 */
#include <fcitx/addoninstance.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx-utils/event.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>
#include <array>
#include <iostream>
#include <memory>
#include <string>

namespace fcitx {
class AlrootIMBridge : public AddonInstance {
    Instance *instance_;
    int server_{-1}, client_{-1};
    std::unique_ptr<EventSourceIO> listener_, reader_;
    std::array<unsigned char, 131072> in_;
    size_t len_{0};
    unsigned long handled_{0};
    std::string path_;

    void closeClient() {
        reader_.reset();
        if (client_>=0) close(client_);
        client_=-1;
        len_=0;
    }
    void closeListener() {
        listener_.reset();
        if (server_>=0) close(server_);
        server_=-1;
        if (!path_.empty()) unlink(path_.c_str());
    }
    bool onAccept() {
        int next=accept4(server_,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if(next>=0) {
            closeClient();
            client_=next;
            reader_=instance_->eventLoop().addIOEvent(client_,IOEventFlag::In,
                [this](EventSourceIO*,int,IOEventFlags){
                    readMessages();return true;
                });
        }
        return true;
    }
    bool doMessage(char opcode,const std::string& text) {
        InputContext* focused=nullptr;
        instance_->inputContextManager().foreachFocused(
            [&](InputContext* ic){
                if(ic->hasFocus() && !focused) focused=ic;
                return true;
            });
        if (!focused)
            return false;
        // This is intentionally only for an actual focused Fcitx input
        // context. XTerm, games, Wine and clients without a native IM frontend
        // must retain the existing XTEST fallback.
        if(opcode=='C') {
            if (!focused->capabilityFlags().test(CapabilityFlag::Preedit))
                return false;
            // P11 explicitly requested Underline. The user wants the
            // same plain-text appearance as English, not an underlined
            // composing span. GTK/Qt receive a real preedit and cursor,
            // but no decorative underline formatting.
            Text preedit(text,TextFormatFlag::NoFlag);
            preedit.setCursor(static_cast<int>(text.size()));
            focused->inputPanel().setClientPreedit(preedit);
            focused->updatePreedit();
        } else if(opcode=='T') {
            if (!focused->capabilityFlags().test(CapabilityFlag::Preedit))
                return false;
            focused->commitString(text);
            focused->inputPanel().setClientPreedit(Text{});
            focused->updatePreedit();
        } else if(opcode=='F'||opcode=='R') {
            if (!focused->capabilityFlags().test(CapabilityFlag::Preedit))
                return false;
            focused->inputPanel().setClientPreedit(Text{});
            focused->updatePreedit();
        } else if(opcode=='D') {
            // A native GTK input method can delete committed surrounding
            // text via its own IMContext. Sending a separate XTEST
            // Backspace into a focused Fcitx widget can cause the host IM
            // to reinterpret old composition state and replay English.
            if (!focused->capabilityFlags().test(CapabilityFlag::SurroundingText))
                return false;
            int before=0,after=0;
            if (sscanf(text.c_str(),"%d %d",&before,&after)!=2 ||
                before<0 || before>128 || after<0 || after>128)
                return false;
            if (before+after>0)
                focused->deleteSurroundingText(-before,before+after);
        } else return false;
        handled_++;
        if (handled_ % 64 == 0)
            std::cerr << "ALROOT_FCITX preedit/commit operations=" << handled_
                      << " no client text logged\n";
        return true;
    }
    void readMessages() {
        while(client_>=0) {
            ssize_t n=recv(client_,in_.data()+len_,in_.size()-len_,MSG_DONTWAIT);
            if(n==0){closeClient();break;}
            if(n<0) {
                if(errno!=EAGAIN && errno!=EINTR)closeClient();
                break;
            }
            len_+=n;
            size_t offset=0;
            while(len_-offset>=5) {
                uint32_t count;
                memcpy(&count,in_.data()+offset+1,4);
                count=ntohl(count);
                if(count>16384){closeClient();return;}
                if(len_-offset<count+5)break;
                std::string text(reinterpret_cast<char*>(in_.data()+offset+5),count);
                const bool accepted = doMessage(static_cast<char>(in_[offset]),text);
                const char ack = accepted ? '1' : '0';
                if (send(client_, &ack, 1, MSG_DONTWAIT | MSG_NOSIGNAL) != 1) {
                    closeClient(); return;
                }
                offset+=5+count;
            }
            if(offset) {
                memmove(in_.data(),in_.data()+offset,len_-offset);
                len_-=offset;
            }
        }
    }
public:
    AlrootIMBridge(AddonManager *m):instance_(m->instance()) {
        const char *raw=getenv("ALROOT_FCITX_BRIDGE_SOCKET");
        if(!raw || strlen(raw)>100){std::cerr<<"ALROOT_FCITX socket env not set\n";return;}
        path_=raw;
        server_=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
        if(server_<0)throw std::runtime_error("socket failed");
        sockaddr_un addr{};
        addr.sun_family=AF_UNIX;
        strcpy(addr.sun_path,path_.c_str());
        unlink(path_.c_str());
        umask(0077);
        if(bind(server_,(sockaddr*)&addr,sizeof(addr))<0||
           chmod(path_.c_str(),0600)<0||listen(server_,2)<0)
            throw std::runtime_error("bind/listen failed");
        listener_=instance_->eventLoop().addIOEvent(server_,IOEventFlag::In,
                    [this](EventSourceIO*,int,IOEventFlags){return onAccept();});
        std::cerr << "ALROOT_FCITX_NATIVE_BRIDGE_READY path="
                  <<path_ <<'\n';
    }
    ~AlrootIMBridge() override {closeClient();closeListener();}
};
class AlrootFactory final :public AddonFactory {
public: AddonInstance *create(AddonManager *m) override {
    return new AlrootIMBridge(m);
}};
}
FCITX_ADDON_FACTORY_V2(alrootbridge,fcitx::AlrootFactory)
