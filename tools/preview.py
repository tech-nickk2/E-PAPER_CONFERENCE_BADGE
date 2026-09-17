#!/usr/bin/env python3
"""
Host-side preview of the badge layout.

There is no host C compiler here, so this re-implements the parts of the
firmware that decide where things land: the EPD47 glyph renderer (reading the
real compressed font data out of the library headers), the portrait canvas
mapping from canvas.cpp, and the layout from badge_render.cpp.

It exists to answer "does the badge actually look right and does anything
overlap or clip" without flashing hardware. Keep it in step with
badge_render.cpp if you move the layout around.

    python tools/preview.py [--out preview]
"""
import argparse, os, re, sys, zlib, unicodedata
import numpy as np
from PIL import Image

LIB = ".pio/libdeps/badge/LilyGo-EPD47/src"
EPD_W, EPD_H = 960, 540
CANVAS_W, CANVAS_H = EPD_H, EPD_W          # 540 x 960 portrait

# --------------------------------------------------------------------------
# font loading -- mirrors get_glyph()/draw_char()/get_char_bounds() in font.c
# --------------------------------------------------------------------------
class Font:
    def __init__(self, path, stem):
        src = open(path, encoding="utf-8", errors="replace").read()
        src = re.sub(r"//[^\n]*", "", src)          # glyph tables are commented

        m = re.search(re.escape(stem) + r"Bitmaps\[\d+\]\s*=\s*\{(.*?)\};", src, re.S)
        self.bitmap = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1)))

        m = re.search(re.escape(stem) + r"Glyphs\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
        self.glyphs = []
        for row in re.findall(r"\{([^{}]*)\}", m.group(1)):
            v = [int(t, 0) for t in row.split(",") if t.strip()]
            # width, height, advance_x, left, top, compressed_size, data_offset
            self.glyphs.append(tuple(v))

        m = re.search(re.escape(stem) + r"Intervals\[\]\s*=\s*\{(.*?)\};", src, re.S)
        self.intervals = [tuple(int(t, 0) for t in row.split(",") if t.strip())
                          for row in re.findall(r"\{([^{}]*)\}", m.group(1))]

        m = re.search(r"const GFXfont " + re.escape(stem) + r"\s*=\s*\{(.*?)\};", src, re.S)
        tail = [t.strip() for t in m.group(1).split(",") if t.strip()]
        # bitmap, glyph, intervals, interval_count, compressed, advance_y, asc, desc
        self.compressed = int(tail[4])
        self.advance_y  = int(tail[5])
        self.ascender   = int(tail[6])
        self.descender  = int(tail[7])
        self.name = stem

    def glyph(self, cp):
        for first, last, off in self.intervals:
            if first <= cp <= last:
                return self.glyphs[off + (cp - first)]
            if cp < first:
                return None
        return None

    def bits(self, g):
        """Decompressed coverage nibbles for one glyph, as a (h, w) array."""
        w, h, _adv, _l, _t, csize, off = g
        if w == 0 or h == 0:
            return np.zeros((0, 0), np.uint8)
        bw = (w + 1) // 2
        raw = zlib.decompress(self.bitmap[off:off + csize]) if self.compressed \
              else self.bitmap[off:off + bw * h]
        a = np.frombuffer(raw[:bw * h], np.uint8).reshape(h, bw)
        lo, hi = a & 0x0F, a >> 4
        out = np.empty((h, bw * 2), np.uint8)
        out[:, 0::2], out[:, 1::2] = lo, hi     # even x in the low nibble
        return out[:, :w]

    def measure(self, text):
        """Ink width, exactly as get_text_bounds() computes *w."""
        x, minx, maxx = 0, 10 ** 6, -1
        for ch in text:
            g = self.glyph(ord(ch))
            if g is None:
                continue
            x1 = x + g[3]
            if x1 < minx: minx = x1
            if x1 + g[0] > maxx: maxx = x1 + g[0]
            x += g[2]
        if maxx < 0:
            return 0
        return maxx - min(0, minx)

    def draw(self, buf, text, cx, cy):
        """Render into a (H, W) nibble buffer; cy is the baseline. 0=black."""
        H, W = buf.shape
        for ch in text:
            g = self.glyph(ord(ch))
            if g is None:
                continue
            w, h, adv, left, top = g[0], g[1], g[2], g[3], g[4]
            bm = self.bits(g)
            for yy in range(h):
                ty = cy - top + yy
                if ty < 0 or ty >= H:
                    continue
                for xx in range(w):
                    tx = cx + left + xx
                    if tx < 0 or tx >= W:
                        continue
                    buf[ty, tx] = 15 - bm[yy, xx]   # fg=0 bg=15 colour LUT
            cx += adv
        return cx

FONTS = {}
def load_fonts():
    for stem, hdr in (("Roboto12", "roboto12.h"), ("Roboto18", "roboto18.h"),
                      ("Roboto", "roboto.h"),     ("Roboto32", "roboto32.h")):
        p = os.path.join(LIB, hdr)
        if not os.path.exists(p):
            sys.exit("missing %s -- run `pio run` once so the library is fetched" % p)
        FONTS[stem] = Font(p, stem)
    return FONTS

# --------------------------------------------------------------------------
# canvas.cpp
# --------------------------------------------------------------------------
INK, PAPER = 0, 15
def GREY(n): return n
SPAD = 10

class Canvas:
    def __init__(self, flip=False):
        self.fb = np.full((EPD_H, EPD_W), PAPER, np.uint8)      # landscape panel
        self.scratch = np.full((EPD_H, EPD_W), PAPER, np.uint8)
        self.flip = flip
        self.warnings = []

    def map(self, x, y):
        if self.flip:
            return y, CANVAS_W - 1 - x
        return CANVAS_H - 1 - y, x

    def px(self, x, y, c):
        if not (0 <= x < CANVAS_W and 0 <= y < CANVAS_H):
            return
        lx, ly = self.map(x, y)
        self.fb[ly, lx] = c

    def clear(self, c): self.fb[:, :] = c

    def fill(self, x, y, w, h, c):
        if w <= 0 or h <= 0:
            return
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(CANVAS_W, x + w), min(CANVAS_H, y + h)
        for j in range(y0, y1):
            for i in range(x0, x1):
                lx, ly = self.map(i, j)
                self.fb[ly, lx] = c

    def frame(self, x, y, w, h, t, c):
        self.fill(x, y, w, t, c); self.fill(x, y + h - t, w, t, c)
        self.fill(x, y + t, t, h - 2 * t, c); self.fill(x + w - t, y + t, t, h - 2 * t, c)

    # ---- text ----
    def fold(self, s):
        out = []
        for ch in s:
            cp = ord(ch)
            if 0x20 <= cp < 0x7F:
                out.append(ch); continue
            d = "".join(c for c in unicodedata.normalize("NFKD", ch) if ord(c) < 128)
            out.append(d if d else "?")
        return "".join(out)

    def width(self, font, s):
        return FONTS[font].measure(self.fold(s))

    def text(self, x, ytop, font, s, align="left", invert=False):
        s = self.fold(s)
        if not s:
            return 0
        f = FONTS[font]
        w = f.measure(s)
        if w <= 0:
            return 0
        sw = min(w + 2 * SPAD + 8, EPD_W)
        sh = min(f.ascender - f.descender + 2 * SPAD, EPD_H)
        self.scratch[:sh, :sw] = PAPER
        f.draw(self.scratch, s, SPAD, SPAD + f.ascender)

        dx = x - (w // 2 if align == "center" else w if align == "right" else 0)
        for j in range(sh):
            row = self.scratch[j, :sw]
            for i in np.nonzero(row != PAPER)[0]:
                v = row[i]
                self.px(dx - SPAD + int(i), ytop - SPAD + j, 15 - v if invert else v)
        return w

    def fit(self, fonts, s, maxw):
        for f in fonts:
            if self.width(f, s) <= maxw:
                return f
        return fonts[-1]

    def paragraph(self, x, ytop, maxw, maxlines, font, s, align="left",
                  invert=False, measure_only=False):
        text = self.fold(s).strip()
        if not text or maxlines <= 0:
            return 0
        f = FONTS[font]
        lines, pos, n = 0, 0, len(text)
        while pos < n and lines < maxlines:
            last, probe, line = -1, pos, ""
            while probe < n:
                sp = text.find(" ", probe)
                end = n if sp < 0 else sp
                cand = text[pos:end]
                if f.measure(cand) > maxw and last >= 0:
                    break
                last, line = end, cand
                if sp < 0:
                    probe = n; break
                probe = sp + 1
            if last < 0:
                break
            if lines == maxlines - 1 and last < n:
                e = line + "..."
                while f.measure(e) > maxw and len(line) > 1:
                    line = line[:-1]; e = line + "..."
                line = e
            if not measure_only:
                self.text(x, ytop + lines * f.advance_y, font, line, align, invert)
            lines += 1
            pos = last
            while pos < n and text[pos] == " ":
                pos += 1
        return lines

    # ---- QR ----
    def qr(self, x, y, box, payload, invert=False):
        import qrcode as qrlib
        if not payload:
            return 0
        q = qrlib.QRCode(error_correction=qrlib.constants.ERROR_CORRECT_M,
                         border=0, box_size=1)
        q.add_data(payload); q.make(fit=True)
        mods = q.get_matrix()
        size = len(mods)
        QUIET = 3
        span = size + 2 * QUIET
        ms = max(1, box // span)
        side = ms * span
        light, dark = (INK, PAPER) if invert else (PAPER, INK)
        self.fill(x, y, side, side, light)
        for my in range(size):
            for mx in range(size):
                if mods[my][mx]:
                    self.fill(x + (mx + QUIET) * ms, y + (my + QUIET) * ms, ms, ms, dark)
        return side

    def photo(self, x, y, w, h, path, invert=False):
        if not os.path.exists(path):
            return False
        d = open(path, "rb").read()
        if len(d) < 8 or d[:4] != b"EPB1":
            return False
        sw = d[4] | (d[5] << 8); sh = d[6] | (d[7] << 8)
        rb = (sw + 1) // 2
        for j in range(h):
            row = 8 + (j * sh // h) * rb
            for i in range(w):
                sx = i * sw // w
                b = d[row + (sx >> 1)]
                n = (b >> 4) if (sx & 1) else (b & 15)
                self.px(x + i, y + j, 15 - n if invert else n)
        return True

    def invert_all(self):
        self.fb = 15 - self.fb

# --------------------------------------------------------------------------
# badge_render.cpp
# --------------------------------------------------------------------------
M, HDR_H, QR_BOX, RULE = 22, 86, 150, 3
PHOTO_GAP, FACT_GAP, FACT_PAD, FACT_LINES, PHOTO_MIN, PHOTO_MAX = 26, 18, 12, 3, 180, 340
NAME_FONTS  = ["Roboto32", "Roboto", "Roboto18"]
TITLE_FONTS = ["Roboto", "Roboto18", "Roboto12"]
SMALL_FONTS = ["Roboto18", "Roboto12"]

DEFAULT_CFG = dict(
    name="Ryan Kiprotich", pronouns="he/him",
    profession="Embedded Systems Engineer", company="Waziup",
    funFact="Once debugged an I2C bus at 3am using nothing but a bent paperclip and stubbornness.",
    event="IoT Summit 2026", footer="Hall B - Table 12",
    qrText="https://waziup.org", qrCaption="Scan to connect",
    website="waziup.org", email="ryan.kiprotich@waziup.org",
    showPhoto=True, flip=False, invert=False, border=True)

def qr_payload(cfg): return cfg["qrText"]


def longest_word_w(c, font, s):
    return max([c.width(font, w) for w in s.split(" ") if w] or [0])


def centred_block(c, y, contentW, fonts, s, measure_only, why=None):
    if not s:
        return 0
    for f in fonts:
        if c.width(f, s) <= contentW:
            if not measure_only:
                c.text(CANVAS_W // 2, y, f, s, "center")
            if why is not None and not measure_only:
                why.append("%s: %s x1" % (s[:18], f))
            return FONTS[f].advance_y
    for f in fonts:
        if longest_word_w(c, f, s) > contentW:
            continue
        need = c.paragraph(0, 0, contentW, 8, f, s, "center", measure_only=True)
        if 1 <= need <= 2:
            if not measure_only:
                c.paragraph(CANVAS_W // 2, y, contentW, 2, f, s, "center")
                if why is not None:
                    why.append("%s: %s x%d" % (s[:18], f, need))
            return need * FONTS[f].advance_y
    f = fonts[-1]
    lines = c.paragraph(CANVAS_W // 2, y, contentW, 2, f, s, "center",
                        measure_only=measure_only)
    if why is not None and not measure_only:
        why.append("%s: %s x%d TRUNCATED" % (s[:18], f, lines))
    return lines * FONTS[f].advance_y


def draw_identity(c, y, contentW, cfg, measure_only, why=None):
    y += centred_block(c, y, contentW, NAME_FONTS, cfg["name"], measure_only, why)
    if cfg["pronouns"]:
        if not measure_only:
            c.text(CANVAS_W // 2, y, "Roboto12", cfg["pronouns"], "center")
        y += FONTS["Roboto12"].advance_y
    y += 8
    h = centred_block(c, y, contentW, TITLE_FONTS, cfg["profession"], measure_only, why)
    if h:
        y += h + 2
    y += centred_block(c, y, contentW, SMALL_FONTS, cfg["company"].upper(), measure_only, why)
    y += 12
    if not measure_only:
        c.fill(M + 60, y, contentW - 120, 2, INK)
    return y + 16


def fact_lines(c, cfg, font, innerW):
    return c.paragraph(0, 0, innerW, 8, font, cfg["funFact"], measure_only=True)


def fact_reserve(c, cfg, contentW):
    if not cfg["funFact"]:
        return 0
    lines = min(FACT_LINES, max(1, fact_lines(c, cfg, "Roboto12", contentW - 32)))
    return 2 * FACT_PAD + FONTS["Roboto12"].advance_y + 2 + lines * FONTS["Roboto12"].advance_y


def render(cfg, photo_path):
    c = Canvas(flip=cfg["flip"])
    c.clear(PAPER)
    contentW = CANVAS_W - 2 * M
    contentTop = HDR_H + 26
    inv = cfg["invert"]

    c.fill(0, 0, CANVAS_W, HDR_H, INK)
    if cfg["event"]:
        ev = cfg["event"].upper()
        f = c.fit(SMALL_FONTS, ev, contentW - 16)
        c.text(CANVAS_W // 2, (HDR_H - FONTS[f].advance_y) // 2, f, ev, "center", True)
    c.fill(0, HDR_H + 7, CANVAS_W, RULE, INK)

    bottom = CANVAS_H - M
    if cfg["footer"]:
        fy = bottom - FONTS["Roboto12"].advance_y
        c.text(CANVAS_W // 2, fy, "Roboto12", cfg["footer"], "center")
        bottom = fy - 10

    qrTop = bottom - QR_BOX
    side = c.qr(M, qrTop, QR_BOX, qr_payload(cfg), inv)
    cx = M + side + (20 if side else 0)
    cw = CANVAS_W - M - cx
    if cw >= 80:
        contact = cfg["website"] or cfg["email"] or ""
        capL = c.paragraph(cx, 0, cw, 2, "Roboto18", cfg["qrCaption"], measure_only=True)
        conL = c.paragraph(cx, 0, cw, 2, "Roboto12", contact, measure_only=True) if contact else 0
        blockH = capL * FONTS["Roboto18"].advance_y +                  (6 + conL * FONTS["Roboto12"].advance_y if conL else 0)
        ty = max(qrTop, qrTop + (QR_BOX - blockH) // 2)
        if capL:
            c.paragraph(cx, ty, cw, 2, "Roboto18", cfg["qrCaption"])
            ty += capL * FONTS["Roboto18"].advance_y + 6
        if conL:
            c.paragraph(cx, ty, cw, 2, "Roboto12", contact)

    identityH = draw_identity(c, 0, contentW, cfg, True)
    slack = (qrTop - FACT_GAP) - contentTop - identityH

    photoBox = 0
    if cfg["showPhoto"]:
        want = slack - PHOTO_GAP - fact_reserve(c, cfg, contentW)
        want = min(want, PHOTO_MAX)
        photoBox = want if want >= PHOTO_MIN else 0
        if not photoBox and slack - PHOTO_GAP >= PHOTO_MIN:
            photoBox = PHOTO_MIN

    y = contentTop
    if photoBox:
        px = (CANVAS_W - photoBox) // 2
        if not c.photo(px, y, photoBox, photoBox, photo_path, inv):
            c.fill(px, y, photoBox, photoBox, GREY(13))
            c.text(CANVAS_W // 2, y + photoBox // 2 - FONTS["Roboto12"].advance_y // 2,
                   "Roboto12", "no photo uploaded yet", "center")
        c.frame(px - 5, y - 5, photoBox + 10, photoBox + 10, RULE, INK)
        y += photoBox + PHOTO_GAP
    why = []
    y = draw_identity(c, y, contentW, cfg, False, why)
    c.warnings.extend(why)

    top, fbottom = y, qrTop - FACT_GAP
    boxH = fbottom - top
    if boxH >= 70 and cfg["funFact"]:
        c.fill(M, top, contentW, boxH, GREY(14))
        c.frame(M, top, contentW, boxH, 2, INK)
        ix, iw, ty = M + 16, contentW - 32, top + FACT_PAD
        c.text(ix, ty, "Roboto12", "FUN FACT")
        ty += FONTS["Roboto12"].advance_y + 2
        avail = (top + boxH - FACT_PAD) - ty
        f = "Roboto12"
        if fact_lines(c, cfg, "Roboto18", iw) * FONTS["Roboto18"].advance_y <= avail:
            f = "Roboto18"
        maxlines = avail // FONTS[f].advance_y
        used = c.paragraph(ix, ty, iw, maxlines, f, cfg["funFact"]) if maxlines >= 1 else 0
        c.warnings.append("fun fact %dpx box, %s, %d line(s)" % (boxH, f, used))
    elif cfg["funFact"]:
        c.warnings.append("FUN FACT SKIPPED (only %dpx available)" % boxH)

    if cfg["border"]:
        c.frame(0, 0, CANVAS_W, CANVAS_H, RULE, INK)
    if inv:
        c.invert_all()
    c.warnings.insert(0, "photo %dpx, identity %dpx, slack %dpx, QR top y=%d"
                      % (photoBox, identityH, slack, qrTop))
    return c

def make_test_photo(path, w=320, h=320):
    """A synthetic head-and-shoulders stand-in, dithered the way the browser does."""
    yy, xx = np.mgrid[0:h, 0:w]
    img = 235 - 60 * (yy / h)                                   # backdrop gradient
    img -= 120 * np.exp(-(((xx - w * .5) / (w * .22)) ** 2 + ((yy - h * .42) / (h * .26)) ** 2))
    img -= 70 * np.exp(-(((xx - w * .5) / (w * .42)) ** 2 + ((yy - h * 1.05) / (h * .38)) ** 2))
    img += 18 * np.sin(xx / 7.0) * np.exp(-((yy - h * .42) / (h * .3)) ** 2)
    buf = np.clip(img, 0, 255).astype(np.float64)
    out = np.zeros((h, w), np.uint8)
    for j in range(h):                                          # Floyd-Steinberg
        for i in range(w):
            v = buf[j, i]
            n = int(np.clip(round(v / 17), 0, 15))
            out[j, i] = n
            e = v - n * 17
            if i + 1 < w: buf[j, i + 1] += e * 7 / 16
            if j + 1 < h:
                if i: buf[j + 1, i - 1] += e * 3 / 16
                buf[j + 1, i] += e * 5 / 16
                if i + 1 < w: buf[j + 1, i + 1] += e / 16
    rb = (w + 1) // 2
    data = bytearray(b"EPB1" + bytes([w & 255, w >> 8, h & 255, h >> 8]) + bytes(rb * h))
    for j in range(h):
        for i in range(w):
            o = 8 + j * rb + (i >> 1)
            data[o] |= (out[j, i] << 4) if (i & 1) else out[j, i]
    open(path, "wb").write(bytes(data))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="preview")
    ap.add_argument("--no-photo", action="store_true")
    ap.add_argument("--dark", action="store_true")
    ap.add_argument("--flip", action="store_true")
    a = ap.parse_args()

    load_fonts()
    photo = os.path.join("tools", "_testphoto.bin")
    if not os.path.exists(photo):
        make_test_photo(photo)

    cfg = dict(DEFAULT_CFG, invert=a.dark, flip=a.flip, showPhoto=not a.no_photo)
    c = render(cfg, photo)

    # The panel framebuffer, exactly as epd_draw_grayscale_image would see it.
    Image.fromarray((c.fb * 17).astype(np.uint8), "L").save(a.out + "_panel.png")
    # Undo the rotation so we see what a person holding the badge sees.
    port = np.zeros((CANVAS_H, CANVAS_W), np.uint8)
    for x in range(CANVAS_W):
        for yv in range(CANVAS_H):
            lx, ly = c.map(x, yv)
            port[yv, x] = c.fb[ly, lx]
    Image.fromarray((port * 17).astype(np.uint8), "L").save(a.out + "_badge.png")

    print("\n".join(" - " + w for w in c.warnings))
    print("wrote %s_badge.png and %s_panel.png" % (a.out, a.out))

if __name__ == "__main__":
    main()
