# gluco_waveshare

Reloj de sobremesa con glucosa y pronóstico para **Waveshare ESP32-S3-Touch-LCD-4.3B / 4.3B-BOX**. Versión **1.0** en español. Código fuente bajo licencia MIT.

> Proyecto personal no afiliado a Waveshare ni a Abbott. Utiliza LibreLinkUp, cuya API no es pública y puede cambiar. No sustituye el sensor, la aplicación oficial ni sus alarmas; verifica allí las lecturas antes de tomar decisiones de tratamiento.

## Funciones

- Glucosa actual, flecha de tendencia y **hasta 12 horas** de histórico de LibreLinkUp. Cada descarga reemplaza la gráfica anterior en RAM para reflejar los valores devueltos por el servicio. La curva visual pasa exactamente por sus muestras y no genera extremos adicionales; una línea punteada enlaza la última muestra histórica y la lectura actual cuando están separadas menos de 30 minutos. El eje horizontal marca las horas en punto cada tres horas y se desplaza con el tiempo. Toca la gráfica para consultar la hora y el valor de una muestra; la marca se borra al apagar la pantalla o al recibir otra lectura. Colores: rojo <70; verde 70–180; amarillo >180–240; naranja >240 mg/dL.
- Selector de personas que comparten lecturas con la **cuenta receptora LibreLinkUp**, accesible desde la pantalla de glucosa.
- Reloj, fecha, clima actual, sensación térmica, máxima y mínima; previsión de seis horas o cuatro días.
- Configuración principal desde **Ajustes de la pantalla**, con teclado táctil: redes cercanas y guardadas, SSID manual para redes ocultas, ubicación y cuenta receptora LibreLinkUp con selección de persona. QR y móvil como alternativa.
- Ajustes, hasta cinco redes Wi-Fi y selección del usuario guardados en NVS tras el apagado. Glucosa cada dos minutos, con un único reintento espaciado ante fallo transitorio; clima cada 30 minutos. Una sola petición HTTPS a la vez.
- La línea de estado principal muestra solo «Leyendo glucosa...» o la fecha y hora de la última lectura válida. Si todavía no hay ninguna muestra, muestra «Sin lectura válida».
- Tema oscuro con tarjetas azuladas, lectura destacada con el color de glucosa y umbrales punteados en la gráfica. En glucosa, toca la zona **por encima de la gráfica** para apagar la retroiluminación; otro toque la enciende. El engranaje abre Ajustes.
- El repintado de LVGL está limitado a **10 Hz** para reducir el trabajo de la interfaz mientras se usa Wi-Fi. El barrido eléctrico del panel RGB conserva su frecuencia de funcionamiento.
- El panel RGB trabaja a 12 MHz con el perfil de inicialización Waveshare y buffers de rebote de veinte líneas. Al despertar resincroniza el barrido antes de encender la retroiluminación.

No incluye Alexa, alarmas, Nightscout ni firmware precompilado.

## Materiales

| Elemento | Necesario | Observación |
| --- | --- | --- |
| [Waveshare ESP32-S3-Touch-LCD-4.3B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B) o **4.3B-BOX** | Sí | 800 × 480; 16 MB flash y 8 MB PSRAM. La 4.3 sin **B** es otra placa. |
| Cable USB-C **con datos** | Sí | Para grabar y alimentar. Busca el puerto rotulado **UART** según Waveshare. |
| Alimentación USB de 5 V estable | Sí | Se recomienda una fuente de 2 A para tener margen y un cable de calidad. |
| PC con Ubuntu 24.04, Python 3 y acceso a Internet | Para compilar | El script instala PlatformIO y esptool en un entorno virtual propio. |
| Wi-Fi doméstico de 2,4 GHz | Sí | El ESP32-S3 debe poder acceder a Internet para LibreLinkUp y el clima. |
| Móvil con navegador | Opcional | Solo para configurar con QR. |
| Cuenta LibreLinkUp receptora de al menos una persona | Para glucosa | El portal de informes LibreView por sí solo no entrega estas lecturas. |
| Tarjeta microSD, conversores RS485 o CAN | No | Esta aplicación no los utiliza. |

Detalles y enlaces oficiales: [docs/MATERIALES.md](docs/MATERIALES.md).

## Compilación e instalación en Ubuntu 24.04

