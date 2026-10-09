# Revisión técnica y validación — 2.0.0

Fecha: 2026-10-08. Base: `RSanchez996/gluco_waveshare`, commit `8f66481796cffacfc3bd8f94aa7a576ceae8dc33`.

## Hallazgos y decisiones

En la base revisada, GUI y consultas estaban en CPU1, `web.handleClient()` se ejecutaba en el bucle de pantalla, y ajustes con `String` mutables se compartían sin un propietario único. El framebuffer único podía modificarse mientras se barría. Los reinicios RGB después de consultas o escrituras podían recuperar el aspecto sin eliminar el origen del problema. La asignación de un core distinto por sí sola tampoco elimina la competencia sobre el bus de PSRAM.

Se ha reescrito el arranque, el backend de ajustes, HTTP, portal y entrega de píxeles. Se mantiene la lógica de lectura de LibreLinkUp y la UI táctil, revisándolas para snapshots, bounds, datos de error visibles, caché de usuarios y el nuevo tamaño de gráfica. Las dependencias LVGL y Arduino son código C/C++ de terceros conservado con su licencia.

## Reparto de trabajo

| Trabajo | Core | Prioridad | Pila reservada | Propietario |
| --- | --- | --- | --- | --- |
| LVGL, táctil, iluminación y render | 1 | 2 | 12.288 B internos | `gui` |
| Inicialización/ISR del RGB | 1 | interrupción | Gestionada por IDF | Driver LCD, iniciado en `gui` |
| LibreLinkUp, clima, escaneo, Wi-Fi y NVS | 0 | 0 | 20.480 B internos | `data` |
| Formularios y respuestas del portal | 0 | 1 | 8.192 B internos | `esp_http_server` |
| Wi-Fi, TCP/IP y eventos | SDK | SDK | SDK | ESP-IDF |

El trabajador ejecuta primero una operación encolada, después el control del portal y finalmente las consultas periódicas. Las transiciones AP/STA ocurren en ese mismo trabajador; no existe una tarea paralela que cambie la radio durante TLS. Las peticiones del portal solo encolan trabajo. La GUI publica órdenes mediante atomics/colas y jamás llama al cliente HTTP ni guarda NVS.

Los dos idle y `gui` siguen vigilados. Prioridad 0 en `data` permite que idle comparta CPU incluso durante operaciones criptográficas; las lecturas y el lector JSON ceden explícitamente. El timeout GUI/idle de 8 s no se usa como timeout de red. HTTPS usa presupuesto cooperativo de 30 s, timeout de conexión de 12 s y de lectura de 2 s. Una operación de transporte en curso puede terminar después del presupuesto; se cancela al recuperar el control. El login admite como máximo cuatro redirecciones declaradas de región; la renovación por token caducado está acotada.

## Pantalla y memoria

| Recurso | Ubicación | Tamaño/configuración |
| --- | --- | --- |
| Framebuffers RGB | PSRAM | 2 × 800 × 480 × 2 = 1.536.000 B |
| Bounce buffers RGB | SRAM interna/DMA | 2 × 800 × 20 × 2 = 64.000 B |
| Buffer parcial LVGL | SRAM interna | 800 × 12 × 2 = 19.200 B |
| Cuerpo HTTP reutilizable | PSRAM | 524.289 B; límite variable por endpoint |
| JSON de gráfica | PSRAM | 72 KiB, filtro de campos |
| Instrucciones/rodata XIP | PSRAM | Reubicación realizada por el SDK |
| TLS, caché NVS y buffers de red permitidos | PSRAM | Asignadores del SDK |
| Reserva de malloc interno | Interna | 131.072 B; no es memoria adicional |

El buffer de 20 líneas divide exactamente el fotograma en 24 bloques, número par. LVGL renderiza en SRAM y copia cada tile al framebuffer trasero. Al terminar un refresco solicita el nuevo framebuffer al driver y espera `on_frame_buf_complete`. En IDF 5.5.5, el callback en modo bounce se ejecuta tras copiar el último bloque y cambiar `bb_fb_index`; ya no se lee el framebuffer liberado desde PSRAM. Entonces se copian al liberado únicamente los spans de filas dañadas para que ambos mantengan el mismo contenido. No se escribe simultáneamente sobre el framebuffer usado para alimentar los bounce buffers.

