# Base del demo oficial Waveshare

Este proyecto mantiene el mismo punto de partida del ejemplo oficial
`Arduino/examples/09_lvgl_Porting` incluido en
`ESP32-S3-Touch-LCD-4.3B-BOX-Demo`.

## Correspondencia con el demo

| Demo de Waveshare | Gluco Waveshare |
|---|---|
| Perfil `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3_B` | Definido en `firmware/platformio.ini` |
| `ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED=1` | Definido en `firmware/platformio.ini` |
| Creacion de `Board` | `firmware/src/display.cpp` |
| `board->init()` y `board->begin()` | `displayInit()` en `firmware/src/display.cpp` |
| LCD RGB ST7262 de 800 x 480 | Proporcionado por el perfil oficial |
| Tactil GT911 y expansor CH422G | Proporcionados por el perfil oficial |
| Puerto LVGL | `flush()` y `touch()` en `firmware/src/display.cpp` |
| Bucle `lv_timer_handler()` | `loop()` en `firmware/src/main.cpp` |

## Arranque de la aplicacion

1. `setup()` comprueba la PSRAM y crea el estado de la aplicacion.
2. `displayInit()` inicializa primero placa, LCD, tactil y LVGL con el perfil
   oficial.
3. Despues se leen los ajustes guardados y se crea la interfaz.
4. Wi-Fi se activa cuando la pantalla ya esta operativa.
5. Si falta configuracion, se abre primero el QR de Wi-Fi y despues el QR local
   para los servicios que necesitan Internet.

La logica adicional de reloj, clima, LibreLinkUp, grafica y configuracion web
esta separada del arranque de hardware para no modificar el perfil oficial del
panel.

## Archivos principales

- `firmware/src/display.cpp`: adaptacion directa del arranque del demo.
- `firmware/src/main.cpp`: orden de inicio y bucle LVGL.
- `firmware/src/providers.cpp`: LibreLinkUp y Open-Meteo.
- `firmware/src/ui.cpp`: reloj, lectura, flecha, colores y grafica de 8 horas.
- `firmware/src/config.cpp`: NVS, los dos portales QR y la API local de ajustes.
- `firmware/src/network.cpp`: tarea periodica de glucosa y clima.
- `firmware/src/http.cpp`: cliente HTTPS comun para los proveedores externos.
- `web/`: HTML, CSS y JavaScript editables del portal.

## Regenerar la web embebida

Despues de modificar `web/`, ejecuta desde la raiz del proyecto:

```bash
python3 tools/embed_web.py
```

El resultado actualizado queda en `firmware/include/web_assets.hpp` y se
compila junto con el firmware.
