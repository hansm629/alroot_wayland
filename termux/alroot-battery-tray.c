/*
 * Alroot Android BatteryManager status icon for the isolated XFCE panel.
 * This is a separate GTK XEmbed status icon on the Alroot Xwayland DISPLAY,
 * never a D-Bus UPower implementation and never a device-global permission.
 */
#define GDK_DISABLE_DEPRECATION_WARNINGS
#include <gtk/gtk.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static GtkStatusIcon *icon;
static const char *snapshot;
static int old_level = -2;
static gboolean old_charge;
static gboolean embedded_reported;

static void draw_icon(int level, gboolean charging) {
    const int w=45,h=30;
    cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h);
    cairo_t *cr=cairo_create(surface);
    cairo_set_source_rgba(cr,0,0,0,0);
    cairo_paint(cr);
    cairo_set_source_rgba(cr,.13,.17,.20,.96);
    cairo_rectangle(cr,1,2,w-3,h-4);
    cairo_fill(cr);
    cairo_set_source_rgb(cr,1,1,1);
    cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr,17);
    char percent[9];
    if(level < 0) snprintf(percent,sizeof(percent),"--%%");
    else snprintf(percent,sizeof(percent),"%d%%",level);
    cairo_text_extents_t ext;
    cairo_text_extents(cr,percent,&ext);
    cairo_move_to(cr,(w-ext.width)/2-ext.x_bearing,
                  (h-ext.height)/2-ext.y_bearing);
    cairo_show_text(cr,percent);
    if(charging) {
        cairo_set_source_rgb(cr,0.98,0.78,0.24);
        cairo_rectangle(cr,w-4,3,2,h-6);
        cairo_fill(cr);
    }
    GdkPixbuf *pix=gdk_pixbuf_get_from_surface(surface,0,0,w,h);
    if(pix) {
        gtk_status_icon_set_from_pixbuf(icon,pix);
        g_object_unref(pix);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    char tip[96];
    if(level >= 0)
        snprintf(tip,sizeof(tip),"Alroot Android battery %d%% (%s)",
                 level,charging?"charging":"battery");
    else snprintf(tip,sizeof(tip),"Alroot Android battery unavailable");
    gtk_status_icon_set_tooltip_text(icon,tip);
}
static gboolean tick(gpointer user) {
    (void)user;
    gchar *json=NULL;
    gsize size=0;
    int level=-1;
    gboolean charge=FALSE;
    struct stat st;
    if(stat(snapshot,&st)==0 && time(NULL)-st.st_mtime<180 &&
       g_file_get_contents(snapshot,&json,&size,NULL) && size<2048) {
        char *start=strstr(json,"\"percentage\"");
        if(start && (start=strchr(start,':'))!=NULL) {
            int parsed=-1;
            if(sscanf(start+1," %d",&parsed)==1 && parsed>=0 && parsed<=100)
                level=parsed;
        }
        if(strstr(json,"\"CHARGING\"") || strstr(json,"\"FULL\""))charge=TRUE;
    }
    g_free(json);
    if(level!=old_level || charge!=old_charge) {
        draw_icon(level,charge);
        old_level=level;
        old_charge=charge;
    }
    if(!embedded_reported && gtk_status_icon_is_embedded(icon)) {
        embedded_reported=TRUE;
        g_message("Alroot battery tray embedded on isolated Xwayland panel");
    }
    return G_SOURCE_CONTINUE;
}
int main(int argc,char **argv) {
    const char *display=getenv("DISPLAY");
    if(argc!=2 || !display || !strncmp(display,":0",2)) {
        fprintf(stderr,"Battery tray requires Alroot-only Xwayland DISPLAY\n");
        return 2;
    }
    snapshot=argv[1];
    gtk_init(&argc,&argv);
    icon=gtk_status_icon_new();
    gtk_status_icon_set_title(icon,"Alroot battery");
    gtk_status_icon_set_visible(icon,TRUE);
    tick(NULL);
    g_timeout_add_seconds(15,tick,NULL);
    gtk_main();
    g_object_unref(icon);
    return 0;
}
