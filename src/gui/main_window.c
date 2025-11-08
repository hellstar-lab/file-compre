/* SPDX-License-Identifier: MIT */
// MainWindow: Single-window GUI for Advanced File Compressor

#include "main_window.h"
#include "drop_area.h"
#include "job_thread.h"

#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

struct _MainWindow {
    GtkApplicationWindow parent_instance;
    GtkWidget *header;
    GtkWidget *compress_btn;
    GtkWidget *decompress_btn;
    GtkWidget *progress;
    GtkWidget *log_view;
    GtkTextBuffer *log_buf;
    GtkWidget *drop_area;

    gchar *out_dir;
    GPtrArray *paths; // array of char* (owned)

    JobThread *job;
};

G_DEFINE_TYPE(MainWindow, main_window, GTK_TYPE_APPLICATION_WINDOW)

static void main_window_append_log(MainWindow *self, const gchar *line) {
    GtkTextBuffer *buf = self->log_buf;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, line, -1);
    gtk_text_buffer_insert(buf, &end, "\n", -1);
}

static void main_window_set_progress(MainWindow *self, gdouble pct) {
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress), CLAMP(pct / 100.0, 0.0, 1.0));
    gchar *text = g_strdup_printf("%.1f %%", pct);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(self->progress), text);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(self->progress), TRUE);
    g_free(text);
}

static void main_window_show_finish_dialog(MainWindow *self) {
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(self), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
                                            "All jobs finished");
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Open output folder", GTK_RESPONSE_ACCEPT);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Show in file manager", GTK_RESPONSE_YES);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Close", GTK_RESPONSE_CLOSE);
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (resp == GTK_RESPONSE_ACCEPT) {
        g_autofree gchar *uri = g_strdup_printf("file://%s", self->out_dir);
        g_app_info_launch_default_for_uri(uri, NULL, NULL);
    } else if (resp == GTK_RESPONSE_YES) {
#ifdef G_OS_WIN32
        g_autofree gchar *cmd = g_strdup_printf("explorer.exe %s", self->out_dir);
        g_spawn_command_line_async(cmd, NULL);
#elif defined(__APPLE__)
        g_autofree gchar *cmd = g_strdup_printf("open %s", self->out_dir);
        g_spawn_command_line_async(cmd, NULL);
#else
        g_autofree gchar *cmd = g_strdup_printf("xdg-open %s", self->out_dir);
        g_spawn_command_line_async(cmd, NULL);
#endif
    }
}

static void on_uris_dropped(DropArea *area, GPtrArray *uris, gpointer user_data) {
    MainWindow *self = MAIN_WINDOW(user_data);
    for (guint i = 0; i < uris->len; i++) {
        const gchar *uri = g_ptr_array_index(uris, i);
        g_autofree gchar *path = g_filename_from_uri(uri, NULL, NULL);
        if (!path) continue;
        g_ptr_array_add(self->paths, g_strdup(path));
        g_autofree gchar *msg = g_strdup_printf("Added: %s", path);
        main_window_append_log(self, msg);
    }
}

typedef struct { MainWindow *self; gdouble pct; } ProgressThunk;
static gboolean progress_idle_cb(gpointer data) {
    ProgressThunk *pt = (ProgressThunk *)data;
    main_window_set_progress(pt->self, pt->pct);
    g_free(pt);
    return G_SOURCE_REMOVE;
}
static void on_job_progress(gdouble pct, gpointer user_data) {
    MainWindow *self = MAIN_WINDOW(user_data);
    ProgressThunk *pt = g_new(ProgressThunk, 1);
    pt->self = self;
    pt->pct = pct;
    g_idle_add(progress_idle_cb, pt);
}

typedef struct { MainWindow *self; gchar *line; } LogThunk;
static gboolean log_idle_cb(gpointer data) {
    LogThunk *lt = (LogThunk *)data;
    main_window_append_log(lt->self, lt->line);
    g_free(lt->line);
    g_free(lt);
    return G_SOURCE_REMOVE;
}
static void on_job_log(const gchar *line, gpointer user_data) {
    MainWindow *self = MAIN_WINDOW(user_data);
    LogThunk *lt = g_new(LogThunk, 1);
    lt->self = self;
    lt->line = g_strdup(line);
    g_idle_add(log_idle_cb, lt);
}

typedef struct { MainWindow *self; gchar *msg; } ErrThunk;
static gboolean err_idle_cb(gpointer data) {
    ErrThunk *et = (ErrThunk *)data;
    gboolean is_readability_error = g_str_has_prefix(et->msg, "Cannot read ");
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(et->self), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE, "%s", et->msg);
    if (is_readability_error) {
        gtk_dialog_add_button(GTK_DIALOG(dlg), "Open Full Disk Access…", GTK_RESPONSE_ACCEPT);
    }
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Close", GTK_RESPONSE_CLOSE);
    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (is_readability_error && resp == GTK_RESPONSE_ACCEPT) {
#ifdef __APPLE__
        // Open macOS System Settings → Privacy & Security → Full Disk Access
        g_spawn_command_line_async("open x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles", NULL);
#endif
    }
    g_free(et->msg);
    g_free(et);
    return G_SOURCE_REMOVE;
}
static void on_job_error(const gchar *msg, gpointer user_data) {
    MainWindow *self = MAIN_WINDOW(user_data);
    ErrThunk *et = g_new(ErrThunk, 1);
    et->self = self;
    et->msg = g_strdup(msg);
    g_idle_add(err_idle_cb, et);
}

