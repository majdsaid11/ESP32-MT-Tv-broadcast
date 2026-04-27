#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <driver/i2s.h>
#include <math.h>

// =====================================================
// TFT PINS
// =====================================================
#define TFT_CS    7
#define TFT_DC    5
#define TFT_RST   2
#define TFT_MOSI  6
#define TFT_SCLK  1
#define TFT_BL    8

// =====================================================
// MIC PINS - INMP441
// =====================================================
#define I2S_WS    3
#define I2S_SCK   4
#define I2S_SD    10
#define I2S_PORT  I2S_NUM_0

// إذا L/R على GND جرّب LEFT
// إذا طلع noise أو قراءات غريبة جرّب ONLY_RIGHT
#define MIC_CHANNEL_FORMAT I2S_CHANNEL_FMT_ONLY_LEFT

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// =====================================================
// SETTINGS
// =====================================================
const int   MIC_BIT_SHIFT        = 10;
const int   SAMPLE_COUNT         = 256;

// تنظيف السكون
const float RMS_NOISE_GATE       = 2200.0f;
const float SILENCE_OFFSET       = 0.22f;
const int   MIN_BAR_CUTOFF       = 18;

// hysteresis حتى ما ينطنط بالسكون
const int   SILENCE_ENTER        = 18;
const int   SILENCE_EXIT         = 28;

// تطبيع المستوى
const float RMS_NORMALIZE_TOP    = 31000.0f;

// ضغط خفيف فقط بالمستويات العالية
const float RMS_COMPRESS_START   = 5000.0f;
const float RMS_COMPRESS_RATIO   = 0.22f;

// سلوك DJ
const float ATTACK_SPEED         = 0.92f;
const float RELEASE_SPEED_LOW    = 0.18f;
const float RELEASE_SPEED_MID    = 0.14f;
const float RELEASE_SPEED_HIGH   = 0.10f;

// peak line
const bool  ENABLE_PEAK_LINE     = true;
const float PEAK_DROP_LOW        = 4.0f;
const float PEAK_DROP_MID        = 6.5f;
const float PEAK_DROP_HIGH       = 10.0f;
const int   PEAK_HOLD_MS         = 50;
const int   PEAK_FALL_MS         = 12;

const int   DRAW_INTERVAL_MS     = 12;

const bool  DEBUG_SERIAL_RMS     = false;

// =====================================================
// UI COLORS
// =====================================================
#define COLOR_BG        ST77XX_BLACK
#define COLOR_FRAME     ST77XX_WHITE
#define COLOR_TEXT      ST77XX_WHITE
#define COLOR_SUBTEXT   ST77XX_CYAN
#define COLOR_LOW       ST77XX_GREEN
#define COLOR_MID       ST77XX_YELLOW
#define COLOR_HIGH      ST77XX_RED
#define COLOR_PEAK      ST77XX_WHITE
#define COLOR_GRID      0x39E7
#define COLOR_PANEL     0x0841
#define COLOR_OFFSEG    0x1082

// =====================================================
// SCREEN LAYOUT
// =====================================================
const int SCREEN_W = 320;
const int SCREEN_H = 240;

const int TITLE_Y   = 18;

const int PANEL_X   = 18;
const int PANEL_Y   = 52;
const int PANEL_W   = 284;
const int PANEL_H   = 116;

const int BAR_X     = 34;
const int BAR_Y     = 92;
const int BAR_W     = 252;
const int BAR_H     = 26;

const int SEGMENTS  = 28;
const int SEG_GAP   = 2;

const int LABEL_Y   = 126;
const int PERCENT_Y = 188;

// =====================================================
// AUDIO STATE
// =====================================================
int32_t i2sSamples[SAMPLE_COUNT];

float displayLevel = 0.0f;
float peakLevel = 0.0f;

bool silenceLatch = true;

unsigned long lastPeakHold = 0;
unsigned long lastPeakFall = 0;
unsigned long lastDraw = 0;

// =====================================================
// HELPERS
// =====================================================
uint16_t segmentColorByIndex(int i) {
  float p = (float)i / (float)SEGMENTS;
  if (p < 0.60f) return COLOR_LOW;
  if (p < 0.80f) return COLOR_MID;
  return COLOR_HIGH;
}

int levelToSegments(int levelPx) {
  int seg = map(levelPx, 0, BAR_W, 0, SEGMENTS);
  return constrain(seg, 0, SEGMENTS);
}

