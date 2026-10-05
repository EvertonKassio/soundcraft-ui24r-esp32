/*
 * display.cpp - Ponte entre o painel ST7701 (via Arduino_GFX, barramento
 * RGB direto do ESP32-S3), o toque capacitivo GT911 e o LVGL 9.6.
 */
#include "display.h"
#include "board_config.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>

/* ----------------------------------------------------------- painel */
static Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, PINO_LCD_CS, PINO_LCD_SCK, PINO_LCD_SDA,
    GFX_NOT_DEFINED /* MISO */);

static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    PINO_LCD_DE, PINO_LCD_VSYNC, PINO_LCD_HSYNC, PINO_LCD_PCLK,
    PINO_LCD_R0, PINO_LCD_R1, PINO_LCD_R2, PINO_LCD_R3, PINO_LCD_R4,
    PINO_LCD_G0, PINO_LCD_G1, PINO_LCD_G2, PINO_LCD_G3, PINO_LCD_G4, PINO_LCD_G5,
    PINO_LCD_B0, PINO_LCD_B1, PINO_LCD_B2, PINO_LCD_B3, PINO_LCD_B4,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    LCD_LARGURA, LCD_ALTURA, rgbpanel, PAINEL_ROTACAO, true /* auto_flush */,
    bus, GFX_NOT_DEFINED /* RST */,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

/* ------------------------------------------------------------ toque */
static TAMC_GT911 ts = TAMC_GT911(
    PINO_TOQUE_SDA, PINO_TOQUE_SCL, PINO_TOQUE_INT, PINO_TOQUE_RST,
    LCD_LARGURA, LCD_ALTURA);

/* -------------------------------------------------------------lvgl */
static const uint32_t LINHAS_BUFFER = 60; /* altura de cada buffer parcial */

static uint8_t *buf1;
static uint8_t *buf2;
static lv_display_t *display;
static lv_indev_t *touchInput;
static uint32_t lastInteraction = 0, lastMixerConnection = 0;
static bool displaySleeping = false, suppressWakeTouch = false;
static constexpr uint32_t DISCONNECTED_SLEEP_MS = 10UL * 60 * 1000;
static constexpr uint32_t CONNECTED_SLEEP_MS = 1UL * 60 * 60 * 1000;

bool display_sleeping() { return displaySleeping; }

void display_idle(bool mixerConnected) {
  const uint32_t now = millis();
  if (mixerConnected) lastMixerConnection = now;
  const uint32_t idle = now - lastInteraction;
  const bool shouldSleep = mixerConnected
    ? idle >= CONNECTED_SLEEP_MS
    : idle >= DISCONNECTED_SLEEP_MS && now - lastMixerConnection >= DISCONNECTED_SLEEP_MS;
  if (!displaySleeping && shouldSleep) {
    displaySleeping = true;
    digitalWrite(PINO_RETROILUM, LOW);
    Serial.println("[display] repouso por inatividade");
  }
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_display_flush_ready(disp);
}

static void lvgl_touch_cb(lv_indev_t *drv, lv_indev_data_t *data) {
  ts.read();
  if (ts.isTouched) {
    lastInteraction = millis();
    if (displaySleeping) {
      displaySleeping = false;
      suppressWakeTouch = true;
      digitalWrite(PINO_RETROILUM, HIGH);
      Serial.println("[display] acordou por toque");
    }
    // Consume the entire waking gesture, including a held finger.
    if (suppressWakeTouch) {
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    int32_t x = ts.points[0].x;
    int32_t y = ts.points[0].y;
#if TOQUE_TROCAR_XY
    int32_t t = x; x = y; y = t;
#endif
#if TOQUE_INVERTER_X
    x = (LCD_LARGURA - 1) - x;
#endif
#if TOQUE_INVERTER_Y
    y = (LCD_ALTURA - 1) - y;
#endif
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
#if DEBUG_TOQUE
    static int32_t x_bruto_ant = -1, y_bruto_ant = -1;
    if (ts.points[0].x != x_bruto_ant || ts.points[0].y != y_bruto_ant) {
      Serial.printf("[toque] bruto=(%d,%d) calibrado=(%ld,%ld)\n",
                    ts.points[0].x, ts.points[0].y, (long)x, (long)y);
      x_bruto_ant = ts.points[0].x;
      y_bruto_ant = ts.points[0].y;
    }
#endif
  } else {
    suppressWakeTouch = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void display_init() {
  pinMode(PINO_RETROILUM, OUTPUT);
  digitalWrite(PINO_RETROILUM, LOW); /* so acende depois do 1o quadro desenhado */

  gfx->begin();
  gfx->invertDisplay(PAINEL_INVERTER_CORES ? true : false);
  gfx->fillScreen(BLACK);

  Wire.begin(PINO_TOQUE_SDA, PINO_TOQUE_SCL);
  ts.begin();
  ts.setRotation(ROTATION_NORMAL);

  lv_init();
  // Tick is supplied by main.cpp; partial RGB565 buffers reside in PSRAM.

  size_t bytes_buf = LCD_LARGURA * LINHAS_BUFFER * 2;
  buf1 = (uint8_t *)heap_caps_malloc(bytes_buf, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = (uint8_t *)heap_caps_malloc(bytes_buf, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf1 || !buf2) {
    Serial.println("Falha: buffers do display. Verifique PSRAM OPI.");
    while (true) delay(1000);
  }
  display = lv_display_create(LCD_LARGURA, LCD_ALTURA);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, buf1, buf2, bytes_buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, lvgl_flush_cb);
  touchInput = lv_indev_create();
  lv_indev_set_type(touchInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_display(touchInput, display);
  lv_indev_set_read_cb(touchInput, lvgl_touch_cb);
  lv_indev_set_long_press_time(touchInput, TOQUE_LONGO_MS);
  lastInteraction = lastMixerConnection = millis();
  digitalWrite(PINO_RETROILUM, HIGH);
}
