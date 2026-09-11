/*
 * Dino Game — ported to 2.8" ST7789 (240x320) + XPT2046 touch, ESP32-S3
 * -----------------------------------------------------------------------
 * Original game engine: TRexCore.h (untouched, unchanged)
 * Display driver: t-rex-duino-TFT.h (a copy of t-rex-duino.h with the
 *   SH1106/I2C OLED driver swapped for ST7789_DinoAdapter.h, which mimics
 *   the same begin()/setInverse()/setAddressingMode()/fillScreen()
 *   interface but draws scaled-up pixels on the TFT instead of pushing
 *   bytes over I2C to a 0.9" OLED).
 *
 * CONTROLS:
 *   - JUMP: tap ANYWHERE on the touchscreen, OR press the tactile push-
 *     button wired to JUMP_BUTTON_PIN -- either one jumps.
 *   - SETTINGS: press the ESP32-S3's onboard BOOT button (GPIO0) once,
 *     from anywhere, to open the settings menu (rotation + color invert).
 *
 * WIRING (2.8" ST7789 + XPT2046, from your pinout sheet):
 *   TFT   VCC 3.3V  GND GND  CS 10  RST 8  DC 9  MOSI 11  SCK 13  LED 3.3V  MISO 12
 *   TOUCH TCLK 13  T_CS 7  T_DIN 11  T_DO 12  T_IRQ 6
 *   (T_CLK/T_DIN/T_DO share the TFT's SPI bus pins — that's normal;
 *    each device is selected by its own CS: TFT_CS=10, TOUCH_CS=7)
 *   JUMP button: one leg to 3.3V, the diagonal leg to JUMP_BUTTON_PIN
 *   BOOT button: built into the ESP32-S3 dev board (GPIO0), no wiring needed
 *
 * Libraries needed (Arduino Library Manager):
 *   - Adafruit GFX Library
 *   - Adafruit ST7735 and ST7789 Library
 *   - XPT2046_Touchscreen (by Paul Stoffregen)
 * -----------------------------------------------------------------------
 */

#include <Arduino.h>
#include <SPI.h>

// Tactile push-button wired here for JUMP: one leg to 3.3V, the diagonal
// leg to this GPIO. Pressed = HIGH, released = LOW (matches
// buttonPressed()'s "== HIGH" check). INPUT_PULLDOWN keeps the pin safely
// LOW when not pressed. The touchscreen ALSO triggers jump (see
// buttonPressed() in t-rex-duino-TFT.h) -- both inputs are OR'd together.
#define JUMP_BUTTON_PIN 4
#define BUZZER_PIN 21   // not physically wired -- moved off GPIO12 since that pin is TFT_MISO on this board

#include "t-rex-duino-TFT.h"

void setup() {
  Serial.begin(9600);
  Serial.println("=== DINO GAME - 2.8in ST7789 TFT BUILD ===");
  pinMode(JUMP_BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(BUZZER_PIN, OUTPUT);
  settings_initBootButton(); // GPIO0 / onboard BOOT button opens the settings menu
}

void loop() {
  runDinoGame();
}
