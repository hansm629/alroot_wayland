/* S16 cross-libc DRI3 shared-fence safety test.  No user X session required.
 * On Android/Bionic, the native libxshmfence creates a 96-byte pthread fence.
 * On Debian/glibc, memfd_create+ftruncate(4) is a 4-byte shared-futex fence.
 * The Alroot private Xwayland-only shim must handle both without UB and
 * with genuine cross-process synchronization (not fake always-signaled).
 */
#define _GNU_SOURCE
#include <X11/xshmfence.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "bionic96") && strcmp(argv[1], "glibc4") &&
                      strcmp(argv[1], "invalid8"))) {
        fprintf(stderr, "usage: %s bionic96|glibc4|invalid8\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!strcmp(argv[1], "invalid8")) {
        /* Invalid/unknown ABI must FAIL CLOSED rather than interpret a
         * random 8-byte shared region as a pthread mutex or futex. */
        int badfd = memfd_create("xshmfence", MFD_CLOEXEC);
        if (badfd < 0 || ftruncate(badfd, 8) != 0) return 15;
        errno = 0;
        struct xshmfence *unexpected = xshmfence_map_shm(badfd);
        int saved_errno = errno;
        close(badfd);
        if (unexpected || saved_errno != EINVAL) {
            fprintf(stderr, "S16_DUALABI_INVALID8_REJECTED=FAIL errno:%d\n", saved_errno);
            return 16;
        }
        puts("S16_DUALABI_INVALID8_REJECTED=PASS");
        return 0;
    }
    int glibc4 = !strcmp(argv[1], "glibc4");
    int fd = glibc4 ? memfd_create("xshmfence", MFD_CLOEXEC) : xshmfence_alloc_shm();
    if (fd < 0) { perror("alloc"); return 3; }
    if (glibc4 && ftruncate(fd, 4) != 0) { perror("ftruncate"); return 4; }
    struct stat st = {0};
    if (fstat(fd, &st) != 0 || st.st_size != (glibc4 ? 4 : 96)) {
        fprintf(stderr, "S16_DUALABI_UNEXPECTED_SIZE=%lld\n", (long long)st.st_size);
        return 5;
    }
    struct xshmfence *f = xshmfence_map_shm(fd);
    if (!f) { perror("map_shm"); return 6; }
    xshmfence_reset(f);
    if (xshmfence_query(f) != 0) return 7;
    if (xshmfence_trigger(f) != 0) return 8;
    if (xshmfence_query(f) != 1 || xshmfence_await(f) != 0) return 9;
    xshmfence_reset(f);
    if (xshmfence_query(f) != 0) return 10;
    printf("S16_DUALABI_%s_NATIVE_STATE_MACHINE=PASS\n", argv[1]);

    pid_t child = fork();
    if (child < 0) { perror("fork"); return 11; }
    if (child == 0) {
        int result = xshmfence_await(f);
        _exit(result == 0 ? 0 : 12);
    }
    usleep(50000);
    if (xshmfence_trigger(f) != 0) return 13;
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 14;
    printf("S16_DUALABI_%s_CROSS_PROCESS_WAKE=PASS\n", argv[1]);
    xshmfence_unmap_shm(f);
    close(fd);
    return 0;
}
