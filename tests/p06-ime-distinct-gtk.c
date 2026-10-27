#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
static GtkWidget *entry;
static const char *expected;
static gboolean finish(gpointer u) {
 const char *got=gtk_entry_get_text(GTK_ENTRY(entry));
 printf("P06_GUI_EXPECTED_MATCH=%d expected_chars=%ld actual_chars=%ld utf8valid=%d\n",
  strcmp(got,expected)==0,(long)g_utf8_strlen(expected,-1),
  (long)g_utf8_strlen(got,-1),g_utf8_validate(got,-1,NULL));
 for (const char *q=got; *q; q=g_utf8_next_char(q)) {
   printf("U+%04x ",g_utf8_get_char(q));
 }
 printf("\n");
 fflush(stdout);
 gtk_main_quit();
 return G_SOURCE_REMOVE;
}
int main(int argc,char **argv) {
 expected=argc>1?argv[1]:"가나다라마바사아자차";
 gtk_init(&argc,&argv);
 GtkWidget *window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
 gtk_window_set_title(GTK_WINDOW(window),"Alroot P06 isolated Unicode");
 gtk_window_set_default_size(GTK_WINDOW(window),300,90);
 entry=gtk_entry_new();
 gtk_container_add(GTK_CONTAINER(window),entry);
 gtk_widget_show_all(window);
 gtk_window_present(GTK_WINDOW(window));
 gtk_widget_grab_focus(entry);
 g_timeout_add(3200,finish,NULL);
 gtk_main();
 return 0;
}
