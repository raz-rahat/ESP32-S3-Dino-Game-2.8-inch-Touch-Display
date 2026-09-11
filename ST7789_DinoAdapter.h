/*
 * ST7789_DinoAdapter.h  (v3 - any-direction rotation, real touch coords,
 *                             boot-button settings entry)
 * -----------------------------------------------------------------
 * Drop-in replacement for the SH1106<I2C> object used by
 * t-rex-duino.h / TRexCore.h.
 *
 * TRexCore.h/t-rex-duino.h only ever calls these 4 methods on the
 * display object, so this adapter only needs to implement them:
 *
 *   lcd.begin();
 *   lcd.setInverse(bool);
 *   lcd.setAddressingMode(...);        // no-op here, kept for API compat
 *   lcd.fillScreen(buffer, size, stride);
 *
 * WHAT'S NEW IN v3:
 *  - Full 4-way rotation support (0/90/180/270) instead of only the two
 *    landscape orientations. The physical panel is 240x320; landscape
 *    rotations (1/3) are logically 320x240, portrait rotations (0/2) are
 *    240x320. Total pixel count is identical either way (76800px), so
 *    the SAME frame buffer allocation is reused -- only the logical
 *    width/height used for scaling + the SPI addr window change.
 *  - Real, raw touch access (getRawTouchPoint) instead of just
 *    "any touch = press", so Settings.h can do its own calibrated,
 *    rotation-aware, user-adjustable mapping for real tappable buttons.
 *
 * Wiring used (matches MatrixRain_ESP32S3.ino / your pinout sheet):
 *   TFT_SCK   13
 *   TFT_MISO  12
 *   TFT_MOSI  11
 *   TFT_CS    10
 *   TFT_DC     9
 *   TFT_RST    8
 *   TFT_LED    5   (backlight, set to -1 if wired straight to 3V3)
 *   TOUCH_CS   7
 *   TOUCH_IRQ  6   (optional, comment out TOUCH_IRQ define if not wired)
 * -----------------------------------------------------------------
 */

#ifndef _ST7789_DINO_ADAPTER_H_
#define _ST7789_DINO_ADAPTER_H_

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <XPT2046_Touchscreen.h>
#include <esp_heap_caps.h>

/* ---------------- Pin map (edit if your wiring differs) ---------------- */
#ifndef TFT_SCK
#define TFT_SCK   13
#endif
#ifndef TFT_MISO
#define TFT_MISO  12
#endif
#ifndef TFT_MOSI
#define TFT_MOSI  11
#endif
#ifndef TFT_CS
#define TFT_CS    10
#endif
#ifndef TFT_DC
#define TFT_DC     9
#endif
#ifndef TFT_RST
#define TFT_RST    8
#endif
#ifndef TFT_LED
#define TFT_LED    5      // set to -1 if backlight is tied directly to 3V3
#endif
#ifndef TOUCH_CS
#define TOUCH_CS   7
#endif
// #define TOUCH_IRQ  6   // uncomment if T_IRQ is wired

/* ---------------- Touch calibration (reuse from MatrixRain) ---------------- */
#ifndef RAW_X_MIN
#define RAW_X_MIN 430
#define RAW_X_MAX 3706
#define RAW_Y_MIN 310
#define RAW_Y_MAX 3679
#endif

/* Physical panel is 240x320 (portrait native). */
#define TFT_PHYS_W 240
#define TFT_PHYS_H 320

/* Default/native logical screen size (landscape, rotation 3) -- also
   used to size the frame-buffer allocation (total pixel count is the
   same for any rotation, so one allocation covers all orientations). */
#define SCREEN_W_DEFAULT 320
#define SCREEN_H_DEFAULT 240

/* Game's native monochrome canvas size (from t-rex-duino.h) */
#define GAME_W 128
#define GAME_H 64

/* Colors (RGB565) */
#define COL_BG    0x0000   // black
#define COL_FG    0xFFFF   // white

/* ------------------------------------------------------------------ */
class ST7789DinoDisplay {
public:
  enum AddressingMode {
    HorizontalAddressingMode = 0x00,
    VerticalAddressingMode   = 0x01,
    PageAddressingMode       = 0x02
  };

  ST7789DinoDisplay()
    : tft(&SPI, TFT_CS, TFT_DC, TFT_RST),
#ifdef TOUCH_IRQ
      touch(TOUCH_CS, TOUCH_IRQ)
#else
      touch(TOUCH_CS)
#endif
  {}

