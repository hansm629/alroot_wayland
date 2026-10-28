#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static GtkWidget *entry,*window;
static gboolean shot_requested = FALSE, saved = FALSE;
static gboolean screenshot(gpointer data) {
  (void)data;
  GdkWindow *gdk=gtk_widget_get_window(window);
  if (!gdk) return G_SOURCE_REMOVE;
  GtkAllocation box;
  gtk_widget_get_allocation(window,&box);
  GdkPixbuf *pix = gdk_pixbuf_get_from_window(gdk,0,0,box.width,box.height);
  if (pix) {
    GError *error=NULL;
    const char *out=getenv("ALROOT_TEST_IMAGE");
    if (!out) out="/data/data/com.termux/files/home/alroot_wayland/.source/p13-visual/preedit.png";
    saved=gdk_pixbuf_save(pix,out,"png",&error,NULL);
    if(error){g_printerr("ERR=%s\n",error->message);g_error_free(error);}
    g_object_unref(pix);
    printf("GTK_P13_SCREENSHOT_SAVED=%d\n",saved);
    fflush(stdout);
  }
  return G_SOURCE_REMOVE;
}
static void preedit(GtkEntry *e,gchar *s,gpointer ctx) {
  (void)e;(void)ctx;
  if (s&&*s&&!shot_requested) {
    shot_requested=TRUE;
    g_timeout_add(75,screenshot,NULL);
  }
}
static gboolean done(gpointer unused) {
  (void)unused;
  printf("GTK_P13_FINAL_TEXT_CHARS=%ld\n",(long)g_utf8_strlen(gtk_entry_get_text(GTK_ENTRY(entry)),-1));
  gtk_main_quit();
  return G_SOURCE_REMOVE;
}
int main(int argc,char **argv) {
  gtk_init(&argc,&argv);
  window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window),"P13 native preedit caret visual");
  gtk_window_set_default_size(GTK_WINDOW(window),720,180);
  GtkWidget *outer=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
  gtk_container_set_border_width(GTK_CONTAINER(outer),15);
  gtk_container_add(GTK_CONTAINER(window),outer);
  GtkWidget *label=gtk_label_new("Fcitx5 Hangul composition (not committed)");
  gtk_box_pack_start(GTK_BOX(outer),label,FALSE,FALSE,0);
  entry=gtk_entry_new();
  PangoFontDescription *font=pango_font_description_from_string("Noto Sans CJK KR 26");
  gtk_widget_override_font(entry,font);
  pango_font_description_free(font);
  gtk_box_pack_start(GTK_BOX(outer),entry,FALSE,FALSE,0);
  g_signal_connect(entry,"preedit-changed",G_CALLBACK(preedit),NULL);
  gtk_widget_show_all(window);
  gtk_widget_grab_focus(entry);
  gtk_window_present(GTK_WINDOW(window));
  g_timeout_add(1800,done,NULL);
  gtk_main();
  return saved?0:1;
}
