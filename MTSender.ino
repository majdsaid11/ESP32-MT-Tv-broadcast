#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#include <ArduinoJson.h>
#include <time.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= WIFI =================

const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// غيّرهم حسب مدينتك
const float LATITUDE  = 33.5138;
const float LONGITUDE = 36.2765;

// توقيت سوريا / الأردن / السعودية +3
const long GMT_OFFSET_SEC = 3 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// ================= LED =================
// D4 = GPIO2
// ESP8266 LED  غالباً عكسي:على
// LOW = ON
// HIGH = OFF

#define STATUS_LED 2

void ledOn() {
  digitalWrite(STATUS_LED, LOW);
}

void ledOff() {
  digitalWrite(STATUS_LED, HIGH);
}

void ledBlink(int times, int d) {
  for (int i = 0; i < times; i++) {
    ledOn();
    delay(d);
    ledOff();
    delay(d);
  }
}

// ================= nRF24 =================

// ESP8266 Hardware SPI:
// SCK  -> D5 / GPIO14
// MISO -> D6 / GPIO12
// MOSI -> D7 / GPIO13

#define RF_CE_PIN   5    // D1 / GPIO5
#define RF_CSN_PIN  4    // D2 / GPIO4

#define RF_CHANNEL 108
#define PAYLOAD_SIZE 32

RF24 radio(RF_CE_PIN, RF_CSN_PIN);
const byte address[6] = "00001";

// ================= OLED STARTUP SCREEN =================
// HW-364 غالباً الشاشة SSD1306 I2C على D5/D6 وعنوانها 0x3C
// يتم تحديثها مرة واحدة فقط، وبعدها نرجّع الباص للـ SPI حتى ما تشوش على RF.

#define OLED_WIDTH   128
#define OLED_HEIGHT  64
#define OLED_ADDR    0x3C
#define OLED_RESET   -1

#define OLED_SDA     14   // D5 / GPIO14
#define OLED_SCL     12   // D6 / GPIO12

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ================= WEB =================

ESP8266WebServer server(80);

// ================= DATA =================

String clockLine        = "TIME --:--";
String weatherLine      = "WX loading";
String weatherExtraLine = "Weather loading";

String tickerText = "MT Broadcast live - Breaking news ticker ready";
bool tickerEnabled = true;

String lastPacket = "";
String lastStatus = "Booting";

uint32_t txCount = 0;

unsigned long lastBroadcast = 0;
unsigned long lastTimeUpdate = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastWiFiCheck = 0;

const unsigned long BROADCAST_INTERVAL_MS = 2500;
const unsigned long TIME_INTERVAL_MS      = 30UL * 1000UL;
const unsigned long WEATHER_INTERVAL_MS   = 10UL * 60UL * 1000UL;
const unsigned long WIFI_CHECK_MS         = 10UL * 1000UL;

// ================= HELPERS =================

