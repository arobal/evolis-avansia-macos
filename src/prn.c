/*
 * prn.c - Parser und Validator fuer Avansia-Druckauftraege.
 *
 * Die erwartete Struktur entspricht dem, was libevolis (Evolis SDK 9.4.x)
 * fuer eine Avansia-Karte erzeugt und was im Projekt
 * github.com/qianxueye/avansia-macos-driver (MIT) an echter Hardware
 * verifiziert wurde. Alles andere wird abgelehnt, bevor ein Byte an den
 * Drucker geht.
 *
 * SPDX-License-Identifier: MIT
 */
#include "prn.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BLOCK (32u * 1024u * 1024u)
#define PLANE_PAYLOAD (2 + 24 + AVA_PLANE_BYTES) /* ESC J + 24 Byte Kopf + Daten */

static const char *const kCheckpoints[] = {
    "HEADER_BEGIN", "HEADER_END", "DOC_BEGIN", "DOC_END", "DUPLEX",
    "CARD_BEGIN",   "CARD_END",   "FACE_BEGIN", "FACE_END", NULL};

static void seterr(char *err, size_t errlen, const char *fmt, ...) {
  if (!err || !errlen) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, errlen, fmt, ap);
  va_end(ap);
}

static int known_checkpoint(const char *name) {
  for (int i = 0; kCheckpoints[i]; i++)
    if (!strcmp(name, kCheckpoints[i])) return 1;
  return 0;
}

static int push(prn_doc_t *doc, prn_record_t *r) {
  if (doc->count == doc->cap) {
    size_t cap = doc->cap ? doc->cap * 2 : 32;
    prn_record_t *n = realloc(doc->recs, cap * sizeof(*n));
    if (!n) return -1;
    doc->recs = n;
    doc->cap = cap;
  }
  doc->recs[doc->count++] = *r;
  return 0;
}

void prn_free(prn_doc_t *doc) {
  if (!doc) return;
  for (size_t i = 0; i < doc->count; i++) free(doc->recs[i].frame);
  free(doc->recs);
  memset(doc, 0, sizeof(*doc));
}

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

const uint8_t *prn_payload(const prn_record_t *r, size_t *len) {
  if (r->kind != PRN_FRAME || r->frame_len < 6) {
    *len = 0;
    return NULL;
  }
  *len = r->frame_len - 6;
  return r->frame + 6;
}

int prn_read_doc(FILE *in, prn_doc_t *doc, char *err, size_t errlen) {
  memset(doc, 0, sizeof(*doc));
  for (;;) {
    int kind = fgetc(in);
    if (kind == EOF) {
      if (doc->count == 0) return 0;
      seterr(err, errlen, "Auftrag endet mitten in einer Karte");
      goto fail;
    }
    if (fgetc(in) != ',') {
      seterr(err, errlen, "Unbekanntes Datensatzformat (Typ 0x%02x)", kind);
      goto fail;
    }
    prn_record_t r;
    memset(&r, 0, sizeof(r));
    if (kind == 'D') {
      size_t n = 0;
      int c;
      while ((c = fgetc(in)) != EOF && c != '\n' && n < sizeof(r.name) - 1) r.name[n++] = (char)c;
      if (c != '\n' || !known_checkpoint(r.name)) {
        seterr(err, errlen, "Unbekannte oder defekte Ablaufmarke '%s'", r.name);
        goto fail;
      }
      r.kind = PRN_CHECKPOINT;
      if (push(doc, &r)) goto oom;
      if (!strcmp(r.name, "DOC_END")) return 1;
    } else if (kind == 'B') {
      char digits[16];
      size_t n = 0;
      int c;
      while ((c = fgetc(in)) != EOF && c >= '0' && c <= '9' && n < 10) digits[n++] = (char)c;
      digits[n] = 0;
      if (c != ',' || n == 0) {
        seterr(err, errlen, "Defekte Blocklaenge");
        goto fail;
      }
      unsigned long size = strtoul(digits, NULL, 10);
      if (size < 6 || size > MAX_BLOCK) {
        seterr(err, errlen, "Blocklaenge %lu ausserhalb des erlaubten Bereichs", size);
        goto fail;
      }
      uint8_t *block = malloc(size);
      if (!block) goto oom;
      if (fread(block, 1, size, in) != size || fgetc(in) != '\n') {
        free(block);
        seterr(err, errlen, "Binaerblock ist abgeschnitten");
        goto fail;
      }
      size_t off = 0;
      /* Manche SDK-Versionen setzen ein 4-Byte-Praefix "Db?," vor den Rahmen. */
      if (size >= 4 && block[0] == 'D' && block[1] == 'b' && block[3] == ',') off = 4;
      if (size - off < 6 || block[off] != 0x1C || block[off + 1] != 'P' ||
          be32(block + off + 2) != size - off - 6) {
        free(block);
        seterr(err, errlen, "Ungueltiger Avansia-Druckrahmen");
        goto fail;
      }
      if (off) memmove(block, block + off, size - off);
      r.kind = PRN_FRAME;
      r.frame = block;
      r.frame_len = size - off;
      if (push(doc, &r)) {
        free(block);
        goto oom;
      }
    } else {
      seterr(err, errlen, "Unbekannter Datensatztyp '%c'", kind);
      goto fail;
    }
  }
oom:
  seterr(err, errlen, "Zu wenig Arbeitsspeicher");
fail:
  prn_free(doc);
  return -1;
}

/* ---- Validierung ------------------------------------------------------ */

typedef struct {
  prn_doc_t *doc;
  size_t pos;
  char *err;
  size_t errlen;
} cursor_t;

