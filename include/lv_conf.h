/* Configuracao minima do LVGL 8.3 para o painel de controle da mesa.
 * Baseado no lv_conf_template.h oficial, com so o que este projeto usa
 * habilitado (para manter o binario pequeno). */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>
#include <esp_heap_caps.h>
static inline void *panel_lv_malloc(size_t n) {
  return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void *panel_lv_realloc(void *p, size_t n) {
  return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

/*------------- cor / memoria -------------*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0

#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC panel_lv_malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC panel_lv_realloc
#define LV_MEM_SIZE (176U * 1024U) /* varias telas com ~300 widgets no total */
#define LV_MEM_ADR 0

/*------------- tick / temporizacao -------------*/
#define LV_DISP_DEF_REFR_PERIOD 17
#define LV_INDEV_DEF_READ_PERIOD 17
#define LV_TICK_CUSTOM 0 /* usamos lv_tick_set_cb() manualmente em display.cpp */

/*------------- funcionalidades basicas -------------*/
#define LV_USE_ARC 0
#define LV_USE_BAR 0
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_USE_LINE 0
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 0

#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 1
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_SPAN 0

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 10
#define LV_USE_THEME_BASIC 0
#define LV_USE_THEME_MONO 0

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*------------- fontes (toda a interface usa lv_label com as fontes
 * Montserrat prontas do LVGL - por isso os textos em data.h e ui.cpp
 * sao sem acentuacao: essas fontes cobrem so os caracteres ASCII) */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*------------- entrada de texto (nao usado, mas exigido pelo core) */
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/*------------- log / desempenho -------------*/
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT
#define LV_USE_MEM_MONITOR 0

/*------------- sistema de arquivos (nao usado) -------------*/
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0

/*------------- API antiga em desuso -------------*/
#define LV_USE_USER_DATA 1

#endif /* LV_CONF_H */
