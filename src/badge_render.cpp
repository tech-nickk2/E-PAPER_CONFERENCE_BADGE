#include "badge_render.h"
#include "badge_config.h"
#include "canvas.h"

// The library ships these as `const GFXfont` at file scope, which is internal
// linkage in C++ -- so they must be included from exactly one translation unit.
#include "roboto12.h"   // advance_y 29  -- captions, fun fact
#include "roboto18.h"   // advance_y 44  -- company, header band
#include "roboto.h"     // advance_y 59  -- job title
#include "roboto32.h"   // advance_y 78  -- name

// --- layout constants (portrait, 540 x 960) -------------------------------
#define M          22        // outer margin
#define HDR_H      86        // event band
#define QR_BOX     150       // target QR side, quantised down to whole modules
#define RULE       3
#define PHOTO_GAP  26        // breathing room under the photo
#define FACT_GAP   18        // breathing room above the QR block
#define FACT_PAD   12        // inside the fun fact box, top and bottom
#define FACT_LINES 3         // most lines we will reserve room for up front
#define PHOTO_MIN  180       // below this a portrait is not worth printing
#define PHOTO_MAX  340

static const GFXfont *NAME_FONTS[]  = { &Roboto32, &Roboto, &Roboto18 };
static const GFXfont *TITLE_FONTS[] = { &Roboto, &Roboto18, &Roboto12 };
static const GFXfont *SMALL_FONTS[] = { &Roboto18, &Roboto12 };

// Kept in RTC memory so a wake from deep sleep can tell that the panel already
// shows the current content and skip the five-second refresh.
RTC_DATA_ATTR static uint32_t s_shownFingerprint = 0;
RTC_DATA_ATTR static bool     s_panelValid       = false;

bool badgeNeedsRender()
{
    return !s_panelValid || configFingerprint() != s_shownFingerprint;
}

// Uppercase without touching the config -- labels read better in caps, but we
// should not mangle what the user typed.
static String upper(const String &s)
{
    String o = s;
    o.toUpperCase();
    return o;
}

// Widest single word, so we never pick a face that would run a word off the
// edge -- the wrapper accepts an over-wide word rather than looping forever.
static int longestWordW(const GFXfont *f, const String &s)
{
    int widest = 0, from = 0;
    while (from <= (int)s.length()) {
        int sp = s.indexOf(' ', from);
        int end = (sp < 0) ? s.length() : sp;
        if (end > from) {
            int w = canvasTextWidth(f, s.substring(from, end).c_str());
            if (w > widest) widest = w;
        }
        if (sp < 0) break;
        from = sp + 1;
    }
    return widest;
}

// A centred heading. Prefers the largest face that fits on one line; failing
// that, wraps at the largest face that fits in two -- a wrapped heading reads
// far better than one shrunk to the smallest size in the ladder.
static int centredBlock(int y, int contentW, const GFXfont *const *fonts,
                        int nFonts, const String &s, bool measureOnly)
{
    if (!s.length()) return 0;

    for (int i = 0; i < nFonts; i++) {
        if (canvasTextWidth(fonts[i], s.c_str()) <= contentW) {
            if (!measureOnly)
                canvasDrawText(CANVAS_W / 2, y, fonts[i], s.c_str(), ALIGN_CENTER, false);
            return fonts[i]->advance_y;
        }
    }

    for (int i = 0; i < nFonts; i++) {
        if (longestWordW(fonts[i], s) > contentW) continue;
        int need = canvasDrawParagraph(0, 0, contentW, 8, fonts[i], s.c_str(),
                                       ALIGN_CENTER, false, /*measureOnly=*/true);
        if (need >= 1 && need <= 2) {
            if (!measureOnly)
                canvasDrawParagraph(CANVAS_W / 2, y, contentW, 2, fonts[i],
                                    s.c_str(), ALIGN_CENTER, false, false);
            return need * fonts[i]->advance_y;
        }
    }

    // Pathological input: smallest face, two lines, ellipsised.
    const GFXfont *f = fonts[nFonts - 1];
    int lines = canvasDrawParagraph(CANVAS_W / 2, y, contentW, 2, f, s.c_str(),
                                    ALIGN_CENTER, false, measureOnly);
    return lines * f->advance_y;
}

