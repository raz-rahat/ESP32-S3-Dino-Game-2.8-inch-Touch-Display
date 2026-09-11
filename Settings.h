/*
 * Settings.h  (v3 - multi-page settings: rotation, per-axis touch config,
 *                    touch calibration/verify screen)
 * -----------------------------------------------------------------
 * Settings module for the Dino Game (ESP32-S3 + ST7789 build).
 *
 * Persisted (NVS via ESP32 Preferences):
 *   - rotation     : 0, 1, 2 or 3  (0/90/180/270 degrees)
 *   - colorInvert  : hardware black/white invert of the whole panel
 *   - invertX/Y    : flip touch X / Y axis
 *   - swapXY       : swap the raw touch X/Y axes before mapping
 *
 * DEFAULTS (auto-applied whenever rotation is changed, matching how the
 * panel behaves on this hardware):
 *   - LANDSCAPE (rotation 1 or 3): invertY = ON,  invertX = OFF, swap = OFF
 *   - PORTRAIT  (rotation 0 or 2): invertY = OFF, invertX = OFF, swap = OFF
 * These are just the starting point -- fine-tune per rotation in
 * TOUCH CONFIG and confirm with CALIBRATE / VERIFY.
 *
 * HOW TO OPEN THE SETTINGS MENU:
 *   Press the ESP32-S3's onboard BOOT button (GPIO0) once, from
 *   anywhere -- splash screen, mid-game, or the game-over screen.
 *
 * MENU MAP:
 *   MAIN
 *     -> ROTATION        (pick 0 / 90 / 180 / 270)
 *     -> TOUCH CONFIG     (INV Y, INV X, SWAP AXIS, RESET)
 *     -> CALIBRATE/VERIFY (8-point touch accuracy test)
 *     -> INVERT COLORS    (toggle, applies immediately)
 *     -> SAVE             (writes everything to flash)
 *     [X] top-right        (exits settings entirely, back to the game)
 *   Inside a submenu, [X] goes back to MAIN instead of exiting.
 *
 * This file must be included AFTER ST7789_DinoAdapter.h (needs the
 * ST7789DinoDisplay class, getRawTouchPoint(), getScreenWidth/Height(),
 * getRotation(), setRotation(), setHardwareInvert(), gfx()).
 * -----------------------------------------------------------------
 */

#ifndef _DINO_SETTINGS_H_
#define _DINO_SETTINGS_H_

#include <Preferences.h>

/* ---------------- BOOT button (opens the settings menu) ---------------- */
#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0   // GPIO0 = onboard "BOOT" button on ESP32-S3 dev boards
#endif
#define BOOT_BTN_DEBOUNCE_MS 30

void settings_initBootButton() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP); // BOOT button pulls the pin LOW when pressed
}

// Returns true exactly once, on the press edge (not while held, not on release).
bool settings_bootButtonPressed() {
  static bool lastStable = HIGH;
  static bool lastReading = HIGH;
  static unsigned long lastChangeTime = 0;

  bool reading = digitalRead(BOOT_BUTTON_PIN);
  if (reading != lastReading) {
    lastChangeTime = millis();
    lastReading = reading;
  }
  bool fired = false;
  if ((millis() - lastChangeTime) > BOOT_BTN_DEBOUNCE_MS) {
    if (reading != lastStable) {
      lastStable = reading;
      if (lastStable == LOW) fired = true; // just pressed
    }
  }
  return fired;
}

/* ---------------- Settings data + persistence ---------------- */
struct GameSettings {
  uint8_t rotation    = 3;      // 0/1/2/3 -> 0,90,180,270 degrees
  bool    colorInvert = false;  // hardware panel color invert
  bool    invertX     = false;  // touch X axis flip
  bool    invertY     = true;   // touch Y axis flip
  bool    swapXY      = false;  // swap raw touch X/Y before mapping
};

GameSettings settings;
static Preferences _dinoPrefs;

