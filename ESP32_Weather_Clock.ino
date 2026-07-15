#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <time.h>
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>
#include "external_icons_v12.h"

TFT_eSPI tft = TFT_eSPI();
U8g2_for_TFT_eSPI u8f;
Adafruit_SHT31 sht31 = Adafruit_SHT31();

// =====================================================
// 只修改这里
// =====================================================
const char* WIFI_NAME = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// 北京
const float LATITUDE = 39.9042;
const float LONGITUDE = 116.4074;

const long GMT_OFFSET_SEC = 8 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

const char* NTP_SERVER_1 = "ntp.aliyun.com";
const char* NTP_SERVER_2 = "pool.ntp.org";

// SHT30 / SHT31
const int SENSOR_SDA = 27;
const int SENSOR_SCL = 22;
const uint8_t SENSOR_ADDRESS = 0x44;

// =====================================================
// 数据
// =====================================================
float outdoorTemp = 0;
float maxTemp = 0;
float minTemp = 0;

float tomorrowMaxTemp = 0;
float tomorrowMinTemp = 0;
int tomorrowWeatherCode = -1;
bool tomorrowReady = false;

float indoorTemp = 0;
float indoorHumidity = 0;

int weatherCode = -1;
bool weatherReady = false;
bool sensorReady = false;
bool sensorHasData = false;

// =====================================================
// 刷新控制
// =====================================================
unsigned long lastWeatherUpdate = 0;
unsigned long lastSensorRead = 0;
unsigned long lastSensorDraw = 0;
unsigned long lastReconnectAttempt = 0;

long lastMinuteKey = -1;

const unsigned long WEATHER_INTERVAL =
  30UL * 60UL * 1000UL;

const unsigned long SENSOR_INTERVAL =
  3000UL;

const unsigned long SENSOR_DRAW_INTERVAL =
  5000UL;

const unsigned long RECONNECT_INTERVAL =
  30000UL;

// =====================================================
// 配色
// =====================================================
uint16_t COLOR_BG;
uint16_t COLOR_CARD;
uint16_t COLOR_TEXT;
uint16_t COLOR_MUTED;
uint16_t COLOR_LINE;
uint16_t COLOR_BLUE;
uint16_t COLOR_CYAN;
uint16_t COLOR_GREEN;
uint16_t COLOR_ORANGE;
uint16_t COLOR_RED;

void initColors() {
  COLOR_BG     = tft.color565(250, 248, 243);
  COLOR_CARD   = tft.color565(255, 255, 255);
  COLOR_TEXT   = tft.color565(25, 34, 48);
  COLOR_MUTED  = tft.color565(112, 118, 128);
  COLOR_LINE   = tft.color565(242, 242, 243);
  COLOR_BLUE   = tft.color565(48, 122, 230);
  COLOR_CYAN   = tft.color565(42, 169, 214);
  COLOR_GREEN  = tft.color565(46, 164, 91);
  COLOR_ORANGE = tft.color565(238, 145, 34);
  COLOR_RED    = tft.color565(210, 70, 62);
}

// =====================================================
// 中文
// =====================================================
void prepareChineseFont() {
  u8f.begin(tft);
  u8f.setFontMode(1);
  u8f.setFont(u8g2_font_wqy12_t_gb2312);
}

void drawChinese(
  int x,
  int baselineY,
  const String& text,
  uint16_t color,
  uint16_t background
) {
  u8f.setForegroundColor(color);
  u8f.setBackgroundColor(background);
  u8f.setCursor(x, baselineY);
  u8f.print(text);
}

// =====================================================
// 七段式电子钟
// =====================================================
void drawHorizontalSegment(
  int x,
  int y,
  int width,
  int thick,
  uint16_t color
) {
  int cut = thick / 2;

  tft.fillRect(
    x + cut,
    y,
    width - 2 * cut,
    thick,
    color
  );

  tft.fillTriangle(
    x,
    y + cut,
    x + cut,
    y,
    x + cut,
    y + thick,
    color
  );

  tft.fillTriangle(
    x + width,
    y + cut,
    x + width - cut,
    y,
    x + width - cut,
    y + thick,
    color
  );
}