Extrae este ZIP y abre una terminal **en la carpeta `gluco_waveshare`** que contiene `compilar.sh` y `firmware/`. Instala las herramientas del sistema:

```bash
sudo apt update
sudo apt install -y python3 python3-venv python3-pip git
```

Compila el firmware. La primera vez se descargan PlatformIO, esptool y las dependencias fijadas en `firmware/platformio.ini`:

```bash
chmod +x compilar.sh actualizar.sh
./compilar.sh
```

Conecta un cable USB-C **de datos** al puerto UART de la pantalla. Averigua el puerto real desde la misma carpeta:

```bash
cd firmware
./.build-venv/bin/pio device list
```

Si el puerto es `/dev/ttyACM0`, carga la instalación inicial con:

```bash
./.build-venv/bin/pio run -t upload --upload-port /dev/ttyACM0
```

Sustituye `/dev/ttyACM0` por el puerto encontrado; también puede aparecer como `/dev/ttyUSB0`. Para comprobarlo tras conectar el cable, `ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null` puede ayudarte. Si falta permiso, ejecuta `sudo usermod -aG dialout "$USER"`, **cierra la sesión de Ubuntu y vuelve a entrar** antes de repetir la carga. No hace falta abrir el monitor serie para usar la aplicación.

**Para actualizar conservando ajustes**, vuelve a la raíz del proyecto y usa `./actualizar.sh /dev/ttyACM0`. El script copia primero los 393 216 bytes de la partición NVS a `nvs_backups/` y cancela la carga si no consigue verificar el tamaño. Guarda esa copia en un lugar privado: contiene las contraseñas. No ejecutes `erase_flash` ni una instalación limpia si quieres conservar Wi-Fi, cuenta y ubicación. La tabla de particiones está en `firmware/partitions.csv`. Hay información ampliada en [docs/INSTALACION.md](docs/INSTALACION.md) y [docs/COPIA_NVS.md](docs/COPIA_NVS.md).

## Ajustes desde la pantalla

Toca el engranaje. En **Redes Wi-Fi**, la lista muestra las redes cercanas con su señal y si ya están guardadas, y otra lista muestra las redes guardadas con la conexión actual. Elige una red, escribe su clave y pulsa **Guardar y conectar** o **Guardar para después**. **Escribir SSID** permite registrar una red oculta. Puedes eliminar una red guardada; para eliminar la principal debe quedar otra disponible. Se admiten la red principal y hasta cuatro adicionales.

En **Ubicación y clima**, escribe una localidad, toca **Buscar** y selecciona un resultado para guardar nombre, coordenadas y zona horaria. En **Cuenta LibreLinkUp**, escribe el correo y la contraseña de la cuenta receptora, selecciona la región e inicia sesión. Después, **Gestionar usuarios** permite elegir la persona que quieres mostrar. Las contraseñas guardadas pueden conservarse dejando su campo vacío. El teclado tiene filas QWERTY en español, con ñ; toca **1#** para las vocales acentuadas, números y símbolos. En esa página, **Más** abre otra con `*`, `+`, barra invertida, corchetes y otros signos; **Volver** regresa a las tildes. Una raya azul señala dónde se escribe y el botón superior muestra u oculta el teclado. Cierra Ajustes con la **X** de la esquina superior derecha. Todos los guardados se realizan fuera del hilo de la interfaz.

**Configurar con QR** abre el portal solo mientras se muestra el QR. Si aún no hay Wi-Fi, muestra la red temporal para configurar la conexión; después permite el segundo QR en la red doméstica. Al cerrar o guardar deja de servir la web. El portal local usa `http://` y el ESP32 consulta los servicios externos mediante HTTPS.

Las redes cercanas se buscan solo al entrar en esa pantalla o al pulsar **Buscar**; no hay escaneos periódicos durante las consultas ni cambios de red mientras la conexión funcione. Si se pierde, primero se deja actuar a la reconexión automática; luego se prueban las redes adicionales y la principal de una en una, cada 30 segundos. Una red con buena señal podría no tener acceso a Internet.

Con un usuario ya guardado, el primer trabajo LibreLinkUp es la gráfica; la lista de usuarios se actualiza después. La tarea conserva CPU1 y prioridad 0 para proteger la interfaz y las tareas del sistema. No se ofrece un brillo por software: para eliminar la luz nocturna, apaga la retroiluminación tocando la zona superior de la pantalla de glucosa.

## Primer arranque

