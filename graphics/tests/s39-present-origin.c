/* SPDX-License-Identifier: MIT
 * Own-window coordinate regression for Alroot's S38 resize snapshot.
 *
 * Build on the device:
 *   clang -O2 -std=gnu11 -Wall -Wextra -Werror this.c -lxcb -lxcb-present
 * Run on an ISOLATED test DISPLAY/XAUTHORITY, not the user's live desktop.
 * Default: exit 0 only when landmarks retain their Present (top-left) origin.
 * --expect-centered: exit 0 only when the old center/crop signature is observed.
 *
 * This creates, presents, resizes and reads only its own override-redirect
 * window. It does not redirect/capture other windows or synthesize input.
 * Pixel readback is test-only. No whole-image or geometry-dependent hashes.
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

#define LANDMARKS 3
#define CHECK_PHASES 4

static const int point_x[LANDMARKS] = {60, 180, 320};
static const int point_y[LANDMARKS] = {60, 110, 300};
static const uint32_t colors[2][LANDMARKS] = {
    {0xe02040, 0x20e040, 0x2040e0},
    {0xe0c020, 0xe020e0, 0x20c0e0}
};
static const uint32_t backgrounds[2] = {0x122438, 0x284254};

struct context {
    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_visualtype_t visual;
    xcb_window_t window;
    xcb_present_event_t event_id;
    uint8_t present_opcode;
    unsigned bpp;
    int msb_first;
    unsigned width, height;
    int failed;
};

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int check(struct context *c, xcb_void_cookie_t cookie, const char *operation)
{
    xcb_generic_error_t *error = xcb_request_check(c->conn, cookie);
    if (!error)
        return 1;
    fprintf(stderr, "S39_ORIGIN_ERROR operation=%s code=%u major=%u minor=%u resource=0x%x\n",
            operation, (unsigned)error->error_code, (unsigned)error->major_code,
            (unsigned)error->minor_code, error->resource_id);
    free(error);
    c->failed = 1;
    return 0;
}

static unsigned shift_for(uint32_t mask)
{
    unsigned shift = 0;
    while (mask && !(mask & 1)) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

static uint32_t component_to_pixel(unsigned value, uint32_t mask)
{
    unsigned shift = shift_for(mask);
    uint32_t maximum = mask >> shift;
    return (uint32_t)(((uint64_t)value * maximum + 127) / 255) << shift;
}

static unsigned component_from_pixel(uint32_t pixel, uint32_t mask)
{
    unsigned shift = shift_for(mask);
    uint32_t maximum = mask >> shift;
    return maximum ? (unsigned)((((uint64_t)(pixel & mask) >> shift) * 255 + maximum / 2) / maximum) : 0;
}

static uint32_t pixel_from_rgb(const struct context *c, uint32_t rgb)
{
    return component_to_pixel((rgb >> 16) & 255, c->visual.red_mask) |
           component_to_pixel((rgb >> 8) & 255, c->visual.green_mask) |
           component_to_pixel(rgb & 255, c->visual.blue_mask);
}

static int near_color(uint32_t actual, uint32_t expected)
{
    for (unsigned shift = 0; shift < 24; shift += 8)
        if (abs((int)((actual >> shift) & 255) - (int)((expected >> shift) & 255)) > 1)
            return 0;
    return 1;
}

static int read_pixel(struct context *c, int x, int y, uint32_t *rgb)
{
    if (x < 0 || y < 0 || (unsigned)x >= c->width || (unsigned)y >= c->height) {
        fprintf(stderr, "S39_ORIGIN_ERROR operation=sample_bounds point=%d,%d extent=%ux%u\n",
                x, y, c->width, c->height);
        c->failed = 1;
        return 0;
    }
    xcb_generic_error_t *error = NULL;
    xcb_get_image_reply_t *reply = xcb_get_image_reply(c->conn,
        xcb_get_image(c->conn, XCB_IMAGE_FORMAT_Z_PIXMAP, c->window,
                      (int16_t)x, (int16_t)y, 1, 1, UINT32_MAX), &error);
    if (error || !reply || xcb_get_image_data_length(reply) < (int)(c->bpp / 8)) {
        fprintf(stderr, "S39_ORIGIN_ERROR operation=own_window_read point=%d,%d code=%u\n",
                x, y, error ? (unsigned)error->error_code : 0);
        free(error);
        free(reply);
        c->failed = 1;
        return 0;
    }
    const uint8_t *bytes = xcb_get_image_data(reply);
    uint32_t pixel = 0;
    unsigned count = c->bpp / 8;
    for (unsigned i = 0; i < count; i++)
        pixel |= (uint32_t)bytes[i] << (8 * (c->msb_first ? count - i - 1 : i));
    *rgb = component_from_pixel(pixel, c->visual.red_mask) << 16 |
           component_from_pixel(pixel, c->visual.green_mask) << 8 |
           component_from_pixel(pixel, c->visual.blue_mask);
    free(reply);
    return 1;
}

static int wait_present(struct context *c, uint32_t serial)
{
    int complete = 0, idle = 0;
    int64_t deadline = now_ms() + 4000;
    xcb_flush(c->conn);
    while (now_ms() < deadline && !c->failed && (!complete || !idle)) {
        xcb_generic_event_t *event;
        unsigned drained = 0;
        while (drained++ < 256 && (event = xcb_poll_for_event(c->conn)) != NULL) {
            unsigned type = event->response_type & 0x7f;
            if (!type) {
                xcb_generic_error_t *error = (xcb_generic_error_t *)event;
                fprintf(stderr, "S39_ORIGIN_ERROR operation=event code=%u\n",
                        (unsigned)error->error_code);
                c->failed = 1;
            } else if (type == XCB_GE_GENERIC) {
                xcb_present_generic_event_t *generic = (xcb_present_generic_event_t *)event;
                if (generic->extension == c->present_opcode && generic->event == c->event_id) {
                    if (generic->evtype == XCB_PRESENT_COMPLETE_NOTIFY) {
                        xcb_present_complete_notify_event_t *e = (xcb_present_complete_notify_event_t *)event;
                        if (e->window == c->window && e->serial == serial &&
                            e->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP) {
                            complete = 1;
                            printf("S39_ORIGIN_PRESENT serial=%u mode=%u msc=%" PRIu64 "\n",
                                   serial, (unsigned)e->mode, e->msc);
                            if (e->mode == XCB_PRESENT_COMPLETE_MODE_SKIP) {
                                fprintf(stderr, "S39_ORIGIN_ERROR operation=present_skipped serial=%u\n", serial);
                                c->failed = 1;
                            }
                        }
                    } else if (generic->evtype == XCB_PRESENT_IDLE_NOTIFY) {
                        xcb_present_idle_notify_event_t *e = (xcb_present_idle_notify_event_t *)event;
                        if (e->window == c->window && e->serial == serial)
                            idle = 1;
                    }
                }
            }
            free(event);
        }
        if (complete && idle)
            return 1;
        if (xcb_connection_has_error(c->conn))
            break;
        struct pollfd pfd = { .fd = xcb_get_file_descriptor(c->conn), .events = POLLIN };
        if (poll(&pfd, 1, 20) < 0 && errno != EINTR)
            break;
    }
    fprintf(stderr, "S39_ORIGIN_ERROR operation=present_wait serial=%u complete=%d idle=%d\n",
            serial, complete, idle);
    c->failed = 1;
    return 0;
}

static xcb_pixmap_t make_pattern(struct context *c, unsigned width, unsigned height,
                                 unsigned generation)
{
    xcb_pixmap_t pixmap = xcb_generate_id(c->conn);
    xcb_gcontext_t gc = xcb_generate_id(c->conn);
    if (!check(c, xcb_create_pixmap_checked(c->conn, c->screen->root_depth, pixmap,
                                           c->window, (uint16_t)width, (uint16_t)height), "create_pixmap"))
        return XCB_NONE;
    uint32_t values[] = { pixel_from_rgb(c, backgrounds[generation]), 0 };
    if (!check(c, xcb_create_gc_checked(c->conn, gc, pixmap,
                                       XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES,
                                       values), "create_gc")) {
        xcb_free_pixmap(c->conn, pixmap);
        return XCB_NONE;
    }
    xcb_rectangle_t rectangle = {0, 0, (uint16_t)width, (uint16_t)height};
    if (!check(c, xcb_poly_fill_rectangle_checked(c->conn, pixmap, gc, 1, &rectangle), "fill_background"))
        goto fail;
    for (unsigned i = 0; i < LANDMARKS; i++) {
        uint32_t pixel = pixel_from_rgb(c, colors[generation][i]);
        rectangle = (xcb_rectangle_t){(int16_t)(point_x[i] - 5), (int16_t)(point_y[i] - 5), 11, 11};
        if (!check(c, xcb_change_gc_checked(c->conn, gc, XCB_GC_FOREGROUND, &pixel), "set_landmark_color") ||
            !check(c, xcb_poly_fill_rectangle_checked(c->conn, pixmap, gc, 1, &rectangle), "fill_landmark"))
            goto fail;
    }
    xcb_free_gc(c->conn, gc);
    return pixmap;
fail:
    xcb_free_gc(c->conn, gc);
    xcb_free_pixmap(c->conn, pixmap);
    return XCB_NONE;
}

static int present_pattern(struct context *c, xcb_pixmap_t pixmap, uint32_t serial)
{
    if (!check(c, xcb_present_pixmap_checked(c->conn, c->window, pixmap, serial,
        XCB_NONE, XCB_NONE, 0, 0, XCB_NONE, XCB_NONE, XCB_NONE,
        XCB_PRESENT_OPTION_COPY, 0, 0, 0, 0, NULL), "present_pixmap"))
        return 0;
    return wait_present(c, serial);
}

static int resize_owned(struct context *c, unsigned width, unsigned height)
{
    const uint32_t values[] = { width, height };
    if (!check(c, xcb_configure_window_checked(c->conn, c->window,
        XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values), "resize_owned_window"))
        return 0;
    xcb_generic_error_t *error = NULL;
    xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(c->conn,
        xcb_get_geometry(c->conn, c->window), &error);
    if (error || !geometry || geometry->width != width || geometry->height != height) {
        fprintf(stderr, "S39_ORIGIN_ERROR operation=resize_geometry requested=%ux%u code=%u\n",
                width, height, error ? (unsigned)error->error_code : 0);
        free(error);
        free(geometry);
        c->failed = 1;
        return 0;
    }
    c->width = geometry->width;
    c->height = geometry->height;
    free(geometry);
    return 1;
}

/* Sample exact landmark centers at both competing origin hypotheses. */
static int sample_phase(struct context *c, const char *name, unsigned generation,
                         unsigned source_width, unsigned source_height,
                         int *origin_ok, int *centered_ok)
{
    int dx = (int)c->width - (int)source_width;
    int dy = (int)c->height - (int)source_height;
    /* Match the source-crop/destination-offset calculation in S29. */
    dx = dx >= 0 ? dx / 2 : -((-dx) / 2);
    dy = dy >= 0 ? dy / 2 : -((-dy) / 2);
    unsigned origin_hits = 0, centered_hits = 0;
    uint32_t original[LANDMARKS], centered[LANDMARKS];
    for (unsigned i = 0; i < LANDMARKS; i++) {
        if (!read_pixel(c, point_x[i], point_y[i], &original[i]) ||
            !read_pixel(c, point_x[i] + dx, point_y[i] + dy, &centered[i]))
            return 0;
        origin_hits += near_color(original[i], colors[generation][i]);
        centered_hits += near_color(centered[i], colors[generation][i]);
    }
    *origin_ok = origin_hits == LANDMARKS;
    *centered_ok = centered_hits == LANDMARKS;
    printf("S39_ORIGIN_PHASE phase=%s gen=%u window=%ux%u source=%ux%u centered_offset=%d,%d origin_hits=%u/%u centered_hits=%u/%u classification=%s\n",
           name, generation + 1, c->width, c->height, source_width, source_height,
           dx, dy, origin_hits, LANDMARKS, centered_hits, LANDMARKS,
           !dx && !dy && *origin_ok ? "IDENTICAL_ORIGIN" :
           *origin_ok && !*centered_ok ? "PRESENT_ORIGIN" :
           *centered_ok && !*origin_ok ? "CENTERED_REPLAY" : "OTHER_OR_MIXED");
    for (unsigned i = 0; i < LANDMARKS; i++)
        printf("S39_ORIGIN_POINT phase=%s index=%u expected=%06x original=%d,%d:%06x centered=%d,%d:%06x\n",
               name, i, colors[generation][i], point_x[i], point_y[i], original[i],
               point_x[i] + dx, point_y[i] + dy, centered[i]);
    fflush(stdout);
    return 1;
}

