#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
# include <sched.h>
# include <sys/types.h>
# include <unistd.h>
# include <dirent.h>
# include <time.h>
# define SLASH '/'
# define SLASH_STR "/"
# define IS_SLASH(c) ((c) == '/')
#else
# define SLASH '\\'
# define SLASH_STR "\\"
# define IS_SLASH(c) ((c) == '/' || (c) == '\\')
#endif /* _WIN32 */

#define UVWASI__READDIR_NUM_ENTRIES 1

#include "uvwasi.h"
#include "uv.h"
#include "uv_mapping.h"
#include "fd_table.h"
#include "clocks.h"


static int uvwasi__is_absolute_path(const char* path, size_t path_len) {
  /* TODO(cjihrig): Add a Windows implementation. */
  return path != NULL && path_len > 0 && path[0] == SLASH;
}


static uvwasi_errno_t uvwasi__resolve_path(const struct uvwasi_fd_wrap_t* fd,
                                           const char* path,
                                           size_t path_len,
                                           char* resolved_path,
                                           uvwasi_lookupflags_t flags) {
  /* TODO(cjihrig): path_len is treated as a size. Need to verify if path_len is
     really a string length or a size. Also need to verify if it is null
     terminated. */
  uv_fs_t realpath_req;
  uvwasi_errno_t err;
  char* abs_path;
  char* tok;
  char* ptr;
  int realpath_size;
  int abs_size;
  int input_is_absolute;
  int r;
#ifdef _WIN32
  int i;
#endif /* _WIN32 */

  err = UVWASI_ESUCCESS;
  input_is_absolute = uvwasi__is_absolute_path(path, path_len);

  if (1 == input_is_absolute) {
    /* TODO(cjihrig): Revisit this. Copying is probably not necessary here. */
    abs_size = path_len;
    abs_path = malloc(abs_size);
    if (abs_path == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    memcpy(abs_path, path, abs_size);
  } else {
    /* Resolve the relative path to fd's real path. */
    abs_size = path_len + strlen(fd->real_path) + 2;
    abs_path = malloc(abs_size);
    if (abs_path == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    r = snprintf(abs_path, abs_size, "%s/%s", fd->real_path, path);
    if (r <= 0) {
      err = uvwasi__translate_uv_error(uv_translate_sys_error(errno));
      goto exit;
    }
  }

#ifdef _WIN32
  /* On Windows, convert slashes to backslashes. */
  for (i = 0; i < abs_size; ++i) {
    if (abs_path[i] == '/')
      abs_path[i] = SLASH;
  }
#endif /* _WIN32 */

  ptr = resolved_path;
  tok = strtok(abs_path, SLASH_STR);
  for (; tok != NULL; tok = strtok(NULL, SLASH_STR)) {
    if (0 == strcmp(tok, "."))
      continue;

    if (0 == strcmp(tok, "..")) {
      while (*ptr != SLASH && ptr != resolved_path)
        ptr--;
      *ptr = '\0';
      continue;
    }

#ifdef _WIN32
    /* On Windows, prevent a leading slash in the path. */
    if (ptr == resolved_path)
      r = sprintf(ptr, "%s", tok);
    else
#endif /* _WIN32 */
    r = sprintf(ptr, "%c%s", SLASH, tok);

    if (r < 1) { /* At least one character should have been written. */
      err = uvwasi__translate_uv_error(uv_translate_sys_error(errno));
      goto exit;
    }

    ptr += r;
  }

  if ((flags & UVWASI_LOOKUP_SYMLINK_FOLLOW) == UVWASI_LOOKUP_SYMLINK_FOLLOW) {
    r = uv_fs_realpath(NULL, &realpath_req, resolved_path, NULL);
    if (r == 0) {
      realpath_size = strlen(realpath_req.ptr) + 1;
      if (realpath_size > PATH_MAX_BYTES) {
        err = UVWASI_ENOBUFS;
        uv_fs_req_cleanup(&realpath_req);
        goto exit;
      }

      memcpy(resolved_path, realpath_req.ptr, realpath_size);
    } else if (r != UV_ENOENT) {
      /* Report errors except ENOENT. */
      err = uvwasi__translate_uv_error(r);
      uv_fs_req_cleanup(&realpath_req);
      goto exit;
    }

    uv_fs_req_cleanup(&realpath_req);
  }

  /* Verify that the resolved path is still in the sandbox. */
  if (resolved_path != strstr(resolved_path, fd->real_path)) {
    err = UVWASI_ENOTCAPABLE;
    goto exit;
  }

exit:
  free(abs_path);
  return err;
}


static uvwasi_errno_t uvwasi__lseek(uv_file fd,
                                    uvwasi_filedelta_t offset,
                                    uvwasi_whence_t whence,
                                    uvwasi_filesize_t* newoffset) {
  int real_whence;

  if (whence == UVWASI_WHENCE_CUR)
    real_whence = SEEK_CUR;
  else if (whence == UVWASI_WHENCE_END)
    real_whence = SEEK_END;
  else if (whence == UVWASI_WHENCE_SET)
    real_whence = SEEK_SET;
  else
    return UVWASI_EINVAL;

#ifdef _WIN32
  int64_t r;

  r = _lseeki64(fd, offset, real_whence);
  if (-1L == r)
    return uvwasi__translate_uv_error(uv_translate_sys_error(errno));
#else
  off_t r;

  r = lseek(fd, offset, real_whence);
  if ((off_t) -1 == r)
    return uvwasi__translate_uv_error(uv_translate_sys_error(errno));
#endif /* _WIN32 */

  *newoffset = r;
  return UVWASI_ESUCCESS;
}


static uvwasi_errno_t uvwasi__setup_iovs(uv_buf_t** buffers,
                                         const uvwasi_iovec_t* iovs,
                                         size_t iovs_len) {
  uv_buf_t* bufs;
  size_t i;

  bufs = malloc(iovs_len * sizeof(*bufs));
  if (bufs == NULL)
    return UVWASI_ENOMEM;

  for (i = 0; i < iovs_len; ++i)
    bufs[i] = uv_buf_init(iovs[i].buf, iovs[i].buf_len);

  *buffers = bufs;
  return UVWASI_ESUCCESS;
}


static uvwasi_errno_t uvwasi__setup_ciovs(uv_buf_t** buffers,
                                          const uvwasi_ciovec_t* iovs,
                                          size_t iovs_len) {
  uv_buf_t* bufs;
  size_t i;

  bufs = malloc(iovs_len * sizeof(*bufs));
  if (bufs == NULL)
    return UVWASI_ENOMEM;

  for (i = 0; i < iovs_len; ++i)
    bufs[i] = uv_buf_init((char*)iovs[i].buf, iovs[i].buf_len);

  *buffers = bufs;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_init(uvwasi_t* uvwasi, uvwasi_options_t* options) {
  uv_fs_t realpath_req;
  uv_fs_t open_req;
  uvwasi_errno_t err;
  size_t args_size;
  size_t size;
  size_t offset;
  size_t env_count;
  size_t env_buf_size;
  size_t i;
  int r;

  if (uvwasi == NULL || options == NULL || options->fd_table_size == 0)
    return UVWASI_EINVAL;

  uvwasi->argv_buf = NULL;
  uvwasi->argv = NULL;
  uvwasi->env_buf = NULL;
  uvwasi->env = NULL;
  uvwasi->fds.fds = NULL;

  args_size = 0;
  for (i = 0; i < options->argc; ++i)
    args_size += strlen(options->argv[i]) + 1;

  uvwasi->argc = options->argc;
  uvwasi->argv_buf_size = args_size;

  if (args_size > 0) {
    uvwasi->argv_buf = malloc(args_size);
    if (uvwasi->argv_buf == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    uvwasi->argv = calloc(options->argc, sizeof(char*));
    if (uvwasi->argv == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    offset = 0;
    for (i = 0; i < options->argc; ++i) {
      size = strlen(options->argv[i]) + 1;
      memcpy(uvwasi->argv_buf + offset, options->argv[i], size);
      uvwasi->argv[i] = uvwasi->argv_buf + offset;
      offset += size;
    }
  }

  env_count = 0;
  env_buf_size = 0;
  if (options->envp != NULL) {
    while (options->envp[env_count] != NULL) {
      env_buf_size += strlen(options->envp[env_count]) + 1;
      env_count++;
    }
  }

  uvwasi->envc = env_count;
  uvwasi->env_buf_size = env_buf_size;

  if (env_buf_size > 0) {
    uvwasi->env_buf = malloc(env_buf_size);
    if (uvwasi->env_buf == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    uvwasi->env = calloc(env_count, sizeof(char*));
    if (uvwasi->env == NULL) {
      err = UVWASI_ENOMEM;
      goto exit;
    }

    offset = 0;
    for (i = 0; i < env_count; ++i) {
      size = strlen(options->envp[i]) + 1;
      memcpy(uvwasi->env_buf + offset, options->envp[i], size);
      uvwasi->env[i] = uvwasi->env_buf + offset;
      offset += size;
    }
  }

  for (i = 0; i < options->preopenc; ++i) {
    if (options->preopens[i].real_path == NULL ||
        options->preopens[i].mapped_path == NULL) {
      err = UVWASI_EINVAL;
      goto exit;
    }
  }

  err = uvwasi_fd_table_init(&uvwasi->fds, options->fd_table_size);
  if (err != UVWASI_ESUCCESS)
    goto exit;

  for (i = 0; i < options->preopenc; ++i) {
    r = uv_fs_realpath(NULL,
                       &realpath_req,
                       options->preopens[i].real_path,
                       NULL);
    if (r != 0) {
      err = uvwasi__translate_uv_error(r);
      uv_fs_req_cleanup(&realpath_req);
      goto exit;
    }

    r = uv_fs_open(NULL, &open_req, realpath_req.ptr, 0, 0666, NULL);
    if (r < 0) {
      err = uvwasi__translate_uv_error(r);
      uv_fs_req_cleanup(&realpath_req);
      uv_fs_req_cleanup(&open_req);
      goto exit;
    }

    err = uvwasi_fd_table_insert_preopen(&uvwasi->fds,
                                         open_req.result,
                                         options->preopens[i].mapped_path,
                                         realpath_req.ptr);
    uv_fs_req_cleanup(&realpath_req);
    uv_fs_req_cleanup(&open_req);

    if (err != UVWASI_ESUCCESS)
      goto exit;
  }

  return UVWASI_ESUCCESS;

exit:
  uvwasi_destroy(uvwasi);
  return err;
}


void uvwasi_destroy(uvwasi_t* uvwasi) {
  if (uvwasi == NULL)
    return;

  uvwasi_fd_table_free(&uvwasi->fds);
  free(uvwasi->argv_buf);
  free(uvwasi->argv);
  free(uvwasi->env_buf);
  free(uvwasi->env);
  uvwasi->argv_buf = NULL;
  uvwasi->argv = NULL;
  uvwasi->env_buf = NULL;
  uvwasi->env = NULL;
}


uvwasi_errno_t uvwasi_embedder_remap_fd(uvwasi_t* uvwasi,
                                        const uvwasi_fd_t fd,
                                        uv_file new_host_fd) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  wrap->fd = new_host_fd;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_args_get(uvwasi_t* uvwasi, char** argv, char* argv_buf) {
  size_t i;

  if (uvwasi == NULL || argv == NULL || argv_buf == NULL)
    return UVWASI_EINVAL;

  for (i = 0; i < uvwasi->argc; ++i) {
    argv[i] = argv_buf + (uvwasi->argv[i] - uvwasi->argv_buf);
  }

  memcpy(argv_buf, uvwasi->argv_buf, uvwasi->argv_buf_size);
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_args_sizes_get(uvwasi_t* uvwasi,
                                     size_t* argc,
                                     size_t* argv_buf_size) {
  if (uvwasi == NULL || argc == NULL || argv_buf_size == NULL)
    return UVWASI_EINVAL;

  *argc = uvwasi->argc;
  *argv_buf_size = uvwasi->argv_buf_size;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_clock_res_get(uvwasi_t* uvwasi,
                                    uvwasi_clockid_t clock_id,
                                    uvwasi_timestamp_t* resolution) {
  if (uvwasi == NULL || resolution == NULL)
    return UVWASI_EINVAL;

  switch (clock_id) {
    case UVWASI_CLOCK_MONOTONIC:
    case UVWASI_CLOCK_REALTIME:
      *resolution = 1;  /* Nanosecond precision. */
      return UVWASI_ESUCCESS;
    case UVWASI_CLOCK_PROCESS_CPUTIME_ID:
      return uvwasi__clock_getres_process_cputime(resolution);
    case UVWASI_CLOCK_THREAD_CPUTIME_ID:
      return uvwasi__clock_getres_thread_cputime(resolution);
    default:
      return UVWASI_EINVAL;
  }
}


uvwasi_errno_t uvwasi_clock_time_get(uvwasi_t* uvwasi,
                                     uvwasi_clockid_t clock_id,
                                     uvwasi_timestamp_t precision,
                                     uvwasi_timestamp_t* time) {
  if (uvwasi == NULL || time == NULL)
    return UVWASI_EINVAL;

  switch (clock_id) {
    case UVWASI_CLOCK_MONOTONIC:
      *time = uv_hrtime();
      return UVWASI_ESUCCESS;
    case UVWASI_CLOCK_REALTIME:
      return uvwasi__clock_gettime_realtime(time);
    case UVWASI_CLOCK_PROCESS_CPUTIME_ID:
      return uvwasi__clock_gettime_process_cputime(time);
    case UVWASI_CLOCK_THREAD_CPUTIME_ID:
      return uvwasi__clock_gettime_thread_cputime(time);
    default:
      return UVWASI_EINVAL;
  }
}


uvwasi_errno_t uvwasi_environ_get(uvwasi_t* uvwasi,
                                  char** environment,
                                  char* environ_buf) {
  size_t i;

  if (uvwasi == NULL || environment == NULL || environ_buf == NULL)
    return UVWASI_EINVAL;

  for (i = 0; i < uvwasi->envc; ++i) {
    environment[i] = environ_buf + (uvwasi->env[i] - uvwasi->env_buf);
  }

  memcpy(environ_buf, uvwasi->env_buf, uvwasi->env_buf_size);
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_environ_sizes_get(uvwasi_t* uvwasi,
                                        size_t* environ_count,
                                        size_t* environ_buf_size) {
  if (uvwasi == NULL || environ_count == NULL || environ_buf_size == NULL)
    return UVWASI_EINVAL;

  *environ_count = uvwasi->envc;
  *environ_buf_size = uvwasi->env_buf_size;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_advise(uvwasi_t* uvwasi,
                                uvwasi_fd_t fd,
                                uvwasi_filesize_t offset,
                                uvwasi_filesize_t len,
                                uvwasi_advice_t advice) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
#ifdef POSIX_FADV_NORMAL
  int mapped_advice;
  int r;
#endif /* POSIX_FADV_NORMAL */

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  switch (advice) {
    case UVWASI_ADVICE_DONTNEED:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_DONTNEED;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_NOREUSE:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_NOREUSE;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_NORMAL:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_NORMAL;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_RANDOM:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_RANDOM;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_SEQUENTIAL:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_SEQUENTIAL;
#endif /* POSIX_FADV_NORMAL */
      break;
    case UVWASI_ADVICE_WILLNEED:
#ifdef POSIX_FADV_NORMAL
      mapped_advice = POSIX_FADV_WILLNEED;
#endif /* POSIX_FADV_NORMAL */
      break;
    default:
      return UVWASI_EINVAL;
  }

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_ADVISE, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

#ifdef POSIX_FADV_NORMAL
  r = posix_fadvise(wrap->fd, offset, len, mapped_advice);
  if (r != 0)
    return uvwasi__translate_uv_error(uv_translate_sys_error(r));
#endif /* POSIX_FADV_NORMAL */
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_allocate(uvwasi_t* uvwasi,
                                  uvwasi_fd_t fd,
                                  uvwasi_filesize_t offset,
                                  uvwasi_filesize_t len) {
#if !defined(__POSIX__)
  uv_fs_t req;
  uint64_t st_size;
#endif /* !__POSIX__ */
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_ALLOCATE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  /* Try to use posix_fallocate(). If that's not an option, fall back to the
     race condition prone combination of fstat() + ftruncate(). */
#if defined(__POSIX__)
  r = posix_fallocate(wrap->fd, offset, len);
  if (r != 0)
    return uvwasi__translate_uv_error(uv_translate_sys_error(r));
#else
  r = uv_fs_fstat(NULL, &req, wrap->fd, NULL);
  st_size = req.statbuf.st_size;
  uv_fs_req_cleanup(&req);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  if (st_size < offset + len) {
    r = uv_fs_ftruncate(NULL, &req, wrap->fd, offset + len, NULL);
    if (r != 0)
      return uvwasi__translate_uv_error(r);
  }
#endif /* __POSIX__ */

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_close(uvwasi_t* uvwasi, uvwasi_fd_t fd) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_close(NULL, &req, wrap->fd, NULL);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return uvwasi_fd_table_remove(&uvwasi->fds, fd);
}


uvwasi_errno_t uvwasi_fd_datasync(uvwasi_t* uvwasi, uvwasi_fd_t fd) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_DATASYNC,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_fdatasync(NULL, &req, wrap->fd, NULL);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_fdstat_get(uvwasi_t* uvwasi,
                                    uvwasi_fd_t fd,
                                    uvwasi_fdstat_t* buf) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
#ifndef _WIN32
  int r;
#endif

  if (uvwasi == NULL || buf == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  buf->fs_filetype = wrap->type;
  buf->fs_rights_base = wrap->rights_base;
  buf->fs_rights_inheriting = wrap->rights_inheriting;
#ifdef _WIN32
  buf->fs_flags = 0;  /* TODO(cjihrig): Missing Windows support. */
#else
  r = fcntl(wrap->fd, F_GETFL);
  if (r < 0)
    return uvwasi__translate_uv_error(uv_translate_sys_error(errno));
  buf->fs_flags = r;
#endif /* _WIN32 */

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_fdstat_set_flags(uvwasi_t* uvwasi,
                                          uvwasi_fd_t fd,
                                          uvwasi_fdflags_t flags) {
#ifdef _WIN32
  /* TODO(cjihrig): Missing Windows support. */
  return UVWASI_ENOSYS;
#else
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  int mapped_flags;
  int r;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_FDSTAT_SET_FLAGS,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  mapped_flags = 0;

  if ((flags & UVWASI_FDFLAG_APPEND) == UVWASI_FDFLAG_APPEND)
    mapped_flags |= O_APPEND;

  if ((flags & UVWASI_FDFLAG_DSYNC) == UVWASI_FDFLAG_DSYNC)
#ifdef O_DSYNC
    mapped_flags |= O_DSYNC;
#else
    mapped_flags |= O_SYNC;
#endif /* O_DSYNC */

  if ((flags & UVWASI_FDFLAG_NONBLOCK) == UVWASI_FDFLAG_NONBLOCK)
    mapped_flags |= O_NONBLOCK;

  if ((flags & UVWASI_FDFLAG_RSYNC) == UVWASI_FDFLAG_RSYNC)
#ifdef O_RSYNC
    mapped_flags |= O_RSYNC;
#else
    mapped_flags |= O_SYNC;
#endif /* O_RSYNC */

  if ((flags & UVWASI_FDFLAG_SYNC) == UVWASI_FDFLAG_SYNC)
    mapped_flags |= O_SYNC;

  r = fcntl(wrap->fd, F_SETFL, mapped_flags);
  if (r < 0)
    return uvwasi__translate_uv_error(uv_translate_sys_error(errno));

  return UVWASI_ESUCCESS;
#endif /* _WIN32 */
}


uvwasi_errno_t uvwasi_fd_fdstat_set_rights(uvwasi_t* uvwasi,
                                           uvwasi_fd_t fd,
                                           uvwasi_rights_t fs_rights_base,
                                           uvwasi_rights_t fs_rights_inheriting
                                          ) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  /* Check for attempts to add new permissions. */
  if ((fs_rights_base | wrap->rights_base) > wrap->rights_base)
    return UVWASI_ENOTCAPABLE;
  if ((fs_rights_inheriting | wrap->rights_inheriting) >
      wrap->rights_inheriting) {
    return UVWASI_ENOTCAPABLE;
  }

  wrap->rights_base = fs_rights_base;
  wrap->rights_inheriting = fs_rights_inheriting;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_filestat_get(uvwasi_t* uvwasi,
                                      uvwasi_fd_t fd,
                                      uvwasi_filestat_t* buf) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL || buf == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_FILESTAT_GET,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_fstat(NULL, &req, wrap->fd, NULL);
  if (r != 0) {
    uv_fs_req_cleanup(&req);
    return uvwasi__translate_uv_error(r);
  }

  uvwasi__stat_to_filestat(&req.statbuf, buf);
  uv_fs_req_cleanup(&req);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_filestat_set_size(uvwasi_t* uvwasi,
                                           uvwasi_fd_t fd,
                                           uvwasi_filesize_t st_size) {
  /* TODO(cjihrig): uv_fs_ftruncate() takes an int64_t. st_size is uint64_t. */
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_FILESTAT_SET_SIZE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_ftruncate(NULL, &req, wrap->fd, st_size, NULL);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_filestat_set_times(uvwasi_t* uvwasi,
                                            uvwasi_fd_t fd,
                                            uvwasi_timestamp_t st_atim,
                                            uvwasi_timestamp_t st_mtim,
                                            uvwasi_fstflags_t fst_flags) {
  /* TODO(cjihrig): libuv does not currently support nanosecond precision. */
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  if (fst_flags & ~(UVWASI_FILESTAT_SET_ATIM | UVWASI_FILESTAT_SET_ATIM_NOW |
                    UVWASI_FILESTAT_SET_MTIM | UVWASI_FILESTAT_SET_MTIM_NOW)) {
    return UVWASI_EINVAL;
  }

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_FILESTAT_SET_TIMES,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  /* TODO(cjihrig): st_atim and st_mtim should not be unconditionally passed. */
  r = uv_fs_futime(NULL, &req, wrap->fd, st_atim, st_mtim, NULL);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_pread(uvwasi_t* uvwasi,
                               uvwasi_fd_t fd,
                               const uvwasi_iovec_t* iovs,
                               size_t iovs_len,
                               uvwasi_filesize_t offset,
                               size_t* nread) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_buf_t* bufs;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t uvread;
  int r;

  if (uvwasi == NULL || iovs == NULL || nread == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_READ | UVWASI_RIGHT_FD_SEEK,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__setup_iovs(&bufs, iovs, iovs_len);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_read(NULL, &req, wrap->fd, bufs, iovs_len, offset, NULL);
  uvread = req.result;
  uv_fs_req_cleanup(&req);
  free(bufs);

  if (r < 0)
    return uvwasi__translate_uv_error(r);

  *nread = uvread;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_prestat_get(uvwasi_t* uvwasi,
                                     uvwasi_fd_t fd,
                                     uvwasi_prestat_t* buf) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  if (uvwasi == NULL || buf == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;
  if (wrap->preopen != 1)
    return UVWASI_EINVAL;

  buf->pr_type = UVWASI_PREOPENTYPE_DIR;
  buf->u.dir.pr_name_len = strlen(wrap->path) + 1;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_prestat_dir_name(uvwasi_t* uvwasi,
                                          uvwasi_fd_t fd,
                                          char* path,
                                          size_t path_len) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  size_t size;

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;
  if (wrap->preopen != 1)
    return UVWASI_EBADF;

  size = strlen(wrap->path) + 1;
  if (size > path_len)
    return UVWASI_ENOBUFS;

  memcpy(path, wrap->path, size);
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_pwrite(uvwasi_t* uvwasi,
                                uvwasi_fd_t fd,
                                const uvwasi_ciovec_t* iovs,
                                size_t iovs_len,
                                uvwasi_filesize_t offset,
                                size_t* nwritten) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_buf_t* bufs;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t uvwritten;
  int r;

  if (uvwasi == NULL || iovs == NULL || nwritten == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_WRITE | UVWASI_RIGHT_FD_SEEK,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__setup_ciovs(&bufs, iovs, iovs_len);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_write(NULL, &req, wrap->fd, bufs, iovs_len, offset, NULL);
  uvwritten = req.result;
  uv_fs_req_cleanup(&req);
  free(bufs);

  if (r < 0)
    return uvwasi__translate_uv_error(r);

  *nwritten = uvwritten;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_read(uvwasi_t* uvwasi,
                              uvwasi_fd_t fd,
                              const uvwasi_iovec_t* iovs,
                              size_t iovs_len,
                              size_t* nread) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_buf_t* bufs;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t uvread;
  int r;

  if (uvwasi == NULL || iovs == NULL || nread == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_READ, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__setup_iovs(&bufs, iovs, iovs_len);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_read(NULL, &req, wrap->fd, bufs, iovs_len, -1, NULL);
  uvread = req.result;
  uv_fs_req_cleanup(&req);
  free(bufs);

  if (r < 0)
    return uvwasi__translate_uv_error(r);

  *nread = uvread;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_readdir(uvwasi_t* uvwasi,
                                 uvwasi_fd_t fd,
                                 void* buf,
                                 size_t buf_len,
                                 uvwasi_dircookie_t cookie,
                                 size_t* bufused) {
  /* TODO(cjihrig): Support Windows where seekdir() and telldir() are used. */
  /* TODO(cjihrig): Avoid opening and closing the directory on each call. */
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_dirent_t dirent;
  uv_dirent_t dirents[UVWASI__READDIR_NUM_ENTRIES];
  uv_dir_t* dir;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t name_len;
  size_t available;
  size_t size_to_cp;
  long tell;
  int i;
  int r;

  if (uvwasi == NULL || buf == NULL || bufused == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_READDIR,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  /* Open the directory. */
  r = uv_fs_opendir(NULL, &req, wrap->real_path, NULL);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  /* Setup for reading the directory. */
  dir = req.ptr;
  dir->dirents = dirents;
  dir->nentries = UVWASI__READDIR_NUM_ENTRIES;
  uv_fs_req_cleanup(&req);

#ifndef _WIN32
  /* TODO(cjihrig): Need a Windows equivalent of this logic. */
  /* Seek to the proper location in the directory. */
  if (cookie != UVWASI_DIRCOOKIE_START)
    seekdir(dir->dir, cookie);
#endif

  /* Read the directory entries into the provided buffer. */
  err = UVWASI_ESUCCESS;
  *bufused = 0;
  while (0 != (r = uv_fs_readdir(NULL, &req, dir, NULL))) {
    if (r < 0) {
      err = uvwasi__translate_uv_error(r);
      uv_fs_req_cleanup(&req);
      goto exit;
    }

    for (i = 0; i < r; i++) {
      /* TODO(cjihrig): This should probably be serialized to the buffer
         consistently across platforms. In other words, d_next should always
         be 8 bytes, d_ino should always be 8 bytes, d_namlen should always be
         4 bytes, and d_type should always be 1 byte. */
#ifndef _WIN32
      tell = telldir(dir->dir);
      if (tell < 0) {
        err = uvwasi__translate_uv_error(uv_translate_sys_error(errno));
        uv_fs_req_cleanup(&req);
        goto exit;
      }
#else
      tell = 0; /* TODO(cjihrig): Need to support Windows. */
#endif /* _WIN32 */

      name_len = strlen(dirents[i].name);
      dirent.d_next = (uvwasi_dircookie_t) tell;
      /* TODO(cjihrig): Missing ino libuv (and Windows) support. fstat()? */
      dirent.d_ino = 0;
      dirent.d_namlen = name_len;

      switch (dirents[i].type) {
        case UV_DIRENT_FILE:
          dirent.d_type = UVWASI_FILETYPE_REGULAR_FILE;
          break;
        case UV_DIRENT_DIR:
          dirent.d_type = UVWASI_FILETYPE_DIRECTORY;
          break;
        case UV_DIRENT_SOCKET:
          dirent.d_type = UVWASI_FILETYPE_SOCKET_STREAM;
          break;
        case UV_DIRENT_LINK:
          dirent.d_type = UVWASI_FILETYPE_SYMBOLIC_LINK;
          break;
        case UV_DIRENT_CHAR:
          dirent.d_type = UVWASI_FILETYPE_CHARACTER_DEVICE;
          break;
        case UV_DIRENT_BLOCK:
          dirent.d_type = UVWASI_FILETYPE_BLOCK_DEVICE;
          break;
        case UV_DIRENT_FIFO:
        case UV_DIRENT_UNKNOWN:
        default:
          dirent.d_type = UVWASI_FILETYPE_UNKNOWN;
          break;
      }

      /* Write dirent to the buffer. */
      available = buf_len - *bufused;
      size_to_cp = sizeof(dirent) > available ? available : sizeof(dirent);
      memcpy((char*)buf + *bufused, &dirent, size_to_cp);
      *bufused += size_to_cp;
      /* Write the entry name to the buffer. */
      available = buf_len - *bufused;
      size_to_cp = name_len > available ? available : name_len;
      memcpy((char*)buf + *bufused, &dirents[i].name, size_to_cp);
      *bufused += size_to_cp;
    }

    uv_fs_req_cleanup(&req);

    if (*bufused >= buf_len)
      break;
  }

exit:
  /* Close the directory. */
  r = uv_fs_closedir(NULL, &req, dir, NULL);
  uv_fs_req_cleanup(&req);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return err;
}


uvwasi_errno_t uvwasi_fd_renumber(uvwasi_t* uvwasi,
                                  uvwasi_fd_t from,
                                  uvwasi_fd_t to) {
  struct uvwasi_fd_wrap_t* to_wrap;
  struct uvwasi_fd_wrap_t* from_wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, from, &from_wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi_fd_table_get(&uvwasi->fds, to, &to_wrap, 0, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_close(NULL, &req, to_wrap->fd, NULL);
  uv_fs_req_cleanup(&req);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  memcpy(to_wrap, from_wrap, sizeof(*to_wrap));
  to_wrap->id = to;

  return uvwasi_fd_table_remove(&uvwasi->fds, from);
}


uvwasi_errno_t uvwasi_fd_seek(uvwasi_t* uvwasi,
                              uvwasi_fd_t fd,
                              uvwasi_filedelta_t offset,
                              uvwasi_whence_t whence,
                              uvwasi_filesize_t* newoffset) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  if (uvwasi == NULL || newoffset == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_SEEK, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  return uvwasi__lseek(wrap->fd, offset, whence, newoffset);
}


uvwasi_errno_t uvwasi_fd_sync(uvwasi_t* uvwasi, uvwasi_fd_t fd) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_FD_SYNC,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_fsync(NULL, &req, wrap->fd, NULL);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_fd_tell(uvwasi_t* uvwasi,
                              uvwasi_fd_t fd,
                              uvwasi_filesize_t* offset) {
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;

  if (uvwasi == NULL || offset == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_TELL, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  return uvwasi__lseek(wrap->fd, 0, UVWASI_WHENCE_CUR, offset);
}


uvwasi_errno_t uvwasi_fd_write(uvwasi_t* uvwasi,
                               uvwasi_fd_t fd,
                               const uvwasi_ciovec_t* iovs,
                               size_t iovs_len,
                               size_t* nwritten) {
  struct uvwasi_fd_wrap_t* wrap;
  uv_buf_t* bufs;
  uv_fs_t req;
  uvwasi_errno_t err;
  size_t uvwritten;
  int r;

  if (uvwasi == NULL || iovs == NULL || nwritten == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds, fd, &wrap, UVWASI_RIGHT_FD_WRITE, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__setup_ciovs(&bufs, iovs, iovs_len);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_write(NULL, &req, wrap->fd, bufs, iovs_len, -1, NULL);
  uvwritten = req.result;
  uv_fs_req_cleanup(&req);
  free(bufs);

  if (r < 0)
    return uvwasi__translate_uv_error(r);

  *nwritten = uvwritten;
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_create_directory(uvwasi_t* uvwasi,
                                            uvwasi_fd_t fd,
                                            const char* path,
                                            size_t path_len) {
  char resolved_path[PATH_MAX_BYTES];
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_CREATE_DIRECTORY,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(wrap, path, path_len, resolved_path, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_mkdir(NULL, &req, resolved_path, 0777, NULL);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_filestat_get(uvwasi_t* uvwasi,
                                        uvwasi_fd_t fd,
                                        uvwasi_lookupflags_t flags,
                                        const char* path,
                                        size_t path_len,
                                        uvwasi_filestat_t* buf) {
  char resolved_path[PATH_MAX_BYTES];
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL || path == NULL || buf == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_FILESTAT_GET,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(wrap, path, path_len, resolved_path, flags);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_stat(NULL, &req, resolved_path, NULL);
  if (r != 0) {
    uv_fs_req_cleanup(&req);
    return uvwasi__translate_uv_error(r);
  }

  uvwasi__stat_to_filestat(&req.statbuf, buf);
  uv_fs_req_cleanup(&req);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_filestat_set_times(uvwasi_t* uvwasi,
                                              uvwasi_fd_t fd,
                                              uvwasi_lookupflags_t flags,
                                              const char* path,
                                              size_t path_len,
                                              uvwasi_timestamp_t st_atim,
                                              uvwasi_timestamp_t st_mtim,
                                              uvwasi_fstflags_t fst_flags) {
  /* TODO(cjihrig): libuv does not currently support nanosecond precision. */
  char resolved_path[PATH_MAX_BYTES];
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  if (fst_flags & ~(UVWASI_FILESTAT_SET_ATIM | UVWASI_FILESTAT_SET_ATIM_NOW |
                    UVWASI_FILESTAT_SET_MTIM | UVWASI_FILESTAT_SET_MTIM_NOW)) {
    return UVWASI_EINVAL;
  }

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_FILESTAT_SET_TIMES,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(wrap, path, path_len, resolved_path, flags);
  if (err != UVWASI_ESUCCESS)
    return err;

  /* TODO(cjihrig): st_atim and st_mtim should not be unconditionally passed. */
  r = uv_fs_utime(NULL, &req, resolved_path, st_atim, st_mtim, NULL);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_link(uvwasi_t* uvwasi,
                                uvwasi_fd_t old_fd,
                                uvwasi_lookupflags_t old_flags,
                                const char* old_path,
                                size_t old_path_len,
                                uvwasi_fd_t new_fd,
                                const char* new_path,
                                size_t new_path_len) {
  char resolved_old_path[PATH_MAX_BYTES];
  char resolved_new_path[PATH_MAX_BYTES];
  struct uvwasi_fd_wrap_t* old_wrap;
  struct uvwasi_fd_wrap_t* new_wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  if (uvwasi == NULL || old_path == NULL || new_path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            old_fd,
                            &old_wrap,
                            UVWASI_RIGHT_PATH_LINK_SOURCE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            new_fd,
                            &new_wrap,
                            UVWASI_RIGHT_PATH_LINK_TARGET,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(old_wrap,
                             old_path,
                             old_path_len,
                             resolved_old_path,
                             old_flags);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(new_wrap,
                             new_path,
                             new_path_len,
                             resolved_new_path,
                             0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_link(NULL, &req, resolved_old_path, resolved_new_path, NULL);
  uv_fs_req_cleanup(&req);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_open(uvwasi_t* uvwasi,
                                uvwasi_fd_t dirfd,
                                uvwasi_lookupflags_t dirflags,
                                const char* path,
                                size_t path_len,
                                uvwasi_oflags_t o_flags,
                                uvwasi_rights_t fs_rights_base,
                                uvwasi_rights_t fs_rights_inheriting,
                                uvwasi_fdflags_t fs_flags,
                                uvwasi_fd_t* fd) {
  char resolved_path[PATH_MAX_BYTES];
  uvwasi_rights_t needed_inheriting;
  uvwasi_rights_t needed_base;
  struct uvwasi_fd_wrap_t* dirfd_wrap;
  struct uvwasi_fd_wrap_t wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int flags;
  int read;
  int write;
  int r;

  if (uvwasi == NULL || path == NULL || fd == NULL)
    return UVWASI_EINVAL;

  read = 0 != (fs_rights_base & (UVWASI_RIGHT_FD_READ |
                                 UVWASI_RIGHT_FD_READDIR));
  write = 0 != (fs_rights_base & (UVWASI_RIGHT_FD_DATASYNC |
                                  UVWASI_RIGHT_FD_WRITE |
                                  UVWASI_RIGHT_FD_ALLOCATE |
                                  UVWASI_RIGHT_FD_FILESTAT_SET_SIZE));
  flags = write ? read ? UV_FS_O_RDWR : UV_FS_O_WRONLY : UV_FS_O_RDONLY;
  needed_base = UVWASI_RIGHT_PATH_OPEN;
  needed_inheriting = fs_rights_base | fs_rights_inheriting;

  if ((o_flags & UVWASI_O_CREAT) != 0) {
    flags |= UV_FS_O_CREAT;
    needed_base |= UVWASI_RIGHT_PATH_CREATE_FILE;
  }
  if ((o_flags & UVWASI_O_DIRECTORY) != 0)
    flags |= UV_FS_O_DIRECTORY;
  if ((o_flags & UVWASI_O_EXCL) != 0)
    flags |= UV_FS_O_EXCL;
  if ((o_flags & UVWASI_O_TRUNC) != 0) {
    flags |= UV_FS_O_TRUNC;
    needed_base |= UVWASI_RIGHT_PATH_FILESTAT_SET_SIZE;
  }

  if ((fs_flags & UVWASI_FDFLAG_APPEND) != 0)
    flags |= UV_FS_O_APPEND;
  if ((fs_flags & UVWASI_FDFLAG_DSYNC) != 0) {
    flags |= UV_FS_O_DSYNC;
    needed_inheriting |= UVWASI_RIGHT_FD_DATASYNC;
  }
  if ((fs_flags & UVWASI_FDFLAG_NONBLOCK) != 0)
    flags |= UV_FS_O_NONBLOCK;
  if ((fs_flags & UVWASI_FDFLAG_RSYNC) != 0) {
#ifdef O_RSYNC
    flags |= O_RSYNC; /* libuv has no UV_FS_O_RSYNC. */
#else
    flags |= UV_FS_O_SYNC;
#endif
    needed_inheriting |= UVWASI_RIGHT_FD_SYNC;
  }
  if ((fs_flags & UVWASI_FDFLAG_SYNC) != 0) {
    flags |= UV_FS_O_SYNC;
    needed_inheriting |= UVWASI_RIGHT_FD_SYNC;
  }
  if (write && (flags & (UV_FS_O_APPEND | UV_FS_O_TRUNC)) == 0)
    needed_inheriting |= UVWASI_RIGHT_FD_SEEK;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            dirfd,
                            &dirfd_wrap,
                            needed_base,
                            needed_inheriting);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(dirfd_wrap,
                             path,
                             path_len,
                             resolved_path,
                             dirflags);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_open(NULL, &req, resolved_path, flags, 0666, NULL);
  uv_fs_req_cleanup(&req);

  if (r < 0)
    return uvwasi__translate_uv_error(r);

  err = uvwasi_fd_table_insert_fd(&uvwasi->fds,
                                  r,
                                  flags,
                                  resolved_path,
                                  fs_rights_base,
                                  fs_rights_inheriting,
                                  &wrap);
  if (err != UVWASI_ESUCCESS)
    goto close_file_and_error_exit;

  /* Not all platforms support UV_FS_O_DIRECTORY, so enforce it here as well. */
  if ((o_flags & UVWASI_O_DIRECTORY) != 0 &&
      wrap.type != UVWASI_FILETYPE_DIRECTORY) {
    uvwasi_fd_table_remove(&uvwasi->fds, wrap.id);
    err = UVWASI_ENOTDIR;
    goto close_file_and_error_exit;
  }

  *fd = wrap.id;
  return UVWASI_ESUCCESS;

close_file_and_error_exit:
  uv_fs_close(NULL, &req, r, NULL);
  uv_fs_req_cleanup(&req);
  return err;
}


uvwasi_errno_t uvwasi_path_readlink(uvwasi_t* uvwasi,
                                    uvwasi_fd_t fd,
                                    const char* path,
                                    size_t path_len,
                                    char* buf,
                                    size_t buf_len,
                                    size_t* bufused) {
  char resolved_path[PATH_MAX_BYTES];
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  size_t len;
  int r;

  if (uvwasi == NULL || path == NULL || buf == NULL || bufused == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_READLINK,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(wrap, path, path_len, resolved_path, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_readlink(NULL, &req, resolved_path, NULL);
  if (r != 0) {
    uv_fs_req_cleanup(&req);
    return uvwasi__translate_uv_error(r);
  }

  len = strnlen(req.ptr, buf_len);
  if (len >= buf_len) {
    uv_fs_req_cleanup(&req);
    return UVWASI_ENOBUFS;
  }

  memcpy(buf, req.ptr, len);
  buf[len] = '\0';
  *bufused = len + 1;
  uv_fs_req_cleanup(&req);
  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_remove_directory(uvwasi_t* uvwasi,
                                            uvwasi_fd_t fd,
                                            const char* path,
                                            size_t path_len) {
  char resolved_path[PATH_MAX_BYTES];
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_REMOVE_DIRECTORY,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(wrap, path, path_len, resolved_path, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_rmdir(NULL, &req, resolved_path, NULL);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_rename(uvwasi_t* uvwasi,
                                  uvwasi_fd_t old_fd,
                                  const char* old_path,
                                  size_t old_path_len,
                                  uvwasi_fd_t new_fd,
                                  const char* new_path,
                                  size_t new_path_len) {
  char resolved_old_path[PATH_MAX_BYTES];
  char resolved_new_path[PATH_MAX_BYTES];
  struct uvwasi_fd_wrap_t* old_wrap;
  struct uvwasi_fd_wrap_t* new_wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  if (uvwasi == NULL || old_path == NULL || new_path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            old_fd,
                            &old_wrap,
                            UVWASI_RIGHT_PATH_RENAME_SOURCE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            new_fd,
                            &new_wrap,
                            UVWASI_RIGHT_PATH_RENAME_TARGET,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(old_wrap,
                             old_path,
                             old_path_len,
                             resolved_old_path,
                             0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(new_wrap,
                             new_path,
                             new_path_len,
                             resolved_new_path,
                             0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_rename(NULL, &req, resolved_old_path, resolved_new_path, NULL);
  uv_fs_req_cleanup(&req);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_symlink(uvwasi_t* uvwasi,
                                   const char* old_path,
                                   size_t old_path_len,
                                   uvwasi_fd_t fd,
                                   const char* new_path,
                                   size_t new_path_len) {
  char resolved_new_path[PATH_MAX_BYTES];
  struct uvwasi_fd_wrap_t* wrap;
  uvwasi_errno_t err;
  uv_fs_t req;
  int r;

  if (uvwasi == NULL || old_path == NULL || new_path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_SYMLINK,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(wrap,
                             new_path,
                             new_path_len,
                             resolved_new_path,
                             0);
  if (err != UVWASI_ESUCCESS)
    return err;

  /* Windows support may require setting the flags option. */
  r = uv_fs_symlink(NULL, &req, old_path, resolved_new_path, 0, NULL);
  uv_fs_req_cleanup(&req);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_path_unlink_file(uvwasi_t* uvwasi,
                                       uvwasi_fd_t fd,
                                       const char* path,
                                       size_t path_len) {
  char resolved_path[PATH_MAX_BYTES];
  struct uvwasi_fd_wrap_t* wrap;
  uv_fs_t req;
  uvwasi_errno_t err;
  int r;

  if (uvwasi == NULL || path == NULL)
    return UVWASI_EINVAL;

  err = uvwasi_fd_table_get(&uvwasi->fds,
                            fd,
                            &wrap,
                            UVWASI_RIGHT_PATH_UNLINK_FILE,
                            0);
  if (err != UVWASI_ESUCCESS)
    return err;

  err = uvwasi__resolve_path(wrap, path, path_len, resolved_path, 0);
  if (err != UVWASI_ESUCCESS)
    return err;

  r = uv_fs_unlink(NULL, &req, resolved_path, NULL);
  uv_fs_req_cleanup(&req);

  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_poll_oneoff(uvwasi_t* uvwasi,
                                  const uvwasi_subscription_t* in,
                                  uvwasi_event_t* out,
                                  size_t nsubscriptions,
                                  size_t* nevents) {
  /* TODO(cjihrig): Implement this. */
  return UVWASI_ENOTSUP;
}


uvwasi_errno_t uvwasi_proc_exit(uvwasi_t* uvwasi, uvwasi_exitcode_t rval) {
  exit(rval);
  return UVWASI_ESUCCESS; /* This doesn't happen. */
}


uvwasi_errno_t uvwasi_proc_raise(uvwasi_t* uvwasi, uvwasi_signal_t sig) {
  int r;

  if (uvwasi == NULL)
    return UVWASI_EINVAL;

  r = uvwasi__translate_to_uv_signal(sig);
  if (r == -1)
    return UVWASI_ENOSYS;

  r = uv_kill(uv_os_getpid(), r);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_random_get(uvwasi_t* uvwasi, void* buf, size_t buf_len) {
  int r;

  if (uvwasi == NULL || buf == NULL)
    return UVWASI_EINVAL;

  r = uv_random(NULL, NULL, buf, buf_len, 0, NULL);
  if (r != 0)
    return uvwasi__translate_uv_error(r);

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_sched_yield(uvwasi_t* uvwasi) {
  if (uvwasi == NULL)
    return UVWASI_EINVAL;

#ifndef _WIN32
  if (0 != sched_yield())
    return uvwasi__translate_uv_error(uv_translate_sys_error(errno));
#else
  SwitchToThread();
#endif /* _WIN32 */

  return UVWASI_ESUCCESS;
}


uvwasi_errno_t uvwasi_sock_recv(uvwasi_t* uvwasi,
                                uvwasi_fd_t sock,
                                const uvwasi_iovec_t* ri_data,
                                size_t ri_data_len,
                                uvwasi_riflags_t ri_flags,
                                size_t* ro_datalen,
                                uvwasi_roflags_t* ro_flags) {
  /* TODO(cjihrig): Waiting to implement, pending
                    https://github.com/WebAssembly/WASI/issues/4 */
  return UVWASI_ENOTSUP;
}


uvwasi_errno_t uvwasi_sock_send(uvwasi_t* uvwasi,
                                uvwasi_fd_t sock,
                                const uvwasi_ciovec_t* si_data,
                                size_t si_data_len,
                                uvwasi_siflags_t si_flags,
                                size_t* so_datalen) {
  /* TODO(cjihrig): Waiting to implement, pending
                    https://github.com/WebAssembly/WASI/issues/4 */
  return UVWASI_ENOTSUP;
}


uvwasi_errno_t uvwasi_sock_shutdown(uvwasi_t* uvwasi,
                                    uvwasi_fd_t sock,
                                    uvwasi_sdflags_t how) {
  /* TODO(cjihrig): Waiting to implement, pending
                    https://github.com/WebAssembly/WASI/issues/4 */
  return UVWASI_ENOTSUP;
}
