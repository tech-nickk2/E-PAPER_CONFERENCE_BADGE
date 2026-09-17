#pragma once
#include <Arduino.h>

// Composes the whole badge into the canvas and pushes it to the panel.
// fullClear does the two flush cycles that remove ghosting -- worth it after a
// content change, skippable if you are only nudging the layout.
void badgeRender(bool fullClear);

// Wipes the panel to plain white (used by the "clear screen" button).
void badgeBlank();

// True when the config or photo has changed since whatever is currently held
// on the panel. Survives deep sleep, so waking up does not cost a refresh.
bool badgeNeedsRender();

// Side length, in pixels, of the square the photo will occupy with the current
// settings. The setup page asks for this so the browser can produce pixels at
// exactly the right size -- rescaling a dithered image on the badge would turn
// the dither pattern into noise.
int badgePhotoBox();