  void begin() {
#if TFT_LED >= 0
    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, HIGH);
#endif
    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1);
    tft.init(TFT_PHYS_W, TFT_PHYS_H);
    tft.setRotation(3);           // default landscape; settings_apply() may change this later
    screenW = SCREEN_W_DEFAULT;
    screenH = SCREEN_H_DEFAULT;
    rotation = 3;
    tft.fillScreen(COL_BG);

    touch.begin(SPI);
    touch.setRotation(0);

    // Allocate the off-screen frame buffer in PSRAM (150KB for 320x240x2).
    // Falls back to regular heap if PSRAM alloc fails (will be slower /
    // may fail on boards without PSRAM -- ESP32-S3 N16R8 has 8MB PSRAM).
    // Same allocation is reused for every rotation (same total pixel count).
    frameBuf = (uint16_t*)heap_caps_malloc((size_t)SCREEN_W_DEFAULT * SCREEN_H_DEFAULT * 2,
                                            MALLOC_CAP_SPIRAM);
    if (!frameBuf) {
      frameBuf = (uint16_t*)malloc((size_t)SCREEN_W_DEFAULT * SCREEN_H_DEFAULT * 2);
    }
    if (frameBuf) {
      for (uint32_t i = 0; i < (uint32_t)SCREEN_W_DEFAULT * SCREEN_H_DEFAULT; ++i) frameBuf[i] = COL_BG;
    }
  }

  void setInverse(const bool v) {
    inverse = v;
    // Applied at buffer-fill time (see fillScreen) rather than via
    // tft.invertDisplay(), so it works correctly with the frame buffer.
  }

  void setAddressingMode(const AddressingMode) { /* no-op, kept for API compat */ }

  // Matches SH1106::fillScreen(buffer, size, stride) signature used in
  // t-rex-duino.h. `buffer` is a 1-bit page buffer: each byte packs 8
  // vertically-stacked pixels (LSB = top), laid out row-major:
  //   byte index = page*GAME_W + x   (page = y/8, 8 rows per page)
  //
  // Real SH1106 hardware just keeps writing bytes to a running cursor
  // that auto-wraps at the end of the screen -- this call can arrive in
  // ANY chunk size (the splash screen sends 32-byte chunks, the game
  // loop sends 512-byte chunks). We track a persistent byte cursor and
  // consume from wherever we left off, wrapping at end of frame -- and
  // when the cursor wraps (a full logical frame has just been written),
  // we push the ENTIRE buffer to the panel in one SPI transaction.
  void fillScreen(const uint8_t* buffer, const uint16_t size, const uint8_t stride = 0) {
    if (!frameBuf) return; // allocation failed; nothing we can safely do

    for (uint16_t i = 0; i < size; ++i) {
      const uint16_t byteIndex = _cursor;
      const uint8_t colByte = buffer[i];
      const uint8_t x    = byteIndex % GAME_W;
      const uint8_t page = byteIndex / GAME_W;
      const uint16_t yBase = (uint16_t)page * 8;

      if (yBase < GAME_H) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
          const uint16_t gy = yBase + bit;
          if (gy >= GAME_H) break;
          bool on = (colByte >> bit) & 0x01;
          if (inverse) on = !on;
          writeGamePixelToBuffer(x, gy, on);
        }
      }

      _cursor++;
      if (_cursor >= (uint16_t)GAME_W * (GAME_H / 8)) {
        _cursor = 0;
        pushFrameBuffer(); // full logical frame complete -> one bulk SPI push
      }
    }
  }

  void fillScreen(const uint8_t* buffer) {
    fillScreen(buffer, GAME_W * (GAME_H / 8));
  }

  // ---------------- Native splash screen (sharp, not a stretched bitmap) --
  void drawNativeSplash() {
    tft.fillScreen(COL_BG);
    tft.setTextWrap(false);
    tft.setTextColor(COL_FG);

    // scale text down a bit on the narrower (portrait) orientations so
    // nothing runs off the edge of the panel
    const bool narrow = screenW < 300;

    tft.setTextSize(narrow ? 3 : 4);
    tft.setCursor(14, 24);
    tft.print("Dino Game");

    tft.setTextSize(1);
    tft.setCursor(14, narrow ? 76 : 90);
    tft.print("For More Visit My Youtube Channel");

    tft.setTextSize(narrow ? 2 : 3);
    tft.setCursor(14, narrow ? 90 : 105);
    tft.print("Tech Raz Friday");

    tft.setTextSize(narrow ? 1 : 2);
    tft.setCursor(14, narrow ? 140 : 160);
    tft.print("Contact me on Facebook");
    tft.setCursor(14, narrow ? 155 : 185);
    tft.print("facebook.com/mdraz1995");

    tft.setTextSize(1);
    tft.setCursor(14, screenH - 20);
    tft.print("Hold BOOT button for Settings");

    tft.drawRect(8, 8, screenW - 16, screenH - 16, COL_FG);

    // keep the frame buffer in sync so the first game frame doesn't
    // flash back to whatever was in it before
    if (frameBuf) {
      for (uint32_t i = 0; i < (uint32_t)SCREEN_W_DEFAULT * SCREEN_H_DEFAULT; ++i) frameBuf[i] = COL_BG;
    }
  }

  // ---------------- Touch + button reading for game input ----------------
  // Call this once per loop from your sketch (outside the game engine) if
  // you want touch to also register as "buttonPressed()" for jump/duck.
  // Returns true while finger is down anywhere on screen.
  bool touchPressed() {
    return touch.touched();
  }

  // Raw (uncalibrated, unrotated) touch reading straight from the
  // XPT2046 -- Settings.h turns this into real screen coordinates using
  // the user's calibration bounds + invertX/invertY/swapXY + current
  // rotation. touchPressed() above is still the simple "any touch"
  // check used for in-game jump.
  bool getRawTouchPoint(int32_t &rawX, int32_t &rawY) {
    if (!touch.touched()) return false;
    TS_Point p = touch.getPoint();
    rawX = p.x;
    rawY = p.y;
    return true;
  }

  // ---------------- Settings support (rotation / color invert) -----------
  // All 4 rotations supported. 0/2 = portrait (240x320), 1/3 = landscape
  // (320x240) -- the frame buffer allocation is the same size either way.
  void setRotation(uint8_t r) {
    rotation = r & 0x03;
    tft.setRotation(rotation);
    if (rotation == 0 || rotation == 2) {
      screenW = TFT_PHYS_W;  // 240
      screenH = TFT_PHYS_H;  // 320
    } else {
      screenW = TFT_PHYS_H;  // 320
      screenH = TFT_PHYS_W;  // 240
    }
  }

  uint8_t getRotation() const { return rotation; }
  uint16_t getScreenWidth() const { return screenW; }
  uint16_t getScreenHeight() const { return screenH; }

  // Hardware color invert (swaps foreground/background on the panel).
  // This is independent from the game's own day/night setInverse(), which
  // instead flips bits when composing the frame buffer.
  void setHardwareInvert(bool v) {
    tft.invertDisplay(v);
  }

  // Gives Settings.h (or other helper code) direct access to draw with
  // Adafruit_GFX primitives/text on the real panel, same as drawNativeSplash().
  Adafruit_ST7789& gfx() { return tft; }

