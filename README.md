# gluco_waveshare

Reloj de sobremesa con glucosa y pronóstico para **Waveshare ESP32-S3-Touch-LCD-4.3B / 4.3B-BOX**. Versión **0.8.2** en español. Código fuente bajo licencia MIT.

> Proyecto personal no afiliado a Waveshare ni a Abbott. Utiliza LibreLinkUp, cuya API no es pública y puede cambiar. No sustituye el sensor, la aplicación oficial ni sus alarmas; verifica allí las lecturas antes de tomar decisiones de tratamiento.

## Funciones

- Glucosa, flecha de tendencia y gráfica de **8 horas** con horas reales. Toca dentro de la gráfica para destacar la medición más cercana y consultar su fecha, hora y valor. Colores: rojo <70; verde 70–180; amarillo >180–240; naranja >240 mg/dL.
- Selector de personas que comparten lecturas con la **cuenta receptora LibreLinkUp**, accesible desde la pantalla de glucosa.
- Reloj, fecha, clima actual, sensación térmica, máxima y mínima; previsión de seis horas o cuatro días.
- Configuración desde el móvil con dos QR: Wi-Fi temporal primero, servicios a través de la red doméstica después.
- Ajustes y selección del usuario guardados en NVS tras el apagado. Peticiones de clima y glucosa ordenadas, como máximo una por servicio cada dos minutos.
- Tema oscuro. En glucosa, toca la zona **por encima de la gráfica** para apagar la retroiluminación; otro toque la enciende. El engranaje abre Ajustes.
- Interfaz oscura con tarjetas azuladas, lectura destacada con acento del color
  de glucosa, umbrales punteados en la gráfica y pronóstico más legible. Se
  conservan la distribución y los botones de la versión 0.7.1.

No incluye Alexa, alarmas, Nightscout ni firmware precompilado.

## Materiales

| Elemento | Necesario | Observación |
| --- | --- | --- |
| [Waveshare ESP32-S3-Touch-LCD-4.3B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B) o **4.3B-BOX** | Sí | 800 × 480; 16 MB flash y 8 MB PSRAM. La 4.3 sin **B** es otra placa. |
| Cable USB-C **con datos** | Sí | Para grabar y alimentar. Busca el puerto rotulado **UART** según Waveshare. |
| Alimentación USB de 5 V estable | Sí | Se recomienda una fuente de 2 A para tener margen y un cable de calidad. |
| PC con Python y PlatformIO, y móvil con navegador | Sí | El Wi-Fi doméstico debe ofrecer 2,4 GHz. |
| Cuenta LibreLinkUp receptora de al menos una persona | Para glucosa | El portal de informes LibreView por sí solo no entrega estas lecturas. |
| Tarjeta microSD, conversores RS485 o CAN | No | Esta aplicación no los utiliza. |

Detalles y enlaces oficiales: [docs/MATERIALES.md](docs/MATERIALES.md).

## Instalar en Ubuntu 24.04

Extrae el ZIP o clona el repositorio. Desde la raíz del proyecto:

```bash
sudo apt update
sudo apt install -y python3 python3-venv python3-pip git
chmod +x compilar.sh
./compilar.sh
cd firmware
./.build-venv/bin/pio device list
./.build-venv/bin/pio run -t upload --upload-port /dev/ttyACM0
```

Sustituye `/dev/ttyACM0` por el puerto detectado: puede ser `/dev/ttyUSB0`. La primera compilación descarga dependencias. Si falta permiso para acceder al puerto, ejecuta `sudo usermod -aG dialout "$USER"`, cierra sesión y vuelve a entrar.

**Si ya tenías gluco_waveshare, no ejecutes `erase`:** al borrar toda la flash también desaparecen Wi-Fi, cuenta y ubicación. La tabla de particiones de 0.8.2 es la misma que la de 0.8.1. Para actualizar desde la raíz puedes usar `./actualizar.sh /dev/ttyACM0`, que solo ejecuta `upload`. Antes, haz una [copia de NVS](docs/COPIA_NVS.md) si necesitas proteger los ajustes ante un error de carga. Para Windows, pasos iniciales y solución de problemas: [docs/INSTALACION.md](docs/INSTALACION.md).

## Primer arranque

1. Escanea el **primer QR** y conecta el móvil a la red temporal que muestra la pantalla. Guarda el Wi-Fi de casa de 2,4 GHz.
2. Vuelve a conectar el móvil al Wi-Fi de casa. Cuando aparezca el **segundo QR**, escanéalo.
3. Inicia sesión con la cuenta **LibreLinkUp que recibe lecturas compartidas**, elige la persona y, opcionalmente, busca una localidad.
4. Pulsa **Guardar permanentemente y cerrar**. El portal se cierra y los ajustes quedan en NVS.

El botón **Usuario** de glucosa cambia de persona sin pasar por Ajustes. Para reabrir la configuración, toca el engranaje. El histórico se vuelve a descargar tras cada arranque.

## Contenido

| Ruta | Contenido |
| --- | --- |
| `firmware/` | Código ESP32, LVGL, particiones y configuración PlatformIO. |
| `web/` | Página de configuración que sirve la pantalla. |
| `tools/embed_web.py` | Regenera `firmware/include/web_assets.hpp` al cambiar `web/`. |
| `docs/` | Instalación, materiales, arquitectura y límites. |
| `tests/` | Prueba auxiliar del procesamiento de glucosa. |

Si modificas la web, ejecuta `python3 tools/embed_web.py` antes de compilar y sube tanto `web/` como el `web_assets.hpp` generado. Las dependencias están fijadas en `firmware/platformio.ini`.

## Subirlo a GitHub

Comprueba `git status --short` antes del commit: evita publicar volcados de flash, capturas de tu cuenta o archivos con contraseñas. El `.gitignore` excluye compilaciones y entornos virtuales. Crea primero en GitHub un repositorio vacío llamado `gluco_waveshare` y, desde la raíz local, ejecuta:

```bash
git init
git add .
git commit -m "Publicar gluco_waveshare 0.8.2"
git branch -M main
git remote add origin https://github.com/TU_USUARIO/gluco_waveshare.git
git push -u origin main
```

Sustituye `TU_USUARIO`. Si ya tienes repositorio Git, conserva su historial y omite `git init` y `git remote add origin`.

## Licencias y fuentes

Código propio bajo [MIT](LICENSE). Los glifos Montserrat y las dependencias mantienen sus licencias respectivas: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). El firmware incluye certificados CA públicos para validar HTTPS.

[Instalación](docs/INSTALACION.md) · [Materiales](docs/MATERIALES.md) · [Seguridad](docs/SEGURIDAD_Y_LIMITES.md) · [Documentación oficial](docs/FUENTES_OFICIALES.md) · [Historial](CHANGELOG.md)