int main(int argc, char **argv)
{
    int expect_centered = argc == 2 && !strcmp(argv[1], "--expect-centered");
    if (argc > 2 || (argc == 2 && !expect_centered)) {
        fprintf(stderr, "usage: %s [--expect-centered]\n", argv[0]);
        return 2;
    }
    struct context c = {0};
    xcb_pixmap_t patterns[2] = {XCB_NONE, XCB_NONE};
    int screen_number = 0, status = 3;
    c.conn = xcb_connect(NULL, &screen_number);
    if (xcb_connection_has_error(c.conn)) {
        fprintf(stderr, "S39_ORIGIN_ERROR operation=connect\n");
        goto done;
    }
    const xcb_setup_t *setup = xcb_get_setup(c.conn);
    xcb_screen_iterator_t screens = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screen_number && screens.rem; i++)
        xcb_screen_next(&screens);
    if (!screens.rem)
        goto done;
    c.screen = screens.data;
    c.msb_first = setup->image_byte_order == XCB_IMAGE_ORDER_MSB_FIRST;
    xcb_depth_iterator_t depths = xcb_screen_allowed_depths_iterator(c.screen);
    for (; depths.rem; xcb_depth_next(&depths)) {
        xcb_visualtype_iterator_t visuals = xcb_depth_visuals_iterator(depths.data);
        for (; visuals.rem; xcb_visualtype_next(&visuals))
            if (visuals.data->visual_id == c.screen->root_visual)
                c.visual = *visuals.data;
    }
    xcb_format_iterator_t formats = xcb_setup_pixmap_formats_iterator(setup);
    for (; formats.rem; xcb_format_next(&formats))
        if (formats.data->depth == c.screen->root_depth)
            c.bpp = formats.data->bits_per_pixel;
    if (c.visual._class != XCB_VISUAL_CLASS_TRUE_COLOR ||
        (c.bpp != 24 && c.bpp != 32) ||
        c.screen->width_in_pixels < 640 || c.screen->height_in_pixels < 600) {
        fprintf(stderr, "S39_ORIGIN_ERROR operation=unsupported_visual_or_test_screen class=%u bpp=%u screen=%ux%u\n",
                (unsigned)c.visual._class, c.bpp,
                c.screen->width_in_pixels, c.screen->height_in_pixels);
        goto done;
    }
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c.conn, &xcb_present_id);
    if (!ext || !ext->present) {
        fprintf(stderr, "S39_ORIGIN_ERROR operation=missing_present_extension\n");
        goto done;
    }
    c.present_opcode = ext->major_opcode;
    xcb_generic_error_t *error = NULL;
    xcb_present_query_version_reply_t *version = xcb_present_query_version_reply(c.conn,
        xcb_present_query_version(c.conn, 1, 0), &error);
    if (error || !version) {
        fprintf(stderr, "S39_ORIGIN_ERROR operation=query_version code=%u\n",
                error ? (unsigned)error->error_code : 0);
        free(error);
        free(version);
        goto done;
    }
    free(version);
    c.window = xcb_generate_id(c.conn);
    const uint32_t window_values[] = {pixel_from_rgb(&c, backgrounds[0]), 1,
                                     XCB_EVENT_MASK_STRUCTURE_NOTIFY};
    if (!check(&c, xcb_create_window_checked(c.conn, c.screen->root_depth, c.window,
        c.screen->root, 32, 32, 400, 400, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
        c.screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
        window_values), "create_owned_window"))
        goto done;
    c.width = c.height = 400;
    const char tag_name[] = "_MESA_DRV", tag_value[] = "S39-origin-regression";
    xcb_intern_atom_reply_t *tag = xcb_intern_atom_reply(c.conn,
        xcb_intern_atom(c.conn, 0, sizeof(tag_name) - 1, tag_name), &error);
    if (error || !tag) {
        free(error);
        free(tag);
        c.failed = 1;
        goto done;
    }
    int tagged = check(&c, xcb_change_property_checked(c.conn, XCB_PROP_MODE_REPLACE,
        c.window, tag->atom, XCB_ATOM_STRING, 8, sizeof(tag_value) - 1, tag_value), "tag_owned_window");
    free(tag);
    if (!tagged)
        goto done;
    const char title[] = "Alroot S39 own-window origin regression";
    if (!check(&c, xcb_change_property_checked(c.conn, XCB_PROP_MODE_REPLACE,
        c.window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, sizeof(title) - 1, title), "name_owned_window"))
        goto done;
    c.event_id = xcb_generate_id(c.conn);
    if (!check(&c, xcb_present_select_input_checked(c.conn, c.event_id, c.window,
        XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY | XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY), "select_owned_present_events") ||
        !check(&c, xcb_map_window_checked(c.conn, c.window), "map_owned_window"))
        goto done;
    printf("S39_ORIGIN_READY xid=0x%x override_redirect=1 tagged=1 readback=owned_window_only expected=%s\n",
           c.window, expect_centered ? "known_centered_signature" : "consistent_present_origin");
    patterns[0] = make_pattern(&c, 400, 400, 0);
    patterns[1] = make_pattern(&c, 420, 420, 1);
    if (!patterns[0] || !patterns[1])
        goto done;

    int origin = 0, centered = 0, origin_pass = 1, center_signature = 1;
    unsigned checked = 0;
    if (!present_pattern(&c, patterns[0], 1) ||
        !sample_phase(&c, "initial_present", 0, 400, 400, &origin, &centered))
        goto done;
    if (!origin) {
        fprintf(stderr, "S39_ORIGIN_ERROR operation=initial_pattern_validation\n");
        c.failed = 1;
        goto done;
    }

    const unsigned first_sizes[2][2] = {{500, 500}, {360, 360}};
    const char *first_names[2] = {"enlarge_no_present", "shrink_no_present"};
    for (unsigned i = 0; i < 2; i++) {
        if (!resize_owned(&c, first_sizes[i][0], first_sizes[i][1]) ||
            !sample_phase(&c, first_names[i], 0, 400, 400, &origin, &centered))
            goto done;
        origin_pass &= origin;
        center_signature &= centered && !origin;
        checked++;
    }

    if (!resize_owned(&c, 500, 500) || !present_pattern(&c, patterns[1], 2) ||
        !sample_phase(&c, "new_mismatched_present", 1, 420, 420, &origin, &centered))
        goto done;
    if (!origin) {
        fprintf(stderr, "S39_ORIGIN_ERROR operation=new_present_not_at_requested_origin\n");
        c.failed = 1;
        goto done;
    }

    const unsigned second_sizes[2][2] = {{560, 520}, {380, 380}};
    const char *second_names[2] = {"new_frame_enlarge_no_present", "new_frame_shrink_no_present"};
    for (unsigned i = 0; i < 2; i++) {
        if (!resize_owned(&c, second_sizes[i][0], second_sizes[i][1]) ||
            !sample_phase(&c, second_names[i], 1, 420, 420, &origin, &centered))
            goto done;
        origin_pass &= origin;
        center_signature &= centered && !origin;
        checked++;
    }
    printf("S39_ORIGIN_RESULT consistent_present_origin=%s centered_signature=%s checked=%u expected=%s\n",
           origin_pass && checked == CHECK_PHASES ? "PASS" : "FAIL",
           center_signature && checked == CHECK_PHASES ? "REPRODUCED" : "NOT_REPRODUCED",
           checked, expect_centered ? "centered" : "origin");
    status = (expect_centered ? center_signature : origin_pass) && checked == CHECK_PHASES ? 0 : 1;
done:
    if (c.conn) {
        if (c.window)
            xcb_destroy_window(c.conn, c.window);
        for (unsigned i = 0; i < 2; i++)
            if (patterns[i])
                xcb_free_pixmap(c.conn, patterns[i]);
        xcb_flush(c.conn);
        xcb_disconnect(c.conn);
    }
    return c.failed ? 3 : status;
}