static inline bool _isLandscape(uint8_t rot) { return rot == 1 || rot == 3; }

// Restores the recommended touch defaults for the CURRENT rotation's
// orientation family (landscape vs portrait). Called automatically
// whenever the rotation is changed, and also from the "RESET" button
// in TOUCH CONFIG.
inline void resetTouchDefaults() {
  if (_isLandscape(settings.rotation)) {
    settings.invertY = true;
    settings.invertX = false;
    settings.swapXY  = false;
  } else {
    settings.invertY = false;
    settings.invertX = false;
    settings.swapXY  = false;
  }
}

void settings_load() {
  _dinoPrefs.begin("dinogame", true /* read-only */);
  settings.rotation    = _dinoPrefs.getUChar("rot", 3);
  settings.colorInvert = _dinoPrefs.getBool("inv", false);
  settings.invertX     = _dinoPrefs.getBool("tix", false);
  settings.invertY     = _dinoPrefs.getBool("tiy", true);
  settings.swapXY      = _dinoPrefs.getBool("tsw", false);
  _dinoPrefs.end();
  if (settings.rotation > 3) settings.rotation = 3;
}

void settings_save() {
  _dinoPrefs.begin("dinogame", false /* read-write */);
  _dinoPrefs.putUChar("rot", settings.rotation);
  _dinoPrefs.putBool("inv", settings.colorInvert);
  _dinoPrefs.putBool("tix", settings.invertX);
  _dinoPrefs.putBool("tiy", settings.invertY);
  _dinoPrefs.putBool("tsw", settings.swapXY);
  _dinoPrefs.end();
}

// Push the current settings values onto the actual hardware.
void settings_apply(ST7789DinoDisplay &lcd) {
  lcd.setRotation(settings.rotation);
  lcd.setHardwareInvert(settings.colorInvert);
}

/* ---------------- Touch calibration bounds (raw ADC from XPT2046) ---------------- */
#ifndef CAL_RAW_X_MIN
#define CAL_RAW_X_MIN 430
#define CAL_RAW_X_MAX 3706
#define CAL_RAW_Y_MIN 310
#define CAL_RAW_Y_MAX 3679
#endif

// Maps a raw touch reading to a screen coordinate using the CURRENT
// rotation + the user's invertX/invertY/swapXY settings. Used both for
// hit-testing the settings UI itself and for the calibration/verify
// screen -- so "verify" really is testing the exact mapping the menu uses.
// Returns false if there is no touch right now.
inline bool getMappedTouch(ST7789DinoDisplay &lcd, int &x, int &y) {
  int32_t rawX, rawY;
  if (!lcd.getRawTouchPoint(rawX, rawY)) return false;

  int32_t rx = rawX, ry = rawY;
  if (settings.swapXY) { int32_t t = rx; rx = ry; ry = t; }

  const int w = lcd.getScreenWidth();
  const int h = lcd.getScreenHeight();
  const uint8_t rot = lcd.getRotation();
  const bool landscape = _isLandscape(rot);

  int32_t mx, my;
  if (landscape) {
    // in landscape the physical touch axes are swapped relative to screen x/y
    mx = map(ry, CAL_RAW_Y_MIN, CAL_RAW_Y_MAX, 0, w - 1);
    my = map(rx, CAL_RAW_X_MIN, CAL_RAW_X_MAX, 0, h - 1);
  } else {
    mx = map(rx, CAL_RAW_X_MIN, CAL_RAW_X_MAX, 0, w - 1);
    my = map(ry, CAL_RAW_Y_MIN, CAL_RAW_Y_MAX, 0, h - 1);
  }

  // rotations 2 (180deg) and 3 (270deg) need an extra flip on top of the
  // base landscape/portrait axis assignment above.
  if (rot == 2 || rot == 3) {
    mx = (w - 1) - mx;
    my = (h - 1) - my;
  }

  if (settings.invertX) mx = (w - 1) - mx;
  if (settings.invertY) my = (h - 1) - my;

  x = (int)constrain(mx, 0, w - 1);
  y = (int)constrain(my, 0, h - 1);
  return true;
}