static void drawHeader(int contentW)
{
    canvasFillRect(0, 0, CANVAS_W, HDR_H, INK);
    if (g_cfg.event.length()) {
        String ev = upper(g_cfg.event);
        const GFXfont *f = canvasFitFont(SMALL_FONTS, 2, ev.c_str(), contentW - 16);
        canvasDrawText(CANVAS_W / 2, (HDR_H - f->advance_y) / 2, f,
                       ev.c_str(), ALIGN_CENTER, /*invert=*/true);
    }
    canvasFillRect(0, HDR_H + 7, CANVAS_W, RULE, INK);
}

static int drawPhoto(int y, int box, bool inv)
{
    int px = (CANVAS_W - box) / 2;
    if (!canvasDrawPhoto(px, y, box, box, PHOTO_PATH, inv)) {
        canvasFillRect(px, y, box, box, GREY(13));
        canvasDrawText(CANVAS_W / 2, y + box / 2 - Roboto12.advance_y / 2,
                       &Roboto12, "no photo uploaded yet", ALIGN_CENTER, false);
    }
    canvasFrame(px - 5, y - 5, box + 10, box + 10, RULE, INK);
    return y + box + PHOTO_GAP;
}

// Name, pronouns, title, company and the rule underneath. Run with
// measureOnly first so the photo can be sized against whatever is left.
static int drawIdentity(int y, int contentW, bool measureOnly)
{
    y += centredBlock(y, contentW, NAME_FONTS, 3, g_cfg.name, measureOnly);

    if (g_cfg.pronouns.length()) {
        if (!measureOnly)
            canvasDrawText(CANVAS_W / 2, y, &Roboto12, g_cfg.pronouns.c_str(),
                           ALIGN_CENTER, false);
        y += Roboto12.advance_y;
    }
    y += 8;

    int h = centredBlock(y, contentW, TITLE_FONTS, 3, g_cfg.profession, measureOnly);
    if (h) y += h + 2;

    y += centredBlock(y, contentW, SMALL_FONTS, 2, upper(g_cfg.company), measureOnly);

    y += 12;
    if (!measureOnly) canvasHRule(M + 60, y, contentW - 120, 2, INK);
    return y + 16;
}

// The QR plus its caption, anchored to the bottom of the badge.
// Returns the y coordinate of the top of the block.
static int drawQrBlock(int bottom)
{
    String payload = qrPayload();
    int qrTop = bottom - QR_BOX;

    // side is 0 when the payload is empty or too long to encode; the caption
    // then simply takes the whole width instead.
    int side = canvasDrawQR(M, qrTop, QR_BOX, payload.c_str(), g_cfg.invert);

    int cx = M + side + (side ? 20 : 0);
    int cw = CANVAS_W - M - cx;
    if (cw < 80) return qrTop;

    String contact = g_cfg.website.length() ? g_cfg.website
                   : (g_cfg.email.length()  ? g_cfg.email : String(""));

    // Measure first so the caption can be centred against the QR square.
    int capLines = canvasDrawParagraph(cx, 0, cw, 2, &Roboto18,
                                       g_cfg.qrCaption.c_str(),
                                       ALIGN_LEFT, false, /*measureOnly=*/true);
    int conLines = contact.length()
                 ? canvasDrawParagraph(cx, 0, cw, 2, &Roboto12, contact.c_str(),
                                       ALIGN_LEFT, false, true)
                 : 0;

    int blockH = capLines * Roboto18.advance_y +
                 (conLines ? 6 + conLines * Roboto12.advance_y : 0);
    int ty = qrTop + (QR_BOX - blockH) / 2;
    if (ty < qrTop) ty = qrTop;

    if (capLines) {
        canvasDrawParagraph(cx, ty, cw, 2, &Roboto18, g_cfg.qrCaption.c_str(),
                            ALIGN_LEFT, false, false);
        ty += capLines * Roboto18.advance_y + 6;
    }
    if (conLines)
        canvasDrawParagraph(cx, ty, cw, 2, &Roboto12, contact.c_str(),
                            ALIGN_LEFT, false, false);

    return qrTop;
}

// How many lines the fun fact really wants in a given face. maxLines is
// deliberately generous -- we want the true requirement, not a truncated one.
static int factLines(const GFXfont *f, int innerW)
{
    return canvasDrawParagraph(0, 0, innerW, 8, f, g_cfg.funFact.c_str(),
                               ALIGN_LEFT, false, /*measureOnly=*/true);
}

