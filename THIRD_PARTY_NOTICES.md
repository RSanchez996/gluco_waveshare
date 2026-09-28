# Avisos de terceros

Las dependencias se descargan al compilar y mantienen sus licencias originales:

- Arduino-ESP32 y ESP-IDF: Espressif Systems, LGPL-2.1 / Apache-2.0 segun
  componente.
- ESP32 Display Panel, ESP32 IO Expander y esp-lib-utils: Espressif Systems,
  licencias indicadas en cada repositorio.
- LVGL 8.4: MIT.
- Montserrat (glifos españoles generados para LVGL): copyright 2011 The
  Montserrat Project Authors, SIL Open Font License 1.1. Los cinco archivos
  `firmware/src/gluco_font_*.c` son subconjuntos de Montserrat-Medium.ttf de
  la distribución de LVGL; su licencia y aviso se incluyen en
  [`third_party/Montserrat-OFL.txt`](third_party/Montserrat-OFL.txt), además de
  la licencia MIT correspondiente al resto del código propio.
- ArduinoJson 6.21.5: MIT.
- esptool: GPL-2.0-or-later.
- Datos de autoridades certificadoras: paquete `certifi`/Mozilla. Es una lista
  de certificados CA publicos; la version usada figura en
  `firmware/certs/bundle-info.json`.

El firmware no incorpora codigo de la demo como un arbol vendorizado; usa el
perfil publico de la placa mediante las dependencias fijadas en
`firmware/platformio.ini`.