/* ---------------- Menu state ---------------- */
enum SettingsSubMenu { SETTINGS_MAIN, SETTINGS_ROTATION, SETTINGS_TOUCH_CONFIG, SETTINGS_CALIBRATION };
static SettingsSubMenu _settingsSubMenu = SETTINGS_MAIN;

static int _calStep = 0;
static const int _CAL_TOTAL_STEPS = 8;
static const char* _CAL_STEP_NAMES[] = {
  "UP", "DOWN", "LEFT", "RIGHT",
  "TOP LEFT", "TOP RIGHT", "BOTTOM LEFT", "BOTTOM RIGHT"
};

/* ---------------- UI helpers ---------------- */
inline void _drawCenteredString(ST7789DinoDisplay &lcd, const String &text, int y, uint8_t size, uint16_t color) {
  Adafruit_ST7789 &gfx = lcd.gfx();
  gfx.setTextSize(size);
  gfx.setTextColor(color, COL_BG);
  int16_t x1, y1; uint16_t tw, th;
  gfx.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
  gfx.setCursor(((int)lcd.getScreenWidth() - (int)tw) / 2, y);
  gfx.print(text);
}

// Header with a title and a top-right [X] box. On MAIN, tapping X exits
// the whole settings menu; on a submenu it just returns to MAIN.
inline void _drawHeaderWithExit(ST7789DinoDisplay &lcd, const String &title) {
  Adafruit_ST7789 &gfx = lcd.gfx();
  const int w = lcd.getScreenWidth();
  _drawCenteredString(lcd, title, 8, 2, COL_FG);
  gfx.drawRoundRect(w - 36, 6, 28, 24, 4, COL_FG);
  gfx.setTextColor(COL_FG, COL_BG);
  gfx.setTextSize(2);
  gfx.setCursor(w - 27, 10);
  gfx.print("X");
}

inline bool _hitExit(ST7789DinoDisplay &lcd, int x, int y) {
  const int w = lcd.getScreenWidth();
  return x > w - 45 && x < w - 2 && y > 2 && y < 38;
}

inline void _getCalTargetPos(ST7789DinoDisplay &lcd, int step, int &tx, int &ty) {
  const int w = lcd.getScreenWidth();
  const int h = lcd.getScreenHeight();
  const int margin = 35;
  switch (step) {
    case 0: tx = w / 2;      ty = margin;     break;
    case 1: tx = w / 2;      ty = h - margin; break;
    case 2: tx = margin;     ty = h / 2;      break;
    case 3: tx = w - margin; ty = h / 2;      break;
    case 4: tx = margin;     ty = margin;     break;
    case 5: tx = w - margin; ty = margin;     break;
    case 6: tx = margin;     ty = h - margin; break;
    case 7: tx = w - margin; ty = h - margin; break;
    default: tx = w / 2;     ty = h / 2;      break;
  }
}

/* ---------------- MAIN menu ---------------- */
// Row layout is computed once here and reused by both the drawer and the
// touch router below, so hit-boxes always match what's drawn.
struct _MenuLayout { int x, w, y0, h, gap; };
inline _MenuLayout _mainLayout(ST7789DinoDisplay &lcd) {
  const int w = lcd.getScreenWidth();
  const bool narrow = w < 300;
  _MenuLayout L;
  L.x = 15;
  L.w = w - 30;
  L.h = narrow ? 34 : 28;
  L.y0 = narrow ? 46 : 38;
  L.gap = narrow ? 42 : 34;
  return L;
}

