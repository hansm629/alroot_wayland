/* Private GTK3/VTE cursor visualization, not a user terminal. */
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <stdio.h>

static gboolean finish(gpointer unused) {
    (void)unused;
    puts("P14_VTE_VISUAL_PROBE_FINISHED");
    fflush(stdout);
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Alroot P14 private VTE cursor");
    gtk_window_set_default_size(GTK_WINDOW(window), 550, 135);
    VteTerminal *terminal = VTE_TERMINAL(vte_terminal_new());
    PangoFontDescription *font = pango_font_description_from_string("Monospace 26");
    vte_terminal_set_font(terminal, font);
    pango_font_description_free(font);
    vte_terminal_set_cursor_shape(terminal, VTE_CURSOR_SHAPE_IBEAM);
    vte_terminal_set_cursor_blink_mode(terminal, VTE_CURSOR_BLINK_OFF);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(terminal));
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
    gtk_window_present(GTK_WINDOW(window));
    char *args[] = {"/data/data/com.termux/files/usr/bin/bash",
                    "-c", "printf 'P14> '; cat", NULL};
    vte_terminal_spawn_async(terminal, VTE_PTY_DEFAULT, NULL, args, NULL,
                             G_SPAWN_DEFAULT, NULL, NULL, NULL, -1,
                             NULL, NULL, NULL);
    g_timeout_add(3000, finish, NULL);
    gtk_main();
    return 0;
}
