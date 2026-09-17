#pragma once
//
// canvas.h -- a portrait drawing surface on top of the landscape EPD47 panel.
//
// The ED047TC1 panel is physically 960 x 540. The LilyGo esp32s3 driver has no
// rotation support, so this module owns a 540 x 960 *logical* portrait canvas
// and maps every write into the landscape framebuffer.
//
// Text is the awkward part: write_string() only knows how to lay glyphs out
// horizontally into a 960-wide buffer. So we render each line into a scratch
// framebuffer and then rotate-blit the inked pixels onto the canvas.
//
#include <Arduino.h>
#include "epd_driver.h"

// Portrait logical size. Deliberately expressed in terms of the panel
// constants so the relationship stays obvious.
static const int CANVAS_W = EPD_HEIGHT;  // 540
static const int CANVAS_H = EPD_WIDTH;   // 960

// 4bpp greyscale, 0x00 = black ... 0xFF = white.
#define INK   0x00
#define PAPER 0xFF
#define GREY(n) ((uint8_t)((n) * 17))   // n = 0..15

enum TextAlign { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

bool     canvasBegin();                    // allocates both PSRAM buffers
uint8_t *canvasFramebuffer();
void     canvasSetFlip(bool flip);         // 180 deg rotation of the layout

void canvasClear(uint8_t color);
void canvasPixel(int x, int y, uint8_t color);
void canvasFillRect(int x, int y, int w, int h, uint8_t color);
void canvasFrame(int x, int y, int w, int h, int thickness, uint8_t color);
void canvasHRule(int x, int y, int len, int thickness, uint8_t color);

// Folds UTF-8 down to the printable ASCII the bundled fonts actually carry
// (accents stripped, smart quotes and dashes normalised). Applied
// automatically by every text call below; exposed for callers that need to
// measure a string themselves.
String canvasFold(const char *s);

// --- text -----------------------------------------------------------------
// yTop is the top of the line box; the baseline lands at yTop + font->ascender.
// Returns the ink width that was drawn.
int  canvasTextWidth(const GFXfont *font, const char *s);
int  canvasLineHeight(const GFXfont *font);
int  canvasDrawText(int x, int yTop, const GFXfont *font, const char *s,
                    TextAlign align, bool invert);

// Greedy word wrap. Returns the number of lines drawn; pass measureOnly to
// find out how tall a block would be without touching the canvas.
int  canvasDrawParagraph(int x, int yTop, int maxW, int maxLines,
                         const GFXfont *font, const char *s,
                         TextAlign align, bool invert, bool measureOnly);

// Picks the largest font from the list whose rendering of s fits in maxW.
const GFXfont *canvasFitFont(const GFXfont *const *fonts, int nFonts,
                             const char *s, int maxW);

// --- graphics -------------------------------------------------------------
// Draws the QR into a box <= boxPx wide, top-left anchored, and returns the
// side length actually used (module size is quantised, so it is <= boxPx).
int  canvasDrawQR(int x, int y, int boxPx, const char *text, bool invert);

// Reads PHOTO_PATH-style 4bpp data and nearest-neighbour scales it into w x h.
bool canvasDrawPhoto(int x, int y, int w, int h, const char *path, bool invert);

// --- output ---------------------------------------------------------------
void canvasInvertAll();                    // dark-theme flip of the whole frame
void canvasPush(bool fullClear);           // power on, draw, power off
