/* SPDX-License-Identifier: MIT */
// Public interface for JobThread used by the GUI.
// Provides feature detection to avoid hard dependency on GLib headers for static analysis.

#ifndef AFC_JOB_THREAD_H
#define AFC_JOB_THREAD_H

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include(<gio/gio.h>)
  #define AFC_HAVE_GIO 1
#else
  #define AFC_HAVE_GIO 0
#endif

#if AFC_HAVE_GIO
  #include <glib.h>
  #include <gio/gio.h>

  typedef enum {
      JOB_MODE_COMPRESS = 0,
      JOB_MODE_DECOMPRESS = 1
  } JobMode;

  typedef struct {
      void (*on_progress)(gdouble pct, gpointer user_data);
      void (*on_log)(const gchar *line, gpointer user_data);
      void (*on_error)(const gchar *msg, gpointer user_data);
      void (*on_file_done)(const gchar *out_path, gpointer user_data);
      void (*on_finished)(gpointer user_data);
      gpointer user_data;
  } JobCallbacks;

  typedef struct _JobThread JobThread;

  JobThread *job_thread_new(JobMode mode, const gchar *out_dir, GPtrArray *paths, const JobCallbacks *cb);
  void job_thread_start(JobThread *job);
  void job_thread_free(JobThread *job);

#else  /* AFC_HAVE_GIO == 0 */

  // Minimal declarations to keep headers consumable when GLib/GIO headers are absent.
  typedef enum {
      JOB_MODE_COMPRESS = 0,
      JOB_MODE_DECOMPRESS = 1
  } JobMode;

  typedef struct {
      void (*on_progress)(double pct, void *user_data);
      void (*on_log)(const char *line, void *user_data);
      void (*on_error)(const char *msg, void *user_data);
      void (*on_file_done)(const char *out_path, void *user_data);
      void (*on_finished)(void *user_data);
      void *user_data;
  } JobCallbacks;

  typedef struct _JobThread JobThread;

  // Use opaque pointer for paths to avoid GLib dependency in headers.
  JobThread *job_thread_new(JobMode mode, const char *out_dir, void *paths, const JobCallbacks *cb);
  void job_thread_start(JobThread *job);
  void job_thread_free(JobThread *job);

#endif /* AFC_HAVE_GIO */

#endif // AFC_JOB_THREAD_H