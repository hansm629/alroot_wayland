/*
 * Disposable GTK text entry for an isolated Xvfb test.
 * Only outputs whether the synthetic test string matched; no user data.
 */
#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>

static GtkWidget *entry;
static gboolean finish(gpointer user_data) {
    const char *got = gtk_entry_get_text(GTK_ENTRY(entry));
    const char *expected = "가가가가가가가가가가";
    gboolean ok = g_strcmp0(got, expected) == 0;
    printf("P04_GTK_SYNTHETIC_MATCH=%d chars=%zu\n",
           ok ? 1 : 0, (size_t)g_utf8_strlen(got, -1));
    fflush(stdout);
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(win), 260, 90);
    gtk_window_set_title(GTK_WINDOW(win), "Alroot P04 Headless Input Test");
    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(win), entry);
    gtk_widget_show_all(win);
    gtk_window_present(GTK_WINDOW(win));
    gtk_widget_grab_focus(entry);
    g_timeout_add(5000, finish, NULL);
    gtk_main();
    return 0;
}
