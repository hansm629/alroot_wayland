/*
 * Alroot Wayland X11 IME compatibility bridge (bionic arm64).
 * Local AF_UNIX framing: opcode ('C','T','D','F','R'), uint32_be length, UTF-8.
 * A single X11 XTEST connection sends Unicode key events via a dedicated
 * unused keycode. No clipboard/selection changes, xclip, polling or per-event
 * subprocess creation. Scoped to the Alroot rootful Xwayland display, never :0.
 */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_TEXT 16384
#define QUEUE_BYTES 131072
#define KEY_POOL_CAP 32
static volatile sig_atomic_t running = 1;
static Display *display;
static int key_codes[KEY_POOL_CAP], key_count;
static KeySym key_symbols[KEY_POOL_CAP];
static uint64_t key_recency[KEY_POOL_CAP], key_clock;
static int map_width;
static Window focus_at_composition = None;
static int server = -1, client = -1;
static unsigned char incoming[QUEUE_BYTES];
static size_t incoming_len;
static char socket_path[sizeof(((struct sockaddr_un*)0)->sun_path)];
static char composing[MAX_TEXT+1];
static uint64_t count_ok, count_err, output_bytes;
static uint64_t total_ns;

static int64_t ns_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec*1000000000LL+t.tv_nsec;
}
static void shutdown_flag(int sig) { (void)sig; running=0; }

