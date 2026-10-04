# TAMC_GT911 1.0.2 — adaptação local

Fonte: https://github.com/TAMCTec/gt911-arduino
Licença: MIT (arquivo LICENSE).

A inicialização ignora INT e RESET quando definidos como -1 (UINT8_MAX),
pois esses pinos não são expostos na placa ESP32-4848S040C_I.
Os atrasos e a configuração I2C original foram preservados.
Esta cópia local permite reproduzir a correção após limpar o cache PlatformIO.
