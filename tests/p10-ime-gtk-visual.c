#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
static GtkWidget *entry;
static int changes, empty_frames, any_korean;
static int redraws, blank_redraws;
static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  (void)widget; (void)cr; (void)data;
  redraws++;
  if (any_korean && !*gtk_entry_get_text(GTK_ENTRY(entry))) blank_redraws++;
  return FALSE;
}
static void change(GtkEditable *unused, gpointer data) {
  const char *s=gtk_entry_get_text(GTK_ENTRY(entry));
  (void)unused; (void)data;
  changes++;
  if (!*s && any_korean) empty_frames++;
  if (g_utf8_validate(s,-1,NULL) && g_utf8_strlen(s,-1)>0) any_korean=1;
  printf("GTK_STATE change=%d chars=%d emptied_after_content=%d\n",
        changes,(int)g_utf8_strlen(s,-1),empty_frames);
  fflush(stdout);
}
static gboolean done(gpointer x) {
  const char *s=gtk_entry_get_text(GTK_ENTRY(entry));
  printf("GTK_FINAL chars=%d changes=%d empty_intermediates=%d final_is_ha=%d redraws=%d blank_redraws=%d\n",
      (int)g_utf8_strlen(s,-1),changes,empty_frames,!strcmp(s,"하"),redraws,blank_redraws);
  fflush(stdout);
  gtk_main_quit();
  return G_SOURCE_REMOVE;
}
int main(int argc,char **argv) {
  gtk_init(&argc,&argv);
  GtkWidget *w=gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(w),"Alroot P10 isolated visual transitions");
  gtk_window_set_default_size(GTK_WINDOW(w),320,85);
  entry=gtk_entry_new();
  gtk_container_add(GTK_CONTAINER(w),entry);
  g_signal_connect(entry,"changed",G_CALLBACK(change),NULL);
  g_signal_connect(entry,"draw",G_CALLBACK(on_draw),NULL);
  gtk_widget_show_all(w);
  gtk_window_present(GTK_WINDOW(w));
  gtk_widget_grab_focus(entry);
  g_timeout_add(2200,done,NULL);
  gtk_main();
  return 0;
}