int levelToPercent(int levelPx) {
  int pct = map(levelPx, 0, BAR_W, 0, 100);
  return constrain(pct, 0, 100);
}

// =====================================================
// UI
// =====================================================
void drawStaticUI() {
  tft.fillScreen(COLOR_BG);

  tft.setTextWrap(false);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(96, TITLE_Y);
  tft.print("DJ Audio Meter");

  tft.fillRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 10, COLOR_PANEL);
  tft.drawRoundRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 10, COLOR_FRAME);

  tft.drawRoundRect(BAR_X - 6, BAR_Y - 6, BAR_W + 12, BAR_H + 12, 8, COLOR_FRAME);

  for (int i = 1; i < 10; i++) {
    int x = BAR_X + (BAR_W * i) / 10;
    tft.drawFastVLine(x, BAR_Y - 2, BAR_H + 4, COLOR_GRID);
  }

  int segW = (BAR_W - (SEGMENTS - 1) * SEG_GAP) / SEGMENTS;
  for (int i = 0; i < SEGMENTS; i++) {
    int x = BAR_X + i * (segW + SEG_GAP);
    tft.fillRoundRect(x, BAR_Y, segW, BAR_H, 3, COLOR_OFFSEG);
  }

  tft.setTextSize(1);
  tft.setTextColor(COLOR_LOW);
  tft.setCursor(BAR_X, LABEL_Y);
  tft.print("LOW");

  tft.setTextColor(COLOR_MID);
  tft.setCursor(BAR_X + BAR_W / 2 - 10, LABEL_Y);
  tft.print("MID");

  tft.setTextColor(COLOR_HIGH);
  tft.setCursor(BAR_X + BAR_W - 24, LABEL_Y);
  tft.print("HIGH");

  tft.setTextSize(2);
  tft.setTextColor(COLOR_SUBTEXT);
  tft.setCursor(90, PERCENT_Y);
  tft.print("Level:");
}

void drawMeter(int levelPx, int peakPx) {
  static int lastSegs = -1;
  static int lastPeakPx = -1;

  levelPx = constrain(levelPx, 0, BAR_W);
  peakPx  = constrain(peakPx, 0, BAR_W - 1);

  int currentSegs = levelToSegments(levelPx);
  int segW = (BAR_W - (SEGMENTS - 1) * SEG_GAP) / SEGMENTS;

  if (currentSegs != lastSegs) {
    if (lastSegs < 0) lastSegs = 0;

    if (currentSegs > lastSegs) {
      for (int i = lastSegs; i < currentSegs; i++) {
        int x = BAR_X + i * (segW + SEG_GAP);
        tft.fillRoundRect(x, BAR_Y, segW, BAR_H, 3, segmentColorByIndex(i));
      }
    } else {
      for (int i = currentSegs; i < lastSegs; i++) {
        int x = BAR_X + i * (segW + SEG_GAP);
        tft.fillRoundRect(x, BAR_Y, segW, BAR_H, 3, COLOR_OFFSEG);
      }
    }

    lastSegs = currentSegs;
  }

  if (!ENABLE_PEAK_LINE) return;

  if (lastPeakPx >= 0 && lastPeakPx != peakPx) {
    tft.drawFastVLine(BAR_X + lastPeakPx, BAR_Y - 8, BAR_H + 16, COLOR_PANEL);

    for (int i = 1; i < 10; i++) {
      int x = (BAR_W * i) / 10;
      if (x == lastPeakPx) {
        tft.drawFastVLine(BAR_X + x, BAR_Y - 2, BAR_H + 4, COLOR_GRID);
      }
    }

    tft.drawRoundRect(BAR_X - 6, BAR_Y - 6, BAR_W + 12, BAR_H + 12, 8, COLOR_FRAME);
  }

  tft.drawFastVLine(BAR_X + peakPx, BAR_Y - 8, BAR_H + 16, COLOR_PEAK);
  lastPeakPx = peakPx;
}

void drawPercent(int percent) {
  static int lastPercent = -1;
  if (percent == lastPercent) return;

  tft.fillRect(170, PERCENT_Y, 70, 18, COLOR_BG);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_SUBTEXT);
  tft.setCursor(170, PERCENT_Y);
  tft.print(percent);
  tft.print("%");

  lastPercent = percent;
}

// =====================================================
// MIC SETUP
// =====================================================
void setupMic() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = MIC_CHANNEL_FORMAT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