static bool find_unused_keycode(void) {
    int min_code, max_code;
    XDisplayKeycodes(display, &min_code, &max_code);
    int width = 0;
    KeySym *all = XGetKeyboardMapping(display, min_code,
            max_code-min_code+1, &width);
    if (!all || width < 1 || width > 32) {
        if (all) XFree(all);
        return false;
    }
    for (int k=max_code;k>=min_code;k--) {
        bool mapped = false;
        for (int i=0;i<width;i++) {
            if (all[(k-min_code)*width+i]!=NoSymbol) mapped=true;
        }
        if (!mapped && key_count < KEY_POOL_CAP) key_codes[key_count++]=k;
    }
    if (!key_count) { XFree(all); return false; }
    map_width=width;
    XFree(all);
    return true;
}
static void restore_keymap(void) {
    if (display && key_count) {
        KeySym empty[32]={0};
        for (int i=0;i<key_count;i++) {
            if (key_symbols[i]!=NoSymbol)
                XChangeKeyboardMapping(display,key_codes[i],map_width,empty,1);
        }
        XSync(display,False);
    }
}
static void keypress(KeySym symbol) {
    KeyCode key = XKeysymToKeycode(display,symbol);
    if (!key) return;
    XTestFakeKeyEvent(display,key,True,CurrentTime);
    XTestFakeKeyEvent(display,key,False,CurrentTime);
}
static KeyCode unicode_keycode(uint32_t cp) {
    if (cp == '\n' || cp == '\r')
        return XKeysymToKeycode(display, XK_Return);
    if (cp == '\t')
        return XKeysymToKeycode(display, XK_Tab);
    if (cp < 0x20 || cp == 0x7f || cp > 0x10ffff ||
        (cp>=0xd800 && cp<=0xdfff)) return 0;
    KeySym sym=cp>=0x100 ? (KeySym)(0x01000000u|cp) : (KeySym)cp;
    // GTK interprets XTEST keypresses asynchronously. If a single spare
    // keycode is remapped on every Korean syllable, the client can decode
    // earlier events using the LAST syllable's map ("가나다" -> "가다다").
    // Cache one keysym per spare keycode and do not recycle a code while
    // other unused codes are available.
    int slot=-1, empty=-1, oldest=0;
    for (int i=0;i<key_count;i++) {
        if (key_symbols[i]==sym) { slot=i; break; }
        if (key_symbols[i]==NoSymbol && empty<0) empty=i;
        if (key_recency[i]<key_recency[oldest]) oldest=i;
    }
    if (slot<0) {
        slot=empty>=0 ? empty : oldest;
        if (empty<0) {
            // The cache is full. First flush the prior key events to the
            // X server, then give the client a short bounded dispatch time
            // before recycling a keycode. Real keyboard events are paced
            // much more slowly than this synthetic stress-path fallback.
            XSync(display,False);
            usleep(1500);
        }
        KeySym map[32]={0};
        map[0]=sym;
        XChangeKeyboardMapping(display,key_codes[slot],map_width,map,1);
        XSync(display,False);
        key_symbols[slot]=sym;
    }
    key_recency[slot]=++key_clock;
    return (KeyCode)key_codes[slot];
}
static bool unicode_scalar(uint32_t cp) {
    KeyCode code = unicode_keycode(cp);
    if (!code) return false;
    XTestFakeKeyEvent(display,(unsigned)code,True,CurrentTime);
    XTestFakeKeyEvent(display,(unsigned)code,False,CurrentTime);
    // Avoid a blocking round trip for each composing glyph. X11 processes
    // same-client requests in order; process() flushes the complete edit.
    return true;
}
static bool prewarm_first_utf8(const char *text, size_t length) {
    if (!length) return true;
    unsigned char start = (unsigned char)text[0];
    uint32_t cp;
    size_t tails;
    if (start < 0x80) { cp=start; tails=0; }
    else if ((start & 0xe0)==0xc0) { cp=start&0x1f; tails=1; }
    else if ((start & 0xf0)==0xe0) { cp=start&0x0f; tails=2; }
    else if ((start & 0xf8)==0xf0) { cp=start&7; tails=3; }
    else return false;
    if (tails + 1 > length) return false;
    for(size_t j=0;j<tails;j++) {
        unsigned char next=(unsigned char)text[j+1];
        if ((next&0xc0)!=0x80) return false;
        cp=(cp<<6)|(next&0x3f);
    }
    // First install the Unicode keysym while the previous glyph is still
    // visible. Only THEN enqueue its Backspace + replacement keypresses.
    // Otherwise the XChangeKeyboardMapping/XSync during unicode_scalar()
    // flushed an isolated Backspace frame, making the caret flash.
    return unicode_keycode(cp) != 0;
}
static bool send_utf8(const char *s, size_t size) {
    size_t i=0;
    while (i<size) {
        uint32_t code;
        unsigned char c=s[i++];
        int tail=0;
        if (c<0x80) code=c;
        else if ((c&0xe0)==0xc0) { code=c&0x1f;tail=1; }
        else if ((c&0xf0)==0xe0) { code=c&0x0f;tail=2; }
        else if ((c&0xf8)==0xf0) { code=c&7;tail=3; }
        else return false;
        if (i+tail>size) return false;
        int old_tail=tail;
        while (tail--) {
            unsigned char p=s[i++];
            if ((p&0xc0)!=0x80) return false;
            code=(code<<6)|(p&0x3f);
        }
        if ((old_tail==1 && code<0x80) ||
            (old_tail==2 && code<0x800) ||
            (old_tail==3 && code<0x10000)) return false;
        if (!unicode_scalar(code)) return false;
    }
    // Defer output flush to process(), keeping erase+replacement together.
    return true;
}
static size_t common_prefix(const char *a,const char *b) {
    size_t i=0,safe=0;
    while(a[i] && b[i] && a[i]==b[i]) {
        i++;
        if ((((unsigned char)a[i])&0xc0)!=0x80) safe=i;
    }
    return safe;
}
static int characters(const char *s,size_t n) {
    int count=0;
    for(size_t i=0;i<n;i++)
        if ((((unsigned char)s[i])&0xc0)!=0x80) count++;
    return count;
}
static void erase(size_t count,KeySym symbol) {
    if(count>128)count=128;
    for(size_t i=0;i<count;i++)keypress(symbol);
    // Keep Backspace buffered until its replacement is enqueued.
}
static Window current_focus(void) {
    Window win=None;
    int state=0;
    XGetInputFocus(display,&win,&state);
    return win;
}
static void process(char op, const char *text, size_t len) {
    int64_t start=ns_now();
    bool good=true;
    if (op=='C' || op=='T') {
        Window focus=current_focus();
        if (focus != focus_at_composition && composing[0]) composing[0]=0;
        if (op=='C') focus_at_composition=focus;
        size_t prior=common_prefix(composing,text);
        if (op=='T' && len==0) {
            composing[0]=0;
        } else {
            if (len>prior)
                good=prewarm_first_utf8(text+prior,len-prior);
            if (good && composing[prior])
                erase(characters(composing+prior,
                                 strlen(composing+prior)),XK_BackSpace);
            if(good && len>prior)good=send_utf8(text+prior,len-prior);
            if(good) {
                if(op=='C')memcpy(composing,text,len+1);
                else composing[0]=0;
            }
        }
    } else if(op=='D') {
        unsigned before=0,after=0;
        if (sscanf(text,"%u %u",&before,&after)==2 && before<=128 && after<=128) {
            erase(before,XK_BackSpace);erase(after,XK_Delete);
            composing[0]=0;
        } else good=false;
    } else if(op=='F' || op=='R') {
        composing[0]=0;
    } else good=false;
    XFlush(display);
    if(good)count_ok++;else count_err++;
    output_bytes+=len;
    total_ns+=(uint64_t)(ns_now()-start);
    if((count_ok+count_err)%40==0) {
        fprintf(stderr,"IME-SERVER events=%llu failed=%llu avg-delivery-ms=%.3f bytes=%llu clipboard=UNTOUCHED\n",
                (unsigned long long)count_ok,(unsigned long long)count_err,
                total_ns/1000000.0/(count_ok+count_err),
                (unsigned long long)output_bytes);
    }
}
static void client_close(void) {
    if(client>=0)close(client);
    client=-1;incoming_len=0;composing[0]=0;
}
static bool receive_packets(void) {
    ssize_t count=recv(client,incoming+incoming_len,
            sizeof(incoming)-incoming_len,MSG_DONTWAIT);
    if(count==0)return false;
    if(count<0)return errno==EINTR || errno==EAGAIN;
    incoming_len+=(size_t)count;
    size_t p=0;
    while(incoming_len-p>=5) {
        uint32_t size=0;
        memcpy(&size,incoming+p+1,4);
        size=ntohl(size);
        if(size>MAX_TEXT)return false;
        if(incoming_len-p<size+5)break;
        char payload[MAX_TEXT+1];
        memcpy(payload,incoming+p+5,size);
        payload[size]=0;
        process((char)incoming[p],payload,size);
        p+=size+5;
    }
    if(p) {
        memmove(incoming,incoming+p,incoming_len-p);
        incoming_len-=p;
    }
    return incoming_len<sizeof(incoming);
}
int main(int argc,char **argv) {
    const char *name=getenv("DISPLAY");
    if(argc!=2 || !name || strcmp(name,":0")==0 ||
        strlen(argv[1])>=sizeof(socket_path)) {
        fprintf(stderr,"IME-SERVER requires a private Alroot Xwayland display\n");
        return 2;
    }
    strcpy(socket_path,argv[1]);
    signal(SIGINT,shutdown_flag);
    signal(SIGTERM,shutdown_flag);
    display=XOpenDisplay(NULL);
    if(!display || !find_unused_keycode()) {
        fprintf(stderr,"IME-SERVER no spare X11 keycodes, refusing clipboard fallback\n");
        return 3;
    }
    server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(server<0)return 3;
    struct sockaddr_un addr={0};
    addr.sun_family=AF_UNIX;
    strcpy(addr.sun_path,socket_path);
    unlink(socket_path);
    umask(0077);
    if(bind(server,(struct sockaddr*)&addr,sizeof(addr))<0 ||
        chmod(socket_path,0600)!=0 || listen(server,2)<0)
        return 3;
    fprintf(stderr,"IME-SERVER XTEST Unicode ready spare-keycodes=%d clipboard=UNTOUCHED\n",key_count);
    while(running) {
        struct pollfd fds[2]={
            {.fd=server,.events=POLLIN},
            {.fd=client,.events=POLLIN}
        };
        int rc=poll(fds,2,25);
        if(rc<0 && errno!=EINTR)break;
        if(rc>0 && (fds[0].revents&POLLIN)) {
            int next=accept4(server,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
            if(next>=0) { client_close();client=next; }
        }
        if(client>=0 && (fds[1].revents&(POLLIN|POLLHUP|POLLERR)) &&
            !receive_packets())client_close();
    }
    client_close();
    if(server>=0)close(server);
    unlink(socket_path);
    restore_keymap();
    XCloseDisplay(display);
    fprintf(stderr,"IME-SERVER stopped events=%llu failures=%llu\n",
            (unsigned long long)count_ok,(unsigned long long)count_err);
    return 0;
}