private:
  Adafruit_ST7789 tft;
  XPT2046_Touchscreen touch;
  bool inverse = false;
  uint16_t _cursor = 0;
  uint16_t* frameBuf = nullptr; // SCREEN_W_DEFAULT * SCREEN_H_DEFAULT, RGB565, row-major
  uint8_t rotation = 3;
  uint16_t screenW = SCREEN_W_DEFAULT;
  uint16_t screenH = SCREEN_H_DEFAULT;

  // Writes one logical game pixel (x in 0..127, y in 0..63), stretched to
  // its corresponding block of screen pixels, directly into the frame
  // buffer (RAM only, no SPI -- fast). Uses the CURRENT screenW/screenH,
  // so this naturally adapts to whatever rotation is active.
  inline void writeGamePixelToBuffer(uint8_t gx, uint16_t gy, bool on) {
    const uint16_t color = on ? COL_FG : COL_BG;

    const int32_t sx0 = (int32_t)gx * screenW / GAME_W;
    const int32_t sx1 = (int32_t)(gx + 1) * screenW / GAME_W;
    const int32_t sy0 = (int32_t)gy * screenH / GAME_H;
    const int32_t sy1 = (int32_t)(gy + 1) * screenH / GAME_H;

    for (int32_t y = sy0; y < sy1 && y < screenH; ++y) {
      if (y < 0) continue;
      uint16_t* row = frameBuf + (uint32_t)y * screenW;
      for (int32_t x = sx0; x < sx1 && x < screenW; ++x) {
        if (x < 0) continue;
        row[x] = color;
      }
    }
  }

  // Pushes the entire off-screen buffer to the panel in one SPI transaction.
  inline void pushFrameBuffer() {
    tft.startWrite();
    tft.setAddrWindow(0, 0, screenW, screenH);
    tft.writePixels(frameBuf, (uint32_t)screenW * screenH);
    tft.endWrite();
  }
};

#endif // _ST7789_DINO_ADAPTER_H_
