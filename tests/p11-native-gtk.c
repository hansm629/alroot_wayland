#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
static GtkWidget *entry;
static int text_changes, preedit_changes;
static char final_text[256];
static void on_change(GtkEditable *e,gpointer data) {
    (void)data;
    const char *s=gtk_entry_get_text(GTK_ENTRY(e));
    text_changes++;
    snprintf(final_text,sizeof(final_text),"%s",s);
    fprintf(stderr,"GTK_FCITX_TEXT_CHANGE chars=%ld\n",(long)g_utf8_strlen(s,-1));
    fflush(stderr);
}
static void on_preedit(GtkEntry *e, gchar *str, gpointer data) {
    (void)e;(void)data;
    preedit_changes++;
    fprintf(stderr,"GTK_FCITX_PREEDIT_CHANGED chars=%ld\n",
            str?(long)g_utf8_strlen(str,-1):0);
    fflush(stderr);
}
static gboolean close_probe(gpointer data) {
    (void)data;
    fprintf(stderr,"GTK_FCITX_FINAL_MATCH=%d text_events=%d preedit_events=%d\n",
            strcmp(final_text,"하")==0,text_changes,preedit_changes);
    fflush(stderr);
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}
int main(int argc,char **argv) {
    gtk_init(&argc,&argv);
    GtkWidget *win=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(win),320,92);
    gtk_window_set_title(GTK_WINDOW(win),"Alroot FCITX client native preedit");
    entry=gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(win),entry);
    g_signal_connect(entry,"changed",G_CALLBACK(on_change),NULL);
    g_signal_connect(entry,"preedit-changed",G_CALLBACK(on_preedit),NULL);
    gtk_widget_show_all(win);
    gtk_widget_grab_focus(entry);
    gtk_window_present(GTK_WINDOW(win));
    g_timeout_add(2600,close_probe,NULL);
    gtk_main();
    return 0;
}
