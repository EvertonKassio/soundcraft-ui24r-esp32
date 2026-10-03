/*
 * board_config.h - Pinagem do ESP32-4848S040C(E) (Guition/Sunton, 480x480,
 * ESP32-S3, painel RGB ST7701, toque capacitivo GT911).
 *
 * Os valores abaixo foram conferidos com varios projetos independentes
 * para essa mesma placa (issues e wikis do Arduino_GFX, projeto
 * LVGL_GUITION e a definicao ESPHome do "Guition ESP32-S3-4848S040").
 * Ainda assim, como existem pequenas variacoes de lote/revisao entre
 * placas vendidas como "4848S040C" e "4848S040CE", confira a serigrafia
 * da sua placa se a tela ou o toque nao funcionarem de primeira - veja
 * o README.md, secao "Se algo nao funcionar de primeira".
 */
#pragma once

/* ---------------------------------------------------------------- LCD */
#define PINO_RETROILUM     38   /* backlight (liga em HIGH) */

#define PINO_LCD_CS        39
#define PINO_LCD_SCK       48
#define PINO_LCD_SDA       47

#define PINO_LCD_DE        18
#define PINO_LCD_VSYNC     17
#define PINO_LCD_HSYNC     16
#define PINO_LCD_PCLK      21

#define PINO_LCD_R0        11
#define PINO_LCD_R1        12
#define PINO_LCD_R2        13
#define PINO_LCD_R3        14
#define PINO_LCD_R4        0

#define PINO_LCD_G0        8
#define PINO_LCD_G1        20
#define PINO_LCD_G2        3
#define PINO_LCD_G3        46
#define PINO_LCD_G4        9
#define PINO_LCD_G5        10

#define PINO_LCD_B0        4
#define PINO_LCD_B1        5
#define PINO_LCD_B2        6
#define PINO_LCD_B3        7
#define PINO_LCD_B4        15

#define LCD_LARGURA        480
#define LCD_ALTURA         480

/* Gire o conteudo se ele aparecer de lado (como se a tela estivesse
 * deitada): tente 0, depois 1, 2 e 3, nessa ordem, ate o texto
 * "Painel de Canais" aparecer na horizontal e na orientacao certa. */
#define PAINEL_ROTACAO      2

/* Mude para 1 se as cores aparecerem erradas/em negativo (fundo que
 * deveria ser bem escuro aparecendo claro/colorido, por exemplo). */
#define PAINEL_INVERTER_CORES 0

/* ------------------------------------------------------------- TOQUE */
#define PINO_TOQUE_SDA     19
#define PINO_TOQUE_SCL     45
#define PINO_TOQUE_INT     (-1)  /* nao exposto nesta placa */
#define PINO_TOQUE_RST     (-1)  /* nao exposto nesta placa */

/* Se o toque responder no lugar errado, ajuste estas 3 chaves (uma de
 * cada vez, recompilando a cada tentativa):
 *  - toque aparece "deitado" (o eixo errado)     -> TOQUE_TROCAR_XY
 *  - toque na esquerda acerta a direita (e vice-versa) -> TOQUE_INVERTER_X
 *  - toque em cima acerta embaixo (e vice-versa)  -> TOQUE_INVERTER_Y
 * Com DEBUG_TOQUE=1 (abaixo), o monitor serial mostra as coordenadas
 * cruas de cada toque - toque nos 4 cantos da tela e compare com o
 * que era esperado para descobrir a combinacao certa. */
#define TOQUE_TROCAR_XY    0
#define TOQUE_INVERTER_X   0
#define TOQUE_INVERTER_Y   0

/* Mostra no monitor serial (115200) as coordenadas cru e ja calibrada
 * de cada toque - util para calibrar as 3 chaves acima. Pode deixar
 * em 1 mesmo depois de calibrado, o custo e minimo. */
#define DEBUG_TOQUE        0

/* Tempo (ms) do toque longo para abrir a tela de canais de um grupo */
#define TOQUE_LONGO_MS     500
