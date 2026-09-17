#include "canvas.h"
#include "badge_config.h"
#include <LittleFS.h>
#include <qrcode.h>
#include "ascii_fold.h"

#define FB_STRIDE (EPD_WIDTH / 2)
#define FB_BYTES  (EPD_WIDTH * EPD_HEIGHT / 2)

static uint8_t *s_fb      = nullptr;   // what actually goes to the panel
static uint8_t *s_scratch = nullptr;   // landscape staging area for glyph runs
static bool     s_flip    = false;

// Padding around a text run inside the scratch buffer, so glyphs with a
// negative left bearing or a tall accent are not clipped.
static const int SPAD = 10;

// --------------------------------------------------------------------------
// raw nibble access -- inlined because the photo path touches ~100k pixels
// --------------------------------------------------------------------------
static inline void fbSet(uint8_t *fb, int x, int y, uint8_t c)
{
    uint8_t *p = &fb[y * FB_STRIDE + (x >> 1)];
    if (x & 1) *p = (*p & 0x0F) | (c & 0xF0);
    else       *p = (*p & 0xF0) | (c >> 4);
}

static inline uint8_t fbGet(const uint8_t *fb, int x, int y)
{
    uint8_t b = fb[y * FB_STRIDE + (x >> 1)];
    uint8_t n = (x & 1) ? (b >> 4) : (b & 0x0F);
    return (uint8_t)(n * 17);
}

bool canvasBegin()
{
    if (s_fb) return true;
    s_fb      = (uint8_t *)ps_malloc(FB_BYTES);
    s_scratch = (uint8_t *)ps_malloc(FB_BYTES);
    if (!s_fb || !s_scratch) {
        log_e("out of PSRAM for framebuffers (need 2 x %d bytes)", FB_BYTES);
        return false;
    }
    memset(s_fb, PAPER, FB_BYTES);
    memset(s_scratch, PAPER, FB_BYTES);
    return true;
}

uint8_t *canvasFramebuffer() { return s_fb; }
void     canvasSetFlip(bool flip) { s_flip = flip; }

// --------------------------------------------------------------------------
// portrait -> landscape mapping
//
//   normal : badge reads upright with the USB port on the LEFT
//   flipped: 180 deg, for lanyards that hang the board the other way up
// --------------------------------------------------------------------------
static inline void mapToPanel(int x, int y, int &lx, int &ly)
{
    if (s_flip) { lx = y;                ly = CANVAS_W - 1 - x; }
    else        { lx = CANVAS_H - 1 - y; ly = x;                }
}

void canvasPixel(int x, int y, uint8_t color)
{
    if ((unsigned)x >= (unsigned)CANVAS_W || (unsigned)y >= (unsigned)CANVAS_H) return;
    int lx, ly;
    mapToPanel(x, y, lx, ly);
    fbSet(s_fb, lx, ly, color);
}

void canvasClear(uint8_t color)
{
    // Both nibbles carry the same level, so a memset is exact.
    memset(s_fb, (color & 0xF0) | (color >> 4), FB_BYTES);
}

void canvasFillRect(int x, int y, int w, int h, uint8_t color)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            canvasPixel(x + i, y + j, color);
}

void canvasFrame(int x, int y, int w, int h, int t, uint8_t color)
{
    canvasFillRect(x,         y,         w, t, color);
    canvasFillRect(x,         y + h - t, w, t, color);
    canvasFillRect(x,         y + t,     t, h - 2 * t, color);
    canvasFillRect(x + w - t, y + t,     t, h - 2 * t, color);
}

void canvasHRule(int x, int y, int len, int t, uint8_t color)
{
    canvasFillRect(x, y, len, t, color);
}


// --------------------------------------------------------------------------
// ASCII folding
//
// The bundled Roboto faces only carry U+0020..U+007E and the renderer skips
// anything it has no glyph for, so an unfolded "Jose" would lose its accent
// and a phone-typed curly apostrophe would vanish mid-word. Everything drawn
// goes through here first.
// --------------------------------------------------------------------------
String canvasFold(const char *s)
{
    String out;
    if (!s) return out;
    out.reserve(strlen(s) + 4);

    const uint8_t *p = (const uint8_t *)s;
    while (*p) {
        uint32_t cp;
        uint8_t c = p[0];
        if (c < 0x80) {
            cp = c; p += 1;
        } else if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
        } else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6)
               | (p[2] & 0x3F); p += 3;
        } else if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
                   (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12)
               | ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4;
        } else {
            p += 1; continue;                      // malformed byte, drop it
        }

        if (cp >= 0x20 && cp < 0x7F)    { out += (char)cp;                 continue; }
        if (cp == 9 || cp == 10 || cp == 13) { out += ' ';                 continue; }
        if (cp >= 0xA0 && cp <= 0xFF)   { out += LATIN1_FOLD[cp - 0xA0];   continue; }
        if (cp >= 0x100 && cp <= 0x17F) { out += LATINA_FOLD[cp - 0x100];  continue; }

        switch (cp) {
            case 0x2018: case 0x2019: case 0x2032: out += "'";   break;
            case 0x201C: case 0x201D: case 0x2033: out += '"';   break;
            case 0x2013: case 0x2014: case 0x2212: out += '-';   break;
            case 0x2022: case 0x00B7:              out += '-';   break;
            case 0x2026:                           out += "..."; break;
            case 0x20AC:                           out += "EUR"; break;
            case 0x2122:                           out += "(tm)";break;
            default:                               out += '?';   break;
        }
    }
    return out;
}

