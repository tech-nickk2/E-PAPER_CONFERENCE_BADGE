// ---------------------------------------------------------------------------
//  E-Paper Conference Badge
//  LilyGo T5 4.7" ESP32-S3 (ED047TC1) and pin-compatible boards such as the
//  DFRobot 4.7" e-ink module.
//
//  Boot -> draw the badge (only if it changed) -> open the setup portal ->
//  fall asleep when nobody is using it. The e-ink image survives deep sleep
//  with the power completely off, which is the whole point of the exercise.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#include "epd_driver.h"       // also pulls in utilities.h for BUTTON_1 / BATT_PIN
#include "badge_config.h"
#include "badge_render.h"
#include "canvas.h"
#include "portal.h"

#define WAKE_PIN   ((gpio_num_t)BUTTON_1)
#define LONG_PRESS 1500       // ms held before the badge goes to sleep

static float s_batt = 0.0f;

// portal.cpp reads this for /api/status.
float batteryVolts() { return s_batt; }

// The board divides VBAT by two into an ADC2 pin. ADC2 is unusable once WiFi
// is running, so this has to happen before portalBegin().
static void readBattery()
{
    analogSetPinAttenuation(BATT_PIN, ADC_11db);
    uint32_t acc = 0;
    for (int i = 0; i < 16; i++) acc += analogReadMilliVolts(BATT_PIN);
    s_batt = (acc / 16.0f) * 2.0f / 1000.0f;
}

static void sleepNow()
{
    Serial.printf("[badge] idle, sleeping. Press the button on GPIO%d to wake.\n",
                  (int)WAKE_PIN);
    Serial.flush();

    portalStop();
    epd_poweroff_all();

    rtc_gpio_pullup_en(WAKE_PIN);
    rtc_gpio_pulldown_dis(WAKE_PIN);
    esp_sleep_enable_ext0_wakeup(WAKE_PIN, 0);   // button pulls the pin low
    delay(50);
    esp_deep_sleep_start();
}

// Short press redraws, long press sleeps immediately.
static void serviceButton()
{
    static bool     armed  = false;   // the press that woke us does not count
    static bool     down   = false;
    static uint32_t downAt = 0;

    bool pressed = (digitalRead(BUTTON_1) == LOW);

    // Waking from deep sleep means the button was down as we booted. Wait for
    // it to come up before listening, or a slow release would put us straight
    // back to sleep.
    if (!armed) {
        if (!pressed) armed = true;
        return;
    }

    if (pressed && !down) {
        down = true;
        downAt = millis();
    } else if (pressed && down && millis() - downAt > LONG_PRESS) {
        sleepNow();                       // never returns
    } else if (!pressed && down) {
        down = false;
        if (millis() - downAt > 40) {     // debounce
            Serial.println("[badge] button: redraw");
            badgeRender(true);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println("\n[badge] booting");

    pinMode(BUTTON_1, INPUT_PULLUP);
    readBattery();
    Serial.printf("[badge] battery %.2f V\n", s_batt);

    if (!LittleFS.begin(true))
        Serial.println("[badge] LittleFS mount failed -- settings will not persist");
    configLoad();

    epd_init();
    if (!canvasBegin()) {
        Serial.println("[badge] FATAL: no PSRAM for the framebuffers");
        return;
    }

    esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
    if (badgeNeedsRender()) {
        Serial.println("[badge] content changed, refreshing the panel");
        badgeRender(true);
    } else {
        Serial.printf("[badge] panel already current (wake cause %d), skipping refresh\n",
                      (int)why);
    }

    portalBegin();
    Serial.printf("[badge] portal at http://%s/  (also http://badge.local/)\n",
                  portalAddress().c_str());
    if (g_cfg.sleepAfterMin)
        Serial.printf("[badge] will sleep after %u idle minutes\n", g_cfg.sleepAfterMin);
    else
        Serial.println("[badge] sleep disabled, WiFi stays up");
}

void loop()
{
    portalLoop();
    serviceButton();

    if (g_cfg.sleepAfterMin > 0 &&
        portalIdleMs() > (uint32_t)g_cfg.sleepAfterMin * 60000UL)
        sleepNow();

    delay(2);
}
