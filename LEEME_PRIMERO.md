# Empezar con gluco_waveshare 1.0

Este repositorio contiene el **código fuente** para la Waveshare ESP32-S3-Touch-LCD-4.3B / 4.3B-BOX. No contiene binarios compilados, credenciales ni imágenes de la demo de fábrica.

1. Consulta [materiales y compatibilidad](docs/MATERIALES.md).
2. Sigue [la instalación en Ubuntu 24.04 o Windows](docs/INSTALACION.md) para compilar con PlatformIO y subir el firmware.
3. En la pantalla abre **Ajustes → Redes Wi-Fi**. Selecciona una red de **2,4 GHz** e introduce la clave. Puedes escribir el SSID si no aparece.
4. En **Cuenta LibreLinkUp**, usa la cuenta receptora; elige una persona en **Ajustes → Gestionar usuarios**. Opcionalmente, busca tu ciudad en **Ubicación y clima**.
5. Para cambiar de persona, vuelve a **Ajustes → Gestionar usuarios**. El engranaje abre el menú; **Configurar con QR** permite hacer los mismos ajustes desde un móvil.

**Actualizaciones:** si ya tienes gluco_waveshare, utiliza `./actualizar.sh /dev/ttyACM0` para respaldar NVS y cargar sin borrarla; `erase` borra NVS y requiere volver a introducir Wi-Fi, credenciales y usuario. El primer borrado solo se describe para una instalación limpia desde otra imagen o tabla de particiones.

La pantalla muestra glucosa y su historial de hasta doce horas, reloj y pronóstico. No incluye alarmas ni sustituye la aplicación o el receptor oficiales. [README](README.md) · [Seguridad y límites](docs/SEGURIDAD_Y_LIMITES.md) · [Licencia MIT](LICENSE).
