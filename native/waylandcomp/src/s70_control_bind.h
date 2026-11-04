/* Bind the credential-receiving control socket in a private runtime. An upgrade
 * can leave a filesystem socket behind after Android kills the previous process.
 * Never connect() the listener: SO_PASSCRED makes even a failed AF_UNIX datagram
 * connect autobind it; a later bind(listener, path) will then fail with EINVAL. */
#ifndef ALROOT_S70_CONTROL_BIND_H
#define ALROOT_S70_CONTROL_BIND_H
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int s70_bind_control_fd(int listener,const char *rt,const struct sockaddr_un *addr,
                               socklen_t len,const char **stage) {
    *stage="bind";
    if(bind(listener,(const struct sockaddr *)addr,len)==0)return 0;
    if(errno!=EADDRINUSE)return -1;
    *stage="serialize stale recovery";
    int dir=open(rt,O_DIRECTORY|O_NOFOLLOW|O_RDONLY|O_CLOEXEC);
    if(dir<0)return -1;
    int rc=-1,probe=-1;
    struct stat a,b;
    if(flock(dir,LOCK_EX|LOCK_NB)<0)goto done;
    if(fstat(dir,&a)<0)goto done;
    if(!S_ISDIR(a.st_mode)||a.st_uid!=getuid()||(a.st_mode&077)){errno=EACCES;goto done;}
    *stage="inspect conflicting node";
    if(fstatat(dir,"s65-root.ctl",&a,AT_SYMLINK_NOFOLLOW)<0)goto done;
    if(!S_ISSOCK(a.st_mode)||a.st_uid!=getuid()){errno=EACCES;goto done;}
    *stage="probe existing socket";
    probe=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(probe<0)goto done;
    if(connect(probe,(const struct sockaddr *)addr,len)==0){errno=EADDRINUSE;goto done;}
    if(errno!=ECONNREFUSED)goto done;
    *stage="recheck stale identity";
    if(fstatat(dir,"s65-root.ctl",&b,AT_SYMLINK_NOFOLLOW)<0)goto done;
    if(a.st_dev!=b.st_dev||a.st_ino!=b.st_ino||!S_ISSOCK(b.st_mode)||b.st_uid!=getuid()){
        errno=ESTALE;goto done;
    }
    *stage="unlink stale socket";
    if(unlinkat(dir,"s65-root.ctl",0)<0)goto done;
    *stage="bind after stale socket";
    rc=bind(listener,(const struct sockaddr *)addr,len);
done:;
    int saved=errno;
    if(probe>=0)close(probe);
    close(dir);errno=saved;return rc;
}
#endif
