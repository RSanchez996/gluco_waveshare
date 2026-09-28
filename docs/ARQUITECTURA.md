# Arquitectura

## Base de hardware

La inicialización conserva la secuencia del ejemplo oficial de Waveshare
`Arduino/examples/09_lvgl_Porting`:

1. perfil soportado `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3_B`;
2. ST7262 RGB 800 x 480;
3. GT911 por I2C;
4. expansor CH422G para reset y retroiluminación;
5. LVGL 8.4 con un buffer interno de 18 líneas.

Se usa un framebuffer RGB y el buffer de rebote de diez líneas del perfil
Waveshare v1.0.4. Antes de iniciar el panel se fija el reloj RGB en 12 MHz
para reducir la demanda de PSRAM mientras Wi-Fi está activo. Después de
guardar ajustes en NVS se resincroniza el barrido del panel RGB.

## Arranque por fases

La aplicación no activa Wi-Fi antes de que LCD, táctil y LVGL estén listos. Cada
fase se imprime por serie. La última fase crítica se conserva en memoria RTC
NOINIT con una comprobación de integridad, sin escrituras periódicas en flash.
Los fallos controlados quedan detenidos; tres reinicios consecutivos por WDT
o excepción activan una pantalla de modo seguro.

## Tareas

- El bucle principal es el único que llama a LVGL y al servidor web temporal.
- La tarea periódica de glucosa y clima corre separada del bucle principal en
  CPU1, prioridad 0, como en 0.8.0. El bucle de Arduino tiene prioridad mayor.
  El login y la geocodificación temporales usan el otro núcleo con prioridad
  0. Las consultas se espacian y la lectura HTTP cede CPU cada 4 KiB.
- `stateMutex` protege una instantánea de solo lectura para la interfaz.
- `httpMutex` impide dos conexiones TLS simultaneas.
- Los documentos JSON grandes y el buffer HTTP se reservan en PSRAM.
- Un contador atómico avisa de cambios en ajustes y evita construir cadenas y
  calcular hashes en las cuatro vueltas por segundo del bucle de red.
- Una cola única separa las operaciones TLS: usuarios, glucosa y clima nunca se
  consultan simultáneamente. Glucosa y clima se actualizan cada dos minutos con
  un desfase y un mínimo de 45 segundos entre peticiones.
- La gráfica de LibreLinkUp admite respuesta HTTP fragmentada de hasta 512 KiB,
  pero ArduinoJson solo conserva fecha, valor, unidades y tendencia. Login,
  usuarios utilizan buffers de 64 KiB. El clima solicita ocho horas de datos
  horarios y cuatro días diarios en la misma respuesta, con límites de 32 KiB
  HTTP y 16 KiB de documento JSON.
- La radio usa reconexión automática; el firmware no fuerza una desconexión en
  cada fallo. Si el controlador no se recupera, llama a `WiFi.reconnect()` como
  respaldo una vez por minuto y espera ocho segundos de estabilidad antes de TLS.

## Portal QR

La configuración usa dos fases independientes. En la primera, el ESP32 crea una
red WPA2 temporal con nombre y clave aleatorios. Ese portal solo permite guardar
el Wi-Fi domestico. La red y la clave se escriben inmediatamente en NVS.

El ESP32 cierra despues el punto de acceso, conecta como estacion y muestra un
segundo QR con `http://IP_LOCAL/`. Ese segundo portal permite configurar
LibreLinkUp, elegir usuario, seleccionar ubicacion o volver a cambiar la red.
Si la red guardada no conecta en 25 segundos, se recupera automaticamente el
primer punto de acceso.

Las operaciones que cambian datos requieren un token aleatorio de la sesion y
solo se aceptan por la interfaz correspondiente. El servidor deja de existir al
guardar, cerrar o alcanzar el tiempo limite.

El asistente realiza, en orden:

1. guardado persistente del Wi-Fi desde el portal temporal;
2. conexion a la red local y presentacion del segundo QR;
3. inicio de sesion LibreLinkUp;
4. listado y seleccion explicita del paciente compartido;
5. geocodificacion de la localidad con Open-Meteo;
6. guardado atomico del resto de ajustes en NVS y cierre del portal sin reinicio.

La tarea periódica de glucosa y clima queda pausada mientras hay un portal
abierto, evitando conexiones TLS simultáneas durante la configuración.

El inicio de sesión LibreLinkUp es asíncrono. El `POST` crea una tarea de red y
responde inmediatamente; el móvil consulta `/api/libre/status` una vez por
segundo. El servidor local, LVGL y el QR continúan activos durante la
negociación TLS. La sesión resultante se mantiene únicamente en RAM y se
reutiliza mientras el servidor no la rechace.

