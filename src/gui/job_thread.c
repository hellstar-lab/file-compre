/* SPDX-License-Identifier: MIT */
// JobThread: Spawns compress/decompress CLI processes and parses progress/logs

// Always include the public header; it defines AFC_HAVE_GIO via feature detection.
#include "job_thread.h"
#if AFC_HAVE_GIO
  #include <string.h>
  #ifdef G_OS_UNIX
  #include <unistd.h>
  #endif
  #ifdef __APPLE__
  #include <limits.h>
  #include <mach-o/dyld.h>
  #endif

  struct _JobThread {
      JobMode mode;
      gchar *out_dir;
      GPtrArray *paths; // array of char*, owned copy
      JobCallbacks cb;
      GThread *thread;
      gchar *last_error; // capture last meaningful stderr line
  };

  static gchar *detect_binary(JobMode mode) {
      const gchar *cli_dir = "./build/cli";
      // Also consider absolute path from project root embedded at build time
      const gchar *abs_root =
#ifdef AFC_SOURCE_DIR
          AFC_SOURCE_DIR
#else
          NULL
#endif
          ;
      // Prefer the unified universal CLI if present
      const gchar *name_universal = "universal";
      // Legacy names supported as fallback
      const gchar *name_cli = (mode == JOB_MODE_COMPRESS) ? "compress_file" : "decompress_file";
      const gchar *bin_dir = "./bin";
      const gchar *name_fallback = (mode == JOB_MODE_COMPRESS) ? "universal_comp" : "universal_decompressor";

#ifdef G_OS_WIN32
      // Prefer build/cli/<name>.exe
      g_autofree gchar *p1 = g_build_filename(cli_dir, name_cli, NULL);
      g_autofree gchar *p1e = g_strconcat(p1, ".exe", NULL);
      if (g_file_test(p1e, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(p1e);
      if (g_file_test(p1, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(p1);
      // Fallback to bin/<fallback>.exe
      g_autofree gchar *p2 = g_build_filename(bin_dir, name_fallback, NULL);
      g_autofree gchar *p2e = g_strconcat(p2, ".exe", NULL);
      if (g_file_test(p2e, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(p2e);
      if (g_file_test(p2, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(p2);
  #else
      // POSIX: look for build/cli/universal first, then legacy names
      // If running as a macOS app bundle, prefer bundled CLI next to executable
      #ifdef __APPLE__
      {
          char exe_path[PATH_MAX];
          uint32_t sz = sizeof(exe_path);
          if (_NSGetExecutablePath(exe_path, &sz) == 0) {
              g_autofree gchar *exe_dir = g_path_get_dirname(exe_path);
              // Check for bundled universal first
              g_autofree gchar *buniv = g_build_filename(exe_dir, name_universal, NULL);
              if (g_file_test(buniv, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(buniv);
              // Then look for legacy fallbacks placed in bundle
              g_autofree gchar *bcli = g_build_filename(exe_dir, name_cli, NULL);
              if (g_file_test(bcli, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(bcli);
              g_autofree gchar *bfallback = g_build_filename(exe_dir, name_fallback, NULL);
              g_autofree gchar *bfallback_exe = g_strconcat(bfallback, ".exe", NULL);
              if (g_file_test(bfallback, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(bfallback);
              if (g_file_test(bfallback_exe, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(bfallback_exe);
          }
      }
      #endif
      g_autofree gchar *pu = g_build_filename(cli_dir, name_universal, NULL);
      g_autofree gchar *pue = g_strconcat(pu, ".exe", NULL);
      if (g_file_test(pu, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(pu);
      if (g_file_test(pue, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(pue);
      // Try absolute project path if available
      if (abs_root) {
          g_autofree gchar *apu = g_build_filename(abs_root, "build", "cli", name_universal, NULL);
          g_autofree gchar *apue = g_strconcat(apu, ".exe", NULL);
          if (g_file_test(apu, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(apu);
          if (g_file_test(apue, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(apue);
      }
      g_autofree gchar *p1 = g_build_filename(cli_dir, name_cli, NULL);
      g_autofree gchar *p1e = g_strconcat(p1, ".exe", NULL);
      if (g_file_test(p1, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(p1);
      if (g_file_test(p1e, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(p1e);
      if (abs_root) {
          g_autofree gchar *ap1 = g_build_filename(abs_root, "build", "cli", name_cli, NULL);
          g_autofree gchar *ap1e = g_strconcat(ap1, ".exe", NULL);
          if (g_file_test(ap1, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(ap1);
          if (g_file_test(ap1e, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(ap1e);
      }
      // Fallback to bin/<fallback> (accept both with and without .exe suffix)
      g_autofree gchar *p2 = g_build_filename(bin_dir, name_fallback, NULL);
      g_autofree gchar *p2e = g_strconcat(p2, ".exe", NULL);
      if (g_file_test(p2, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(p2);
      if (g_file_test(p2e, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(p2e);
      if (abs_root) {
          g_autofree gchar *ap2 = g_build_filename(abs_root, "bin", name_fallback, NULL);
          g_autofree gchar *ap2e = g_strconcat(ap2, ".exe", NULL);
          if (g_file_test(ap2, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(ap2);
          if (g_file_test(ap2e, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(ap2e);
      }
  #endif
      return NULL;
  }

  static void emit_progress(JobThread *job, gdouble pct) {
      if (job->cb.on_progress) job->cb.on_progress(pct, job->cb.user_data);
  }
  static void emit_log(JobThread *job, const gchar *line) {
      if (job->cb.on_log) job->cb.on_log(line, job->cb.user_data);
  }
  static void emit_error(JobThread *job, const gchar *msg) {
      if (job->cb.on_error) job->cb.on_error(msg, job->cb.user_data);
  }
  static void emit_file_done(JobThread *job, const gchar *out_path) {
      if (job->cb.on_file_done) job->cb.on_file_done(out_path, job->cb.user_data);
  }
  static void emit_finished(JobThread *job) {
      if (job->cb.on_finished) job->cb.on_finished(job->cb.user_data);
  }

  static void parse_and_emit(JobThread *job, const gchar *line) {
      emit_log(job, line);
      const gchar *p = strstr(line, "Progress:");
      if (p) {
          const gchar *num = p + strlen("Progress:");
          while (*num == ' ') num++;
          gdouble pct = g_ascii_strtod(num, NULL);
          emit_progress(job, pct);
          return;
      }
      p = strstr(line, "Output:");
      if (p) {
          const gchar *path = p + strlen("Output:");
          while (*path == ' ') path++;
          emit_file_done(job, path);
          return;
      }
      p = strstr(line, "CRC32:");
      if (p) {
          // For now, just log; GUI may display or ignore
          return;
      }
  }

  static gpointer thread_func(gpointer data) {
      JobThread *job = (JobThread *)data;
      g_autofree gchar *bin = detect_binary(job->mode);
      if (!bin) {
          emit_error(job, "CLI binary not found (build/cli or bin)");
          emit_finished(job);
          return NULL;
      }

      for (guint i = 0; i < job->paths->len; i++) {
          const gchar *path = g_ptr_array_index(job->paths, i);
          // Pre-check readability to provide actionable guidance (macOS Full Disk Access)
        #ifdef G_OS_UNIX
          if (access(path, R_OK) != 0) {
              gchar *msg = g_strdup_printf("Cannot read %s. On macOS, grant Full Disk Access to Terminal/afc_gui or copy the file into this project.", path);
              emit_error(job, msg);
              g_free(msg);
              continue;
          }
        #endif
          g_autoptr(GSubprocess) proc = NULL;
          g_autoptr(GError) err = NULL;
          // Build argv depending on selected binary and mode
          // Treat any binary whose basename starts with "universal" as the unified CLI
          g_autofree gchar *base = g_path_get_basename(bin);
          gboolean is_universal = (base && g_str_has_prefix(base, "universal"));
          if (is_universal) {
              const gchar *argvv[] = { bin,
                                       (job->mode == JOB_MODE_COMPRESS) ? "-c" : "-d",
                                       path,
                                       "-o",
                                       job->out_dir,
                                       NULL };
              proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, &err,
                                      argvv[0], argvv[1], argvv[2], argvv[3], argvv[4], NULL);
          } else {
              // Legacy CLIs expect: <bin> <path> <out_dir>
              const gchar *argvv[] = { bin, path, job->out_dir, NULL };
              proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, &err,
                                      argvv[0], argvv[1], argvv[2], NULL);
          }
          if (!proc) {
              emit_error(job, err ? err->message : "Failed to spawn process");
              continue;
          }

          GInputStream *out = g_subprocess_get_stdout_pipe(proc);
          GInputStream *errp = g_subprocess_get_stderr_pipe(proc);
          gchar buf[1024];
          GString *acc = g_string_new(NULL);

          while (TRUE) {
              gssize r = g_input_stream_read(out, buf, sizeof(buf) - 1, NULL, NULL);
              if (r > 0) {
                  buf[r] = '\0';
                  g_string_append(acc, buf);
                  gchar **lines = g_strsplit(acc->str, "\n", -1);
                  for (gchar **lp = lines; lp && *lp; ++lp) {
                      if (*(lp + 1) == NULL) {
                          // last (maybe partial), keep in acc
                          g_string_assign(acc, *lp);
                          break;
                      }
                      parse_and_emit(job, *lp);
                  }
                  g_strfreev(lines);
              }

              // Read stderr too (errors or progress)
              gssize re = g_input_stream_read(errp, buf, sizeof(buf) - 1, NULL, NULL);
              if (re > 0) {
                  buf[re] = '\0';
                  // Emit stderr lines directly
                  gchar **lines = g_strsplit(buf, "\n", -1);
                  for (gchar **lp = lines; lp && *lp; ++lp) {
                      if (**lp) {
                          emit_log(job, *lp);
                          // Capture likely error lines for dialog
                          if (g_strstr_len(*lp, -1, "Failed to open") ||
                              g_strstr_len(*lp, -1, "Invalid COMP header") ||
                              g_strstr_len(*lp, -1, "CRC mismatch") ||
                              g_strstr_len(*lp, -1, "Compression failed") ||
                              g_strstr_len(*lp, -1, "Decompression failed") ||
                              g_strstr_len(*lp, -1, "Refusing to decompress")) {
                              g_free(job->last_error);
                              job->last_error = g_strdup(*lp);
                          }
                      }
                  }
                  g_strfreev(lines);
              }

              if (g_subprocess_get_if_exited(proc)) {
                  break;
              }
              g_usleep(100000); // 100 ms polling
          }

          int exit_status = g_subprocess_get_exit_status(proc);
          if (exit_status != 0) {
              if (job->last_error) emit_error(job, job->last_error);
              else emit_error(job, "Process failed");
          }
          if (acc->len > 0) {
              // leftover partial line
              parse_and_emit(job, acc->str);
          }
          g_string_free(acc, TRUE);
      }

      emit_finished(job);
      return NULL;
  }

  JobThread *job_thread_new(JobMode mode, const gchar *out_dir, GPtrArray *paths, const JobCallbacks *cb) {
      JobThread *job = g_new0(JobThread, 1);
      job->mode = mode;
      job->out_dir = g_strdup(out_dir);
      job->paths = g_ptr_array_new_with_free_func(g_free);
      job->last_error = NULL;
      for (guint i = 0; i < paths->len; i++) {
          g_ptr_array_add(job->paths, g_strdup((const gchar *)g_ptr_array_index(paths, i)));
      }
      if (cb) job->cb = *cb;
      return job;
  }

  void job_thread_start(JobThread *job) {
      if (!job->thread) {
          job->thread = g_thread_new("job-thread", thread_func, job);
      }
  }

  void job_thread_free(JobThread *job) {
      if (!job) return;
      if (job->thread) {
          g_thread_join(job->thread);
      }
      if (job->paths) g_ptr_array_free(job->paths, TRUE);
      g_free(job->out_dir);
      g_free(job->last_error);
      g_free(job);
  }

#else  /* AFC_HAVE_GIO == 0 */
  // Fallback implementation without GLib/GIO headers.
  // Uses standard C and pthreads to run the CLI via popen().
  #include <stdlib.h>
  #include <string.h>
  #include <stdio.h>
  #include <unistd.h>
  #include <sys/stat.h>
  #include <pthread.h>

  struct _JobThread {
      JobCallbacks cb;
      JobMode mode;
      char *out_dir;
      char **paths;      // array of owned char*
      size_t n_paths;
      pthread_t thread;
      int thread_started;
      char *last_error;  // store last stderr line with meaningful error
  };

  static void emit_progress(JobThread *job, double pct) {
      if (job->cb.on_progress) job->cb.on_progress(pct, job->cb.user_data);
  }
  static void emit_log(JobThread *job, const char *line) {
      if (job->cb.on_log) job->cb.on_log(line, job->cb.user_data);
  }
  static void emit_error(JobThread *job, const char *msg) {
      if (job->cb.on_error) job->cb.on_error(msg, job->cb.user_data);
  }
  static void emit_file_done(JobThread *job, const char *out_path) {
      if (job->cb.on_file_done) job->cb.on_file_done(out_path, job->cb.user_data);
  }
  static void emit_finished(JobThread *job) {
      if (job->cb.on_finished) job->cb.on_finished(job->cb.user_data);
  }

  static int is_executable(const char *path) {
      if (!path) return 0;
      struct stat st;
      if (stat(path, &st) != 0) return 0;
      if (!S_ISREG(st.st_mode)) return 0;
      return access(path, X_OK) == 0;
  }

  static char *join_path(const char *a, const char *b) {
      size_t al = strlen(a), bl = strlen(b);
      int need_sep = (al > 0 && a[al - 1] != '/' && a[al - 1] != '\\');
      char *out = (char *)malloc(al + need_sep + bl + 1);
      if (!out) return NULL;
      strcpy(out, a);
      if (need_sep) strcat(out, "/");
      strcat(out, b);
      return out;
  }

  static char *detect_binary(JobMode mode) {
      const char *cli_dir = "./build/cli";
      const char *bin_dir = "./bin";
      const char *name_universal = "universal";
      const char *name_fallback = (mode == JOB_MODE_COMPRESS) ? "universal_comp" : "universal_decompressor";

      char *pu = join_path(cli_dir, name_universal);
      char *pue = NULL;
      if (pu) {
          if (is_executable(pu)) return pu;
          pue = (char *)malloc(strlen(pu) + 5);
          if (pue) { strcpy(pue, pu); strcat(pue, ".exe"); }
          free(pu);
          if (pue && is_executable(pue)) return pue;
          free(pue);
      }

      char *pf = join_path(bin_dir, name_fallback);
      char *pfe = NULL;
      if (pf) {
          if (is_executable(pf)) return pf;
          pfe = (char *)malloc(strlen(pf) + 5);
          if (pfe) { strcpy(pfe, pf); strcat(pfe, ".exe"); }
          free(pf);
          if (pfe && is_executable(pfe)) return pfe;
          free(pfe);
      }
      return NULL;
  }

  static void parse_and_emit(JobThread *job, const char *line) {
      if (!line || !*line) return;
      emit_log(job, line);
      const char *p = strstr(line, "Progress:");
      if (p) {
          const char *num = p + strlen("Progress:");
          while (*num == ' ') num++;
          double pct = strtod(num, NULL);
          emit_progress(job, pct);
          return;
      }
      p = strstr(line, "Output:");
      if (p) {
          const char *path = p + strlen("Output:");
          while (*path == ' ') path++;
          emit_file_done(job, path);
          return;
      }
      // Capture common error lines from CLI to surface in dialog
      if (strstr(line, "Failed to open") || strstr(line, "Invalid COMP header") ||
          strstr(line, "CRC mismatch") || strstr(line, "Compression failed") ||
          strstr(line, "Decompression failed") || strstr(line, "Refusing to decompress")) {
          free(job->last_error);
          job->last_error = strdup(line);
      }
  }

  static void *thread_func(void *data) {
      JobThread *job = (JobThread *)data;
      char *bin = detect_binary(job->mode);
      if (!bin) {
          emit_error(job, "CLI binary not found (build/cli or bin)");
          emit_finished(job);
          return NULL;
      }

      for (size_t i = 0; i < job->n_paths; i++) {
          const char *path = job->paths[i];
          int is_universal = 0;
          const char *base = strrchr(bin, '/');
          base = base ? base + 1 : bin;
          if (strncmp(base, "universal", 9) == 0) is_universal = 1;

          char cmd[4096];
          if (is_universal) {
              snprintf(cmd, sizeof(cmd), "%s %s \"%s\" -o \"%s\"",
                       bin,
                       (job->mode == JOB_MODE_COMPRESS) ? "-c" : "-d",
                       path,
                       job->out_dir ? job->out_dir : "output");
          } else {
              snprintf(cmd, sizeof(cmd), "%s \"%s\" \"%s\"",
                       bin, path, job->out_dir ? job->out_dir : "output");
          }

          FILE *fp = popen(cmd, "r");
          if (!fp) {
              emit_error(job, "Failed to spawn process");
              continue;
          }
          char line[1024];
          while (fgets(line, sizeof(line), fp)) {
              size_t len = strlen(line);
              if (len && line[len - 1] == '\n') line[len - 1] = '\0';
              parse_and_emit(job, line);
          }
          int status = pclose(fp);
          if (status != 0) {
              if (job->last_error) {
                  emit_error(job, job->last_error);
              } else {
                  emit_error(job, "Process failed");
              }
          }
      }

      free(bin);
      emit_finished(job);
      return NULL;
  }

  JobThread *job_thread_new(JobMode mode, const char *out_dir, void *paths_any, const JobCallbacks *cb) {
      JobThread *job = (JobThread *)calloc(1, sizeof(JobThread));
      if (!job) return NULL;
      job->mode = mode;
      job->out_dir = out_dir ? strdup(out_dir) : strdup("output");

      // Expect paths_any as a NULL-terminated char** when GLib is unavailable.
      job->paths = NULL;
      job->n_paths = 0;
      if (paths_any) {
          char **pp = (char **)paths_any;
          size_t count = 0;
          while (pp[count]) count++;
          if (count > 0) {
              job->paths = (char **)calloc(count, sizeof(char *));
              if (job->paths) {
                  job->n_paths = count;
                  for (size_t i = 0; i < count; i++) job->paths[i] = strdup(pp[i]);
              }
          }
      }
      if (cb) job->cb = *cb;
      job->last_error = NULL;
      return job;
  }

  void job_thread_start(JobThread *job) {
      if (!job) return;
      if (job->thread_started) return;
      job->thread_started = (pthread_create(&job->thread, NULL, thread_func, job) == 0);
  }

  void job_thread_free(JobThread *job) {
      if (!job) return;
      if (job->thread_started) {
          pthread_join(job->thread, NULL);
      }
      if (job->paths) {
          for (size_t i = 0; i < job->n_paths; i++) free(job->paths[i]);
          free(job->paths);
      }
      free(job->last_error);
      free(job->out_dir);
      free(job);
  }
#endif /* AFC_HAVE_GIO */