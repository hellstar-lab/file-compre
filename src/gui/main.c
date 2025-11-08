/* SPDX-License-Identifier: MIT */
// Advanced File Compressor GUI — Entry point
// Language: C99, Toolkit: GTK3

#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>

#include "main_window.h"

typedef struct {
    gchar *out_dir;
} AppOptions;

static void app_activate(GtkApplication *app, gpointer user_data) {
    AppOptions *opts = (AppOptions *)user_data;
    MainWindow *win = main_window_new(app, opts->out_dir);
    // Ensure all child widgets are visible
    gtk_widget_show_all(GTK_WIDGET(win));
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
    g_autoptr(GtkApplication) app = NULL;
    int status = 0;

    g_autofree gchar *default_out = g_build_path("/", g_get_current_dir(), "output", NULL);
    AppOptions opts = { .out_dir = default_out };

    GOptionEntry entries[] = {
        { "out-dir", 0, 0, G_OPTION_ARG_STRING, &opts.out_dir, "Output directory for results", "PATH" },
        { NULL }
    };

    g_autoptr(GOptionContext) ctx = g_option_context_new("- Advanced File Compressor GUI");
    g_option_context_add_group(ctx, gtk_get_option_group(TRUE));
    g_option_context_add_main_entries(ctx, entries, NULL);
    if (!g_option_context_parse(ctx, &argc, &argv, NULL)) {
        g_printerr("Failed to parse options\n");
        return 1;
    }

    app = gtk_application_new("com.afc.gui", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(app_activate), &opts);
    status = g_application_run(G_APPLICATION(app), argc, argv);

    return status;
}
#else
#include <stdio.h>
int main(void) {
    fprintf(stderr, "GTK development headers not found. GUI disabled in editor diagnostics.\n");
    return 0;
}
#endif