1. En **Ajustes → Redes Wi-Fi**, selecciona el Wi-Fi doméstico de 2,4 GHz, escribe su clave y guarda para conectar. También puedes escribir el SSID a mano.
2. Cuando se conecte, abre **Cuenta LibreLinkUp** e inicia sesión con la cuenta **receptora** de lecturas compartidas. Elige la persona en **Ajustes → Gestionar usuarios**.
3. Opcionalmente, busca y guarda una localidad en **Ubicación y clima**. Vuelve a **Glucosa**.

Para hacerlo con el móvil, toca **Configurar con QR**. El primer QR crea la red temporal cuando no hay Wi-Fi; el segundo permite configurar los servicios desde la red doméstica. El portal se cierra al guardar o al salir.

Para cambiar de persona, entra en **Ajustes → Gestionar usuarios**. La elección se guarda y regresa a glucosa. El histórico se vuelve a descargar tras cada arranque. Si LibreLinkUp entrega una lectura actual pero el histórico está vacío, se conserva temporalmente la última curva válida. La gráfica muestra los datos que proporcione LibreLinkUp, que pueden cubrir menos de 12 horas; las interrupciones largas se dejan sin unir.

## Si algo no conecta

- **No aparece el puerto:** comprueba el cable de datos, el conector UART y el resultado de `./.build-venv/bin/pio device list`. Cambiar de puerto USB puede cambiar `/dev/ttyACM0` por otro nombre.
- **Wi-Fi sin conectar:** usa una red de 2,4 GHz y revisa la contraseña. Ajustes muestra redes guardadas y cercanas; busca otra vez solo cuando lo necesites.
- **Sin lecturas:** comprueba que la cuenta introducida sea la *receptora LibreLinkUp* y que en la aplicación oficial vea al usuario compartido. Revisa también que el móvil que recibe el sensor tenga Internet. La pantalla muestra la hora de la última lectura válida; tras cinco minutos sin una nueva, la lectura se atenúa.
- **Falló el inicio de sesión o la localidad:** revisa el mensaje en la pantalla de ajustes correspondiente. El firmware realiza HTTPS en una tarea distinta del bucle de la pantalla; espera a que termine antes de repetir.
- **WDT:** si aparece un reinicio, el portal muestra la fase en curso en el momento del fallo cuando puede conservarla. Una fase por sí sola no identifica qué tarea disparó el watchdog. El login y la búsqueda de localidad corren en CPU1 con prioridad 0, igual que las lecturas periódicas; se han reducido los JSON del login, separado las peticiones y borrado la marca de fase al concluir. Falta comprobar en la placa si desaparece el reinicio al iniciar sesión.
- **Imagen desplazada:** el LCD RGB lee continuamente un framebuffer en PSRAM, incluso con la luz apagada. Revisa [la explicación del panel](docs/ARQUITECTURA.md) si ocurre tras HTTPS o al despertar. La resincronización se pide al terminar las operaciones de red y antes de volver a iluminar la pantalla; todavía necesita una prueba prolongada en la placa.
- **No puedes usar el portal del QR:** confirma que sigue visible. Para el segundo QR, el móvil y la pantalla deben estar en la misma red. El servidor se cierra al salir de esa vista.

## Contenido

| Ruta | Contenido |
| --- | --- |
| `firmware/` | Código ESP32, LVGL, particiones y configuración PlatformIO. |
| `web/` | Página de configuración que sirve la pantalla. |
| `tools/embed_web.py` | Regenera `firmware/include/web_assets.hpp` al cambiar `web/`. |
| `docs/` | Instalación, materiales, arquitectura y límites. |
| `tests/` | Prueba auxiliar del procesamiento de glucosa. |

Si modificas la web, ejecuta `python3 tools/embed_web.py` antes de compilar y conserva tanto `web/` como el `web_assets.hpp` generado. Las dependencias están fijadas en `firmware/platformio.ini`.

## Licencias y fuentes

Código propio bajo [MIT](LICENSE). Los glifos Montserrat y las dependencias mantienen sus licencias respectivas: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). El firmware incluye certificados CA públicos para validar HTTPS.

[Instalación](docs/INSTALACION.md) · [Materiales](docs/MATERIALES.md) · [Seguridad](docs/SEGURIDAD_Y_LIMITES.md) · [Documentación oficial](docs/FUENTES_OFICIALES.md)