// --------------------------------------------------------------------------
// text
// --------------------------------------------------------------------------
int canvasTextWidth(const GFXfont *font, const char *s)
{
    if (!s || !*s) return 0;
    String t = canvasFold(s);
    if (!t.length()) return 0;
    int32_t cx = 0, cy = 0, x1 = 0, y1 = 0, w = 0, h = 0;
    get_text_bounds(font, t.c_str(), &cx, &cy, &x1, &y1, &w, &h, NULL);
    return (int)w;
}

int canvasLineHeight(const GFXfont *font) { return font->advance_y; }

// Copies the inked pixels of a scratch rectangle onto the canvas, rotating on
// the way. Pure white is transparent, so a label can sit on top of a rule or a
// filled band without punching a white hole in it.
static void blitScratch(int sw, int sh, int dx, int dy, bool invert)
{
    for (int j = 0; j < sh; j++) {
        for (int i = 0; i < sw; i++) {
            uint8_t v = fbGet(s_scratch, i, j);
            if (v == PAPER) continue;
            canvasPixel(dx + i, dy + j, invert ? (uint8_t)(255 - v) : v);
        }
    }
}

int canvasDrawText(int x, int yTop, const GFXfont *font, const char *s,
                   TextAlign align, bool invert)
{
    if (!s || !*s) return 0;
    String t = canvasFold(s);
    if (!t.length()) return 0;

    int32_t cx = SPAD, cy = SPAD + font->ascender;
    int32_t mx = cx, my = cy, x1 = 0, y1 = 0, w = 0, h = 0;
    get_text_bounds(font, t.c_str(), &mx, &my, &x1, &y1, &w, &h, NULL);
    if (w <= 0) return 0;

    int sw = (int)w + 2 * SPAD + 8;
    int sh = font->ascender - font->descender + 2 * SPAD;
    if (sw > EPD_WIDTH)  sw = EPD_WIDTH;
    if (sh > EPD_HEIGHT) sh = EPD_HEIGHT;

    // Clear only the strip we are about to use; wiping all 259 kB of PSRAM for
    // every label would dominate the render time.
    for (int j = 0; j < sh; j++)
        memset(&s_scratch[j * FB_STRIDE], PAPER, (size_t)((sw + 1) / 2));

    writeln(font, t.c_str(), &cx, &cy, s_scratch);

    int dx = x;
    if      (align == ALIGN_CENTER) dx = x - (int)w / 2;
    else if (align == ALIGN_RIGHT)  dx = x - (int)w;

    // scratch (SPAD, SPAD) is the top-left of the line box -> (dx, yTop)
    blitScratch(sw, sh, dx - SPAD, yTop - SPAD, invert);
    return (int)w;
}

const GFXfont *canvasFitFont(const GFXfont *const *fonts, int nFonts,
                             const char *s, int maxW)
{
    for (int i = 0; i < nFonts; i++)
        if (canvasTextWidth(fonts[i], s) <= maxW) return fonts[i];
    return fonts[nFonts - 1];
}

int canvasDrawParagraph(int x, int yTop, int maxW, int maxLines,
                        const GFXfont *font, const char *s,
                        TextAlign align, bool invert, bool measureOnly)
{
    if (!s || !*s || maxLines <= 0) return 0;

    String text = canvasFold(s);
    text.trim();
    int len = text.length();
    if (!len) return 0;

    int lineH = font->advance_y;
    int lines = 0;
    int pos   = 0;

    while (pos < len && lines < maxLines) {
        // Grow the candidate line one word at a time until it stops fitting.
        int    lastGood = -1;
        int    probe    = pos;
        String line;

        while (probe < len) {
            int sp  = text.indexOf(' ', probe);
            int end = (sp < 0) ? len : sp;
            String cand = text.substring(pos, end);
            if (canvasTextWidth(font, cand.c_str()) > maxW && lastGood >= 0) break;
            lastGood = end;
            line     = cand;
            if (sp < 0) { probe = len; break; }
            probe = sp + 1;
        }

        if (lastGood < 0) break;             // a single word wider than maxW

        if (lines == maxLines - 1 && lastGood < len) {
            // Out of room: mark the truncation instead of cutting silently.
            String e = line + "...";
            while (canvasTextWidth(font, e.c_str()) > maxW && line.length() > 1) {
                line.remove(line.length() - 1);
                e = line + "...";
            }
            line = e;
        }

        if (!measureOnly)
            canvasDrawText(x, yTop + lines * lineH, font, line.c_str(), align, invert);

        lines++;
        pos = lastGood;
        while (pos < len && text[pos] == ' ') pos++;
    }
    return lines;
}

