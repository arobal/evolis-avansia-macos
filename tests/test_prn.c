/*
 * test_prn.c - Parser/Validator mit synthetischen Auftraegen pruefen.
 * Erzeugt ausserdem tests/out/synthetic-2cards.prn fuer weitere Tests.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prn.h"

static int fails = 0;
#define CHECK(cond, name)                                     \
  do {                                                        \
    if (cond) {                                               \
      printf("  ok    %s\n", name);                           \
    } else {                                                  \
      printf("  FEHLER %s\n", name);                          \
      fails++;                                                \
    }                                                         \
  } while (0)

static void cp(FILE *f, const char *name) { fprintf(f, "D,%s\n", name); }

static void frame(FILE *f, const void *payload, size_t n, int prefix) {
  size_t total = 6 + n + (prefix ? 4 : 0);
  fprintf(f, "B,%zu,", total);
  if (prefix) fwrite("Db0,", 1, 4, f);
  unsigned char h[6] = {0x1C, 'P', (unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8),
                        (unsigned char)n};
  fwrite(h, 1, 6, f);
  fwrite(payload, 1, n, f);
  fputc('\n', f);
}

static void cmd(FILE *f, const char *s) { frame(f, s, strlen(s), 0); }

static void plane(FILE *f, size_t datalen) {
  size_t n = 2 + 24 + datalen;
  unsigned char *p = calloc(1, n);
  p[0] = 0x1B;
  p[1] = 'J';
  memset(p + 26, 0x80, datalen);
  frame(f, p, n, 0);
  free(p);
}

typedef enum { OK, BAD_CMD, SHORT_PLANE, TRUNCATED, NO_EJECT, PREFIXED } variant_t;

static void face(FILE *f, int short_plane) {
  cp(f, "FACE_BEGIN");
  cmd(f, "\x1bI0,0,2048,1300,7987200.");
  for (int i = 0; i < 3; i++) plane(f, short_plane && i == 1 ? 1000 : AVA_PLANE_BYTES);
  cmd(f, "\x1b" "C0,0,2048,1300,2662400.");
  plane(f, AVA_PLANE_BYTES);
}

static void card(FILE *f, int duplex, variant_t v) {
  cp(f, "HEADER_BEGIN");
  cp(f, "HEADER_END");
  cp(f, "DOC_BEGIN");
  if (duplex) cp(f, "DUPLEX");
  cp(f, "CARD_BEGIN");
  if (v == PREFIXED)
    frame(f, "\x1b@1.", 4, 1);
  else
    cmd(f, "\x1b@1.");
  if (v == BAD_CMD) cmd(f, "\x1bZ9."); /* nicht freigegebener Befehl */
  face(f, v == SHORT_PLANE);
  if (duplex) {
    cmd(f, "\x1bT");
    cmd(f, "\x1b>");
  }
  cp(f, "FACE_END");
  if (duplex) {
    face(f, 0);
    cp(f, "FACE_END");
  }
  if (v != NO_EJECT) cmd(f, "\x1b>");
  cp(f, "CARD_END");
  if (v != TRUNCATED) cp(f, "DOC_END");
}

static int run(const char *path, int *cards, int *duplex_out) {
  FILE *f = fopen(path, "rb");
  if (!f) return -9;
  char err[256];
  int n = 0, rc = 0;
  for (;;) {
    prn_doc_t d;
    int r = prn_read_doc(f, &d, err, sizeof(err));
    if (r == 0) break;
    if (r < 0) {
      rc = -1;
      break;
    }
    if (prn_validate(&d, err, sizeof(err))) {
      prn_free(&d);
      rc = -2;
      break;
    }
    if (duplex_out) duplex_out[n] = d.duplex;
    n++;
    prn_free(&d);
  }
  fclose(f);
  if (rc && getenv("VERBOSE")) printf("        (%s)\n", err);
  *cards = n;
  return rc;
}

static int make(const char *path, int duplex, variant_t v, int count) {
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  for (int i = 0; i < count; i++) card(f, duplex && i == count - 1, v);
  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "tests/out";
  char p[512];
  int cards, dup[8];
  printf("PRN-Parser / Validator\n");

  snprintf(p, sizeof(p), "%s/synthetic-2cards.prn", dir);
  make(p, 1, OK, 2);
  CHECK(run(p, &cards, dup) == 0 && cards == 2 && dup[0] == 0 && dup[1] == 1,
        "2 Karten (einseitig + beidseitig) gueltig");

  snprintf(p, sizeof(p), "%s/bad-cmd.prn", dir);
  make(p, 0, BAD_CMD, 1);
  CHECK(run(p, &cards, NULL) == -2, "unbekannter Befehl wird abgelehnt");

  snprintf(p, sizeof(p), "%s/short-plane.prn", dir);
  make(p, 0, SHORT_PLANE, 1);
  CHECK(run(p, &cards, NULL) == -2, "unvollstaendiger Farbauszug wird abgelehnt");

  snprintf(p, sizeof(p), "%s/truncated.prn", dir);
  make(p, 0, TRUNCATED, 1);
  CHECK(run(p, &cards, NULL) == -1, "abgeschnittener Auftrag wird abgelehnt");

  snprintf(p, sizeof(p), "%s/no-eject.prn", dir);
  make(p, 0, NO_EJECT, 1);
  CHECK(run(p, &cards, NULL) == -2, "fehlender Kartenauswurf wird abgelehnt");

  snprintf(p, sizeof(p), "%s/prefixed.prn", dir);
  make(p, 0, PREFIXED, 1);
  CHECK(run(p, &cards, NULL) == 0 && cards == 1, "Rahmen mit 'Db?,'-Praefix wird akzeptiert");

  snprintf(p, sizeof(p), "%s/garbage.prn", dir);
  FILE *g = fopen(p, "wb");
  fputs("%!PS-Adobe-3.0\n", g);
  fclose(g);
  CHECK(run(p, &cards, NULL) == -1, "Fremddaten (PostScript) werden abgelehnt");

  printf(fails ? "%d Test(s) fehlgeschlagen\n" : "alle PRN-Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