inline void _drawMainRow(ST7789DinoDisplay &lcd, int index, const String &label) {
  Adafruit_ST7789 &gfx = lcd.gfx();
  _MenuLayout L = _mainLayout(lcd);
  int y = L.y0 + index * L.gap;
  gfx.drawRoundRect(L.x, y, L.w, L.h, 4, COL_FG);
  gfx.setTextSize(2);
  gfx.setTextColor(COL_FG, COL_BG);
  gfx.setCursor(L.x + 10, y + (L.h / 2) - 6);
  gfx.print(label);
}

inline void drawSettingsMenu(ST7789DinoDisplay &lcd) {
  Adafruit_ST7789 &gfx = lcd.gfx();
  gfx.fillScreen(COL_BG);
  _drawHeaderWithExit(lcd, "SETTINGS");

  _drawMainRow(lcd, 0, "> ROTATION");
  _drawMainRow(lcd, 1, "> TOUCH CONFIG");
  _drawMainRow(lcd, 2, "> CALIBRATE/VERIFY");
  _drawMainRow(lcd, 3, settings.colorInvert ? "> INVERT COLORS: ON" : "> INVERT COLORS: OFF");
  _drawMainRow(lcd, 4, "> SAVE");
}

/* ---------------- ROTATION submenu ---------------- */
inline void drawRotationMenu(ST7789DinoDisplay &lcd) {
  Adafruit_ST7789 &gfx = lcd.gfx();
  gfx.fillScreen(COL_BG);
  _drawHeaderWithExit(lcd, "ROTATION");

  const int w = lcd.getScreenWidth();
  const bool narrow = w < 300;
  const int btnX = 15, btnW = w - 30;
  const int btnH = narrow ? 34 : 30;
  const int startY = narrow ? 48 : 40;
  const int gap = narrow ? 42 : 36;

  const char* labels[4] = { "0 (PORTRAIT)", "90 (LANDSCAPE)", "180 (PORTRAIT)", "270 (LANDSCAPE)" };
  for (uint8_t i = 0; i < 4; ++i) {
    int y = startY + i * gap;
    bool sel = (settings.rotation == i);
    gfx.drawRoundRect(btnX, y, btnW, btnH, 4, sel ? ST77XX_GREEN : COL_FG);
    gfx.setTextSize(2);
    gfx.setTextColor(sel ? ST77XX_GREEN : COL_FG, COL_BG);
    gfx.setCursor(btnX + 12, y + (btnH / 2) - 6);
    gfx.print(sel ? String("* ") + labels[i] : String("  ") + labels[i]);
  }
}

/* ---------------- TOUCH CONFIG submenu ---------------- */
inline void drawTouchConfigMenu(ST7789DinoDisplay &lcd) {
  Adafruit_ST7789 &gfx = lcd.gfx();
  gfx.fillScreen(COL_BG);
  _drawHeaderWithExit(lcd, "TOUCH CONFIG");

  const int w = lcd.getScreenWidth();
  const bool narrow = w < 300;
  const int btnX = 15, btnW = w - 30;
  const int btnH = narrow ? 34 : 28;
  const int startY = narrow ? 52 : 44;
  const int gap = narrow ? 40 : 34;

  auto row = [&](int i, const char* label, bool val) {
    int y = startY + i * gap;
    gfx.drawRoundRect(btnX, y, btnW, btnH, 4, COL_FG);
    gfx.setTextSize(2);
    gfx.setCursor(btnX + 10, y + (btnH / 2) - 6);
    gfx.setTextColor(COL_FG, COL_BG);
    gfx.print(label);
    gfx.print(val ? "ON" : "OFF");
  };

  row(0, "INV Y (U/D): ", settings.invertY);
  row(1, "INV X (L/R): ", settings.invertX);
  row(2, "SWAP AXIS: ",   settings.swapXY);

  int y3 = startY + 3 * gap;
  gfx.drawRoundRect(btnX, y3, btnW, btnH, 4, ST77XX_RED);
  gfx.setTextSize(2);
  gfx.setTextColor(ST77XX_RED, COL_BG);
  gfx.setCursor(btnX + 10, y3 + (btnH / 2) - 6);
  gfx.print("[ > RESET DEFAULTS < ]");
}