// =====================================================
// READ MIC LEVEL
// =====================================================
int readMicLevel() {
  size_t bytesRead = 0;

  esp_err_t ok = i2s_read(
    I2S_PORT,
    (void*)i2sSamples,
    sizeof(i2sSamples),
    &bytesRead,
    portMAX_DELAY
  );

  if (ok != ESP_OK || bytesRead == 0) return 0;

  int count = bytesRead / sizeof(int32_t);
  if (count <= 0) return 0;

  // إزالة DC offset
  double mean = 0.0;
  for (int i = 0; i < count; i++) {
    int32_t s = (i2sSamples[i] >> MIC_BIT_SHIFT);
    mean += s;
  }
  mean /= count;

  // RMS
  double sumSq = 0.0;
  for (int i = 0; i < count; i++) {
    int32_t s = (i2sSamples[i] >> MIC_BIT_SHIFT);
    double v = s - mean;
    sumSq += v * v;
  }

  float rms = sqrt(sumSq / count);

  if (DEBUG_SERIAL_RMS) {
    Serial.print("RMS: ");
    Serial.println(rms);
  }

  // noise gate
  if (rms < RMS_NOISE_GATE) return 0;

  // compression خفيف بالمنطقة العالية
  if (rms > RMS_COMPRESS_START) {
    rms = RMS_COMPRESS_START + (rms - RMS_COMPRESS_START) * RMS_COMPRESS_RATIO;
  }

  // normalize
  float normalized = rms / RMS_NORMALIZE_TOP;
  normalized = constrain(normalized, 0.0f, 1.0f);

  // silence offset
  normalized = (normalized - SILENCE_OFFSET) / (1.0f - SILENCE_OFFSET);
  normalized = constrain(normalized, 0.0f, 1.0f);

  int levelPx = (int)(normalized * BAR_W);

  // dead zone
  if (levelPx < MIN_BAR_CUTOFF) {
    levelPx = 0;
  }

  // hysteresis للسكون
  if (silenceLatch) {
    if (levelPx <= SILENCE_EXIT) {
      levelPx = 0;
    } else {
      silenceLatch = false;
    }
  } else {
    if (levelPx <= SILENCE_ENTER) {
      levelPx = 0;
      silenceLatch = true;
    }
  }

  return constrain(levelPx, 0, BAR_W);
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.init(240, 320);
  tft.setRotation(1);

  drawStaticUI();
  setupMic();
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  int rawLevel = readMicLevel();

  // fast attack
  if (rawLevel > displayLevel) {
    displayLevel = displayLevel + (rawLevel - displayLevel) * ATTACK_SPEED;
  } else {
    // release حسب المنطقة
    float releaseSpeed;

    if (displayLevel > BAR_W * 0.80f) {
      releaseSpeed = RELEASE_SPEED_HIGH;
    } else if (displayLevel > BAR_W * 0.45f) {
      releaseSpeed = RELEASE_SPEED_MID;
    } else {
      releaseSpeed = RELEASE_SPEED_LOW;
    }

    displayLevel = displayLevel + (rawLevel - displayLevel) * releaseSpeed;
  }

  if (displayLevel < 0.5f) {
    displayLevel = 0.0f;
  }

  // peak hold + fall
  if (ENABLE_PEAK_LINE) {
    if (displayLevel > peakLevel) {
      peakLevel = displayLevel;
      lastPeakHold = millis();
      lastPeakFall = millis();
    } else {
      if (millis() - lastPeakHold >= PEAK_HOLD_MS) {
        if (millis() - lastPeakFall >= PEAK_FALL_MS) {
          float drop;

          if (peakLevel > BAR_W * 0.80f) {
            drop = PEAK_DROP_HIGH;
          } else if (peakLevel > BAR_W * 0.50f) {
            drop = PEAK_DROP_MID;
          } else {
            drop = PEAK_DROP_LOW;
          }

          peakLevel -= drop;

          if (peakLevel < displayLevel) peakLevel = displayLevel;
          if (peakLevel < 0.0f) peakLevel = 0.0f;

          lastPeakFall = millis();
        }
      }
    }
  } else {
    peakLevel = displayLevel;
  }

  if (millis() - lastDraw >= DRAW_INTERVAL_MS) {
    int shown   = constrain((int)displayLevel, 0, BAR_W);
    int peak    = constrain((int)peakLevel, 0, BAR_W - 1);
    int percent = levelToPercent(shown);

    drawMeter(shown, peak);
    drawPercent(percent);

    lastDraw = millis();
  }

  delay(1);
}
