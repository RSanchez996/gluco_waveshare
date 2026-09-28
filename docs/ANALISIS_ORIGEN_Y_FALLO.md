# Analisis del origen y del reinicio anterior

## Imagen de fabrica aportada

La copia `waveshare_factory_backup.bin` es una imagen completa de 16 MiB con
bootloader, tabla de particiones y aplicacion validos. La aplicacion de fabrica
se identifica como `rtc_pcf85063`, fue compilada con ESP-IDF 5.5.4 y contiene
los componentes esperados para RGB, GT911 y LVGL. Eso confirma que el hardware
funciona con la rama 5.5 de ESP-IDF y que la geometria correcta es N16R8.

SHA-256 de la copia analizada:

```text
0b6151af8660a54e071a188a1de7d4313207d3f089fa2122e0a17e31baa31b31
```

La imagen de fabrica se conserva fuera de este paquete y no se redistribuye.

## Demo oficial sin compilar

Se reviso el codigo fuente del ZIP oficial
`ESP32-S3-Touch-LCD-4.3B-BOX-Demo`, no solo sus binarios. La referencia directa
para esta reconstruccion es `Arduino/examples/09_lvgl_Porting`, junto con el
perfil `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3_B` de
`ESP32_Display_Panel`. El perfil define ST7262, RGB 800 x 480, GT911, CH422G,
16 MB QIO y 8 MB OPI PSRAM.

## Causa mas probable del reinicio observado

El registro anterior alcanzaba `esp_phy_enable` al iniciar Wi-Fi y comunicaba
acceso a una region cacheada con cache desactivada. Ocurria con
Arduino-ESP32 3.1.1 / ESP-IDF 5.3.2, despues de que LCD y GT911 ya hubieran
arrancado. Por tanto el fallo no apuntaba al panel ni al tactil, sino a la
combinacion de Wi-Fi, cache y PSRAM de ese nucleo.

Tambien aparecian avisos de NVS inexistente y ausencia de particion de core
dump. El primer aviso es normal en una configuracion vacia; el segundo no era la
causa del fallo, solo impedia guardar un volcado posterior.

## Correccion aplicada

- Arduino-ESP32 3.3.12 y bibliotecas ESP-IDF 5.5.5.
- Inicializacion y dibujo de diagnostico antes de activar Wi-Fi.
- Un solo framebuffer RGB y buffers LVGL internos pequeños.
- Documentos JSON y respuestas HTTP grandes en PSRAM.
- Borrado completo una sola vez al migrar desde la imagen de fabrica o desde
  una tabla de particiones incompatible; las actualizaciones normales conservan NVS.
- Contador de reinicios y modo seguro con mensaje persistente.
- Fases de arranque numeradas por serie a 115200 baudios.

La compilacion correcta reduce mucho la probabilidad del fallo visto, pero la
confirmacion definitiva exige una prueba en la placa fisica.
