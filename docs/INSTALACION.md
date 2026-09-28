# Compilar, grabar y configurar

Se entrega **código fuente**, sin binarios precompilados. Se ha preparado para la **Waveshare ESP32-S3-Touch-LCD-4.3B / 4.3B-BOX**; consulta primero [MATERIALES.md](MATERIALES.md). El proyecto usa PlatformIO con Arduino-ESP32, LVGL 8.4 y versiones fijadas en `firmware/platformio.ini`. La primera compilación necesita Internet; después, el dispositivo necesita una red Wi-Fi de 2,4 GHz con salida a Internet para LibreLinkUp y Open-Meteo.

## Ubuntu 24.04

Desde la raíz del proyecto:

```bash
sudo apt update
sudo apt install -y python3 python3-venv python3-pip git
chmod +x compilar.sh
./compilar.sh
```

El script crea `firmware/.build-venv/` e instala PlatformIO Core `>=6.2.0` ahí. Usa **siempre** `firmware/.build-venv/bin/pio` al grabar, para evitar otro PlatformIO instalado en el sistema. La descarga inicial de la plataforma y dependencias puede llevar varios minutos.

Conecta el **puerto USB-C UART** indicado por Waveshare usando un cable de datos. Averigua el puerto real:

```bash
cd firmware
./.build-venv/bin/pio device list
```

Sustituye `/dev/ttyACM0` si `device list` muestra `/dev/ttyUSB0` u otro puerto. Para instalar o actualizar, el comando habitual es **solo**:

```bash
./.build-venv/bin/pio run -t upload --upload-port /dev/ttyACM0
```

**No uses `erase` para actualizar:** destruye el Wi-Fi, las credenciales y cualquier otra información guardada. Si la placa ya tiene datos configurados, haz antes una [copia de NVS](COPIA_NVS.md). Esta versión conserva las particiones de 0.8.1. Solo si decides hacer una instalación completamente limpia desde una tabla incompatible y aceptas perder los datos se ejecuta el borrado total, antes del `upload`:

```bash
./.build-venv/bin/pio run -t erase --upload-port /dev/ttyACM0
```

Si aparece `Permission denied` para el puerto serie:

```bash
sudo usermod -aG dialout "$USER"
```

Cierra la sesión de Ubuntu y vuelve a entrar antes de repetir la carga. Comprueba también que otro programa no tiene abierto el mismo puerto. Si la placa no entra en modo grabación, Waveshare indica que se mantenga pulsado **BOOT** al encenderla; otra opción es mantener BOOT, pulsar **RESET**, soltar RESET y luego BOOT una vez comience la carga.

El monitor serie es **opcional**; si quieres ver el arranque, utiliza el puerto correcto y `115200`:

```bash
./.build-venv/bin/pio device monitor --port /dev/ttyACM0 --baud 115200
```

## Windows

Instala Python 3 con `py` disponible y Git. Abre PowerShell o CMD en la raíz del proyecto y ejecuta `COMPILAR_WINDOWS.bat`, que instala PlatformIO para ese mismo Python y compila `firmware/`. Conecta la placa con un USB **de datos** en el puerto adecuado. Anota el `COMx` que aparezca en el Administrador de dispositivos:

```bat
py -m platformio device list
py -m platformio run -d firmware -t upload --upload-port COM7
```

Sustituye `COM7` por tu puerto. Si es una instalación desde otra tabla de particiones y aceptas perder todos los datos, haz **antes**:

```bat
py -m platformio run -d firmware -t erase --upload-port COM7
```

Para futuras actualizaciones utiliza solo `upload`. No mezcles en un mismo proyecto instalaciones diferentes de PlatformIO cuando aparezca un aviso de `Obsolete PIO Core`.

## Configuración mediante el móvil

1. Primer QR: conecta el teléfono a la red temporal `GlucoWave-XXXX` que se muestra en pantalla. Abre `http://192.168.4.1` si el portal no se abre automáticamente. Escribe el SSID y la clave del Wi-Fi doméstico de **2,4 GHz** y pulsa **Guardar Wi-Fi y continuar**.
2. Espera a que la pantalla se conecte; vuelve a conectar el móvil al **mismo Wi-Fi doméstico**. Escanea el segundo QR con la dirección local mostrada. No hace falta HTTPS entre el teléfono y el dispositivo: el portal local usa HTTP; el ESP32 usa HTTPS hacia los proveedores.
3. Inicia sesión con la **cuenta receptora LibreLinkUp** (la que ve lecturas compartidas), selecciona la persona y, si quieres el clima, busca una localidad. El uso del portal de informes LibreView por sí solo no proporciona los datos de esta integración.
4. Pulsa **Guardar permanentemente y cerrar**. El portal se detiene; la configuración queda en NVS incluso tras apagar el dispositivo.

Después puedes cambiar de persona desde el botón **Usuario** de glucosa. Para cambiar Wi-Fi, ubicación o credenciales, toca el engranaje y vuelve a abrir el segundo QR. No es necesario borrar la flash para reconfigurarla.

## Comprobaciones y errores frecuentes

- Si falta clima, añade localidad en Ajustes y espera a que terminen las peticiones en serie. Wi-Fi debe tener salida a Internet. Si falta glucosa, confirma en la aplicación oficial que **esa cuenta receptora** ve a la persona que quieres.
- Durante una petición de inicio de sesión, el portal puede tardar. Si falla, mientras el QR esté visible, abre `http://IP_DE_LA_PANTALLA/api/libre/status` desde un equipo de la misma red y consulta el campo `message`. No compartas contraseñas, tokens, volcados de flash ni capturas del formulario.
- Si el móvil indica `Failed to fetch`, verifica que siga en el mismo Wi-Fi y que el QR/IP no hayan cambiado. El portal se cierra al guardar, al cerrarlo en la pantalla o tras diez minutos; con el portal cerrado es normal que HTTP no responda.
- Si hay parpadeos o reinicios, verifica una alimentación USB estable, un buen cable y que el Wi-Fi tenga cobertura. Si aparece `MODO SEGURO`, anota la fase mostrada. El monitor serie es opcional.
- Si no se ven caracteres españoles, comprueba que el proyecto utilice las cinco fuentes `gluco_font_*.c` y `LV_USE_FONT_COMPRESSED` del `lv_conf.h` proporcionado.
- El fichero `firmware/partitions.csv` reserva `0x680000` bytes por partición de aplicación (6.815.744 bytes). La cifra de 1,8 MB para un binario cabe en esa partición.

Si modificas `web/index.html`, `web/style.css` o `web/app.js`, desde la raíz ejecuta `python3 tools/embed_web.py` antes de compilar. El fichero generado `firmware/include/web_assets.hpp` debe viajar con el código del portal.

## Aviso de uso

LibreLinkUp no ofrece una API pública documentada para esta integración. La disponibilidad, región y autenticación pueden cambiar. El proyecto no implementa alertas médicas; confirma siempre las medidas y alarmas en el sistema oficial.
