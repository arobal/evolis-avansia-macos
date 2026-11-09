/*
 * rastertoavansia - CUPS-Filter: CUPS-Raster (RGB, 600 dpi) -> Avansia-Auftrag.
 *
 *   rastertoavansia job user title copies options [datei]
 *
 * Jede Seite wird ein Kartenbild (2048 x 1300). Bei beidseitigem Druck
 * bilden je zwei Seiten eine Karte. Fuer jede Karte erzeugt das offizielle
 * Evolis-SDK (offline) die Druckerdaten; der Filter haengt sie an stdout an.
 *
 * Umgebungsvariablen (fuer Tests):
 *   AVANSIA_SDK        Pfad zu libevolis.dylib
 *   AVANSIA_DUMP_DIR   Kartenbilder (BMP) zusaetzlich hier ablegen
 *   AVANSIA_NO_SDK=1   SDK nicht aufrufen (nur Geometrie testen, keine Ausgabe)
 *
 * SPDX-License-Identifier: MIT
 */
#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "image.h"
#include "sdk.h"

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

typedef struct {
  char ribbon[16];
  char black[24];
  int front_rot180;
  int back_rot180_user;
  int skip_blank_back;
} filter_opts_t;

static volatile sig_atomic_t g_cancel = 0;
static void on_term(int sig) {
  (void)sig;
  g_cancel = 1;
}

static int g_out = -1;         /* echter stdout (Auftragsdaten) */
static char g_tmp[1024] = "";  /* Arbeitsverzeichnis */
static const char *g_dump = NULL;
static int g_no_sdk = 0;

static void choice(ppd_file_t *ppd, int n, cups_option_t *o, const char *key, char *dst, size_t len,
                   const char *def) {
  const char *v = cupsGetOption(key, n, o);
  if (!v && ppd) {
    ppd_choice_t *c = ppdFindMarkedChoice(ppd, key);
    if (c) v = c->choice;
  }
  snprintf(dst, len, "%s", v ? v : def);
}

static int truthy(const char *s) {
  return !strcasecmp(s, "true") || !strcasecmp(s, "yes") || !strcasecmp(s, "on") || !strcmp(s, "1");
}

static void read_options(const char *optstr, filter_opts_t *f) {
  cups_option_t *o = NULL;
  int n = cupsParseOptions(optstr, 0, &o);
  ppd_file_t *ppd = NULL;
  const char *ppdpath = getenv("PPD");
  if (ppdpath && (ppd = ppdOpenFile(ppdpath)) != NULL) {
    ppdMarkDefaults(ppd);
    cupsMarkOptions(ppd, n, o);
  }
  char buf[32];
  choice(ppd, n, o, "AvansiaRibbon", f->ribbon, sizeof(f->ribbon), "YMCK");
  choice(ppd, n, o, "AvansiaBlack", f->black, sizeof(f->black), "ALLBLACKPOINT");
  choice(ppd, n, o, "AvansiaFrontRotate", buf, sizeof(buf), "False");
  f->front_rot180 = truthy(buf);
  choice(ppd, n, o, "AvansiaBackRotate", buf, sizeof(buf), "False");
  f->back_rot180_user = truthy(buf);
  choice(ppd, n, o, "AvansiaSkipBlankBack", buf, sizeof(buf), "True");
  f->skip_blank_back = truthy(buf);
  if (ppd) ppdClose(ppd);
  cupsFreeOptions(n, o);
}

static int copy_file_to_out(const char *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  static char buf[1 << 16];
  ssize_t r;
  int rc = 0;
  while ((r = read(fd, buf, sizeof(buf))) > 0) {
    char *p = buf;
    while (r > 0) {
      ssize_t w = write(g_out, p, (size_t)r);
      if (w < 0) {
        if (errno == EINTR) continue;
        rc = -1;
        goto out;
      }
      p += w;
      r -= w;
    }
  }
  if (r < 0) rc = -1;
out:
  close(fd);
  return rc;
}