/* ---------------- CALIBRATE / VERIFY submenu ---------------- */
inline void drawCalibrationStep(ST7789DinoDisplay &lcd) {
  Adafruit_ST7789 &gfx = lcd.gfx();
  gfx.fillScreen(COL_BG);

  if (_calStep >= _CAL_TOTAL_STEPS) {
    _drawCenteredString(lcd, "VERIFIED OK!", lcd.getScreenHeight() / 2 - 20, 2, ST77XX_GREEN);
    _drawCenteredString(lcd, "TAP TO RETURN", lcd.getScreenHeight() / 2 + 10, 1, COL_FG);
    return;
  }

  const int w = lcd.getScreenWidth();
  const int h = lcd.getScreenHeight();
  const int exitW = 60, exitH = 26;
  const int exitX = (w - exitW) / 2;
  const int exitY = (h - exitH) / 2;

  _drawCenteredString(lcd, String("TAP: ") + _CAL_STEP_NAMES[_calStep], exitY - 26, 2, COL_FG);

  gfx.drawRoundRect(exitX, exitY, exitW, exitH, 4, ST77XX_RED);
  gfx.setTextColor(ST77XX_RED, COL_BG);
  gfx.setTextSize(1);
  gfx.setCursor(exitX + 18, exitY + 9);
  gfx.print("EXIT");

  int tx, ty;
  _getCalTargetPos(lcd, _calStep, tx, ty);
  gfx.drawCircle(tx, ty, 22, COL_FG);
  gfx.drawCircle(tx, ty, 6, ST77XX_YELLOW);
  gfx.drawFastHLine(tx - 28, ty, 56, COL_FG);
  gfx.drawFastVLine(tx, ty - 28, 56, COL_FG);
}

