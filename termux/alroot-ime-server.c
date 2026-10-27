/*
 * Alroot rootful-Xwayland text input bridge (Termux/bionic).
 * One persistent AF_UNIX connection and one persistent Xlib/XTEST connection.
 * Protocol: opcode byte + 32-bit big-endian payload length + UTF-8 payload.
 * C=composition, T=committed text, D=delete (ASCII "before after"),
 * F=finish composition, R=reset. Never listens on TCP or DISPLAY=:0.
 *
 * GPLv3-compatible Alroot project source. Not part of Termux-X11.
 */
#define _GNU_SOURCE
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#define SOCKET_BACKLOG 2
#define RESTORE_MS 120
static volatile sig_atomic_t running = 1;
static Display *dpy;
static Window window;
static Window transient_window;
static Atom clipboard, utf8, targets, property;
static char *saved_clip;
static size_t saved_len;
static char *offered;
static size_t offered_len;
static bool awaiting_paste;
static bool clipboard_owned;
static int64_t restore_at;
static char composition[MAX_TEXT + 1];
static uint8_t input[131072];
static size_t input_len;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static char pending_marker[1024];
static int client = -1, server = -1;
static uint64_t delivered, failed, input_bytes, total_delivery_ns;

static int64_t clock_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static void on_signal(int code) { (void)code; running = 0; }
static void swap_copy(char **p, size_t *len, const void *src, size_t n) {
    char *next = malloc(n + 1);
    if (!next) return;
    if (n) memcpy(next, src, n);
    next[n] = 0;
    free(*p);
    *p = next;
    *len = n;
}

