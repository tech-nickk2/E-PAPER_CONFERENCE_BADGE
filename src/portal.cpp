#include "portal.h"
#include "badge_config.h"
#include "badge_render.h"
#include "canvas.h"
#include "web_ui.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

#define PHOTO_TMP "/photo.upl"

static WebServer server(80);
static DNSServer dns;
static bool      s_apMode  = true;
static uint32_t  s_lastReq = 0;
static File      s_upload;
static bool      s_uploadOk = false;

// A panel refresh blocks for several seconds. Doing it inside a request
// handler would stall the socket and leave the phone staring at a spinner (or
// worse, truncate the reply), so handlers only raise a flag and portalLoop()
// runs the refresh once the response has gone out.
enum Pending { PEND_NONE, PEND_RENDER, PEND_BLANK };
static Pending s_pending = PEND_NONE;

extern float batteryVolts();   // provided by main.cpp

static void touch() { s_lastReq = millis(); }

uint32_t portalIdleMs()  { return millis() - s_lastReq; }
bool     portalIsAP()    { return s_apMode; }
String   portalAddress() { return s_apMode ? WiFi.softAPIP().toString()
                                           : WiFi.localIP().toString(); }

static void sendJson(int code, const String &body)
{
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json", body);
}

static void sendOk(const char *msg = "ok")
{
    sendJson(200, String("{\"ok\":true,\"msg\":\"") + msg + "\"}");
}

static void sendErr(int code, const String &msg)
{
    String m = msg;
    m.replace("\"", "'");
    sendJson(code, String("{\"ok\":false,\"msg\":\"") + m + "\"}");
}

// --------------------------------------------------------------------------
// handlers
// --------------------------------------------------------------------------
static void handleRoot()
{
    touch();
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handleGetConfig()
{
    touch();
    String out;
    configToJson(out);
    sendJson(200, out);
}

static void handlePostConfig()
{
    touch();
    if (!server.hasArg("plain")) { sendErr(400, "empty body"); return; }

    String err;
    if (!configFromJson(server.arg("plain"), err)) { sendErr(400, err); return; }
    if (!configSave()) { sendErr(500, "could not write badge.json"); return; }

    if (server.hasArg("render") && server.arg("render") == "1") {
        s_pending = PEND_RENDER;
        sendOk("saved, redrawing");
        return;
    }
    sendOk("saved");
}

static void handleRender()
{
    touch();
    s_pending = PEND_RENDER;
    sendOk("redrawing");
}

static void handleBlank()
{
    touch();
    s_pending = PEND_BLANK;
    sendOk("clearing");
}

static void handleStatus()
{
    touch();
    String s = "{";
    s += "\"mode\":\"" + String(s_apMode ? "ap" : "sta") + "\",";
    s += "\"ip\":\"" + portalAddress() + "\",";
    s += "\"ssid\":\"" + String(s_apMode ? WiFi.softAPSSID() : WiFi.SSID()) + "\",";
    s += "\"rssi\":" + String(s_apMode ? 0 : WiFi.RSSI()) + ",";
    s += "\"heap\":" + String((uint32_t)ESP.getFreeHeap()) + ",";
    s += "\"psram\":" + String((uint32_t)ESP.getFreePsram()) + ",";
    s += "\"battery\":" + String(batteryVolts(), 2) + ",";
    s += "\"uptime\":" + String(millis() / 1000) + ",";
    int box = badgePhotoBox();
    s += "\"photoW\":" + String(box) + ",";
    s += "\"photoH\":" + String(box) + ",";
    s += "\"hasPhoto\":" + String(LittleFS.exists(PHOTO_PATH) ? "true" : "false") + ",";
    s += "\"canvasW\":" + String(CANVAS_W) + ",";
    s += "\"canvasH\":" + String(CANVAS_H) + ",";
    s += "\"idleMs\":" + String(portalIdleMs());
    s += "}";
    sendJson(200, s);
}

static void handlePhotoGet()
{
    touch();
    File f = LittleFS.open(PHOTO_PATH, "r");
    if (!f) { sendErr(404, "no photo"); return; }
    server.sendHeader("Cache-Control", "no-store");
    server.streamFile(f, "application/octet-stream");
    f.close();
}

static void handlePhotoDelete()
{
    touch();
    LittleFS.remove(PHOTO_PATH);
    sendOk("photo removed");
}

// Streams the multipart body straight to flash; the raw 4bpp image for a
// 320x320 photo is ~51 kB, far too big to buffer in a String.
static void handlePhotoUpload()
{
    HTTPUpload &u = server.upload();

    if (u.status == UPLOAD_FILE_START) {
        touch();
        s_uploadOk = false;
        LittleFS.remove(PHOTO_TMP);
        s_upload = LittleFS.open(PHOTO_TMP, "w");
        if (!s_upload) log_e("cannot open %s for write", PHOTO_TMP);
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (s_upload && s_upload.write(u.buf, u.currentSize) != u.currentSize) {
            log_e("short write -- filesystem full?");
            s_upload.close();
        }
    } else if (u.status == UPLOAD_FILE_END) {
        if (s_upload) s_upload.close();

        // Only promote the temp file if it really is one of our images.
        File chk = LittleFS.open(PHOTO_TMP, "r");
        if (chk) {
            uint8_t hdr[8];
            if (chk.size() > 8 && chk.read(hdr, 8) == 8 && memcmp(hdr, "EPB1", 4) == 0) {
                int w = hdr[4] | (hdr[5] << 8), h = hdr[6] | (hdr[7] << 8);
                size_t need = 8 + (size_t)((w + 1) / 2) * (size_t)h;
                s_uploadOk = (chk.size() >= need);
            }
            chk.close();
        }
        if (s_uploadOk) {
            LittleFS.remove(PHOTO_PATH);
            s_uploadOk = LittleFS.rename(PHOTO_TMP, PHOTO_PATH);
        }
        if (!s_uploadOk) LittleFS.remove(PHOTO_TMP);
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        if (s_upload) s_upload.close();
        LittleFS.remove(PHOTO_TMP);
    }
}

static void handlePhotoDone()
{
    touch();
    if (!s_uploadOk) { sendErr(400, "upload rejected (bad header or no space)"); return; }
    if (server.hasArg("render") && server.arg("render") == "1") {
        s_pending = PEND_RENDER;
        sendOk("photo stored, redrawing");
        return;
    }
    sendOk("photo stored");
}

static void handleNotFound()
{
    // Captive-portal behaviour: anything we do not recognise bounces to the
    // setup page, which is what makes phones pop the "sign in" sheet.
    if (s_apMode) {
        server.sendHeader("Location", String("http://") + portalAddress() + "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.send(404, "text/plain", "not found");
}

// --------------------------------------------------------------------------
// bring-up
// --------------------------------------------------------------------------
static String defaultApSsid()
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[20];
    snprintf(buf, sizeof(buf), "Badge-%02X%02X", mac[4], mac[5]);
    return String(buf);
}

static bool tryStation()
{
    if (!g_cfg.staSsid.length()) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_cfg.staSsid.c_str(), g_cfg.staPass.c_str());
    log_i("joining %s ...", g_cfg.staSsid.c_str());

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) delay(200);

    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.disconnect(true);
    return false;
}