// Height to set aside for the fun fact box before the photo is sized. A one
// line fact leaves the photo bigger; a long one takes its three lines back.
static int factReserve(int contentW)
{
    if (!g_cfg.funFact.length()) return 0;
    int lines = factLines(&Roboto12, contentW - 32);
    if (lines < 1)          lines = 1;
    if (lines > FACT_LINES) lines = FACT_LINES;
    return 2 * FACT_PAD + Roboto12.advance_y + 2 + lines * Roboto12.advance_y;
}

// The fun fact fills whatever vertical slack is left between the identity
// block and the QR block, so the badge stays balanced whether or not there is
// a photo and however long the name wraps.
static void drawFunFact(int top, int bottom, int contentW)
{
    int boxH = bottom - top;
    if (boxH < 70 || !g_cfg.funFact.length()) return;

    canvasFillRect(M, top, contentW, boxH, GREY(14));
    canvasFrame(M, top, contentW, boxH, 2, INK);

    int innerX = M + 16;
    int innerW = contentW - 32;
    int ty     = top + FACT_PAD;

    canvasDrawText(innerX, ty, &Roboto12, "FUN FACT", ALIGN_LEFT, false);
    ty += Roboto12.advance_y + 2;

    int avail = (top + boxH - FACT_PAD) - ty;

    // Use the larger face only when the whole fact fits in it. Picking it just
    // because two lines happen to clear would drop text that the smaller face
    // would have shown in full.
    const GFXfont *f = &Roboto12;
    int big = factLines(&Roboto18, innerW);
    if (big * Roboto18.advance_y <= avail) f = &Roboto18;

    int maxLines = avail / f->advance_y;
    if (maxLines < 1) return;

    canvasDrawParagraph(innerX, ty, innerW, maxLines, f,
                        g_cfg.funFact.c_str(), ALIGN_LEFT, false, false);
}

// Where the body stops and the optional footer line begins. Single source of
// truth so badgeRender() and badgePhotoBox() cannot drift apart.
static int bodyBottom()
{
    int bottom = CANVAS_H - M;
    if (g_cfg.footer.length()) bottom -= Roboto12.advance_y + 10;
    return bottom;
}

// The photo is the elastic element: sizing it from whatever the fixed blocks
// leave behind is what stops the fun fact being squeezed out when the name
// wraps or a footer is set.
static int computePhotoBox(int contentW)
{
    if (!g_cfg.showPhoto) return 0;

    int qrTop     = bodyBottom() - QR_BOX;
    int identityH = drawIdentity(0, contentW, /*measureOnly=*/true);
    int slack     = (qrTop - FACT_GAP) - (HDR_H + 26) - identityH;

    int want = slack - PHOTO_GAP - factReserve(contentW);
    if (want > PHOTO_MAX) want = PHOTO_MAX;
    if (want >= PHOTO_MIN) return want;
    return (slack - PHOTO_GAP >= PHOTO_MIN) ? PHOTO_MIN : 0;
}

int badgePhotoBox()
{
    int box = computePhotoBox(CANVAS_W - 2 * M);
    return box ? box : PHOTO_W;      // fall back to the nominal size
}

void badgeRender(bool fullClear)
{
    if (!canvasBegin()) return;

    canvasSetFlip(g_cfg.flip);
    canvasClear(PAPER);

    const int contentW   = CANVAS_W - 2 * M;
    const int contentTop = HDR_H + 26;
    const bool inv       = g_cfg.invert;

    drawHeader(contentW);

    // Bottom-anchored blocks go down first: everything above has to fit in
    // whatever they leave behind.
    if (g_cfg.footer.length())
        canvasDrawText(CANVAS_W / 2, CANVAS_H - M - Roboto12.advance_y, &Roboto12,
                       g_cfg.footer.c_str(), ALIGN_CENTER, false);
    int qrTop = drawQrBlock(bodyBottom());

    int photoBox = computePhotoBox(contentW);

    int y = contentTop;
    if (photoBox) y = drawPhoto(y, photoBox, inv);
    y = drawIdentity(y, contentW, false);

    drawFunFact(y, qrTop - FACT_GAP, contentW);

    if (g_cfg.border) canvasFrame(0, 0, CANVAS_W, CANVAS_H, RULE, INK);

    // Dark theme is one pass over the framebuffer at the very end; the photo
    // and the QR were drawn pre-inverted so they survive it the right way up.
    if (inv) canvasInvertAll();

    canvasPush(fullClear);

    s_shownFingerprint = configFingerprint();
    s_panelValid       = true;
}

void badgeBlank()
{
    if (!canvasBegin()) return;
    canvasClear(PAPER);
    canvasPush(true);
    s_panelValid = false;
}
