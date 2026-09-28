# Copia de ajustes antes de actualizar

El Wi-Fi, LibreLinkUp, el usuario y la ubicación se guardan en la partición
`nvs` del archivo `firmware/partitions.csv`, en el rango `0x9000`–`0x68FFF`
(`0x60000` bytes). La tabla es idéntica en las versiones 0.8.1 y 0.8.2.

Una carga normal con `pio run -t upload` no incluye `erase`. No hay suficiente
información de la placa afectada para saber por qué se perdieron los ajustes;
si se ejecuta `pio run -t erase`, estos sí se borran. Una copia externa permite
recuperarlos aunque haya que borrar la flash por otro motivo.

## Ubuntu 24.04

Conecta el USB marcado `UART`, cierra cualquier programa que esté usando el
puerto y localiza el dispositivo con `pio device list`. Si no tienes `esptool`,
puedes instalarlo en un entorno separado:

```bash
python3 -m venv .backup-venv
./.backup-venv/bin/python -m pip install esptool
```

Desde la raíz del proyecto, con esptool 5, haz una copia **antes** de actualizar:

```bash
umask 077
./.backup-venv/bin/python -m esptool --chip esp32s3 --port /dev/ttyACM0 \
  read-flash 0x9000 0x60000 nvs_backup_antes_de_actualizar.bin
stat -c '%s bytes' nvs_backup_antes_de_actualizar.bin
./actualizar.sh /dev/ttyACM0
```

El tamaño de la copia debe ser **393216 bytes**. Si la lectura falla, no
continúes con el borrado de flash. Esptool 4 utiliza `read_flash` en vez de
`read-flash`. Sustituye el puerto si tu placa aparece como `/dev/ttyUSB0`.

La copia puede contener las contraseñas de Wi-Fi y LibreLinkUp **sin cifrar**.
Está excluida del repositorio por `.gitignore`; guárdala en un lugar privado,
no la subas a GitHub ni la compartas para depuración.

Si ya se borró la partición y no existe una copia anterior, el firmware no
puede reconstruir las credenciales. En ese caso vuelve a configurar la red y
la cuenta con los dos QR. Una copia de NVS se debe restaurar solo en la misma
placa y con una tabla de particiones compatible.
