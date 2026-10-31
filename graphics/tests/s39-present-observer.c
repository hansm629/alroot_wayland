/* SPDX-License-Identifier: MIT
 * Observe Present completion on the caller-supplied isolated demo window.
 * No PresentPixmap, redraw, resize, redirect, or client event-mask changes.
 * Build: clang -O2 -std=gnu11 -Wall -Wextra -Werror this.c -lxcb -lxcb-present
 * Run under the test DISPLAY/XAUTHORITY: observer WINDOW_XID [seconds=7]
 * Counts describe X Present completion, not Android physical presentation.
 */
#define _POSIX_C_SOURCE 200809L
#include <xcb/xcb.h>
#include <xcb/present.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

struct counts {
    uint64_t complete, copy, flip, skip, other, msc_notify, config;
    uint32_t first_serial, last_serial;
    uint64_t first_msc, last_msc, last_ust;
    unsigned last_mode;
};

static void emit(const struct counts *c, int64_t elapsed, int64_t since_last,
                 uint64_t total, unsigned width, unsigned height)
{
    printf("S39_PRESENT elapsed_ms=%" PRId64 " extent=%ux%u completes=%" PRIu64
           " copy=%" PRIu64 " flip=%" PRIu64 " skip=%" PRIu64 " other=%" PRIu64
           " msc_notify=%" PRIu64 " configure=%" PRIu64 " first_serial=%" PRIu32
           " last_serial=%" PRIu32 " first_msc=%" PRIu64 " last_msc=%" PRIu64
           " last_ust=%" PRIu64 " last_mode=%u since_last_complete_ms=%" PRId64
           " total_completes=%" PRIu64 "\n",
           elapsed, width, height, c->complete, c->copy, c->flip, c->skip, c->other,
           c->msc_notify, c->config, c->first_serial, c->last_serial, c->first_msc,
           c->last_msc, c->last_ust, c->last_mode, since_last, total);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s owned-isolated-window-xid [seconds=7]\n", argv[0]);
        return 2;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(argv[1], &end, 0);
    if (!parsed || *end || parsed > UINT32_MAX) return 2;
    xcb_window_t window = (xcb_window_t)parsed;
    double duration = argc == 3 ? strtod(argv[2], &end) : 7.0;
    if ((argc == 3 && *end) || !(duration > 0.0 && duration <= 30.0)) return 2;
    xcb_connection_t *connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) {
        fprintf(stderr, "S39_PRESENT_ERROR=connect\n");
        xcb_disconnect(connection);
        return 3;
    }
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(connection, &xcb_present_id);
    if (!ext || !ext->present) {
        fprintf(stderr, "S39_PRESENT_ERROR=missing_extension\n");
        xcb_disconnect(connection);
        return 3;
    }
    xcb_generic_error_t *error = NULL;
    xcb_present_query_version_reply_t *version = xcb_present_query_version_reply(
        connection, xcb_present_query_version(connection, 1, 0), &error);
    if (error || !version) {
        fprintf(stderr, "S39_PRESENT_ERROR=query_version code=%u\n", error ? (unsigned)error->error_code : 0);
        free(error); free(version); xcb_disconnect(connection);
        return 3;
    }
    unsigned major = version->major_version, minor = version->minor_version;
    free(version);
    xcb_present_event_t event_id = xcb_generate_id(connection);
    error = xcb_request_check(connection, xcb_present_select_input_checked(
        connection, event_id, window,
        XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY | XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY));
    if (error) {
        fprintf(stderr, "S39_PRESENT_ERROR=select_input code=%u\n", (unsigned)error->error_code);
        free(error); xcb_disconnect(connection);
        return 4;
    }
    xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(
        connection, xcb_get_geometry(connection, window), &error);
    if (error || !geometry) {
        fprintf(stderr, "S39_PRESENT_ERROR=geometry code=%u\n", error ? (unsigned)error->error_code : 0);
        free(error); free(geometry); xcb_disconnect(connection);
        return 4;
    }
    unsigned width = geometry->width, height = geometry->height;
    free(geometry);
    int64_t start = now_ms(), deadline = start + (int64_t)(duration * 1000), next_report = start + 500;
    int64_t last_complete = start, max_gap = 0;
    uint64_t total = 0, total_config = 0;
    struct counts bucket = {0};
    int failed = 0;
    printf("S39_PRESENT_READY xid=0x%x event_id=0x%x version=%u.%u extent=%ux%u period_ms=500 start_monotonic_ms=%" PRId64 "\n",
           window, event_id, major, minor, width, height, start);
    fflush(stdout);
    while (now_ms() < deadline && !failed) {
        xcb_generic_event_t *event;
        /* Bound each drain pass so an event flood cannot suppress reports. */
        unsigned drained = 0;
        while (drained++ < 1024 && (event = xcb_poll_for_event(connection)) != NULL) {
            unsigned type = event->response_type & 0x7f;
            if (type == 0) {
                xcb_generic_error_t *e = (xcb_generic_error_t *)event;
                fprintf(stderr, "S39_PRESENT_ERROR=asynchronous code=%u\n", (unsigned)e->error_code);
                failed = 1;
            } else if (type == XCB_GE_GENERIC) {
                xcb_present_generic_event_t *generic = (xcb_present_generic_event_t *)event;
                if (generic->extension == ext->major_opcode && generic->event == event_id) {
                    if (generic->evtype == XCB_PRESENT_COMPLETE_NOTIFY) {
                        xcb_present_complete_notify_event_t *complete = (xcb_present_complete_notify_event_t *)event;
                        if (complete->window == window && complete->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP) {
                            int64_t now = now_ms(), gap = now - last_complete;
                            if (gap > max_gap) max_gap = gap;
                            last_complete = now;
                            if (!bucket.complete) {
                                bucket.first_serial = complete->serial;
                                bucket.first_msc = complete->msc;
                            }
                            bucket.complete++; total++;
                            bucket.last_serial = complete->serial;
                            bucket.last_msc = complete->msc;
                            bucket.last_ust = complete->ust;
                            bucket.last_mode = complete->mode;
                            switch (complete->mode) {
                            case XCB_PRESENT_COMPLETE_MODE_COPY: bucket.copy++; break;
                            case XCB_PRESENT_COMPLETE_MODE_FLIP: bucket.flip++; break;
                            case XCB_PRESENT_COMPLETE_MODE_SKIP: bucket.skip++; break;
                            default: bucket.other++; break;
                            }
                        } else if (complete->window == window) {
                            bucket.msc_notify++;
                        }
                    } else if (generic->evtype == XCB_PRESENT_CONFIGURE_NOTIFY) {
                        xcb_present_configure_notify_event_t *configure = (xcb_present_configure_notify_event_t *)event;
                        if (configure->window == window) {
                            width = configure->width; height = configure->height;
                            bucket.config++; total_config++;
                        }
                    }
                }
            }
            free(event);
        }
        int64_t now = now_ms();
        if (now >= next_report) {
            emit(&bucket, now - start, now - last_complete, total, width, height);
            memset(&bucket, 0, sizeof(bucket));
            do { next_report += 500; } while (next_report <= now);
        }
        if (xcb_connection_has_error(connection)) {
            fprintf(stderr, "S39_PRESENT_ERROR=connection_lost\n"); failed = 1; break;
        }
        int64_t wake = next_report < deadline ? next_report : deadline;
        int wait_ms = (int)(wake - now);
        if (wait_ms < 0) wait_ms = 0;
        if (drained > 1024) wait_ms = 0;
        struct pollfd descriptor = {xcb_get_file_descriptor(connection), POLLIN, 0};
        if (poll(&descriptor, 1, wait_ms) < 0 && errno != EINTR) {
            fprintf(stderr, "S39_PRESENT_ERROR=poll\n"); failed = 1;
        }
    }
    int64_t finish = now_ms();
    if (bucket.complete || bucket.config || finish < next_report)
        emit(&bucket, finish - start, finish - last_complete, total, width, height);
    if (finish - last_complete > max_gap) max_gap = finish - last_complete;
    printf("S39_PRESENT_SUMMARY completes=%" PRIu64 " configure=%" PRIu64
           " max_receive_gap_ms=%" PRId64 " observer_error=%d physical_presentation=NOT_MEASURED\n",
           total, total_config, max_gap, failed);
    xcb_present_select_input(connection, event_id, window, 0);
    xcb_flush(connection);
    xcb_disconnect(connection);
    return failed ? 5 : 0;
}
