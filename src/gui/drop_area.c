/* SPDX-License-Identifier: MIT */
// DropArea: Accepts file/folder URIs via drag-and-drop

#include "drop_area.h"

#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <cairo.h>

struct _DropArea {
    GtkDrawingArea parent_instance;
};

G_DEFINE_TYPE(DropArea, drop_area, GTK_TYPE_DRAWING_AREA)

enum {
    SIGNAL_URIS_DROPPED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void on_drag_data_received(GtkWidget *widget,
                                  GdkDragContext *context,
                                  gint x, gint y,
                                  GtkSelectionData *selection_data,
                                  guint info, guint time) {
    DropArea *self = DROP_AREA(widget);
    if (gtk_selection_data_get_length(selection_data) <= 0)
        return;
    const gchar *data = (const gchar *)gtk_selection_data_get_data(selection_data);
    if (!data) return;

    // Parse text/uri-list — lines of URIs
    g_autoptr(GPtrArray) uris = g_ptr_array_new_with_free_func(g_free);
    gchar **lines = g_strsplit(data, "\n", -1);
    for (gchar **p = lines; p && *p; ++p) {
        if (**p == '\0') continue;
        g_ptr_array_add(uris, g_strdup(*p));
    }
    g_strfreev(lines);

    g_signal_emit(self, signals[SIGNAL_URIS_DROPPED], 0, uris);
    gtk_drag_finish(context, TRUE, FALSE, time);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    // Simple hint text
    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
    cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18);
    cairo_move_to(cr, 20, alloc.height / 2);
    cairo_show_text(cr, "Drag & drop files or folders here");
    return FALSE;
}

static void drop_area_init(DropArea *self) {
    GtkTargetEntry targets[] = {
        { (gchar *)"text/uri-list", 0, 0 }
    };
    gtk_drag_dest_set(GTK_WIDGET(self), GTK_DEST_DEFAULT_ALL, targets, 1, GDK_ACTION_COPY);
    g_signal_connect(self, "drag-data-received", G_CALLBACK(on_drag_data_received), NULL);
    g_signal_connect(self, "draw", G_CALLBACK(on_draw), NULL);
}

static void drop_area_class_init(DropAreaClass *klass) {
    signals[SIGNAL_URIS_DROPPED] =
        g_signal_new("uris-dropped",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL,
                     G_TYPE_NONE, 1, G_TYPE_POINTER /* GPtrArray* */);
}

DropArea *drop_area_new(void) {
    return g_object_new(DROP_TYPE_AREA, NULL);
}
#else
// No GTK headers available; rely on stub in drop_area.h
#endif