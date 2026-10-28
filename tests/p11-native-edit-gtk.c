#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

/* Private GTK IM context receiving native preedit/commit and Backspace. */
static GtkWidget *entry;
static int preedit_events, actual_changes, empty_actual;
static const char *expected;
static gboolean seen_nonempty;

static void on_text_changed(GtkEditable *editable, gpointer user_data) {
    (void)user_data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
    actual_changes++;
    if (*text) seen_nonempty=TRUE;
    else if (seen_nonempty) empty_actual++;
}
static void on_preedit(GtkEntry *widget, char *text, gpointer user_data) {
    (void)widget; (void)text; (void)user_data;
    preedit_events++;
}
static gboolean finish(gpointer user_data) {
    (void)user_data;
    const char *actual = gtk_entry_get_text(GTK_ENTRY(entry));
    int match = !strcmp(actual, expected);
    printf("P11_NATIVE_EDIT_MATCH=%d actual_changes=%d preedit_changes=%d "
           "empty_actual_changes=%d expected_chars=%ld actual_chars=%ld\n",
           match, actual_changes, preedit_events, empty_actual,
           (long)g_utf8_strlen(expected,-1),
           (long)g_utf8_strlen(actual,-1));
    fflush(stdout);
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    expected=argv[1];
    gtk_init(&argc,&argv);
    GtkWidget *window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window),"Alroot P11 native GTK edit test");
    gtk_window_set_default_size(GTK_WINDOW(window),320,80);
    entry=gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(window),entry);
    g_signal_connect(entry,"changed",G_CALLBACK(on_text_changed),NULL);
    g_signal_connect(entry,"preedit-changed",G_CALLBACK(on_preedit),NULL);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(entry);
    g_timeout_add(2500,finish,NULL);
    gtk_main();
    return 0;
}