static void keycode(KeySym symbol, bool press) {
    KeyCode code = XKeysymToKeycode(dpy, symbol);
    if (code) XTestFakeKeyEvent(dpy, code, press, CurrentTime);
}
static void tap(KeySym symbol) {
    keycode(symbol, true);
    keycode(symbol, false);
}
static void delete_chars(size_t count, KeySym symbol) {
    if (count > 128) count = 128;
    for (size_t i = 0; i < count; i++) tap(symbol);
    XFlush(dpy);
}
static int codepoints(const char *s, size_t n) {
    int count = 0;
    for (size_t i = 0; i < n; i++)
        if ((((unsigned char)s[i]) & 0xc0) != 0x80) count++;
    return count;
}
/* Never split a UTF-8 scalar when computing prefix differences. */
static size_t common_prefix(const char *a, const char *b) {
    size_t at = 0, boundary = 0;
    while (a[at] && b[at] && a[at] == b[at]) {
        ++at;
        if ((((unsigned char)a[at]) & 0xc0) != 0x80) boundary = at;
    }
    return boundary;
}
static bool ascii_simple(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126)
            return false;
    return true;
}
static void ascii_type(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        KeySym symbol = c;
        bool shifted = c >= 'A' && c <= 'Z';
        if (shifted) keycode(XK_Shift_L, true);
        tap(symbol);
        if (shifted) keycode(XK_Shift_L, false);
    }
    XFlush(dpy);
}
static bool terminal_focus(void) {
    Window focus = None, parent, root, *children = NULL;
    int revert;
    XGetInputFocus(dpy, &focus, &revert);
    for (int i = 0; i < 24 && focus != None && focus != PointerRoot; i++) {
        XClassHint hint = {0};
        if (XGetClassHint(dpy, focus, &hint)) {
            bool terminal = false;
            const char *c = hint.res_class ? hint.res_class : "";
            const char *n = hint.res_name ? hint.res_name : "";
            if (strcasestr(c, "terminal") || strcasestr(c, "xterm") ||
                strcasestr(c, "konsole") || strcasestr(n, "terminal") ||
                strcasestr(n, "xterm") || strcasestr(n, "konsole"))
                terminal = true;
            if (hint.res_name) XFree(hint.res_name);
            if (hint.res_class) XFree(hint.res_class);
            if (terminal) return true;
        }
        unsigned int count = 0;
        if (!XQueryTree(dpy, focus, &root, &parent, &children, &count)) break;
        if (children) XFree(children);
        if (parent == None || parent == focus) break;
        focus = parent;
    }
    return false;
}
static void paste_shortcut(void) {
    bool terminal = terminal_focus();
    if (getenv("ALROOT_IME_DEBUG"))
        fprintf(stderr, "IME-SERVER focus terminal=%d\n", terminal);
    keycode(XK_Control_L, true);
    if (terminal) keycode(XK_Shift_L, true);
    tap(XK_v);
    if (terminal) keycode(XK_Shift_L, false);
    keycode(XK_Control_L, false);
    XFlush(dpy);
}
/* Event handling uses X11's actual SelectionRequest acknowledgement. */
static bool handle_event(XEvent *e) {
    if (e->type != SelectionRequest) return false;
    XSelectionRequestEvent *r = &e->xselectionrequest;
    if (r->selection != clipboard) return false;
    if (getenv("ALROOT_IME_DEBUG")) {
        char *name = XGetAtomName(dpy, r->target);
        fprintf(stderr, "IME-SERVER SelectionRequest target=%s requestor=0x%lx\n",
                name ? name : "(null)", (unsigned long)r->requestor);
        if (name) XFree(name);
    }
    XSelectionEvent reply = {0};
    reply.type = SelectionNotify;
    reply.display = r->display;
    reply.requestor = r->requestor;
    reply.selection = r->selection;
    reply.target = r->target;
    reply.time = r->time;
    reply.property = None;
    Atom target = r->property != None ? r->property : r->target;
    if (r->target == targets) {
        Atom available[] = {targets, utf8, XA_STRING};
        XChangeProperty(dpy, r->requestor, target, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)available, 3);
        reply.property = target;
    } else if (r->target == utf8 || r->target == XA_STRING) {
        const char *payload = offered ? offered : "";
        size_t len = offered ? offered_len : 0;
        if (len < 1048576) {
            XChangeProperty(dpy, r->requestor, target, r->target, 8,
                            PropModeReplace, (const unsigned char *)payload, len);
            reply.property = target;
            awaiting_paste = false;
        }
    }
    XSendEvent(dpy, r->requestor, False, 0, (XEvent *)&reply);
    XFlush(dpy);
    return true;
}
static void xevents(void) {
    while (XPending(dpy)) {
        XEvent e;
        XNextEvent(dpy, &e);
        handle_event(&e);
    }
}
static bool read_previous_clipboard(void) {
    if (XGetSelectionOwner(dpy, clipboard) == None) {
        swap_copy(&saved_clip, &saved_len, "", 0);
        return true;
    }
    if (XGetSelectionOwner(dpy, clipboard) == window) return true;
    XConvertSelection(dpy, clipboard, utf8, property, window, CurrentTime);
    XFlush(dpy);
    int64_t until = clock_ns() + 55000000LL;
    while (clock_ns() < until) {
        while (XPending(dpy)) {
            XEvent e;
            XNextEvent(dpy, &e);
            if (e.type == SelectionNotify && e.xselection.requestor == window) {
                if (e.xselection.property == None) return false;
                Atom actual; int format; unsigned long count, extra;
                unsigned char *data = NULL;
                if (XGetWindowProperty(dpy, window, property, 0, 262144,
                        True, AnyPropertyType, &actual, &format, &count, &extra,
                        &data) == Success && format == 8 && extra == 0 &&
                        count <= 1048576 && data) {
                    swap_copy(&saved_clip, &saved_len, data, count);
                    XFree(data);
                    return true;
                }
                if (data) XFree(data);
                return false;
            }
            handle_event(&e);
        }
        usleep(1000);
    }
    return false;
}
static void restore_clipboard(void) {
    if (!clipboard_owned) return;
    Window current = XGetSelectionOwner(dpy, clipboard);
    if (current != window && current != transient_window) {
        /* Never undo a genuine clipboard change made by another app. */
        clipboard_owned = false;
        awaiting_paste = false;
        restore_at = 0;
        unlink(pending_marker);
        return;
    }
    swap_copy(&offered, &offered_len, saved_clip ? saved_clip : "", saved_len);
    XSetSelectionOwner(dpy, clipboard, window, CurrentTime);
    XFlush(dpy);
    awaiting_paste = false;
    restore_at = 0;
    unlink(pending_marker);
    /* We remain the selection owner, but keep the user's previous UTF-8
       content rather than leaving the transient IME syllable on clipboard. */
}
static bool type_unicode(const char *bytes, size_t n) {
    if (n == 0) return true;
    if (ascii_simple(bytes, n)) {
        ascii_type(bytes, n);
        return true;
    }
    Window old_owner = XGetSelectionOwner(dpy, clipboard);
    if (old_owner != window && old_owner != transient_window) {
        /* The previous clipboard owner may have changed while the IME
           daemon was idle. Snapshot it anew rather than restoring stale
           clipboard contents from a prior composition. */
        clipboard_owned = false;
    }
    if (!clipboard_owned && !read_previous_clipboard()) {
        /* Clipboard cannot be captured; protect the user's data. */
        return false;
    }
    swap_copy(&offered, &offered_len, bytes, n);
    clipboard_owned = true;
    int marker = open(pending_marker, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
    if (marker >= 0) close(marker);
    /* Each UTF-8 paste uses a *different* X11 SelectionOwner window from
       the restored clipboard. VTE caches clipboard owners and may otherwise
       paste the OLD, potentially large text even though the server offers
       the new string on the same owner window. */
    XSetSelectionOwner(dpy, clipboard, transient_window, CurrentTime);
    if (XGetSelectionOwner(dpy, clipboard) != transient_window) return false;
    /* Let VTE process the clipboard owner transition before injecting
       Ctrl+Shift+V, without stalling the Android UI thread. */
    if (terminal_focus()) {
        XSync(dpy, False);
        usleep(8000);
    }
    awaiting_paste = true;
    restore_at = clock_ns() + RESTORE_MS * 1000000LL;
    paste_shortcut();
    /* Respond to the actual selection request instead of sleeping 350ms
       unconditionally. The polling is bounded; when it times out, restore
       the user's clipboard and report a failed X selection delivery. */
    int64_t until = clock_ns() + 65000000LL;
    while (awaiting_paste && clock_ns() < until) {
        struct pollfd pfd = {.fd = XConnectionNumber(dpy), .events = POLLIN};
        poll(&pfd, 1, 3);
        xevents();
    }
    bool handled = !awaiting_paste;
    if (handled) {
        // VTE often makes more than one UTF8_STRING request in the paste
        // pipeline. Its first request merely checks the selection, while a
        // later read inserts the text. Keep our transient UTF-8 selection
        // alive for a short bounded completion interval, unlike the
        // original 350ms sleep *per syllable*.
        int64_t grace = terminal_focus() ? 22000000LL : 8000000LL;
        int64_t done = clock_ns() + grace;
        while (clock_ns() < done) {
            struct pollfd pfd = {.fd = XConnectionNumber(dpy), .events = POLLIN};
            poll(&pfd, 1, 3);
            xevents();
        }
    }
    restore_clipboard();
    return handled;
}
static void apply_text(char operation, const char *s, size_t n) {
    int64_t start = clock_ns();
    bool ok = true;
    if (operation == 'C') {
        if (!strcmp(composition, s)) return;
        size_t prefix = common_prefix(composition, s);
        delete_chars(codepoints(composition + prefix,
                        strlen(composition + prefix)), XK_BackSpace);
        ok = type_unicode(s + prefix, n - prefix);
        if (ok) {
            memcpy(composition, s, n);
            composition[n] = '\0';
        }
    } else if (operation == 'T') {
        if (!n) {
            composition[0] = '\0';
        } else {
            size_t prefix = common_prefix(composition, s);
            delete_chars(codepoints(composition + prefix,
                        strlen(composition + prefix)), XK_BackSpace);
            ok = type_unicode(s + prefix, n - prefix);
            if (ok) composition[0] = '\0';
        }
    } else if (operation == 'D') {
        unsigned before = 0, after = 0;
        if (sscanf(s, "%u %u", &before, &after) == 2 &&
                before <= 128 && after <= 128) {
            delete_chars(before, XK_BackSpace);
            delete_chars(after, XK_Delete);
            composition[0] = '\0';
        } else ok = false;
    } else if (operation == 'F' || operation == 'R') {
        composition[0] = '\0';
    } else ok = false;
    input_bytes += n;
    if (ok) delivered++; else failed++;
    total_delivery_ns += clock_ns() - start;
    if (delivered + failed > 0 && (delivered + failed) % 40 == 0) {
        fprintf(stderr,
                "IME-SERVER events=%llu failed=%llu avg-delivery-ms=%.2f data-bytes=%llu\n",
                (unsigned long long)delivered, (unsigned long long)failed,
                total_delivery_ns / 1000000. / (delivered + failed),
                (unsigned long long)input_bytes);
    }
}
static void close_client(void) {
    if (client >= 0) close(client);
    client = -1; input_len = 0;
    composition[0] = '\0';
}
static bool drain_client(void) {
    ssize_t n = recv(client, input + input_len, sizeof(input) - input_len, MSG_DONTWAIT);
    if (n == 0) return false;
    if (n < 0) return errno == EINTR || errno == EAGAIN;
    input_len += n;
    size_t offset = 0;
    while (input_len - offset >= 5) {
        uint32_t payload;
        memcpy(&payload, input + offset + 1, 4);
        payload = ntohl(payload);
        if (payload > MAX_TEXT || input_len + payload > sizeof(input))
            return false;
        if (input_len - offset < payload + 5) break;
        char text[MAX_TEXT + 1];
        if (payload) memcpy(text, input + offset + 5, payload);
        text[payload] = '\0';
        apply_text((char)input[offset], text, payload);
        offset += payload + 5;
    }
    if (offset) {
        memmove(input, input + offset, input_len - offset);
        input_len -= offset;
    }
    return input_len < sizeof(input);
}
int main(int argc, char **argv) {
    if (argc != 2 || !getenv("DISPLAY") || !strcmp(getenv("DISPLAY"), ":0")) {
        fprintf(stderr, "IME-SERVER only accepts private Alroot Xwayland\n");
        return 2;
    }
    if (strlen(argv[1]) >= sizeof(socket_path)) return 2;
    strcpy(socket_path, argv[1]);
    if (snprintf(pending_marker, sizeof(pending_marker), "%s.active", socket_path)
            >= (int)sizeof(pending_marker)) return 2;
    unlink(pending_marker); // avoid leaving the legacy clipboard broker paused after a crash
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "IME-SERVER cannot open X display\n"); return 3; }
    window = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1,
                                0, 0, 0);
    transient_window = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1,
                                           0, 0, 0);
    clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    targets = XInternAtom(dpy, "TARGETS", False);
    property = XInternAtom(dpy, "_ALROOT_IME_SAVED", False);
    server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (server < 0) return 3;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, socket_path);
    unlink(socket_path);
    umask(0077);
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        chmod(socket_path, 0600) != 0 || listen(server, SOCKET_BACKLOG) < 0)
        return 3;
    fprintf(stderr, "IME-SERVER ready: private socket, persistent X11 connection\n");
    while (running) {
        struct pollfd fds[3] = {
            {.fd = server, .events = POLLIN},
            {.fd = XConnectionNumber(dpy), .events = POLLIN},
            {.fd = client, .events = POLLIN}
        };
        if (restore_at && clock_ns() >= restore_at) restore_clipboard();
        int r = poll(fds, 3, 12);
        if (r < 0 && errno != EINTR) break;
        if (r > 0 && (fds[0].revents & POLLIN)) {
            int next = accept4(server, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (next >= 0) { close_client(); client = next; }
        }
        if (client >= 0 && (fds[2].revents & (POLLIN | POLLERR | POLLHUP)) &&
                !drain_client()) close_client();
        xevents();
    }
    restore_clipboard();
    unlink(pending_marker);
    close_client();
    close(server);
    unlink(socket_path);
    free(saved_clip); free(offered);
    XDestroyWindow(dpy, window);
    XDestroyWindow(dpy, transient_window);
    XCloseDisplay(dpy);
    fprintf(stderr, "IME-SERVER stopped events=%llu failures=%llu\n",
            (unsigned long long)delivered, (unsigned long long)failed);
    return 0;
}
