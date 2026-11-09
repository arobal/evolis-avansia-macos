/*
 * test_image.c - Kartengeometrie und BMP-Format pruefen.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"

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

static const uint8_t *px(const card_image_t *c, int x, int y) { return c->rgb + ((size_t)y * AVA_CARD_W + x) * 3; }
static int is(const uint8_t *p, int r, int g, int b) { return p[0] == r && p[1] == g && p[2] == b; }

static uint8_t *raster(int w, int h) {
  uint8_t *r = malloc((size_t)w * h * 3);
  memset(r, 255, (size_t)w * h * 3);
  return r;
}
static void set(uint8_t *r, int w, int x, int y, int R, int G, int B) {
  uint8_t *p = r + ((size_t)y * w + x) * 3;
  p[0] = R;
  p[1] = G;
  p[2] = B;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "tests/out";
  printf("Kartengeometrie / BMP\n");
  card_image_t c;
  card_alloc(&c);

  /* 1. CR80 Querformat, 600 dpi: 2022 x 1275, Ecken markiert */
  int w = 2022, h = 1275;
  uint8_t *r = raster(w, h);
  set(r, w, 0, 0, 255, 0, 0);          /* oben links rot */
  set(r, w, w - 1, h - 1, 0, 0, 255);  /* unten rechts blau */
  fit_mode_t m = card_from_raster(&c, r, w, h, 3, 600, 600);
  int ox = (2048 - w) / 2, oy = (1300 - h) / 2; /* 13, 12 */
  CHECK(m == FIT_PHYSICAL, "CR80 600 dpi -> Originalgroesse");
  CHECK(is(px(&c, ox, oy), 255, 0, 0), "Ecke oben links an zentrierter Position");
  CHECK(is(px(&c, 0, 0), 255, 0, 0), "Beschnitt oben links = verlaengerter Randpixel");
  CHECK(is(px(&c, ox + w - 1, oy + h - 1), 0, 0, 255), "Ecke unten rechts an zentrierter Position");
  CHECK(is(px(&c, 2047, 1299), 0, 0, 255), "Beschnitt unten rechts = verlaengerter Randpixel");
  free(r);

  /* 2. CR80 Hochformat 1275 x 2022: oben links muss nach oben rechts (90 Grad im Uhrzeigersinn) */
  w = 1275;
  h = 2022;
  r = raster(w, h);
  set(r, w, 0, 0, 255, 0, 0);
  set(r, w, 0, h - 1, 0, 255, 0); /* unten links gruen -> oben links */
  m = card_from_raster(&c, r, w, h, 3, 600, 600);
  CHECK(m == FIT_PHYSICAL, "Hochformat -> Originalgroesse");
  CHECK(is(px(&c, 2047 - 13, 12), 255, 0, 0) || is(px(&c, 2048 - 13 - 1, 12), 255, 0, 0),
        "Hochformat: oben links -> oben rechts");
  CHECK(is(px(&c, 13, 12), 0, 255, 0), "Hochformat: unten links -> oben links");
  free(r);

  /* 3. Beschnittformat 2048 x 1300 -> 1:1 */
  w = 2048;
  h = 1300;
  r = raster(w, h);
  set(r, w, 0, 0, 1, 2, 3);
  set(r, w, 2047, 1299, 4, 5, 6);
  m = card_from_raster(&c, r, w, h, 3, 600, 600);
  CHECK(m == FIT_PHYSICAL && is(px(&c, 0, 0), 1, 2, 3) && is(px(&c, 2047, 1299), 4, 5, 6),
        "Beschnittformat wird 1:1 uebernommen");
  free(r);

  /* 4. Fremdformat (A6 quer, 300 dpi) -> auf Karte skaliert, Mitte bleibt Mitte */
  w = 1748;
  h = 1240;
  r = raster(w, h);
  for (int y = h / 2 - 5; y < h / 2 + 5; y++)
    for (int x = w / 2 - 5; x < w / 2 + 5; x++) set(r, w, x, y, 0, 0, 0);
  m = card_from_raster(&c, r, w, h, 3, 300, 300);
  CHECK(m == FIT_COVER && is(px(&c, 1024, 650), 0, 0, 0) && is(px(&c, 0, 0), 255, 255, 255),
        "Fremdformat wird skaliert und zentriert");
  free(r);

  /* 5. Graustufen-Raster (1 Byte/Pixel) */
  w = 2022;
  h = 1275;
  r = malloc((size_t)w * h);
  memset(r, 200, (size_t)w * h);
  card_from_raster(&c, r, w, h, 1, 600, 600);
  CHECK(is(px(&c, 1000, 600), 200, 200, 200), "Graustufen werden zu RGB");
  free(r);

  /* 6. Drehung und Leerseiten-Erkennung */
  memset(c.rgb, 255, (size_t)AVA_CARD_W * AVA_CARD_H * 3);
  CHECK(card_is_blank(&c), "weisse Karte ist leer");
  c.rgb[0] = 10;
  CHECK(!card_is_blank(&c), "Karte mit einem Pixel ist nicht leer");
  card_rotate180(&c);
  CHECK(px(&c, 2047, 1299)[0] == 10 && px(&c, 0, 0)[0] == 255, "180-Grad-Drehung");

  /* 7. BMP-Kopf so, wie ihn das Evolis-SDK verlangt */
  char path[512];
  snprintf(path, sizeof(path), "%s/geometry.bmp", dir);
  memset(c.rgb, 255, (size_t)AVA_CARD_W * AVA_CARD_H * 3);
  uint8_t *p0 = c.rgb; /* oben links rot */
  p0[0] = 255;
  p0[1] = 0;
  p0[2] = 0;
  CHECK(card_write_bmp(&c, path) == 0, "BMP schreiben");
  FILE *f = fopen(path, "rb");
  uint8_t hdr[54];
  size_t got = fread(hdr, 1, 54, f);
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  /* oben links liegt bei bottom-up in der letzten Zeile */
  uint8_t pix[3];
  fseek(f, 54 + (long)(AVA_CARD_H - 1) * AVA_CARD_W * 3, SEEK_SET);
  size_t got2 = fread(pix, 1, 3, f);
  fclose(f);
  int wbmp = hdr[18] | hdr[19] << 8, hbmp = hdr[22] | hdr[23] << 8;
  CHECK(got == 54 && hdr[0] == 'B' && hdr[1] == 'M' && wbmp == 2048 && hbmp == 1300 && hdr[28] == 24 &&
            hdr[10] == 54 && hdr[14] == 40 && size == 54 + 2048 * 1300 * 3,
        "BMP: 2048 x 1300, 24 Bit, unkomprimiert, bottom-up");
  CHECK(got2 == 3 && pix[0] == 0 && pix[1] == 0 && pix[2] == 255, "BMP: Pixelreihenfolge BGR, bottom-up");

  card_free(&c);
  printf(fails ? "%d Test(s) fehlgeschlagen\n" : "alle Geometrie-Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