void drawVerticalSegment(
  int x,
  int y,
  int thick,
  int height,
  uint16_t color
) {
  int cut = thick / 2;

  tft.fillRect(
    x,
    y + cut,
    thick,
    height - 2 * cut,
    color
  );

  tft.fillTriangle(
    x + cut,
    y,
    x,
    y + cut,
    x + thick,
    y + cut,
    color
  );

  tft.fillTriangle(
    x + cut,
    y + height,
    x,
    y + height - cut,
    x + thick,
    y + height - cut,
    color
  );
}

void drawSevenDigit(
  int digit,
  int x,
  int y,
  uint16_t color
) {
  static const uint8_t map[10] = {
    0b1111110,
    0b0110000,
    0b1101101,
    0b1111001,
    0b0110011,
    0b1011011,
    0b1011111,
    0b1110000,
    0b1111111,
    0b1111011
  };

  const int digitWidth = 27;
  const int digitHeight = 63;
  const int thick = 5;
  const int halfHeight = 31;

  uint8_t mask = map[digit];

  if (mask & 0b1000000) {
    drawHorizontalSegment(
      x + 4,
      y,
      digitWidth - 8,
      thick,
      color
    );
  }

  if (mask & 0b0100000) {
    drawVerticalSegment(
      x + digitWidth - thick,
      y + 4,
      thick,
      halfHeight - 6,
      color
    );
  }

  if (mask & 0b0010000) {
    drawVerticalSegment(
      x + digitWidth - thick,
      y + halfHeight + 2,
      thick,
      halfHeight - 6,
      color
    );
  }

  if (mask & 0b0001000) {
    drawHorizontalSegment(
      x + 4,
      y + digitHeight - thick,
      digitWidth - 8,
      thick,
      color
    );
  }

  if (mask & 0b0000100) {
    drawVerticalSegment(
      x,
      y + halfHeight + 2,
      thick,
      halfHeight - 6,
      color
    );
  }

  if (mask & 0b0000010) {
    drawVerticalSegment(
      x,
      y + 4,
      thick,
      halfHeight - 6,
      color
    );
  }

  if (mask & 0b0000001) {
    drawHorizontalSegment(
      x + 4,
      y + halfHeight - thick / 2,
      digitWidth - 8,
      thick,
      color
    );
  }
}

void drawSevenTime(
  const String& timeText,
  int x,
  int y,
  uint16_t color
) {
  int cursor = x;

  for (int i = 0; i < timeText.length(); i++) {
    char c = timeText[i];

    if (c == ':') {
      tft.fillCircle(cursor + 3, y + 21, 2, color);
      tft.fillCircle(cursor + 3, y + 42, 2, color);
      cursor += 8;
    } else {
      drawSevenDigit(c - '0', cursor, y, color);
      cursor += 31;
    }
  }
}

// =====================================================
// 外部图标素材：Font Awesome Free 转换为 RGB565
// =====================================================
const uint16_t* weatherLargeAsset(int code) {
  if (code == 0) return weather_clear_58;
  if (code == 1 || code == 2) return weather_partly_58;
  if (code == 3) return weather_cloudy_58;
  if (code == 45 || code == 48) return weather_fog_58;

  if (
    (code >= 51 && code <= 67) ||
    (code >= 80 && code <= 82)
  ) {
    return weather_rain_58;
  }

  if (
    (code >= 71 && code <= 77) ||
    (code >= 85 && code <= 86)
  ) {
    return weather_snow_58;
  }

  if (code >= 95 && code <= 99) {
    return weather_thunder_58;
  }

  return weather_cloudy_58;
}

const uint16_t* weatherSmallAsset(int code) {
  if (code == 0) return weather_clear_28;
  if (code == 1 || code == 2) return weather_partly_28;
  if (code == 3) return weather_cloudy_28;
  if (code == 45 || code == 48) return weather_fog_28;

  if (
    (code >= 51 && code <= 67) ||
    (code >= 80 && code <= 82)
  ) {
    return weather_rain_28;
  }

  if (
    (code >= 71 && code <= 77) ||
    (code >= 85 && code <= 86)
  ) {
    return weather_snow_28;
  }

  if (code >= 95 && code <= 99) {
    return weather_thunder_28;
  }

  return weather_cloudy_28;
}

void drawWeatherIconLarge(
  int x,
  int y,
  int code
) {
  tft.pushImage(
    x,
    y,
    58,
    58,
    weatherLargeAsset(code)
  );
}