String cutText(String s, int maxLen) {
  s.trim();
  if (s.length() > maxLen) s = s.substring(0, maxLen);
  return s;
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

String getIP() {
  if (WiFi.status() != WL_CONNECTED) return "No WiFi";
  return WiFi.localIP().toString();
}

String weatherCodeText(int code) {
  if (code == 0) return "Clear";
  if (code == 1 || code == 2) return "Partly cloudy";
  if (code == 3) return "Cloudy";
  if (code == 45 || code == 48) return "Fog";
  if (code == 51 || code == 53 || code == 55) return "Drizzle";
  if (code == 61 || code == 63 || code == 65) return "Rain";
  if (code == 71 || code == 73 || code == 75) return "Snow";
  if (code == 80 || code == 81 || code == 82) return "Showers";
  if (code == 95) return "Thunder";
  if (code == 96 || code == 99) return "Storm";
  return "WX code " + String(code);
}

void restoreSPIBusForRF() {
  SPI.begin();
  pinMode(RF_CSN_PIN, OUTPUT);
  digitalWrite(RF_CSN_PIN, HIGH);
  radio.stopListening();
}

void showStartupOLED() {
  Serial.println("OLED startup screen...");

  digitalWrite(RF_CSN_PIN, HIGH);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED begin FAILED");
    restoreSPIBusForRF();
    return;
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("MT Broadcast");

  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  oled.setCursor(0, 16);
  oled.print("IP:");
  oled.println(getIP());

  oled.setCursor(0, 30);
  oled.print("RF CHANNEL:");
  oled.println(RF_CHANNEL);

  oled.setCursor(0, 46);
  oled.println("| - On AIR = * |");

  oled.setCursor(0, 56);
  oled.println("Enjoy... MAJDTECH ");

  oled.display();

  delay(100);

  restoreSPIBusForRF();

  Serial.println("OLED image frozen, SPI restored for RF");
}

// ================= RF SEND =================

bool sendPacket(String msg) {
  msg.trim();
  msg = cutText(msg, 31);

  char payload[PAYLOAD_SIZE];
  memset(payload, 0, sizeof(payload));
  msg.toCharArray(payload, PAYLOAD_SIZE);

  radio.stopListening();

  // Broadcast بدون ACK
  bool ok1 = radio.write(payload, PAYLOAD_SIZE);
  delay(10);
  bool ok2 = radio.write(payload, PAYLOAD_SIZE);

  txCount++;
  lastPacket = msg;
  lastStatus = (ok1 || ok2) ? "AIR TX" : "TX FAIL";

  // رمشة صغيرة مع كل إرسال
  ledOff();
  delay(18);
  ledOn();

  Serial.print("BROADCAST: ");
  Serial.print(msg);
  Serial.print(" | ");
  Serial.println(lastStatus);

  return ok1 || ok2;
}

void broadcastScreen(String title, String l1, String l2, String l3) {
  sendPacket("TITLE:" + cutText(title, 25));
  delay(30);

  sendPacket("L1:" + cutText(l1, 28));
  delay(30);

  sendPacket("L2:" + cutText(l2, 28));
  delay(30);

  sendPacket("L3:" + cutText(l3, 28));
  delay(30);
}

void broadcastHome() {
  broadcastScreen(
    "MT CH 108",
    clockLine,
    weatherLine,
    weatherExtraLine
  );
}

// ================= TICKER FULL SEND =================
// مهم: الشريط لا يتحرك من المرسل.
// المرسل يرسل النص كامل، والمستقبل يحركه محلياً بدون flicker.

void sendTickerFull() {
  String s = tickerText;
  s.trim();

  if (!tickerEnabled) {
    s = "SIGNAL LIVE";
  }

  if (s.length() == 0) {
    s = "MT Broadcast live";
  }

  if (s.length() > 140) {
    s = s.substring(0, 140);
  }

  Serial.println("Sending full ticker...");

  sendPacket("TKCLR");
  delay(45);

  for (int i = 0; i < s.length(); i += 28) {
    String chunk = s.substring(i, i + 28);
    sendPacket("TK:" + chunk);
    delay(45);
  }

  sendPacket("TKEND");
}

// ================= TIME 12H =================

bool updateTimeLine() {
  time_t now = time(nullptr);

  if (now < 100000) {
    clockLine = "TIME syncing";
    return false;
  }

  struct tm* timeinfo = localtime(&now);

  char buf[16];
  strftime(buf, sizeof(buf), "%I:%M %p", timeinfo);

  String t = String(buf);

  // شيل الصفر بالبداية: 06:30 PM -> 6:30 PM
  if (t.startsWith("0")) {
    t.remove(0, 1);
  }

  clockLine = "TIME " + t;
  return true;
}

// ================= WEATHER =================

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    weatherLine = "WX no WiFi";
    weatherExtraLine = "Check connection";
    return false;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude=" + String(LATITUDE, 4);
  url += "&longitude=" + String(LONGITUDE, 4);
  url += "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code";
  url += "&timezone=auto";

  Serial.println("Fetching weather...");

  if (!http.begin(client, url)) {
    weatherLine = "WX begin err";
    weatherExtraLine = "HTTP start fail";
    return false;
  }

  int code = http.GET();

  if (code != 200) {
    weatherLine = "WX HTTP " + String(code);
    weatherExtraLine = "Weather API fail";
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    weatherLine = "WX json err";
    weatherExtraLine = "Parse failed";
    return false;
  }

  float temp = doc["current"]["temperature_2m"] | 0.0;
  int hum = doc["current"]["relative_humidity_2m"] | 0;
  float wind = doc["current"]["wind_speed_10m"] | 0.0;
  int wcode = doc["current"]["weather_code"] | -1;

  weatherLine = "WX " + String(temp, 1) + "C  H" + String(hum) + "%";
  weatherLine = cutText(weatherLine, 28);

  weatherExtraLine = weatherCodeText(wcode) + "  W" + String(wind, 0) + "km/h";
  weatherExtraLine = cutText(weatherExtraLine, 28);

  Serial.println(weatherLine);
  Serial.println(weatherExtraLine);

  return true;
}