/* Erzeugt eine Karte und schreibt deren Auftragsdaten nach stdout. */
static int emit_card(int no, card_image_t *front, card_image_t *back, int duplex_tumble,
                     const filter_opts_t *f) {
  char fpath[1200], bpath[1200], ppath[1200];
  snprintf(fpath, sizeof(fpath), "%s/karte-%03d-vorne.bmp", g_tmp, no);
  snprintf(bpath, sizeof(bpath), "%s/karte-%03d-hinten.bmp", g_tmp, no);
  snprintf(ppath, sizeof(ppath), "%s/karte-%03d.prn", g_tmp, no);

  if (f->front_rot180) card_rotate180(front);
  if (back && f->skip_blank_back && card_is_blank(back)) {
    fprintf(stderr, "INFO: Karte %d: Rueckseite ist leer und wird nicht gedruckt\n", no);
    back = NULL;
  }
  /* An der echten Avansia geprueft (Firmware 6.1): Ohne Drehung passt die
   * Rueckseite, wenn man die Querformat-Karte um die KURZE Kante wendet
   * (seitlich, wie eine Buchseite). Fuer das Wenden um die LANGE Kante
   * (Duplex=DuplexNoTumble) - z. B. eine Hochformat-Karte seitlich
   * umdrehen - muss die Rueckseite daher um 180 Grad gedreht werden.
   * Das entspricht der CUPS-Definition von Tumble. */
  int back_rot = back ? ((!duplex_tumble) ^ f->back_rot180_user) : 0;

  if (card_write_bmp(front, fpath) || (back && card_write_bmp(back, bpath))) {
    fprintf(stderr, "ERROR: Kartenbild konnte nicht geschrieben werden (%s)\n", strerror(errno));
    return -1;
  }
  if (g_dump) {
    char d[1200];
    snprintf(d, sizeof(d), "%s/karte-%03d-vorne.bmp", g_dump, no);
    card_write_bmp(front, d);
    if (back) {
      snprintf(d, sizeof(d), "%s/karte-%03d-hinten.bmp", g_dump, no);
      card_write_bmp(back, d);
    }
  }
  fprintf(stderr, "DEBUG: Karte %d: %s, Farbband %s, Schwarz %s, Rueckseite gedreht: %s\n", no,
          back ? "beidseitig" : "einseitig", f->ribbon, f->black, back_rot ? "ja" : "nein");

  int rc = 0;
  if (!g_no_sdk) {
    ava_sdk_opts_t o = {f->ribbon, f->black, f->black, back_rot};
    char err[256];
    if (ava_sdk_make_prn(fpath, back ? bpath : NULL, &o, ppath, err, sizeof(err))) {
      fprintf(stderr, "ERROR: %s\n", err);
      rc = -1;
    } else if (copy_file_to_out(ppath)) {
      fprintf(stderr, "ERROR: Auftragsdaten konnten nicht weitergegeben werden\n");
      rc = -1;
    }
  }
  unlink(fpath);
  unlink(bpath);
  unlink(ppath);
  if (!rc) fprintf(stderr, "PAGE: %d 1\n", no);
  return rc;
}

static void cleanup_tmp(void) {
  if (g_tmp[0]) rmdir(g_tmp);
}