void drawWeatherIconSmall(
  int x,
  int y,
  int code
) {
  tft.pushImage(
    x,
    y,
    28,
    28,
    weatherSmallAsset(code)
  );
}

// =====================================================
// 外部小图标
// =====================================================
void drawTempMiniIcon(
  int x,
  int y,
  uint16_t color
) {
  tft.pushImage(
    x,
    y,
    22,
    22,
    icon_thermometer_22
  );
}

void drawHumidityMiniIcon(
  int x,
  int y,
  uint16_t color
) {
  tft.pushImage(
    x,
    y,
    22,
    22,
    icon_droplet_22
  );
}

// =====================================================
// 单位与文案
// =====================================================
void drawDegreeC(
  int x,
  int y,
  uint16_t color,
  uint16_t background,
  bool compact
) {
  // Tiny degree mark + compact C for a true unit appearance
  tft.drawCircle(
    x + 1,
    y + 1,
    1,
    color
  );

  tft.setFreeFont(NULL);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, background);

  tft.drawString(
    "C",
    x + 4,
    y - 3,
    1
  );
}

String weatherDescriptionCN(int code) {
  if (code == 0) return "晴";
  if (code == 1) return "大部晴朗";
  if (code == 2) return "多云";
  if (code == 3) return "阴";
  if (code == 45 || code == 48) return "雾";

  if (code >= 51 && code <= 57) return "毛毛雨";
  if (code >= 61 && code <= 63) return "小雨";
  if (code >= 65 && code <= 67) return "大雨";
  if (code >= 71 && code <= 77) return "雪";
  if (code >= 80 && code <= 82) return "阵雨";
  if (code >= 85 && code <= 86) return "阵雪";
  if (code >= 95 && code <= 99) return "雷雨";

  return "未知";
}

String chineseWeekday(int weekday) {
  const char* names[] = {
    "星期日",
    "星期一",
    "星期二",
    "星期三",
    "星期四",
    "星期五",
    "星期六"
  };

  return names[weekday];
}



// =====================================================
// 网络、传感器与接口
// =====================================================
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    attempts < 40
  ) {
    delay(500);
    attempts++;
  }

  return WiFi.status() == WL_CONNECTED;
}

void initSensor() {
  Wire.begin(SENSOR_SDA, SENSOR_SCL);
  sensorReady = sht31.begin(SENSOR_ADDRESS);
}

void updateSensor() {
  if (!sensorReady) return;

  if (
    sensorHasData &&
    millis() - lastSensorRead < SENSOR_INTERVAL
  ) {
    return;
  }

  float newTemperature =
    sht31.readTemperature();

  float newHumidity =
    sht31.readHumidity();

  if (
    !isnan(newTemperature) &&
    !isnan(newHumidity)
  ) {
    indoorTemp = newTemperature;
    indoorHumidity = newHumidity;
    sensorHasData = true;
  }

  lastSensorRead = millis();
}

bool updateWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String url =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=" + String(LATITUDE, 4) +
    "&longitude=" + String(LONGITUDE, 4) +
    "&current=temperature_2m,weather_code"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min"
    "&timezone=Asia%2FShanghai"
    "&forecast_days=2";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  if (!http.begin(client, url)) {
    return false;
  }

  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;

  if (deserializeJson(doc, payload)) {
    return false;
  }

  outdoorTemp =
    doc["current"]["temperature_2m"] | 0.0;

  weatherCode =
    doc["current"]["weather_code"] | -1;

  maxTemp =
    doc["daily"]["temperature_2m_max"][0] | 0.0;

  minTemp =
    doc["daily"]["temperature_2m_min"][0] | 0.0;

  tomorrowWeatherCode =
    doc["daily"]["weather_code"][1] | -1;

  tomorrowMaxTemp =
    doc["daily"]["temperature_2m_max"][1] | 0.0;

  tomorrowMinTemp =
    doc["daily"]["temperature_2m_min"][1] | 0.0;

  tomorrowReady =
    tomorrowWeatherCode >= 0;

  weatherReady = true;
  lastWeatherUpdate = millis();

  return true;
}

