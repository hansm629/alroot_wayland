#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

static GtkWidget *entry;
static guint changes;
static gint64 first_hangul_us;
static const char *expected = "hello국";

static void on_change(GtkEditable *unused, gpointer data) {
    (void)unused;
    (void)data;
    changes++;
    const char *value = gtk_entry_get_text(GTK_ENTRY(entry));
    if (!first_hangul_us) {
        for (const char *cursor = value; *cursor; cursor = g_utf8_next_char(cursor)) {
            if (g_utf8_get_char(cursor) >= 0x1100) {
                first_hangul_us = g_get_monotonic_time();
                printf("FIRST_HANGUL_VISIBLE_US=%lld\n",
                       (long long)first_hangul_us);
                fflush(stdout);
                break;
            }
        }
    }
}
static gboolean completed(gpointer unused) {
    (void)unused;
    const char *actual = gtk_entry_get_text(GTK_ENTRY(entry));
    int ok = strcmp(actual, expected) == 0;
    printf("P09_X11_GTK_MATCH=%d changes=%u chars=%ld\n",
           ok, changes, (long)g_utf8_strlen(actual, -1));
    fflush(stdout);
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 360, 84);
    gtk_window_set_title(GTK_WINDOW(window), "Alroot P09 private IME");
    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(window), entry);
    g_signal_connect(entry, "changed", G_CALLBACK(on_change), NULL);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(entry);
    g_timeout_add(2100, completed, NULL);
    gtk_main();
    return 0;
}
