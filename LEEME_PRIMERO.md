# Empezar con gluco_waveshare 0.8.7

Este repositorio contiene el **código fuente** para la Waveshare ESP32-S3-Touch-LCD-4.3B / 4.3B-BOX. No contiene binarios compilados, credenciales ni imágenes de la demo de fábrica.

1. Consulta [materiales y compatibilidad](docs/MATERIALES.md).
2. Sigue [la instalación en Ubuntu 24.04 o Windows](docs/INSTALACION.md) para compilar con PlatformIO y subir el firmware.
3. Escanea el QR temporal y guarda el Wi-Fi de **2,4 GHz**. Después, desde el mismo Wi-Fi doméstico, escanea el segundo QR e inicia sesión en la cuenta **receptora LibreLinkUp**.
4. Elige una persona y pulsa **Guardar permanentemente y cerrar**. Opcionalmente, busca tu ciudad para el clima.
5. Usa **Usuario** en glucosa para cambiar de persona. El engranaje abre el QR de ajustes; en la pestaña Wi-Fi del segundo portal puedes añadir redes adicionales.

**Actualizaciones:** si ya tienes gluco_waveshare, utiliza `./actualizar.sh /dev/ttyACM0` para respaldar NVS y cargar sin borrarla; `erase` borra NVS y requiere volver a introducir Wi-Fi, credenciales y usuario. El primer borrado solo se describe para una instalación limpia desde otra imagen o tabla de particiones.

La pantalla muestra glucosa y su historial de hasta diez horas, reloj y pronóstico. No incluye alarmas ni sustituye la aplicación o el receptor oficiales. [README](README.md) · [Seguridad y límites](docs/SEGURIDAD_Y_LIMITES.md) · [Licencia MIT](LICENSE).