// =====================================================
// 布局
// =====================================================
void drawCardBase(
  int x,
  int y,
  int width,
  int height
) {
  uint16_t softCard =
    tft.color565(254, 254, 253);

  tft.fillRoundRect(
    x,
    y,
    width,
    height,
    10,
    softCard
  );

  tft.drawRoundRect(
    x,
    y,
    width,
    height,
    10,
    COLOR_LINE
  );
}

void drawHeader(const struct tm& timeInfo) {
  tft.fillRect(
    0,
    0,
    320,
    34,
    COLOR_BG
  );

  drawChinese(
    14,
    23,
    "北京",
    COLOR_TEXT,
    COLOR_BG
  );

  String dateText =
    String(timeInfo.tm_mon + 1) +
    "月" +
    String(timeInfo.tm_mday) +
    "日 " +
    chineseWeekday(timeInfo.tm_wday);

  drawChinese(
    111,
    23,
    dateText,
    COLOR_TEXT,
    COLOR_BG
  );

  bool online =
    WiFi.status() == WL_CONNECTED;

  uint16_t statusColor =
    online
      ? COLOR_GREEN
      : COLOR_RED;

  drawChinese(
    269,
    23,
    online ? "在线" : "离线",
    statusColor,
    COLOR_BG
  );

  tft.fillCircle(
    306,
    16,
    4,
    statusColor
  );

  tft.drawFastHLine(
    10,
    32,
    300,
    COLOR_LINE
  );
}

void drawTimeArea(const struct tm& timeInfo) {
  tft.fillRect(
    0,
    34,
    155,
    126,
    COLOR_BG
  );

  char timeBuffer[6];

  strftime(
    timeBuffer,
    sizeof(timeBuffer),
    "%H:%M",
    &timeInfo
  );

  drawSevenTime(
    String(timeBuffer),
    7,
    47,
    COLOR_TEXT
  );
}

void drawWeatherArea() {
  tft.fillRect(
    155,
    34,
    165,
    126,
    COLOR_BG
  );

  if (!weatherReady) {
    drawChinese(
      205,
      105,
      "天气加载中",
      COLOR_MUTED,
      COLOR_BG
    );

    return;
  }

  drawWeatherIconLarge(
    162,
    40,
    weatherCode
  );

  String outdoorValue =
    String(outdoorTemp, 0);

  tft.setFreeFont(&FreeSans18pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);

  tft.drawString(
    outdoorValue,
    239,
    54
  );

  int valueWidth =
    tft.textWidth(outdoorValue);

  drawDegreeC(
    239 + valueWidth - 1,
    58,
    COLOR_MUTED,
    COLOR_BG,
    false
  );

  drawChinese(
    246,
    104,
    weatherDescriptionCN(weatherCode),
    COLOR_TEXT,
    COLOR_BG
  );

  drawChinese(
    178,
    145,
    "最高",
    COLOR_MUTED,
    COLOR_BG
  );

  tft.setFreeFont(NULL);
  tft.setTextColor(COLOR_ORANGE, COLOR_BG);
  tft.drawString(
    String(maxTemp, 0),
    208,
    133,
    2
  );

  drawChinese(
    238,
    145,
    "最低",
    COLOR_MUTED,
    COLOR_BG
  );

  tft.setTextColor(COLOR_BLUE, COLOR_BG);
  tft.drawString(
    String(minTemp, 0),
    268,
    133,
    2
  );
}

void drawTemperatureCard() {
  const int x = 7;
  const int y = 166;
  const int width = 100;
  const int height = 66;

  drawCardBase(
    x,
    y,
    width,
    height
  );

  drawTempMiniIcon(
    x + 10,
    y + 7,
    COLOR_BLUE
  );

  drawChinese(
    x + 34,
    y + 18,
    "室内温度",
    COLOR_MUTED,
    COLOR_CARD
  );

  String value =
    sensorHasData
      ? String(indoorTemp, 1)
      : "--.-";

  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_CARD);

  tft.drawString(
    value,
    x + 18,
    y + 32
  );

  int valueWidth =
    tft.textWidth(value);

  drawDegreeC(
    x + 18 + valueWidth,
    y + 39,
    COLOR_MUTED,
    COLOR_CARD,
    true
  );
}