El callback y la implementación de `__atomic_fetch_add_4` están en IRAM en el mapa enlazado. El driver 5.5.5 detecta recuentos EOF incompletos y solicita recuperación en VSYNC. `CONFIG_LCD_RGB_RESTART_IN_VSYNC` queda desactivado para no forzar un reinicio cada fotograma. La aplicación tiene una petición explícita de recuperación, pero no la dispara después de cada HTTPS o guardado.

Perfil de placa conservado: HSYNC GPIO46, VSYNC GPIO3, DE GPIO5, PCLK GPIO7; D0–D15: 14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40. I²C GT911/CH422G: SDA8, SCL9; interrupción táctil GPIO4. Se conservan las secuencias de reset/iluminación del perfil oficial. Temporizaciones horizontal/vertical 4/8/8 y flanco negativo de PCLK. La frecuencia se ajusta a 12 MHz, valor que ya utilizaba el proyecto original; el perfil de biblioteca propone 16 MHz.

Las optimizaciones Wi-Fi que ubican código adicional en IRAM se desactivan para recuperar SRAM. Los buffers estáticos RX/TX se fijan en 8 cada uno, RX dinámicos en 16 y caché TX en 16. Esta aplicación prioriza memoria disponible y estabilidad del RGB sobre máximo throughput de Wi-Fi. Estas decisiones necesitan comprobación de margen real en placa: el mapa de enlace no representa las asignaciones dinámicas durante TLS.

## Datos y configuración

Cada lector recibe una copia de `Config` bajo mutex. Solo el trabajador persiste los ajustes después del arranque. La cola de operaciones admite un trabajo pendiente y rechaza nuevos trabajos mientras está ocupado. Los resultados de escaneo/login/geocodificación son estructuras acotadas con mutex independientes.

NVS usa el namespace y clave originales `glucowave/settings`, además de la caché compatible `glucousers`. No se borra silenciosamente una NVS inaccesible o inválida. Las lecturas de glucosa e históricos permanecen en RAM. Las escrituras de caché de usuarios se producen solo cuando cambia la lista. La etapa anterior al reset se conserva en RTC RAM, no mediante escrituras periódicas a flash.

Los históricos se acotan a 160 muestras/12 h. El ordenamiento estable en el propio array evita una asignación de heap y mantiene la última corrección cuando el proveedor repite una fecha. `ValueInMgPerDl` tiene prioridad; el fallback `Value` solo se acepta si declara unidades mg/dL. No se trata un valor mmol/L como mg/dL. `FactoryTimestamp` se interpreta en UTC, admitiendo el formato de LibreLinkUp y fechas ISO con zona explícita. Las muestras inválidas/futuras se rechazan. Un error de consulta conserva la última lectura con su fecha, se muestra en la banda de estado y el dato pasa a color atenuado cuando está caducado.

El portal se abre expresamente y se cierra tras guardar o después de 10 min sin actividad. Hay límites de sockets/formulario/tiempo, token de formulario e interfaz AP/STA comprobada. Devuelve si existe una contraseña guardada, nunca la contraseña. Guarda de forma asíncrona, permitiendo seguir respondiendo al navegador. Se validan longitudes, regiones, redes repetidas y coordenadas. Los binarios mantienen TLS con comprobación de certificados y esperan hora NTP; no contienen credenciales.

## Pruebas ejecutadas

