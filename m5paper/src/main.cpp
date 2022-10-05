#include <M5EPD.h>
#include <WiFi.h>

#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi setting
#define WIFI_SSID "PLEASE REPLACE HERE"
#define WIFI_PASSWORD "PLEASE REPLACE HERE"

// API setting
#define CALENDAR_API_URL "PLEASE REPLACE HERE"
#define CALENDAR_API_KEY "PLEASE REPLACE HERE"

// Global type
typedef StaticJsonDocument<1024> JsonDoc;

// Functions
JsonDoc callApi(const char *key, const char *url);
float getRemainBattery();
void drawStatusBar(M5EPD_Canvas *canvas, long width);
void drawItems(M5EPD_Canvas *canvas, long width, JsonDoc *events);

// Global vars
const char *NTP_SERVER = "ntp.nict.jp";
const long GMT_OFFSET_SEC = 60 * 60 * 9; // JST
const int DAYLIGHT_OFFSET_SEC = 0;

const int WIFI_STATUS_CHECK_INTERVAL = 1000; // 1 sec
const int UPDATE_INTERVAL = 10 * 1000; // 10 sec

const int MAX_DISPLAY_EVENTS = 6;

const int NORMAL_FONT_SIZE = 24; // 3 for default font
const int TITLE_FONT_SIZE = 40; // 9 for default font

const int MARGIN_TOP = 5;
const int MARGIN_LEFT = 10;
const int MARGIN_HLINE_BOTTOM = 15;
const int MARGIN_NORMAL_FONT_BOTTOM = 10;
const int MARGIN_STATUS_BAR_BOTTOM = 30;
const int MARGIN_ITEMS_BOTTOM = 30;
const int NORMAL_FONT_HEIGHT = 30;
const int TITLE_FONT_HEIGHT_WITH_MARGIN = 75;
const int STATUS_BAR_HEIGHT = 40; // 30 for default font
const int STATUS_BAR_HLINE_HEIGHT = 5;
const int BATTERY_STR_WIDTH = 115;
const unsigned int STATUS_BAR_PARTITION_COLOR = 31;

JsonDoc prevCalendarEvents;
JsonDoc prevShoppingItems;

M5EPD_Canvas canvas(&M5.EPD);

void setup()
{
    // Setup M5Paper
    M5.begin();
    M5.EPD.SetRotation(90);  // Vertical
    M5.TP.SetRotation(90);
    M5.EPD.Clear(true);
    M5.RTC.begin();
    M5.BatteryADCBegin();

    // Load Font
    Serial.println("START loading font");
    esp_err_t err = canvas.loadFont("/font.ttf", SD);
    if (err != ESP_OK) {
        Serial.println(err);
        return;
    }
    Serial.println("END loading font");

    // Connect WiFi
    Serial.println("START connecting WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(WIFI_STATUS_CHECK_INTERVAL);
        Serial.print(".");
    }
    Serial.println(" OK");
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("END connecting WiFi");

    // Time sync
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    // Setup Canvas
    canvas.createCanvas(M5EPD_PANEL_H, M5EPD_PANEL_W);  // H and W are switched because of vertical
    canvas.createRender(NORMAL_FONT_SIZE);
    canvas.createRender(TITLE_FONT_SIZE);
    canvas.setTextSize(NORMAL_FONT_SIZE);
}

void loop()
{
    canvas.fillRect(0, 0, M5EPD_PANEL_H, M5EPD_PANEL_W, BLACK);

    // Draw status bar
    Serial.println("START draw status bar");
    drawStatusBar(&canvas, M5EPD_PANEL_H);
    Serial.println("END draw status bar");

    // Get calendar events
    Serial.println("START call listCalendarEvents function");
    JsonDoc events = callApi(CALENDAR_API_KEY, CALENDAR_API_URL);
    if (!events.containsKey("items")) { events = prevCalendarEvents; }
    else { prevCalendarEvents = events; }
    Serial.println("END call listCalendarEvents function");

    // Draw calendar events and shopping items
    Serial.println("START draw items");
    drawItems(&canvas, M5EPD_PANEL_H, &events);
    Serial.println("END draw items");

    // Update canvas
    canvas.pushCanvas(0, 0, UPDATE_MODE_GLR16);

    // Wait for next update timing
    delay(UPDATE_INTERVAL);
}

/**
 * Calculate remaining battery using M5Paper_FactoryTest code
 * https://github.com/m5stack/M5Paper_FactoryTest/blob/ef8d1ff94490a9364479231d6ba7e343d9adaa06/src/frame/frame_main.cpp#L272
 * @return Remaining battery value
 */