// ================= WEB PAGE =================

void redirectHome() {
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleRoot() {
  String html = "";

  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial;background:#0b1020;color:white;text-align:center;margin:0;padding:18px 18px 72px;}";
  html += ".card{background:#121a2b;border:1px solid #2a3550;border-radius:14px;padding:14px;margin:12px 0;}";
  html += "input{font-size:20px;padding:12px;width:90%;border-radius:10px;border:0;margin:5px;background:#e8eef7;}";
  html += "button{font-size:18px;padding:13px 18px;margin:6px;border:0;border-radius:10px;color:white;}";
  html += ".send{background:#00b894;}";
  html += ".btn{background:#2563eb;}";
  html += ".danger{background:#ef4444;}";
  html += ".txt{color:#9fb0c8;font-size:15px;}";
  html += ".ticker{position:fixed;left:0;right:0;bottom:0;background:#b00020;color:white;height:44px;overflow:hidden;border-top:2px solid #ff4d4d;}";
  html += ".move{display:inline-block;white-space:nowrap;padding-left:100%;font-size:20px;font-weight:bold;line-height:44px;animation:roll 14s linear infinite;}";
  html += "@keyframes roll{0%{transform:translateX(0);}100%{transform:translateX(-100%);}}";
  html += "</style></head><body>";

  html += "<h2>MT Broadcast Station</h2>";

  html += "<div class='card txt'>";
  html += "<p>IP: " + htmlEscape(getIP()) + "</p>";
  html += "<p>MT Channel: " + String(RF_CHANNEL) + " | NO ACK</p>";
  html += "<p>TX Count: " + String(txCount) + "</p>";
  html += "<p>Status: " + htmlEscape(lastStatus) + "</p>";
  html += "<p>Last: " + htmlEscape(lastPacket) + "</p>";
  html += "</div>";

  html += "<div class='card txt'>";
  html += "<p><b>Live Broadcast Data</b></p>";
  html += "<p>" + htmlEscape(clockLine) + "</p>";
  html += "<p>" + htmlEscape(weatherLine) + "</p>";
  html += "<p>" + htmlEscape(weatherExtraLine) + "</p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<form action='/ticker'>";
  html += "<input name='t' maxlength='140' placeholder='Breaking news ticker text' value='" + htmlEscape(tickerText) + "'>";
  html += "<br><button class='send' type='submit'>Save + Send Ticker</button>";
  html += "</form>";
  html += "<a href='/tickerOn'><button class='btn'>Ticker ON</button></a>";
  html += "<a href='/tickerOff'><button class='danger'>Ticker OFF</button></a>";
  html += "<a href='/sendTicker'><button class='btn'>Resend Ticker</button></a>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<form action='/raw'>";
  html += "<input name='r' maxlength='31' placeholder='Raw: TITLE:Hi / L1:Text'>";
  html += "<br><button class='send' type='submit'>Broadcast Raw</button>";
  html += "</form>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<a href='/broadcast'><button class='btn'>Broadcast Now</button></a>";
  html += "<a href='/refresh'><button class='btn'>Refresh Weather</button></a>";
  html += "<a href='/clear'><button class='danger'>Clear RX Screen</button></a>";
  html += "<a href='/ping'><button class='btn'>Ping</button></a>";
  html += "</div>";

  html += "<div class='ticker'><div class='move'>";
  html += htmlEscape(tickerEnabled ? tickerText : "SIGNAL LIVE");
  html += "</div></div>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleTicker() {
  if (server.hasArg("t")) {
    tickerText = server.arg("t");
    tickerText.trim();

    if (tickerText.length() > 140) {
      tickerText = tickerText.substring(0, 140);
    }
  }

  sendTickerFull();
  broadcastHome();
  redirectHome();
}

void handleTickerOn() {
  tickerEnabled = true;
  sendTickerFull();
  broadcastHome();
  redirectHome();
}

void handleTickerOff() {
  tickerEnabled = false;
  sendTickerFull();
  broadcastHome();
  redirectHome();
}

void handleSendTicker() {
  sendTickerFull();
  redirectHome();
}

void handleRaw() {
  if (!server.hasArg("r")) {
    server.send(400, "text/plain", "Missing raw");
    return;
  }

  sendPacket(server.arg("r"));
  redirectHome();
}

void handleBroadcast() {
  broadcastHome();
  redirectHome();
}

void handleRefresh() {
  updateTimeLine();
  fetchWeather();
  broadcastHome();
  redirectHome();
}

void handleClear() {
  sendPacket("CLR");
  redirectHome();
}

void handlePing() {
  sendPacket("PING");
  redirectHome();
}

// ================= FAIL BLINK =================

void failBlinkForever() {
  while (1) {
    ledOn();
    delay(120);
    ledOff();
    delay(120);
  }
}

// ================= SETUP =================

void setup() {
  pinMode(STATUS_LED, OUTPUT);
  ledOff();

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("HW-364 MT Broadcast boot");
  Serial.println("Mode: BROADCAST / NO ACK");
  Serial.println("Prayer removed | Weather details enabled | 12H time");

  ledBlink(2, 120);

  // ===== nRF24 INIT =====

  Serial.println("Starting nRF24...");

  pinMode(RF_CSN_PIN, OUTPUT);
  digitalWrite(RF_CSN_PIN, HIGH);

  SPI.begin();

  if (!radio.begin()) {
    Serial.println("radio.begin FAILED");
    failBlinkForever();
  }

  if (!radio.isChipConnected()) {
    Serial.println("nRF24 NOT connected");
    failBlinkForever();
  }

  radio.setChannel(RF_CHANNEL);
  radio.setDataRate(RF24_250KBPS);
  radio.setPALevel(RF24_PA_LOW);

  radio.setAddressWidth(5);
  radio.setCRCLength(RF24_CRC_16);

  // Broadcast مثل التلفزيون، بدون ACK
  radio.setAutoAck(false);
  radio.setRetries(0, 0);

  radio.disableDynamicPayloads();
  radio.setPayloadSize(PAYLOAD_SIZE);

  radio.openWritingPipe(address);
  radio.stopListening();

  Serial.println("nRF24 ready");

  // ===== WIFI INIT =====

  Serial.println("Connecting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.hostname("MT-Broadcast");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    ledOn();
    delay(120);
    ledOff();
    delay(380);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // ===== TIME INIT =====

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");

  updateTimeLine();
  fetchWeather();

  lastTimeUpdate = millis();
  lastWeatherUpdate = millis();

  // ===== WEB SERVER =====

  server.on("/", handleRoot);
  server.on("/ticker", handleTicker);
  server.on("/tickerOn", handleTickerOn);
  server.on("/tickerOff", handleTickerOff);
  server.on("/sendTicker", handleSendTicker);
  server.on("/raw", handleRaw);
  server.on("/broadcast", handleBroadcast);
  server.on("/refresh", handleRefresh);
  server.on("/clear", handleClear);
  server.on("/ping", handlePing);

  server.begin();

  Serial.println("Web server ready");
  Serial.print("Open: http://");
  Serial.println(WiFi.localIP());

  // ===== OLED ONE TIME STATUS =====
  showStartupOLED();

  lastStatus = "WEB + RF ON";
  ledOn();

  // أول بث
  broadcastHome();
  delay(120);
  sendTickerFull();

  lastBroadcast = millis();
}

// ================= LOOP =================

void loop() {
  server.handleClient();

  unsigned long now = millis();

  // بث مستمر لبيانات القناة
  if (now - lastBroadcast >= BROADCAST_INTERVAL_MS) {
    lastBroadcast = now;
    broadcastHome();
  }

  // تحديث الوقت 12 ساعة
  if (now - lastTimeUpdate >= TIME_INTERVAL_MS) {
    lastTimeUpdate = now;
    updateTimeLine();
  }

  // تحديث الطقس
  if (now - lastWeatherUpdate >= WEATHER_INTERVAL_MS) {
    lastWeatherUpdate = now;
    fetchWeather();
  }

  // إعادة محاولة WiFi إذا فصل
  if (now - lastWiFiCheck >= WIFI_CHECK_MS) {
    lastWiFiCheck = now;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, reconnecting...");
      WiFi.reconnect();
    }
  }
}
