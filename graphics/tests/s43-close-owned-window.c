/* SPDX-License-Identifier: MIT */
/*
 * Inspect, or ask the window manager to close, a window owned by a known child.
 * Build: clang -O2 -std=gnu11 -Wall -Wextra -Werror this.c -lxcb -lxcb-res
 * Usage: s43-close-owned-window [--inspect] XID PID
 *
 * XRes supplies the actual local client PID.  The client-controlled
 * _NET_WM_PID property is deliberately not used as ownership evidence.
 * A successful close result means the EWMH request was sent; the caller must
 * separately wait for its child to exit and for the managed window to vanish.
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <xcb/xcb.h>
#include <xcb/res.h>

static struct {
    xcb_connection_t *connection;
    const char *stage;
    xcb_window_t window;
    xcb_window_t root;
    uint32_t client_pid;
    uint32_t frame[4];
    int expected_pid;
    bool inspect;
} state = {.stage = "arguments"};

static _Noreturn void fail(const char *reason, xcb_generic_error_t *error)
{
    unsigned int x_error = error ? error->error_code : 0;
    int connection_error = state.connection
        ? xcb_connection_has_error(state.connection) : 0;
    /* The stage and reason strings are fixed literals, not command-line text. */
    printf("{\"status\":\"FAIL\",\"mode\":\"%s\",\"stage\":\"%s\","
           "\"reason\":\"%s\",\"xid\":\"0x%08" PRIx32 "\","
           "\"expected_pid\":%d,\"client_pid\":%" PRIu32 ","
           "\"x_error\":%u,\"connection_error\":%d}\n",
           state.inspect ? "inspect" : "close", state.stage, reason,
           state.window, state.expected_pid, state.client_pid,
           x_error, connection_error);
    fprintf(stderr, "s43-close-owned-window: %s: %s (X error %u)\n",
            state.stage, reason, x_error);
    free(error);
    if (state.connection)
        xcb_disconnect(state.connection);
    exit(EXIT_FAILURE);
}

static uint32_t number(const char *text, int base, uint32_t maximum)
{
    char *end = NULL;
    if (!text || !isdigit((unsigned char)text[0]))
        fail("invalid_numeric_argument", NULL);
    errno = 0;
    unsigned long long value = strtoull(text, &end, base);
    if (errno || !end || *end || value == 0 || value > maximum)
        fail("numeric_argument_out_of_range", NULL);
    return (uint32_t)value;
}

static xcb_atom_t atom(const char *name)
{
    xcb_generic_error_t *error = NULL;
    state.stage = name;
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
        state.connection,
        xcb_intern_atom(state.connection, 1, (uint16_t)strlen(name), name),
        &error);
    if (error || !reply)
        fail("atom_query_failed", error);
    xcb_atom_t result = reply->atom;
    free(reply);
    if (result == XCB_ATOM_NONE)
        fail("required_atom_missing", NULL);
    return result;
}

static xcb_get_property_reply_t *property32(
    xcb_window_t window, xcb_atom_t property, xcb_atom_t type,
    uint32_t maximum, const char *stage)
{
    xcb_generic_error_t *error = NULL;
    state.stage = stage;
    xcb_get_property_reply_t *reply = xcb_get_property_reply(
        state.connection,
        xcb_get_property(state.connection, 0, window, property,
                         XCB_GET_PROPERTY_TYPE_ANY, 0, maximum),
        &error);
    if (error || !reply)
        fail("property_query_failed", error);
    if (reply->type != type || reply->format != 32)
        fail("property_missing_or_wrong_type", NULL);
    if (reply->bytes_after || reply->value_len > maximum ||
        xcb_get_property_value_length(reply) != (int)(reply->value_len * 4))
        fail("property_truncated_or_malformed", NULL);
    return reply;
}

static bool contains(const xcb_get_property_reply_t *reply, uint32_t value)
{
    const uint32_t *values = xcb_get_property_value(reply);
    for (uint32_t i = 0; i < reply->value_len; ++i)
        if (values[i] == value)
            return true;
    return false;
}

static void require_owner(void)
{
    xcb_generic_error_t *error = NULL;
    const xcb_res_client_id_spec_t spec = {
        .client = state.window,
        .mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID
    };
    state.stage = "xres_ownership";
    if (kill((pid_t)state.expected_pid, 0) != 0)
        fail("expected_process_not_live", NULL);
    xcb_res_query_client_ids_reply_t *reply = xcb_res_query_client_ids_reply(
        state.connection, xcb_res_query_client_ids(state.connection, 1, &spec),
        &error);
    if (error || !reply)
        fail("client_pid_query_failed", error);
    xcb_res_client_id_value_iterator_t ids =
        xcb_res_query_client_ids_ids_iterator(reply);
    if (ids.rem != 1 || !ids.data ||
        ids.data->spec.mask != XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID ||
        xcb_res_client_id_value_value_length(ids.data) != 1)
        fail("client_pid_missing_or_ambiguous", NULL);
    /* The returned spec.client may be the client's base, rather than this XID. */
    state.client_pid = xcb_res_client_id_value_value(ids.data)[0];
    free(reply);
    if (state.client_pid != (uint32_t)state.expected_pid)
        fail("client_pid_mismatch", NULL);
}

