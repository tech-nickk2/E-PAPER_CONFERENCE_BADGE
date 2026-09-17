#pragma once
#include <Arduino.h>

// Brings up WiFi (STA if credentials are stored, otherwise a captive AP) and
// starts the HTTP API + setup page.
void   portalBegin();
void   portalLoop();
void   portalStop();

// Milliseconds since the last request. main.cpp uses this to decide when the
// badge has been left alone long enough to go back to sleep.
uint32_t portalIdleMs();

String portalAddress();     // e.g. "192.168.4.1" -- printed on the serial log
bool   portalIsAP();
