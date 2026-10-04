#pragma once
#include <lvgl.h>

/* Inicializa o painel RGB (Arduino_GFX), a retroiluminacao, o toque
 * GT911 e o LVGL (buffers de desenho + display driver + input driver).
 * Chame uma unica vez em setup(). */
void display_init();

// Apaga a retroiluminacao por inatividade; rede e toque continuam ativos.
void display_idle(bool mixerConnected);
bool display_sleeping();