La geocodificación también responde inmediatamente y se consulta con
`/api/geocode/status`; no realiza HTTPS desde el bucle LVGL. Ambos trabajos
comparten el mutex HTTPS y no hacen llamadas LVGL desde sus tareas. Las tareas
temporales retornan de sus funciones de trabajo antes de borrarse para liberar
documentos JSON y cadenas dinámicas.

## Histórico de 10 horas y usuarios

Cada consulta combina los datos devueltos por LibreLinkUp con el buffer local,
agrupa muestras por intervalos de cinco minutos, descarta valores fuera de diez
horas y conserva hasta 120 puntos. El histórico permanece solo en RAM y se vuelve a solicitar tras reiniciar: no
se escribe flash durante la actualización periódica. La lista de conexiones
LibreLinkUp sí se conserva en NVS y se refresca en segundo plano. El botón `Usuario` permite seleccionar
una conexión desde la pantalla principal. El cambio se procesa en la tarea de
red, se guarda en NVS y borra solo los puntos en RAM del paciente anterior. El
histórico de versiones anteriores en NVS se ignora sin borrarlo: en este firmware
la gráfica se reconstruye desde el proveedor. La selección espera a que la tarea
termine la petición HTTPS anterior; la interfaz sigue atendiendo el tacto.
El reloj continúa durante la espera y la consulta muestra segundos transcurridos.

## Interfaz v0.8.1

La gráfica de diez horas convierte cada marca del eje horizontal a la hora
local del reloj. La portada tiene botones de clima y usuario y un engranaje
para Ajustes. Tocar la zona superior, por encima de la gráfica, apaga solo la
retroiluminación cuando se muestra la portada; tocar la gráfica selecciona la
medición real más cercana en el eje temporal y muestra una línea con fecha,
hora y valor. No hay acción de apagado en el reloj. La tarjeta del clima
muestra sensación, máxima y mínima en ambas vistas. La fecha de la última
lectura sigue visible. La versión 0.8.1 conserva las posiciones de 0.7.1 y
usa una paleta azul muy oscura, tarjetas con bordes finos y un acento del color
de la lectura. Los umbrales 70, 180 y 240 están señalados por líneas punteadas
del color correspondiente. No presenta mensajes de arranque en la vista normal.
La vista del clima permite alternar entre seis
horas y cuatro días sin repetir la petición: los datos están en la instantánea
de estado. Los iconos de tiempo son formas vectoriales pequeñas dibujadas por
LVGL, sin emojis ni bitmaps adicionales. La condición de día/noche la devuelve
Open-Meteo mediante `is_day`, de modo que la luna no depende de una regla fija
por hora. El modo seguro sigue pudiendo presentar información de reinicios.

## Fuentes de pantalla

Los tamaños 14, 16, 20, 24 y 28 usan fuentes LVGL generadas directamente con
ASCII y los caracteres `áéíóúüñÁÉÍÓÚÜÑ¿¡ºªçÇ·°`. No se depende del mecanismo de
fuente alternativa. Las Montserrat integradas equivalentes se desactivan para
evitar duplicar su ocupación en flash.

## Modulos excluidos

No hay codigo, credenciales, pantallas ni rutas web de Alexa o alarmas.

## Redes conocidas y primer dato (v0.8.7)

El segundo portal QR acepta cuatro redes adicionales y escribe su lista en la
misma entrada de ajustes NVS. El formulario de servicios conserva la lista aun
cuando el cliente web no la envíe. El código ignora las antiguas claves de
brillo visual guardadas por la versión 0.8.5; no necesita borrar NVS.

La tarea de datos sigue fijada en CPU1/prioridad 0. La búsqueda periódica de redes de 0.8.6 se retira: la radio no escanea ni cambia de SSID mientras está conectada y consultando servicios. Si se desconecta, espera 15 segundos para el reintento automático y después intenta cada red adicional y la principal con intervalos de 30 segundos. Con solo la red principal, se conserva el reintento de respaldo cada minuto.

El portal QR ofrece un escaneo asíncrono solo cuando se abre la pestaña Wi-Fi o se pulsa Actualizar lista. Espera como máximo ocho segundos y conserva los resultados en RAM hasta cerrar el portal. El escaneo comparte el semáforo HTTPS y no puede iniciarse mientras corre un login LibreLinkUp, una búsqueda de ciudad u otra petición de red; al guardar Wi-Fi o cerrar el portal cancela cualquier búsqueda pendiente. Como el radio comparte recursos con la pantalla RGB, un escaneo solicitado puede causar una perturbación breve; durante la visualización normal no habrá escaneos periódicos.

Con paciente seleccionado se consulta la gráfica directamente antes de
refrescar la lista de usuarios; ambas operaciones siguen separadas 45 segundos.
No se elevan prioridades de HTTPS, pues priorizar la red frente al bucle LVGL
y las tareas del sistema puede reabrir la situación de WDT observada antes.