void portalBegin()
{
    WiFi.persistent(false);

    s_apMode = !tryStation();
    if (s_apMode) {
        String ssid = g_cfg.apSsid.length() ? g_cfg.apSsid : defaultApSsid();
        WiFi.mode(WIFI_AP);
        if (g_cfg.apPass.length() >= 8) WiFi.softAP(ssid.c_str(), g_cfg.apPass.c_str());
        else                            WiFi.softAP(ssid.c_str());
        delay(100);
        dns.setErrorReplyCode(DNSReplyCode::NoError);
        dns.start(53, "*", WiFi.softAPIP());
        log_i("AP  %s  http://%s/", ssid.c_str(), WiFi.softAPIP().toString().c_str());
    } else {
        log_i("STA %s  http://%s/", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    }

    if (MDNS.begin("badge")) MDNS.addService("http", "tcp", 80);

    server.on("/",             HTTP_GET,  handleRoot);
    server.on("/api/config",   HTTP_GET,  handleGetConfig);
    server.on("/api/config",   HTTP_POST, handlePostConfig);
    server.on("/api/status",   HTTP_GET,  handleStatus);
    server.on("/api/render",   HTTP_POST, handleRender);
    server.on("/api/blank",    HTTP_POST, handleBlank);
    server.on("/api/photo",    HTTP_GET,  handlePhotoGet);
    server.on("/api/photo",    HTTP_POST, handlePhotoDone, handlePhotoUpload);
    server.on("/api/photo",    HTTP_DELETE, handlePhotoDelete);
    server.onNotFound(handleNotFound);

    server.begin();
    touch();
}

void portalLoop()
{
    if (s_apMode) dns.processNextRequest();
    server.handleClient();

    if (s_pending != PEND_NONE) {
        Pending job = s_pending;
        s_pending = PEND_NONE;
        if (job == PEND_RENDER) badgeRender(true);
        else                    badgeBlank();
        touch();            // the refresh itself counts as activity
    }
}

void portalStop()
{
    server.stop();
    if (s_apMode) dns.stop();
    MDNS.end();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}