/* ---------------- Touch router ---------------- */
// Returns true only when the settings menu should close entirely.
inline bool _handleSettingsTouch(ST7789DinoDisplay &lcd, int x, int y) {
  if (_settingsSubMenu != SETTINGS_CALIBRATION && _hitExit(lcd, x, y)) {
    if (_settingsSubMenu == SETTINGS_MAIN) return true; // exit settings entirely
    _settingsSubMenu = SETTINGS_MAIN;
    drawSettingsMenu(lcd);
    return false;
  }

  if (_settingsSubMenu == SETTINGS_MAIN) {
    _MenuLayout L = _mainLayout(lcd);
    if (x < L.x || x > L.x + L.w) return false;
    for (int i = 0; i < 5; ++i) {
      int rowY = L.y0 + i * L.gap;
      if (y < rowY || y > rowY + L.h) continue;
      switch (i) {
        case 0: _settingsSubMenu = SETTINGS_ROTATION; drawRotationMenu(lcd); break;
        case 1: _settingsSubMenu = SETTINGS_TOUCH_CONFIG; drawTouchConfigMenu(lcd); break;
        case 2: _settingsSubMenu = SETTINGS_CALIBRATION; _calStep = 0; drawCalibrationStep(lcd); break;
        case 3:
          settings.colorInvert = !settings.colorInvert;
          lcd.setHardwareInvert(settings.colorInvert); // live preview
          drawSettingsMenu(lcd);
          break;
        case 4:
          settings_save();
          _drawCenteredString(lcd, "Saved!", lcd.getScreenHeight() - 20, 1, ST77XX_GREEN);
          delay(600);
          drawSettingsMenu(lcd);
          break;
      }
      break;
    }
  }
  else if (_settingsSubMenu == SETTINGS_ROTATION) {
    const int w = lcd.getScreenWidth();
    const bool narrow = w < 300;
    const int btnX = 15, btnW = w - 30;
    const int btnH = narrow ? 34 : 30;
    const int startY = narrow ? 48 : 40;
    const int gap = narrow ? 42 : 36;

    if (x >= btnX && x <= btnX + btnW) {
      for (uint8_t i = 0; i < 4; ++i) {
        int rowY = startY + i * gap;
        if (y >= rowY && y <= rowY + btnH) {
          if (settings.rotation != i) {
            settings.rotation = i;
            lcd.setRotation(i);       // live preview (screen dims may change)
            resetTouchDefaults();     // re-apply recommended defaults for the new orientation family
          }
          drawRotationMenu(lcd);
          break;
        }
      }
    }
  }
  else if (_settingsSubMenu == SETTINGS_TOUCH_CONFIG) {
    const int w = lcd.getScreenWidth();
    const bool narrow = w < 300;
    const int btnX = 15, btnW = w - 30;
    const int btnH = narrow ? 34 : 28;
    const int startY = narrow ? 52 : 44;
    const int gap = narrow ? 40 : 34;

    if (x >= btnX && x <= btnX + btnW) {
      if (y >= startY && y <= startY + btnH) { settings.invertY = !settings.invertY; drawTouchConfigMenu(lcd); }
      else if (y >= startY + gap && y <= startY + gap + btnH) { settings.invertX = !settings.invertX; drawTouchConfigMenu(lcd); }
      else if (y >= startY + 2*gap && y <= startY + 2*gap + btnH) { settings.swapXY = !settings.swapXY; drawTouchConfigMenu(lcd); }
      else if (y >= startY + 3*gap && y <= startY + 3*gap + btnH) { resetTouchDefaults(); drawTouchConfigMenu(lcd); }
    }
  }
  else if (_settingsSubMenu == SETTINGS_CALIBRATION) {
    const int w = lcd.getScreenWidth();
    const int h = lcd.getScreenHeight();
    const int exitW = 60, exitH = 26;
    const int exitX = (w - exitW) / 2;
    const int exitY = (h - exitH) / 2;

    if (_calStep >= _CAL_TOTAL_STEPS) {
      // any tap on the "VERIFIED OK!" screen returns to MAIN
      _settingsSubMenu = SETTINGS_MAIN;
      drawSettingsMenu(lcd);
      return false;
    }

    if (x >= exitX && x <= exitX + exitW && y >= exitY && y <= exitY + exitH) {
      _settingsSubMenu = SETTINGS_MAIN;
      drawSettingsMenu(lcd);
      return false;
    }

    int tx, ty;
    _getCalTargetPos(lcd, _calStep, tx, ty);
    int dx = x - tx, dy = y - ty;
    bool accurate = (dx * dx + dy * dy) <= (38 * 38);

    Adafruit_ST7789 &gfx = lcd.gfx();
    if (accurate) {
      gfx.fillCircle(tx, ty, 24, ST77XX_GREEN);
      _drawCenteredString(lcd, "CORRECT!", exitY + 35, 2, ST77XX_GREEN);
    } else {
      gfx.fillCircle(tx, ty, 24, ST77XX_RED);
      gfx.fillCircle(x, y, 6, ST77XX_YELLOW);
      _drawCenteredString(lcd, "MISSED!", exitY + 35, 2, ST77XX_RED);
    }
    delay(700);
    _calStep++;
    drawCalibrationStep(lcd);
  }

  return false;
}

/* ---------------- Entry point ---------------- */
// Blocking settings menu. Call from anywhere (splash / gameplay pause /
// game-over) when the BOOT button is pressed.
void settings_showMenu(ST7789DinoDisplay &lcd) {
  _settingsSubMenu = SETTINGS_MAIN;
  drawSettingsMenu(lcd);

  bool lastTouchDown = false;
  while (true) {
    int mx, my;
    bool touchedNow = getMappedTouch(lcd, mx, my);

    if (touchedNow && !lastTouchDown) {
      if (_handleSettingsTouch(lcd, mx, my)) {
        lastTouchDown = touchedNow;
        return; // leave settings entirely
      }
    }
    lastTouchDown = touchedNow;
    yield();
  }
}

#endif // _DINO_SETTINGS_H_