// --------------------------------------------------------------------------
// QR
// --------------------------------------------------------------------------
#define QR_MAX_VERSION 12
#define QR_QUIET       3       // modules of white margin on every side

int canvasDrawQR(int x, int y, int boxPx, const char *text, bool invert)
{
    if (!text || !*text) return 0;

    static uint8_t modules[((QR_MAX_VERSION * 4 + 17) *
                            (QR_MAX_VERSION * 4 + 17) + 7) / 8];
    QRCode qr;
    int8_t rc = -1;

    // The smallest version that holds the payload keeps the modules large, and
    // therefore scannable at lanyard distance.
    for (uint8_t v = 2; v <= QR_MAX_VERSION; v++) {
        rc = qrcode_initText(&qr, modules, v, ECC_MEDIUM, text);
        if (rc == 0) break;
    }
    if (rc != 0) {
        log_e("QR payload of %u bytes does not fit version %d",
              (unsigned)strlen(text), QR_MAX_VERSION);
        return 0;
    }

    int span = qr.size + 2 * QR_QUIET;
    int ms   = boxPx / span;
    if (ms < 1) ms = 1;
    int side = ms * span;

    // Scanners cope badly with inverted symbols, so in dark mode we draw the
    // QR pre-inverted and let canvasInvertAll() put it back the right way up.
    uint8_t light = invert ? INK   : PAPER;
    uint8_t dark  = invert ? PAPER : INK;

    canvasFillRect(x, y, side, side, light);
    for (int my = 0; my < qr.size; my++)
        for (int mx = 0; mx < qr.size; mx++)
            if (qrcode_getModule(&qr, mx, my))
                canvasFillRect(x + (mx + QR_QUIET) * ms,
                               y + (my + QR_QUIET) * ms, ms, ms, dark);
    return side;
}

// --------------------------------------------------------------------------
// photo
//
// File layout (little endian):  "EPB1" | u16 width | u16 height | rows
// Each row is ceil(width/2) bytes; the low nibble holds the even-x pixel,
// which matches the packing epd_draw_pixel() uses internally.
// --------------------------------------------------------------------------
bool canvasDrawPhoto(int x, int y, int w, int h, const char *path, bool invert)
{
    File f = LittleFS.open(path, "r");
    if (!f) return false;

    uint8_t hdr[8];
    if (f.read(hdr, 8) != 8 || memcmp(hdr, "EPB1", 4) != 0) { f.close(); return false; }
    int sw = hdr[4] | (hdr[5] << 8);
    int sh = hdr[6] | (hdr[7] << 8);
    if (sw <= 0 || sh <= 0 || sw > 2048 || sh > 2048) { f.close(); return false; }

    size_t rowBytes = (size_t)((sw + 1) / 2);
    size_t need     = rowBytes * (size_t)sh;
    if (f.size() < 8 + need) { f.close(); return false; }

    uint8_t *img = (uint8_t *)ps_malloc(need);
    if (!img) { f.close(); return false; }
    size_t got = f.read(img, need);
    f.close();
    if (got != need) { free(img); return false; }

    // Nearest neighbour. The browser normally hands us the exact size already;
    // this only matters if an odd-sized image ends up on the filesystem.
    for (int j = 0; j < h; j++) {
        const uint8_t *row = &img[(size_t)((int64_t)j * sh / h) * rowBytes];
        for (int i = 0; i < w; i++) {
            int sxi = (int)((int64_t)i * sw / w);
            uint8_t b = row[sxi >> 1];
            uint8_t n = (sxi & 1) ? (b >> 4) : (b & 0x0F);
            uint8_t v = (uint8_t)(n * 17);
            canvasPixel(x + i, y + j, invert ? (uint8_t)(255 - v) : v);
        }
    }
    free(img);
    return true;
}

// --------------------------------------------------------------------------
// output
// --------------------------------------------------------------------------
void canvasInvertAll()
{
    for (size_t i = 0; i < (size_t)FB_BYTES; i++) s_fb[i] = ~s_fb[i];
}

void canvasPush(bool fullClear)
{
    epd_poweron();
    if (fullClear) {
        // Two passes: the second chases out the ghost of the previous badge.
        epd_clear();
        epd_clear();
    }
    epd_draw_grayscale_image(epd_full_screen(), s_fb);
    epd_poweroff();
}
