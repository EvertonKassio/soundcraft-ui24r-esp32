/*
 * display.cpp - Ponte entre o painel ST7701 (via Arduino_GFX, barramento
 * RGB direto do ESP32-S3), o toque capacitivo GT911 e o LVGL 8.3.
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
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_color_t *buf2;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

static void lvgl_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_disp_flush_ready(disp);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  ts.read();
  if (ts.isTouched) {
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
  /* O "tick" do LVGL (lv_tick_inc) e alimentado a cada volta do loop()
   * em main.cpp, com base em millis() - compativel com qualquer versao
   * do LVGL 8.x, sem depender de lv_tick_set_cb (so em versoes mais
   * novas) nem de um timer de hardware dedicado. */

  size_t bytes_buf = LCD_LARGURA * LINHAS_BUFFER * sizeof(lv_color_t);
  buf1 = (lv_color_t *)heap_caps_malloc(bytes_buf, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = (lv_color_t *)heap_caps_malloc(bytes_buf, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf1 || !buf2) {
    Serial.println("Falha: buffers do display. Verifique PSRAM OPI.");
    while (true) delay(1000);
  }
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_LARGURA * LINHAS_BUFFER);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_LARGURA;
  disp_drv.ver_res = LCD_ALTURA;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lvgl_touch_cb;
  indev_drv.long_press_time = TOQUE_LONGO_MS;
  lv_indev_drv_register(&indev_drv);

  digitalWrite(PINO_RETROILUM, HIGH);
}