static void on_job_file_done(const gchar *out_path, gpointer user_data) {
    MainWindow *self = MAIN_WINDOW(user_data);
    g_autofree gchar *msg = g_strdup_printf("Output: %s", out_path);
    main_window_append_log(self, msg);
}

static gboolean finished_idle_cb(gpointer data) {
    MainWindow *self = MAIN_WINDOW(data);
    main_window_set_progress(self, 100.0);
    main_window_show_finish_dialog(self);
    return G_SOURCE_REMOVE;
}
static void on_job_finished(gpointer user_data) {
    g_idle_add(finished_idle_cb, user_data);
}

static void run_job(MainWindow *self, JobMode mode) {
    if (self->paths->len == 0) {
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(self), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
                                                "Drop files or folders first");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }
    if (self->job) {
        return; // already running
    }

    JobCallbacks cb = {
        .on_progress = on_job_progress,
        .on_log = on_job_log,
        .on_error = on_job_error,
        .on_file_done = on_job_file_done,
        .on_finished = on_job_finished,
        .user_data = self,
    };

    self->job = job_thread_new(mode, self->out_dir, self->paths, &cb);
    job_thread_start(self->job);
}

static void on_compress(GtkButton *btn, gpointer user_data) {
    run_job(MAIN_WINDOW(user_data), JOB_MODE_COMPRESS);
}

static void on_decompress(GtkButton *btn, gpointer user_data) {
    run_job(MAIN_WINDOW(user_data), JOB_MODE_DECOMPRESS);
}

static void main_window_construct_ui(MainWindow *self) {
    gtk_window_set_title(GTK_WINDOW(self), "Advanced File Compressor");
    gtk_window_set_default_size(GTK_WINDOW(self), 900, 600);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(self), vbox);

    // Buttons
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    self->compress_btn = gtk_button_new_with_label("Compress");
    self->decompress_btn = gtk_button_new_with_label("Decompress");
    gtk_box_pack_start(GTK_BOX(hbox), self->compress_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), self->decompress_btn, TRUE, TRUE, 0);
    g_signal_connect(self->compress_btn, "clicked", G_CALLBACK(on_compress), self);
    g_signal_connect(self->decompress_btn, "clicked", G_CALLBACK(on_decompress), self);

    // Drop area
    self->drop_area = GTK_WIDGET(drop_area_new());
    gtk_widget_set_size_request(self->drop_area, -1, 200);
    gtk_box_pack_start(GTK_BOX(vbox), self->drop_area, FALSE, FALSE, 0);
    g_signal_connect(self->drop_area, "uris-dropped", G_CALLBACK(on_uris_dropped), self);

    // Progress
    self->progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), self->progress, FALSE, FALSE, 0);

    // Log view
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
    self->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(self->log_view), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled), self->log_view);
    self->log_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->log_view));
}

static void main_window_dispose(GObject *obj) {
    MainWindow *self = MAIN_WINDOW(obj);
    if (self->job) {
        job_thread_free(self->job);
        self->job = NULL;
    }
    G_OBJECT_CLASS(main_window_parent_class)->dispose(obj);
}

static void main_window_finalize(GObject *obj) {
    MainWindow *self = MAIN_WINDOW(obj);
    if (self->paths) {
        for (guint i = 0; i < self->paths->len; i++) {
            g_free(g_ptr_array_index(self->paths, i));
        }
        g_ptr_array_free(self->paths, TRUE);
    }
    g_free(self->out_dir);
    G_OBJECT_CLASS(main_window_parent_class)->finalize(obj);
}

static void main_window_class_init(MainWindowClass *klass) {
    GObjectClass *gobj_class = G_OBJECT_CLASS(klass);
    gobj_class->dispose = main_window_dispose;
    gobj_class->finalize = main_window_finalize;
}

static void main_window_init(MainWindow *self) {
    self->paths = g_ptr_array_new_with_free_func(g_free);
    main_window_construct_ui(self);
}

MainWindow *main_window_new(GtkApplication *app, const gchar *out_dir) {
    MainWindow *self = g_object_new(MAIN_TYPE_WINDOW, "application", app, NULL);
    self->out_dir = g_strdup(out_dir ? out_dir : "output");
    return self;
}
#else
// Fallback: no GTK available — rely on header stub
#include <stddef.h>
#endif