void drawHumidityCard() {
  const int x = 110;
  const int y = 166;
  const int width = 100;
  const int height = 66;

  drawCardBase(
    x,
    y,
    width,
    height
  );

  drawHumidityMiniIcon(
    x + 10,
    y + 7,
    COLOR_CYAN
  );

  drawChinese(
    x + 34,
    y + 18,
    "室内湿度",
    COLOR_MUTED,
    COLOR_CARD
  );

  String value =
    sensorHasData
      ? String(indoorHumidity, 0)
      : "--";

  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_CARD);

  tft.drawString(
    value,
    x + 31,
    y + 32
  );

  int valueWidth =
    tft.textWidth(value);

  tft.setFreeFont(NULL);
  tft.setTextColor(COLOR_MUTED, COLOR_CARD);

  tft.drawString(
    "%",
    x + 31 + valueWidth + 2,
    y + 41,
    2
  );
}

void drawTomorrowCard() {
  const int x = 213;
  const int y = 166;
  const int width = 100;
  const int height = 66;

  drawCardBase(
    x,
    y,
    width,
    height
  );

  if (!tomorrowReady) {
    drawChinese(
      x + 31,
      y + 37,
      "明日加载中",
      COLOR_MUTED,
      COLOR_CARD
    );
    return;
  }

  // Smaller icon and cleaner spacing
  drawWeatherIconSmall(
    x + 8,
    y + 6,
    tomorrowWeatherCode
  );

  drawChinese(
    x + 42,
    y + 17,
    "明日天气",
    COLOR_MUTED,
    COLOR_CARD
  );

  drawChinese(
    x + 47,
    y + 38,
    weatherDescriptionCN(tomorrowWeatherCode),
    COLOR_TEXT,
    COLOR_CARD
  );

  String rangeText =
    String(tomorrowMinTemp, 0) +
    "°/" +
    String(tomorrowMaxTemp, 0) +
    "°";

  tft.setFreeFont(NULL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_CARD);

  tft.drawString(
    rangeText,
    x + 66,
    y + 58,
    2
  );
}

void drawAll() {
  struct tm timeInfo;

  if (!getLocalTime(&timeInfo, 3000)) {
    return;
  }

  tft.fillScreen(COLOR_BG);

  drawHeader(timeInfo);
  drawTimeArea(timeInfo);
  drawWeatherArea();

  drawTemperatureCard();
  drawHumidityCard();
  drawTomorrowCard();
}

// =====================================================
// 初始化与循环
// =====================================================
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);

  // 外部图标已转换为 RGB565 数组
  tft.setSwapBytes(true);
  initColors();
  prepareChineseFont();

  tft.fillScreen(COLOR_BG);

  initSensor();
  updateSensor();

  if (connectWiFi()) {
    configTime(
      GMT_OFFSET_SEC,
      DAYLIGHT_OFFSET_SEC,
      NTP_SERVER_1,
      NTP_SERVER_2
    );

    updateWeather();
  }

  drawAll();
}

void loop() {
  updateSensor();

  if (
    WiFi.status() != WL_CONNECTED &&
    millis() - lastReconnectAttempt >=
      RECONNECT_INTERVAL
  ) {
    lastReconnectAttempt = millis();

    if (connectWiFi()) {
      configTime(
        GMT_OFFSET_SEC,
        DAYLIGHT_OFFSET_SEC,
        NTP_SERVER_1,
        NTP_SERVER_2
      );

      updateWeather();
      drawAll();
    }
  }

  if (
    WiFi.status() == WL_CONNECTED &&
    (
      !weatherReady ||
      millis() - lastWeatherUpdate >=
        WEATHER_INTERVAL
    )
  ) {
    if (updateWeather()) {
      drawWeatherArea();
      drawTomorrowCard();
    }
  }


  struct tm timeInfo;

  if (getLocalTime(&timeInfo, 50)) {
    long minuteKey =
      (long)timeInfo.tm_yday * 1440L +
      (long)timeInfo.tm_hour * 60L +
      timeInfo.tm_min;

    if (minuteKey != lastMinuteKey) {
      lastMinuteKey = minuteKey;

      drawHeader(timeInfo);
      drawTimeArea(timeInfo);
    }
  }

  if (
    millis() - lastSensorDraw >=
      SENSOR_DRAW_INTERVAL
  ) {
    lastSensorDraw = millis();

    drawTemperatureCard();
    drawHumidityCard();
  }

  delay(20);
}
