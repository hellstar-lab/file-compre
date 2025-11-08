/* SPDX-License-Identifier: MIT */
#pragma once

#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define DROP_TYPE_AREA (drop_area_get_type())
G_DECLARE_FINAL_TYPE(DropArea, drop_area, DROP, AREA, GtkDrawingArea)

DropArea *drop_area_new(void);

G_END_DECLS
#else
// Fallback declarations when GTK headers are unavailable
typedef struct _DropArea DropArea;

// Ensure NULL is available for stub returns
#include <stddef.h>

static inline DropArea *drop_area_new(void) {
    return NULL;
}
#endif