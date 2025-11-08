/* SPDX-License-Identifier: MIT */
#pragma once

// Guard GTK-dependent declarations to reduce diagnostics when headers are missing
#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MAIN_TYPE_WINDOW (main_window_get_type())
G_DECLARE_FINAL_TYPE(MainWindow, main_window, MAIN, WINDOW, GtkApplicationWindow)

MainWindow *main_window_new(GtkApplication *app, const gchar *out_dir);

G_END_DECLS

#else
// Fallback declarations when GTK headers are unavailable
typedef struct _MainWindow MainWindow;
typedef struct _GtkApplication GtkApplication;

// Ensure NULL is defined for stub return
#include <stddef.h>

// Provide a minimal stub to satisfy callers in non-GTK environments
static inline MainWindow *main_window_new(GtkApplication *app, const char *out_dir) {
    (void)app;
    (void)out_dir;
    return NULL;
}
#endif