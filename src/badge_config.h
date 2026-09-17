#pragma once
#include <Arduino.h>

// Where the badge keeps its state on LittleFS.
#define CFG_PATH    "/badge.json"
#define PHOTO_PATH  "/photo.bin"

// Nominal photo size. The live target is badgePhotoBox(), which adapts to how
// much room the rest of the layout leaves; these are only the fallback used
// before the badge has reported a size. Anything else is rescaled on the badge.
#define PHOTO_W 320
#define PHOTO_H 320

struct BadgeConfig {
    // --- who you are -------------------------------------------------
    String name       = "Your Name";
    String pronouns   = "";
    String profession = "Your Job Title";
    String company    = "Your Company";
    String funFact    = "Ask me about Robotics.";
    String event      = "CONFERENCE 2026";
    String footer     = "";

    // --- QR code -----------------------------------------------------
    // qrMode: "url" | "text" | "vcard" | "wifi"
    String qrMode    = "url";
    String qrText    = "https://example.com";
    String qrCaption = "Scan to connect";
    String email     = "";
    String phone     = "";
    String website   = "";

    // --- appearance --------------------------------------------------
    bool showPhoto = true;
    bool flip      = false;   // rotate the portrait layout 180 deg
    bool invert    = false;   // dark theme (white ink on black)
    bool border    = true;    // hairline frame around the badge

    // --- power / network ---------------------------------------------
    uint16_t sleepAfterMin = 10;   // 0 = never sleep, stay on the portal
    String apSsid = "";            // empty -> Badge-XXXX from the MAC
    String apPass = "badge1234";   // >= 8 chars, or empty for an open AP
    String staSsid = "";
    String staPass = "";
};

extern BadgeConfig g_cfg;

bool   configLoad();
bool   configSave();
void   configToJson(String &out);
bool   configFromJson(const String &json, String &err);

// Resolved payload that actually gets encoded into the QR symbol.
String qrPayload();

// Cheap change-detector so we only burn an e-ink refresh when something moved.
uint32_t configFingerprint();