static int payload_is(const prn_record_t *r, const char *bytes, size_t n) {
  size_t len;
  const uint8_t *p = prn_payload(r, &len);
  return p && len == n && !memcmp(p, bytes, n);
}

static int payload_starts(const prn_record_t *r, const char *bytes, size_t n) {
  size_t len;
  const uint8_t *p = prn_payload(r, &len);
  return p && len >= n && !memcmp(p, bytes, n);
}

static int take_cp(cursor_t *c, const char *name) {
  if (c->pos >= c->doc->count || c->doc->recs[c->pos].kind != PRN_CHECKPOINT ||
      strcmp(c->doc->recs[c->pos].name, name)) {
    seterr(c->err, c->errlen, "Erwartet Ablaufmarke %s an Position %zu", name, c->pos);
    return -1;
  }
  c->pos++;
  return 0;
}

static int peek_cp(cursor_t *c, const char *name) {
  return c->pos < c->doc->count && c->doc->recs[c->pos].kind == PRN_CHECKPOINT &&
         !strcmp(c->doc->recs[c->pos].name, name);
}

static int peek_frame(cursor_t *c, const char *bytes, size_t n) {
  return c->pos < c->doc->count && c->doc->recs[c->pos].kind == PRN_FRAME &&
         payload_is(&c->doc->recs[c->pos], bytes, n);
}

static int take_frame(cursor_t *c, const char *bytes, size_t n, const char *what) {
  if (!peek_frame(c, bytes, n)) {
    seterr(c->err, c->errlen, "Erwartet Befehl '%s' an Position %zu", what, c->pos);
    return -1;
  }
  c->pos++;
  return 0;
}

static int take_plane(cursor_t *c) {
  if (c->pos >= c->doc->count || c->doc->recs[c->pos].kind != PRN_FRAME) {
    seterr(c->err, c->errlen, "Farbauszug fehlt an Position %zu", c->pos);
    return -1;
  }
  size_t len;
  const uint8_t *p = prn_payload(&c->doc->recs[c->pos], &len);
  if (len != PLANE_PAYLOAD || p[0] != 0x1B || p[1] != 'J') {
    seterr(c->err, c->errlen, "Farbauszug an Position %zu ist kein vollstaendiges 600-dpi-Raster", c->pos);
    return -1;
  }
  c->pos++;
  return 0;
}

#define CMD_START "\x1b@1."
#define CMD_EJECT "\x1b>"
#define CMD_FLIP "\x1bT"
#define CMD_COLOR "\x1bI0,0,2048,1300,7987200."
#define CMD_BLACK "\x1b" "C0,0,2048,1300,2662400."

int prn_validate(prn_doc_t *doc, char *err, size_t errlen) {
  cursor_t c = {doc, 0, err, errlen};

  /* 1. Allowlist: nur bekannte Druckbefehle, keine Wartungs-/Einstellbefehle. */
  doc->frames = 0;
  doc->bytes = 0;
  for (size_t i = 0; i < doc->count; i++) {
    prn_record_t *r = &doc->recs[i];
    if (r->kind != PRN_FRAME) continue;
    doc->frames++;
    doc->bytes += r->frame_len;
    if (payload_is(r, CMD_START, 4) || payload_is(r, CMD_EJECT, 2) || payload_is(r, CMD_FLIP, 2) ||
        payload_starts(r, "\x1bI", 2) || payload_starts(r, "\x1bJ", 2) || payload_starts(r, "\x1b" "C", 2))
      continue;
    size_t len;
    const uint8_t *p = prn_payload(r, &len);
    seterr(err, errlen, "Auftrag enthaelt einen nicht freigegebenen Befehl (%02x %02x) an Position %zu",
           len > 0 ? p[0] : 0, len > 1 ? p[1] : 0, i);
    return -1;
  }

  /* 2. Struktur einer einzelnen Karte. */
  if (take_cp(&c, "HEADER_BEGIN") || take_cp(&c, "HEADER_END") || take_cp(&c, "DOC_BEGIN")) return -1;
  doc->duplex = 0;
  if (peek_cp(&c, "DUPLEX")) {
    doc->duplex = 1;
    c.pos++;
  }
  doc->faces = doc->duplex ? 2 : 1;
  if (take_cp(&c, "CARD_BEGIN") || take_frame(&c, CMD_START, 4, "Karte starten")) return -1;

  for (int face = 0; face < doc->faces; face++) {
    if (take_cp(&c, "FACE_BEGIN")) return -1;
    int planes = 0;
    if (peek_frame(&c, CMD_COLOR, sizeof(CMD_COLOR) - 1)) {
      c.pos++;
      for (int k = 0; k < 3; k++)
        if (take_plane(&c)) return -1;
      planes += 3;
    }
    if (peek_frame(&c, CMD_BLACK, sizeof(CMD_BLACK) - 1)) {
      c.pos++;
      if (take_plane(&c)) return -1;
      planes += 1;
    }
    if (planes == 0) {
      seterr(err, errlen, "Seite %d enthaelt keine Druckdaten", face + 1);
      return -1;
    }
    if (doc->duplex && face == 0) {
      if (take_frame(&c, CMD_FLIP, 2, "Karte wenden") || take_frame(&c, CMD_EJECT, 2, "Seitenende")) return -1;
    }
    if (take_cp(&c, "FACE_END")) return -1;
  }
  if (take_frame(&c, CMD_EJECT, 2, "Kartenende") || take_cp(&c, "CARD_END") || take_cp(&c, "DOC_END"))
    return -1;
  if (c.pos != doc->count) {
    seterr(err, errlen, "Nach DOC_END folgen weitere Daten");
    return -1;
  }
  return 0;
}
