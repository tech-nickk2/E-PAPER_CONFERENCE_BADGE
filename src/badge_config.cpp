#include "badge_config.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

BadgeConfig g_cfg;

static void applyJson(JsonObjectConst o)
{
    BadgeConfig d;  // defaults for any key the client left out
    g_cfg.name       = o["name"]       | d.name;
    g_cfg.pronouns   = o["pronouns"]   | d.pronouns;
    g_cfg.profession = o["profession"] | d.profession;
    g_cfg.company    = o["company"]    | d.company;
    g_cfg.funFact    = o["funFact"]    | d.funFact;
    g_cfg.event      = o["event"]      | d.event;
    g_cfg.footer     = o["footer"]     | d.footer;

    g_cfg.qrMode     = o["qrMode"]     | d.qrMode;
    g_cfg.qrText     = o["qrText"]     | d.qrText;
    g_cfg.qrCaption  = o["qrCaption"]  | d.qrCaption;
    g_cfg.email      = o["email"]      | d.email;
    g_cfg.phone      = o["phone"]      | d.phone;
    g_cfg.website    = o["website"]    | d.website;

    g_cfg.showPhoto  = o["showPhoto"]  | d.showPhoto;
    g_cfg.flip       = o["flip"]       | d.flip;
    g_cfg.invert     = o["invert"]     | d.invert;
    g_cfg.border     = o["border"]     | d.border;

    g_cfg.sleepAfterMin = o["sleepAfterMin"] | d.sleepAfterMin;
    g_cfg.apSsid     = o["apSsid"]     | d.apSsid;
    g_cfg.apPass     = o["apPass"]     | d.apPass;
    g_cfg.staSsid    = o["staSsid"]    | d.staSsid;
    g_cfg.staPass    = o["staPass"]    | d.staPass;
}

bool configLoad()
{
    File f = LittleFS.open(CFG_PATH, "r");
    if (!f) {
        log_i("no %s yet, using defaults", CFG_PATH);
        return false;
    }
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (e) {
        log_e("config parse failed: %s", e.c_str());
        return false;
    }
    applyJson(doc.as<JsonObjectConst>());
    return true;
}

static void fillDoc(JsonDocument &doc)
{
    doc["name"]       = g_cfg.name;
    doc["pronouns"]   = g_cfg.pronouns;
    doc["profession"] = g_cfg.profession;
    doc["company"]    = g_cfg.company;
    doc["funFact"]    = g_cfg.funFact;
    doc["event"]      = g_cfg.event;
    doc["footer"]     = g_cfg.footer;

    doc["qrMode"]     = g_cfg.qrMode;
    doc["qrText"]     = g_cfg.qrText;
    doc["qrCaption"]  = g_cfg.qrCaption;
    doc["email"]      = g_cfg.email;
    doc["phone"]      = g_cfg.phone;
    doc["website"]    = g_cfg.website;

    doc["showPhoto"]  = g_cfg.showPhoto;
    doc["flip"]       = g_cfg.flip;
    doc["invert"]     = g_cfg.invert;
    doc["border"]     = g_cfg.border;

    doc["sleepAfterMin"] = g_cfg.sleepAfterMin;
    doc["apSsid"]     = g_cfg.apSsid;
    doc["apPass"]     = g_cfg.apPass;
    doc["staSsid"]    = g_cfg.staSsid;
    doc["staPass"]    = g_cfg.staPass;
}

bool configSave()
{
    JsonDocument doc;
    fillDoc(doc);

    // Write to a temp file first so a power cut mid-save cannot leave a
    // truncated badge.json behind.
    File f = LittleFS.open(CFG_PATH ".tmp", "w");
    if (!f) return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    if (!ok) { LittleFS.remove(CFG_PATH ".tmp"); return false; }

    LittleFS.remove(CFG_PATH);
    return LittleFS.rename(CFG_PATH ".tmp", CFG_PATH);
}

