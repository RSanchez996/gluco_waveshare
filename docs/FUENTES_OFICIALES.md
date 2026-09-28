# Fuentes y decisiones

## Waveshare

- Producto 4.3B (ficha de 4.3B y 4.3B-BOX): <https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B>
- Repositorio oficial 4.3B: <https://github.com/waveshareteam/ESP32-S3-Touch-LCD-4.3B>
- Recursos y demo: <https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B/Resources-And-Documents>
- Desarrollo Arduino: <https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B/Arduino>
- Grabacion de firmware: <https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B/Firmware-Flashing>
- Perfil oficial de ESP32 Display Panel: <https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/docs/board/board_waveshare.md>
- Esquema oficial 4.3B: <https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3B/ESP32-S3-Touch-LCD-4.3B-Sch.pdf>

La documentacion confirma 800 x 480, RGB, 16 MB flash, 8 MB PSRAM, ST7262,
GT911 y CH422G. Este paquete no redistribuye los binarios demo: usa el perfil
de placa y el flujo de inicializacion documentados por el fabricante.

El perfil oficial controla la retroiluminación de la 4.3B mediante el CH422G
como interruptor. Esta versión retira el regulador visual de la 0.8.5 y
mantiene solo el apagado físico ya probado en la 0.8.4.

El codigo se contrasto con el ZIP oficial
`ESP32-S3-Touch-LCD-4.3B-BOX-Demo` y, en particular, con
`Arduino/examples/09_lvgl_Porting` y el perfil 4.3B de
`ESP32_Display_Panel`.

El perfil oficial recomienda OPI PSRAM, QIO a 80 MHz, 16 MB de flash y USB CDC
activado para la 4.3B. La ficha actual de Waveshare declara 5 V / 450 mA de
consumo típico y alimentación de 5 V por USB-C. Para las pruebas con Wi-Fi se
recomienda una fuente estable de 5 V con capacidad de 2 A como margen práctico;
esto no indica que el equipo consuma 2 A constantemente. Waveshare indica que
el puerto Type-C marcado UART sirve para grabar y leer el registro serie.

## Espressif y LVGL

- Arduino-ESP32 3.3.12: <https://github.com/espressif/arduino-esp32/releases/tag/3.3.12>
- ESP32 Display Panel: <https://github.com/esp-arduino-libs/ESP32_Display_Panel/releases/tag/v1.0.4>
- PSRAM en ESP-IDF 5.5: <https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-guides/external-ram.html>
- LVGL 8.4: <https://github.com/lvgl/lvgl/releases/tag/v8.4.0>
- Fuentes LVGL 8.4: <https://docs.lvgl.io/8.4/overview/font.html>
- Configuración LVGL 8.4 de fuentes comprimidas:
  <https://github.com/lvgl/lvgl/blob/v8.4.0/src/font/lv_font_fmt_txt.c>
- API Wi-Fi Arduino-ESP32: <https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html>
- FAQ oficial sobre interferencias del LCD RGB y PSRAM:
  <https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html>
- Modos de framebuffer y buffer de rebote RGB en ESP32-S3:
  <https://docs.espressif.com/projects/esp-idf/en/v5.3.6/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html>

El proyecto fija Arduino-ESP32 3.3.12 y las bibliotecas de ESP-IDF 5.5.5.
Sustituye la combinacion 3.1.1 / 5.3.2 que aparecia en el fallo anterior al
activar Wi-Fi.

Espressif documenta que el barrido RGB puede sufrir corrupción si el reloj de
píxeles supera el ancho de banda disponible de PSRAM o si una escritura en
flash interrumpe el acceso al framebuffer. La 0.8.1 reduce el reloj RGB y
mantiene las fases de diagnóstico en RTC para evitar esas escrituras durante
las consultas; solo las modificaciones reales de configuración usan NVS.

En LVGL 8.4 la conversión de fuentes comprime los mapas por defecto. Las cinco
fuentes `gluco_font_*.c` tienen `bitmap_format = 1`. Si
`LV_USE_FONT_COMPRESSED` está desactivado, `lv_font_get_bitmap_fmt_txt()`
devuelve NULL para todos sus caracteres; la alpha.2 activa el decodificador y
comprueba caracteres españoles al inicio.

## Servicios

- LibreLinkUp oficial: <https://www.librelinkup.com/>
- Descripción del funcionamiento en España (Abbott):
  <https://www.freestyle.abbott/es-es/productos/conectividad/librelinkup.html>
- Estado del servicio de Abbott: <https://status.freestyle.abbott/>
- Open-Meteo Forecast API: <https://open-meteo.com/en/docs>
- Open-Meteo Geocoding API: <https://open-meteo.com/en/docs/geocoding-api>

Abbott no publica una API de LibreView para este uso. La integracion implementa
el protocolo privado empleado por LibreLinkUp; por tanto puede dejar de funcionar
si el proveedor cambia autenticacion, regiones, cabeceras o formato JSON.
La documentación oficial describe la aplicación del receptor de datos, no las
rutas HTTP `llu/auth/login`, `llu/connections` o `graph`: esas rutas se contrastan
con implementaciones comunitarias, no tienen garantía de compatibilidad.

Como contraste técnico se ha revisado la implementación ESP32
<https://github.com/SpaceTeddy/LibreLinkUpESP32>, que documenta que la respuesta
`/graph` llega con transferencia HTTP fragmentada y recomienda límites duros de
conexión, TLS y lectura. También se ha contrastado el flujo actual
login → conexiones → gráfica con
<https://github.com/robberwick/pylibrelinkup>. Ambos son proyectos no oficiales;
solo se usan para verificar el comportamiento del protocolo privado.
