# Materiales y compatibilidad

## Equipo necesario

| Cantidad | Material | Especificación |
| ---: | --- | --- |
| 1 | Pantalla Waveshare | **ESP32-S3-Touch-LCD-4.3B** (SKU 27848) o **4.3B-BOX** (SKU 28141); LCD y táctil integrados. La versión BOX añade la carcasa. |
| 1 | Cable USB-C **de datos** | Para conectarla al ordenador por el puerto marcado **UART**. Un cable solo de carga no permite grabarla. |
| 1 | Alimentador USB | 5 V estables para uso permanente. Se recomienda que pueda entregar 2 A como margen, especialmente si hubo reinicios o fallos de Wi-Fi. |
| 1 | Ordenador | Ubuntu 24.04 o Windows, Python 3 y conexión a Internet para obtener PlatformIO y las bibliotecas. |
| 1 | Teléfono con navegador | Para escanear los QR y configurar los servicios. |
| 1 | Red doméstica de 2,4 GHz | El ESP32-S3 no se conecta a redes exclusivas de 5 GHz. |
| 1 | Cuenta LibreLinkUp receptora | Necesaria para ver glucosa; debe recibir las lecturas de alguien que haya habilitado compartir en la aplicación oficial. |

La ficha de la [Waveshare 4.3B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B) indica LCD de 800 × 480, **16 MB flash**, **8 MB PSRAM**, Wi-Fi 2,4 GHz, alimentación **USB-C 5 V** y un consumo típico declarado de **5 V / 450 mA**. La recomendación de una fuente con margen de 2 A es una medida práctica de estabilidad, **no** un requisito de consumo continuo de 2 A.

No hace falta cablear sensores ni añadir módulo de glucosa a la placa: las lecturas llegan mediante Wi-Fi desde LibreLinkUp. Tampoco se requieren microSD, módulo CAN, RS485, batería, RTC externo ni soldadura. La zona de antena de la placa debe quedar despejada; Waveshare desaconseja cubrirla con metal o plástico.

## Identificar el modelo antes de compilar

El proyecto se configuró para el perfil de biblioteca `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3_B`. Comprueba que el nombre **4.3B** figura en la placa o en la caja. Una 4.3 o una 4.3C puede requerir otro perfil, conexiones de pantalla y ajustes de PSRAM. El firmware se detiene si no detecta los 8 MB de PSRAM que espera.

Waveshare publica [el producto y sus especificaciones](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B), [el esquema y la demo original](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B/Resources-And-Documents) y [el uso del puerto de grabación](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B/Instructions-For-Use). El nombre del puerto en el PC depende del sistema: `/dev/ttyACM0`, `/dev/ttyUSB0` o `COMx` son ejemplos, no valores fijos.

## Montaje mínimo

1. Coloca la pantalla sobre una superficie estable, deja libre la antena y conecta un cable de datos al puerto USB-C correspondiente.
2. Graba el programa siguiendo [INSTALACION.md](INSTALACION.md).
3. Para uso permanente, alimenta el equipo con una fuente USB adecuada sin cambiar el cable a mitad de una grabación.

La pantalla se puede utilizar sin datos climáticos; para habilitarlos busca una localidad durante la configuración. No hay que instalar ningún sensor adicional para el reloj ni para el pronóstico.