void configToJson(String &out)
{
    JsonDocument doc;
    fillDoc(doc);
    // Never hand passwords back to the browser; send a placeholder instead.
    doc["apPass"]  = g_cfg.apPass.length()  ? "__unchanged__" : "";
    doc["staPass"] = g_cfg.staPass.length() ? "__unchanged__" : "";
    out = "";
    serializeJson(doc, out);
}

bool configFromJson(const String &json, String &err)
{
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, json);
    if (e) { err = e.c_str(); return false; }
    if (!doc.is<JsonObject>()) { err = "expected a JSON object"; return false; }

    // Honour the "unchanged password" placeholder from configToJson().
    String keepAp = g_cfg.apPass, keepSta = g_cfg.staPass;
    applyJson(doc.as<JsonObjectConst>());
    if (g_cfg.apPass  == "__unchanged__") g_cfg.apPass  = keepAp;
    if (g_cfg.staPass == "__unchanged__") g_cfg.staPass = keepSta;

    // An AP password shorter than 8 chars is rejected by the WiFi stack and
    // would silently strand the user, so treat it as "open network".
    if (g_cfg.apPass.length() > 0 && g_cfg.apPass.length() < 8) g_cfg.apPass = "";

    if (g_cfg.sleepAfterMin > 240) g_cfg.sleepAfterMin = 240;
    return true;
}

static void vcardLine(String &s, const char *tag, const String &v)
{
    if (v.length()) { s += tag; s += v; s += "\r\n"; }
}

String qrPayload()
{
    if (g_cfg.qrMode == "vcard") {
        String s = "BEGIN:VCARD\r\nVERSION:3.0\r\n";
        s += "N:" + g_cfg.name + "\r\n";
        s += "FN:" + g_cfg.name + "\r\n";
        vcardLine(s, "ORG:",   g_cfg.company);
        vcardLine(s, "TITLE:", g_cfg.profession);
        vcardLine(s, "EMAIL;TYPE=INTERNET:", g_cfg.email);
        vcardLine(s, "TEL;TYPE=CELL:",       g_cfg.phone);
        vcardLine(s, "URL:",   g_cfg.website);
        s += "END:VCARD";
        return s;
    }
    if (g_cfg.qrMode == "wifi") {
        // qrText holds "SSID|password" for a guest-network share.
        int bar = g_cfg.qrText.indexOf('|');
        String ssid = bar < 0 ? g_cfg.qrText : g_cfg.qrText.substring(0, bar);
        String pass = bar < 0 ? String("")   : g_cfg.qrText.substring(bar + 1);
        return "WIFI:T:" + String(pass.length() ? "WPA" : "nopass") +
               ";S:" + ssid + ";P:" + pass + ";;";
    }
    return g_cfg.qrText;   // "url" and "text" are both just literal payloads
}

static void hashStr(uint32_t &h, const String &s)
{
    for (size_t i = 0; i < s.length(); i++)
        h = (h * 16777619u) ^ (uint8_t)s[i];
}

uint32_t configFingerprint()
{
    uint32_t h = 2166136261u;
    hashStr(h, g_cfg.name);      hashStr(h, g_cfg.pronouns);
    hashStr(h, g_cfg.profession);hashStr(h, g_cfg.company);
    hashStr(h, g_cfg.funFact);   hashStr(h, g_cfg.event);
    hashStr(h, g_cfg.footer);    hashStr(h, g_cfg.qrCaption);
    hashStr(h, qrPayload());
    h = (h * 16777619u) ^ (g_cfg.showPhoto ? 1 : 0);
    h = (h * 16777619u) ^ (g_cfg.flip   ? 1 : 0);
    h = (h * 16777619u) ^ (g_cfg.invert ? 1 : 0);
    h = (h * 16777619u) ^ (g_cfg.border ? 1 : 0);

    // Fold in the photo so replacing the image also triggers a redraw.
    File f = LittleFS.open(PHOTO_PATH, "r");
    if (f) {
        h = (h * 16777619u) ^ (uint32_t)f.size();
        f.close();
    }
    return h;
}