float getRemainBattery()
{
    uint32_t vol = M5.getBatteryVoltage();
    if (vol < 3300) { vol = 3300; }
    else if (vol > 4350) { vol = 4350; }

    float battery = (float)(vol - 3300) / (float)(4350 - 3300);
    if (battery <= 0.01) { battery = 0.01; }
    if (battery > 1) { battery = 1; }

    return battery;
}

/**
 * Draw status bar
 * @param (canvas) M5EPD canvas object
 * @param (widht) Status bar width
 */
void drawStatusBar(M5EPD_Canvas *canvas, long width)
{
    // Draw partition horizontal line
    canvas->fillRect(0, STATUS_BAR_HEIGHT - STATUS_BAR_HLINE_HEIGHT, width, STATUS_BAR_HLINE_HEIGHT, STATUS_BAR_PARTITION_COLOR);

    // Draw status bar contents
    // Battery
    char battString[32];
    sprintf(battString, "bat:%.0f%%", getRemainBattery() * 100);
    canvas->drawString(battString, width - BATTERY_STR_WIDTH, MARGIN_TOP);

    // Current time
    struct tm now;
    bool succeeded = getLocalTime(&now);
    if (!succeeded) { Serial.println("getLocalTime() failed"); }

    char buf[30];
    strftime(buf, sizeof(buf) - 1, "%Y-%m-%d %H:%M:%S", &now);
    canvas->drawString(buf, MARGIN_LEFT, MARGIN_TOP);
}

/**
 * Call API
 * @param (key) API key
 * @param (url) API URL
 * @return API result JSON object
 */
JsonDoc callApi(const char *key, const char *url)
{
    HTTPClient client;
    client.begin(url);
    client.addHeader("x-api-key", key);
    Serial.printf("Access to %s\n", url);

    const int statusCode = client.GET();
    Serial.printf("Response status: %d\n", statusCode);

    String payload = client.getString();
    Serial.println(payload);

    JsonDoc doc;
    if (statusCode == 200) {
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return doc;
        }
        serializeJsonPretty(doc, Serial);
        Serial.println("");
        return doc;
    }

    return doc;
}

/**
 * Draw calender events and shopping items
 * @param (canvas) M5EPD canvas object
 * @param (width) Draw area width
 * @param (events) Calendar events JSON object
 */
void drawItems(M5EPD_Canvas *canvas, long width, JsonDoc *events)
{
    int dy = STATUS_BAR_HEIGHT + MARGIN_STATUS_BAR_BOTTOM;
    // Draw events
    canvas->setTextSize(TITLE_FONT_SIZE);
    canvas->drawString("Plans", MARGIN_LEFT, dy);
    dy += TITLE_FONT_HEIGHT_WITH_MARGIN;
    canvas->drawFastHLine(0, dy, M5EPD_PANEL_H, STATUS_BAR_PARTITION_COLOR);
    dy += MARGIN_HLINE_BOTTOM;
    canvas->setTextSize(NORMAL_FONT_SIZE);

    int numItems = (*events)["num_items"];
    if (numItems > MAX_DISPLAY_EVENTS) { numItems = MAX_DISPLAY_EVENTS; }

    for (int i = 0; i < numItems; i++) {
        const char *title = (*events)["items"][i]["title"];
        const char *displayTime = (*events)["items"][i]["displayTime"];

        Serial.printf("Event No: %d\n", i + 1);
        Serial.println(title);
        Serial.println(displayTime);

        canvas->drawString(displayTime, MARGIN_LEFT, dy);
        dy += NORMAL_FONT_HEIGHT;
        canvas->drawString(title, MARGIN_LEFT, dy);
        dy += NORMAL_FONT_HEIGHT + MARGIN_NORMAL_FONT_BOTTOM;
        canvas->drawFastHLine(0, dy, M5EPD_PANEL_H, STATUS_BAR_PARTITION_COLOR);
        dy += MARGIN_HLINE_BOTTOM;
    }

    if (numItems == 0) {
        canvas->drawString("NO schedules", MARGIN_LEFT, dy);
        dy += NORMAL_FONT_HEIGHT + MARGIN_NORMAL_FONT_BOTTOM;
        canvas->drawFastHLine(0, dy, M5EPD_PANEL_H, STATUS_BAR_PARTITION_COLOR);
        dy += MARGIN_HLINE_BOTTOM;
    }
}