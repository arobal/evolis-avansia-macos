/*
 * sdk.c - libevolis per dlopen laden und Avansia-Auftraege offline erzeugen.
 *
 * libevolis wird NICHT mit diesem Projekt verteilt. Das Installationsskript
 * laedt das offizielle Paket "evolis-sdk" von PyPI (SHA-256 geprueft) und
 * entnimmt daraus lib/macos/libevolis.dylib (Universal: arm64 + x86_64).
 *
 * SPDX-License-Identifier: MIT
 */
#include "sdk.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*fn_open_t)(const char *, int);
typedef void (*fn_close_t)(void *);
typedef bool (*fn_setting_t)(void *, int, const char *);
typedef void (*fn_forced_t)(void *, const char *, const char *);
typedef int (*fn_image_t)(void *, int, const char *);
typedef int (*fn_tofile_t)(void *, const char *);
typedef const char *(*fn_version_t)(void);
typedef void (*fn_log_console_t)(bool);

static struct {
  void *lib;
  fn_open_t open;
  fn_close_t close;
  fn_setting_t setting;
  fn_forced_t forced;
  fn_image_t imagep;
  fn_tofile_t tofile;
  fn_version_t version;
} S;

#define SETTING_RIBBON_TYPE 52 /* evolis_settingkey_t GRibbonType */
#define OFFLINE_DEVICE "socket://127.0.0.1:65534?type=avansia"
#define OPEN_MODE_DIRECT 1

static void seterr(char *err, size_t errlen, const char *fmt, ...) {
  if (!err || !errlen) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, errlen, fmt, ap);
  va_end(ap);
}

#define LOAD(field, name)                                             \
  do {                                                                \
    *(void **)(&S.field) = dlsym(S.lib, name);                        \
    if (!S.field) {                                                   \
      seterr(err, errlen, "Funktion %s fehlt in libevolis", name);    \
      dlclose(S.lib);                                                 \
      S.lib = NULL;                                                   \
      return -1;                                                      \
    }                                                                 \
  } while (0)

int ava_sdk_load(const char *path, char *err, size_t errlen) {
  if (S.lib) return 0;
  if (!path || !*path) path = getenv("AVANSIA_SDK");
  if (!path || !*path) path = AVANSIA_DEFAULT_SDK;
  S.lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!S.lib) {
    seterr(err, errlen, "Evolis-SDK nicht ladbar (%s): %s", path, dlerror());
    return -1;
  }
  LOAD(open, "evolis_open_with_mode");
  LOAD(close, "evolis_close");
  LOAD(setting, "evolis_print_set_setting");
  LOAD(forced, "evolis_print_set_forced_setting");
  LOAD(imagep, "evolis_print_set_imagep");
  LOAD(tofile, "evolis_print_to_file");
  LOAD(version, "evolis_version");
  /* SDK-Protokollausgaben nicht auf stdout schreiben lassen. */
  fn_log_console_t console = (fn_log_console_t)dlsym(S.lib, "evolis_log_set_console");
  if (console) console(false);
  return 0;
}

const char *ava_sdk_version(void) {
  const char *v = S.version ? S.version() : NULL;
  return v ? v : "?";
}

static int valid_choice(const char *v, const char *const *allowed) {
  for (int i = 0; allowed[i]; i++)
    if (v && !strcmp(v, allowed[i])) return 1;
  return 0;
}

int ava_sdk_make_prn(const char *front, const char *back, const ava_sdk_opts_t *o, const char *out,
                     char *err, size_t errlen) {
  static const char *const ribbons[] = {"YMCK", "YMCKK", NULL};
  static const char *const blacks[] = {"ALLBLACKPOINT", "NOBLACKPOINT", NULL};
  if (!S.lib) {
    seterr(err, errlen, "Evolis-SDK nicht geladen");
    return -1;
  }
  if (!valid_choice(o->ribbon, ribbons) || !valid_choice(o->black_front, blacks) ||
      !valid_choice(o->black_back, blacks)) {
    seterr(err, errlen, "Nicht unterstuetzte Option (Farbband %s, Schwarz %s/%s)", o->ribbon, o->black_front,
           o->black_back);
    return -1;
  }
  void *ctx = S.open(OFFLINE_DEVICE, OPEN_MODE_DIRECT);
  if (!ctx) {
    seterr(err, errlen, "SDK-Kontext fuer die Avansia konnte nicht angelegt werden");
    return -1;
  }
  int rc = -1;
  char ribbon[32];
  snprintf(ribbon, sizeof(ribbon), "RC_%s_A", o->ribbon);
  if (!S.setting(ctx, SETTING_RIBBON_TYPE, ribbon)) {
    seterr(err, errlen, "SDK lehnt Farbband %s ab", ribbon);
    goto done;
  }
  S.forced(ctx, "FBlackManagement", o->black_front);
  if (S.imagep(ctx, 0, front) < 0) {
    seterr(err, errlen, "SDK konnte das Vorderseitenbild nicht laden");
    goto done;
  }
  if (back) {
    S.forced(ctx, "BBlackManagement", o->black_back);
    S.forced(ctx, "GDuplexType", "DUPLEX_CC");
    S.forced(ctx, "BPageRotate180", o->back_rotate180 ? "ON" : "OFF");
    if (S.imagep(ctx, 1, back) < 0) {
      seterr(err, errlen, "SDK konnte das Rueckseitenbild nicht laden");
      goto done;
    }
  }
  int r = S.tofile(ctx, out);
  if (r < 0) {
    seterr(err, errlen, "SDK-Rendering fehlgeschlagen (Code %d)", r);
    goto done;
  }
  rc = 0;
done:
  S.close(ctx);
  return rc;
}