int main(int argc, char *argv[]) {
  if (argc < 6 || argc > 7) {
    fprintf(stderr, "Aufruf: %s job user title copies options [datei]\n", argv[0]);
    return 1;
  }
  /* SDK-Ausgaben duerfen den Datenstrom nicht verunreinigen:
   * echten stdout sichern, fd 1 auf stderr umbiegen. */
  g_out = dup(1);
  dup2(2, 1);

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_term;
  sigaction(SIGTERM, &sa, NULL);

  g_dump = getenv("AVANSIA_DUMP_DIR");
  g_no_sdk = getenv("AVANSIA_NO_SDK") && !strcmp(getenv("AVANSIA_NO_SDK"), "1");

  filter_opts_t f;
  memset(&f, 0, sizeof(f));
  read_options(argv[5], &f);

  if (!g_no_sdk) {
    char err[512];
    if (ava_sdk_load(NULL, err, sizeof(err))) {
      fprintf(stderr, "ERROR: %s\n", err);
      fprintf(stderr, "ERROR: Bitte das Installationsskript erneut ausfuehren.\n");
      return 1;
    }
    fprintf(stderr, "DEBUG: Evolis SDK %s geladen\n", ava_sdk_version());
  }

  const char *tmpbase = getenv("TMPDIR");
  if (!tmpbase || !*tmpbase) tmpbase = "/tmp";
  snprintf(g_tmp, sizeof(g_tmp), "%s/avansia.XXXXXX", tmpbase);
  if (!mkdtemp(g_tmp)) {
    fprintf(stderr, "ERROR: Temporaeres Verzeichnis nicht anlegbar: %s\n", strerror(errno));
    return 1;
  }
  atexit(cleanup_tmp);

  int fd = 0;
  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
    fprintf(stderr, "ERROR: Datei %s nicht lesbar\n", argv[6]);
    return 1;
  }
  cups_raster_t *ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
  if (!ras) {
    fprintf(stderr, "ERROR: Kein CUPS-Raster in der Eingabe\n");
    return 1;
  }

  cups_page_header2_t hdr;
  card_image_t pending = {0}, cur = {0};
  int have_pending = 0, pending_tumble = 0;
  int page = 0, cards = 0, rc = 0;
  uint8_t *pagebuf = NULL;
  size_t pagecap = 0;

  while (!g_cancel && cupsRasterReadHeader2(ras, &hdr)) {
    page++;
    int bpp = (int)hdr.cupsBitsPerPixel / 8;
    if (hdr.cupsBitsPerColor != 8 || (bpp != 3 && bpp != 1) || hdr.cupsColorOrder != CUPS_ORDER_CHUNKED) {
      fprintf(stderr, "ERROR: Nicht unterstuetztes Rasterformat (%u Bit/Farbe, %u Bit/Pixel, Farbraum %u)\n",
              hdr.cupsBitsPerColor, hdr.cupsBitsPerPixel, hdr.cupsColorSpace);
      rc = 1;
      break;
    }
    size_t need = (size_t)hdr.cupsBytesPerLine * hdr.cupsHeight;
    if (need > pagecap) {
      free(pagebuf);
      pagebuf = malloc(need);
      pagecap = pagebuf ? need : 0;
      if (!pagebuf) {
        fprintf(stderr, "ERROR: Zu wenig Arbeitsspeicher\n");
        rc = 1;
        break;
      }
    }
    for (unsigned y = 0; y < hdr.cupsHeight; y++) {
      if (cupsRasterReadPixels(ras, pagebuf + (size_t)y * hdr.cupsBytesPerLine, hdr.cupsBytesPerLine) !=
          hdr.cupsBytesPerLine) {
        fprintf(stderr, "ERROR: Raster von Seite %d ist unvollstaendig\n", page);
        rc = 1;
        goto done;
      }
    }
    /* Graustufen (1 Byte) direkt; RGB: Zeilenlaenge kann gepolstert sein */
    uint8_t *src = pagebuf;
    uint8_t *packed = NULL;
    if (hdr.cupsBytesPerLine != hdr.cupsWidth * (unsigned)bpp) {
      packed = malloc((size_t)hdr.cupsWidth * hdr.cupsHeight * bpp);
      if (!packed) {
        rc = 1;
        break;
      }
      for (unsigned y = 0; y < hdr.cupsHeight; y++)
        memcpy(packed + (size_t)y * hdr.cupsWidth * bpp, pagebuf + (size_t)y * hdr.cupsBytesPerLine,
               (size_t)hdr.cupsWidth * bpp);
      src = packed;
    }
    if (card_alloc(&cur)) {
      free(packed);
      rc = 1;
      break;
    }
    fit_mode_t m = card_from_raster(&cur, src, (int)hdr.cupsWidth, (int)hdr.cupsHeight, bpp,
                                    (int)hdr.HWResolution[0], (int)hdr.HWResolution[1]);
    free(packed);
    fprintf(stderr, "DEBUG: Seite %d: %ux%u px @ %ux%u dpi, %s\n", page, hdr.cupsWidth, hdr.cupsHeight,
            hdr.HWResolution[0], hdr.HWResolution[1],
            m == FIT_PHYSICAL ? "Originalgroesse + Randbeschnitt" : "auf Kartenflaeche skaliert");

    if (hdr.Duplex) {
      if (!have_pending) {
        pending = cur;
        cur.rgb = NULL;
        have_pending = 1;
        pending_tumble = hdr.Tumble ? 1 : 0;
        continue;
      }
      fprintf(stderr, "INFO: Bereite Karte %d vor\n", cards + 1);
      if (emit_card(++cards, &pending, &cur, pending_tumble, &f)) rc = 1;
      card_free(&pending);
      card_free(&cur);
      have_pending = 0;
    } else {
      if (have_pending) { /* Wechsel von Duplex auf Simplex */
        if (emit_card(++cards, &pending, NULL, 0, &f)) rc = 1;
        card_free(&pending);
        have_pending = 0;
      }
      fprintf(stderr, "INFO: Bereite Karte %d vor\n", cards + 1);
      if (emit_card(++cards, &cur, NULL, 0, &f)) rc = 1;
      card_free(&cur);
    }
    if (rc) break;
  }
  if (!rc && !g_cancel && have_pending) {
    if (emit_card(++cards, &pending, NULL, 0, &f)) rc = 1;
  }
done:
  card_free(&pending);
  card_free(&cur);
  free(pagebuf);
  cupsRasterClose(ras);
  if (fd) close(fd);
  if (!rc && cards == 0 && !g_cancel) {
    fprintf(stderr, "ERROR: Keine Seiten im Druckauftrag\n");
    rc = 1;
  }
  if (!rc) fprintf(stderr, "INFO: %d Karte(n) vorbereitet\n", cards);
  close(g_out);
  return rc;
}