- Build y enlace real con Xtensa GCC 14.2.0 y ESP-IDF 5.5.5 para `esp32s3`. Comprobaciones del tamaño de bootloader y aplicación superadas. Fuentes LVGL y SDK realmente recompiladas, sin enlazar un SDK Arduino precompilado.
- `tools/test_all.py --sanitize`: pruebas de umbrales, caducidad, UTC, fechas no válidas, unidades, gaps, posiciones, rollover de `millis`, orden estable y 1.000 históricos aleatorios dañados. ASan y UBSan activados; LeakSanitizer desactivado por la restricción del entorno de ejecución, sin afirmar análisis de fugas de larga duración.
- `providers_test.cpp` ejecuta **el providers.cpp de producción** y ArduinoJson con transporte/Preferences simulados: filtros, mg/dL frente a mmol/L, duplicados, conservación ante 503/datos inválidos, 401 y renovación acotada, rechazo de login, redirección de región, lista/caché de usuarios y rechazo de caché sin terminadores. No verifica TLS real ni el estado actual de una cuenta.
- Diez pruebas del instalador: hashes de las imágenes incluidas, checksum/cifrado de particiones, mapa compatible, mapa distinto, respaldo truncado, placa vacía, tabla vacía con NVS existente, borrado solicitado y capacidad de flash incorrecta. Se comprueba que los caminos de error no escriben ni borran flash.
- Sintaxis JavaScript del portal y scripts de shell comprobada.
- `tools/render_ui.py`: interfaz de producción y LVGL 8.4.0 compilados en host, fuentes comprimidas españolas verificadas y pantallas Home/Ajustes renderizadas a 800×480. Las vistas son datos de ejemplo, no una fotografía de hardware. Dependencias de esta prueba opcional: gcc/g++, ninja, Python y Pillow.

`docs/build-summary.txt` contiene tamaños de enlace y hashes del binario entregado. No se ha ejecutado una prueba de carga sobre hardware. Tampoco se ha conectado una cuenta real LibreLinkUp; el servicio es una API privada sin contrato estable público.

## Aceptación sobre hardware

| Prueba | Resultado esperado |
| --- | --- |
| 24–48 h de consultas y reloj | Sin desplazamiento RGB, línea intermitente, panic ni watchdog |
| Abrir QR mientras hay HTTPS | Interfaz táctil sigue respondiendo; portal espera al trabajador antes de cambiar radio |
| Guardar ajustes con RGB activo | Persisten al reiniciar; no hay desincronización durante NVS |
| Cambio repetido de usuario | Se vacía la lectura anterior antes de publicar la nueva |
| Router apagado/encendido | Estado de error visible, dato antiguo atenuado, reconexión y consulta al recuperarse |
| Credenciales incorrectas/HTTP 429 | Error comprensible y reintentos espaciados, sin bucle de login |
| Dormir/despertar y navegar | Primer toque despierta sin activar accidentalmente un botón |
| Logs `[HEALTH]` durante todas las pruebas | Heap y mayor bloque se estabilizan; margen GUI/data preferiblemente superior a 2.048 B |

Se incluye `bin/gluco_waveshare.elf` para resolver direcciones de un backtrace con `xtensa-esp32s3-elf-addr2line -pfiaC -e bin/gluco_waveshare.elf DIRECCION`, desde el entorno ESP-IDF.

Si alguna prueba falla, conserva el log serie completo desde el arranque, el modelo/revisión exacto de placa y el mensaje `[HEALTH]` anterior al fallo. Si el margen de pila cae por debajo de 2 KiB o el mayor bloque interno cae repetidamente por debajo de 16 KiB, revisa el reparto con esos datos; no desactives el watchdog ni subas PCLK para ocultar el problema. Usa una alimentación y cable adecuados si aparece un reset `Tensión baja`.

## Fuentes primarias consultadas

- [Waveshare: ESP32-S3-Touch-LCD-4.3B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B): hardware, controladores, memoria y entorno.
- [Waveshare: ESP-IDF](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B/ESP-IDF): entorno y ejemplo del fabricante.
- [Waveshare: recursos y documentos](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B/Resources-And-Documents): referencias al esquema y demos. Se revisó la documentación web y el perfil de placa de la biblioteca; no se afirma haber ejecutado el demo de fábrica.
- [Espressif: RGB LCD, IDF 5.5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html): bounce, PSRAM, XIP, varios framebuffers y reinicio en VSYNC.
- [Arduino como componente ESP-IDF](https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html).
- Fuentes fijadas de `ESP32_Display_Panel/docs/envs/use_with_idf.md` y `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3_B.h`.
- Fuente exacta `esp-idf/components/esp_lcd/rgb/esp_lcd_panel_rgb.c` v5.5.5: intercambio/EOF y condiciones de liberación del framebuffer.
- Kconfig del SDK para PSRAM, mbedTLS, NVS, Wi-Fi y watchdog; guardas de `sdk_guard.hpp` verificadas en la compilación.