int main(int argc, char **argv)
{
    int first = 1;
    if (argc > 1 && strcmp(argv[1], "--inspect") == 0) {
        state.inspect = true;
        first = 2;
    }
    if (argc != first + 2) {
        fprintf(stderr, "Usage: s43-close-owned-window [--inspect] XID PID\n");
        fail("invalid_arguments", NULL);
    }
    state.window = number(argv[first], 0, UINT32_MAX);
    state.expected_pid = (int)number(argv[first + 1], 10, INT_MAX);

    state.stage = "connection";
    state.connection = xcb_connect(NULL, NULL);
    if (!state.connection || xcb_connection_has_error(state.connection))
        fail("x_connection_failed", NULL);

    state.stage = "xres_extension";
    const xcb_query_extension_reply_t *extension =
        xcb_get_extension_data(state.connection, &xcb_res_id);
    if (!extension || !extension->present)
        fail("xres_extension_missing", NULL);
    xcb_generic_error_t *error = NULL;
    xcb_res_query_version_reply_t *version = xcb_res_query_version_reply(
        state.connection, xcb_res_query_version(state.connection, 1, 2), &error);
    if (error || !version)
        fail("xres_version_query_failed", error);
    bool version_ok = version->server_major > 1 ||
        (version->server_major == 1 && version->server_minor >= 2);
    free(version);
    if (!version_ok)
        fail("xres_1_2_required", NULL);

    state.stage = "window_tree";
    xcb_query_tree_reply_t *tree = xcb_query_tree_reply(
        state.connection, xcb_query_tree(state.connection, state.window), &error);
    if (error || !tree)
        fail("window_query_failed", error);
    state.root = tree->root;
    free(tree);
    if (state.root == XCB_WINDOW_NONE)
        fail("window_root_missing", NULL);
    require_owner();

    xcb_atom_t clients = atom("_NET_CLIENT_LIST");
    xcb_atom_t protocols = atom("WM_PROTOCOLS");
    xcb_atom_t wm_delete = atom("WM_DELETE_WINDOW");
    xcb_atom_t extents = atom("_NET_FRAME_EXTENTS");
    xcb_atom_t allowed = atom("_NET_WM_ALLOWED_ACTIONS");
    xcb_atom_t action_close = atom("_NET_WM_ACTION_CLOSE");
    xcb_atom_t net_close = atom("_NET_CLOSE_WINDOW");

    xcb_get_property_reply_t *property = property32(
        state.root, clients, XCB_ATOM_WINDOW, 65536, "managed_window");
    if (!contains(property, state.window))
        fail("window_not_managed", NULL);
    free(property);

    property = property32(state.window, protocols, XCB_ATOM_ATOM, 256,
                          "wm_delete_protocol");
    if (!contains(property, wm_delete))
        fail("wm_delete_window_missing", NULL);
    free(property);

    property = property32(state.window, extents, XCB_ATOM_CARDINAL, 4,
                          "frame_extents");
    if (property->value_len != 4)
        fail("invalid_frame_extents", NULL);
    memcpy(state.frame, xcb_get_property_value(property), sizeof(state.frame));
    free(property);
    if (state.frame[2] == 0)
        fail("top_decoration_missing", NULL);

    property = property32(state.window, allowed, XCB_ATOM_ATOM, 256,
                          "allowed_actions");
    if (!contains(property, action_close))
        fail("close_action_missing", NULL);
    free(property);

    if (!state.inspect) {
        /* Recheck ownership immediately before the only mutating request. */
        require_owner();
        state.stage = "close_request";
        xcb_client_message_event_t event = {0};
        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.window = state.window;
        event.type = net_close;
        event.data.data32[0] = XCB_CURRENT_TIME;
        event.data.data32[1] = 1; /* EWMH source: a normal application. */
        xcb_void_cookie_t cookie = xcb_send_event_checked(
            state.connection, 0, state.root,
            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
            (const char *)&event);
        error = xcb_request_check(state.connection, cookie);
        if (error || xcb_connection_has_error(state.connection))
            fail("close_request_failed", error);
    }

    printf("{\"status\":\"PASS\",\"mode\":\"%s\","
           "\"xid\":\"0x%08" PRIx32 "\",\"root\":\"0x%08" PRIx32 "\","
           "\"expected_pid\":%d,\"client_pid\":%" PRIu32 ","
           "\"ownership\":\"xres_local_client_pid\",\"managed\":true,"
           "\"wm_delete_window\":true,\"close_allowed\":true,"
           "\"frame_extents\":{\"left\":%" PRIu32 ",\"right\":%" PRIu32 ","
           "\"top\":%" PRIu32 ",\"bottom\":%" PRIu32 "},"
           "\"close_request\":\"%s\"}\n",
           state.inspect ? "inspect" : "close", state.window, state.root,
           state.expected_pid, state.client_pid, state.frame[0], state.frame[1],
           state.frame[2], state.frame[3], state.inspect ? "NOT_SENT" : "SENT");
    xcb_disconnect(state.connection);
    return EXIT_SUCCESS;
}
