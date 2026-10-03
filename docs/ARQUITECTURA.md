# Arquitectura

## Base de hardware

La inicialización conserva la secuencia del ejemplo oficial de Waveshare
`Arduino/examples/09_lvgl_Porting`:

1. perfil soportado `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3_B`;
2. ST7262 RGB 800 x 480;
3. GT911 por I2C;
4. expansor CH422G para reset y retroiluminación;
5. LVGL 8.4 con un buffer interno de 18 líneas.

Se usa un framebuffer RGB en PSRAM y un buffer de rebote de veinte líneas.
El perfil Waveshare v1.0.4 configura diez líneas; la guía de Espressif
recomienda veinte o más cuando la recarga DMA puede retrasarse. Antes de iniciar
el panel se fija el reloj RGB en 12 MHz para reducir la demanda de PSRAM
mientras Wi-Fi está activo. LVGL usa un solo buffer interno de dieciocho líneas:
`drawBitmap` de RGB copia al framebuffer de forma síncrona. Pasar de dos a un
buffer LVGL libera 28 800 bytes y duplicar los dos buffers de rebote consume
32 000 bytes adicionales; el balance interno es de unos 3 200 bytes.
Después de guardar ajustes en NVS se resincroniza el barrido del panel RGB.
La búsqueda táctil de localidades filtra los metadatos de Open-Meteo antes de
reservar el JSON y usa una tarea HTTPS de prioridad 0 en CPU1. Al terminar,
solicita una única resincronización RGB que ejecuta el bucle de LVGL, incluso
si se abandonó la página de búsqueda. Esto recupera un desplazamiento del
barrido; no impide por completo una perturbación transitoria mientras la radio
y la pantalla compiten por el acceso a PSRAM.
LVGL agrupa los redibujados cada 100 ms (10 Hz); su temporizador táctil y el
bucle principal siguen atendiendo eventos con independencia de ese período.
La frecuencia de redibujado de LVGL no modifica el barrido eléctrico RGB ni
garantiza por sí sola que desaparezcan destellos causados por falta de ancho
de banda de memoria durante TLS.

## Arranque por fases

La aplicación no activa Wi-Fi antes de que LCD, táctil y LVGL estén listos. Cada
fase se imprime por serie. La última fase crítica se conserva en memoria RTC
NOINIT con una comprobación de integridad, sin escrituras periódicas en flash.
La marca se limpia al concluir cada trabajo y distingue login HTTPS, lectura
de usuarios y análisis JSON. Señala la fase que estaba activa, pero no
identifica por sí sola la tarea que hizo saltar el watchdog.
Los fallos controlados quedan detenidos; tres reinicios consecutivos por WDT
o excepción activan una pantalla de modo seguro.

## Tareas

- El bucle principal es el único que llama a LVGL y al servidor web temporal.
- La tarea periódica de glucosa y clima corre separada del bucle principal en
  CPU1, prioridad 0, como en 0.8.0. El bucle de Arduino tiene prioridad mayor.
  El login y la geocodificación temporales también usan CPU1/prioridad 0.
  El portal pausa la tarea
  periódica; LVGL y el servidor local siguen en el bucle de Arduino, que
  conserva mayor prioridad. Las consultas se espacian y la lectura HTTP cede
  CPU cada 4 KiB.
- `stateMutex` protege una instantánea de solo lectura para la interfaz.
- `httpMutex` impide dos conexiones TLS simultaneas.
- Los documentos JSON grandes y el buffer HTTP se reservan en PSRAM.
- Un contador atómico avisa de cambios en ajustes y evita construir cadenas y
  calcular hashes en las cuatro vueltas por segundo del bucle de red.
- Una cola única separa las operaciones TLS: usuarios, glucosa y clima nunca se
  consultan simultáneamente. Glucosa se consulta cada dos minutos; ante un
  error transitorio se permite un solo reintento espaciado. Clima se consulta
  cada 30 minutos, con un mínimo de 45 segundos entre peticiones HTTPS.
- La gráfica de LibreLinkUp admite respuesta HTTP fragmentada de hasta 512 KiB,
  pero ArduinoJson solo conserva fecha, valor, unidades y tendencia. Login,
  usuarios utilizan buffers de 64 KiB. El clima solicita ocho horas de datos
  horarios y cuatro días diarios en la misma respuesta, con límites de 32 KiB
  HTTP y 16 KiB de documento JSON.
- La radio usa reconexión automática; el firmware no fuerza una desconexión en
  cada fallo. Si el controlador no se recupera, llama a `WiFi.reconnect()` como
  respaldo una vez por minuto y espera ocho segundos de estabilidad antes de TLS.

## Ajustes táctiles y portal QR

El engranaje abre un menú local LVGL. El teclado, la lista de redes cercanas y
guardadas, la búsqueda de localidades y el inicio de sesión LibreLinkUp se
operan desde la pantalla. Las búsquedas Wi-Fi son asíncronas y bajo demanda;
login y geocodificación comparten las tareas HTTPS de baja prioridad. El
trabajo periódico se pausa mientras el menú de ajustes está activo. Guardar
NVS se despacha a una tarea de baja prioridad que toma el mutex HTTPS antes
de escribir, para evitar flash y TLS simultáneos. No se modifica la tabla de
particiones ni el formato de los ajustes anteriores.

El QR es una opción expresa del menú. Solo entonces se levanta el servidor:

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
responde inmediatamente; el móvil consulta `/api/libre/status` cada tres
segundos. El servidor local, LVGL y el QR continúan activos durante TLS. Solo
se retienen los campos JSON necesarios para el token y la lista de usuarios,
y ambas peticiones se separan medio segundo. La sesión resultante se mantiene
únicamente en RAM y se reutiliza mientras el servidor no la rechace.

La afinidad del login y de la geocodificación es la misma desde el navegador y
desde el teclado de la pantalla. Se evita la carga TLS de aplicación en CPU0, que atiende al Wi-Fi en
la configuración habitual. No se aumenta ni desactiva el watchdog: si vuelve
a dispararse, el portal muestra el motivo del último reinicio y la fase en
curso. La fase indica contexto, no identifica la tarea culpable.

La geocodificación también responde inmediatamente y se consulta con
`/api/geocode/status`; no realiza HTTPS desde el bucle LVGL. Ambos trabajos
comparten el mutex HTTPS y no hacen llamadas LVGL desde sus tareas. Las tareas
temporales retornan de sus funciones de trabajo antes de borrarse para liberar
documentos JSON y cadenas dinámicas.

## Histórico de hasta 12 horas y usuarios (v0.8.8)

Cada respuesta válida con histórico del endpoint `/graph` sustituye los puntos históricos en RAM,
sin mezclar mediciones actuales de consultas previas. Se normalizan los tiempos,
se descartan datos fuera de la ventana móvil de doce horas y se guardan como
máximo 160 muestras. La lectura actual y su flecha se mantienen en un campo
separado, sin reescribir el valor de una muestra histórica. Si el proveedor
devuelve menos historial, se muestra únicamente lo recibido. Si devuelve una
lectura actual válida pero el array histórico vacío, conserva la última curva
válida hasta que vuelva a recibirse un histórico.

Abbott distingue la lectura actual del historial: Libre 2 mide cada minuto y
conserva puntos históricos a intervalos de 15 minutos durante ocho horas; el
gráfico compartido LibreLinkUp muestra hasta doce horas. La subida del móvil a
la nube necesita Internet, pero Abbott no publica un intervalo fijo garantizado
para cada actualización en el servidor. Por eso se consulta el punto actual
cada dos minutos sin interpretar una respuesta HTTP 200 con el mismo timestamp
como un motivo para consultar de nuevo inmediatamente. Referencias oficiales:
https://pro.freestyle.abbott/es-es/bienvenida/ayuda/preguntas-frecuentes/preguntas-frecuentes.html?q=freestyle-spain-question-9
https://www.freestyle.abbott/es-es/productos/conectividad/librelinkup.html

El trazado interpola de forma monótona entre puntos que disten como máximo
30 minutos. No cambia los valores originales, no crea máximos o mínimos
nuevos y deja las lagunas largas sin unir. La última muestra histórica se
une con la lectura actual con trazos punteados solo cuando distan menos de
30 minutos. El eje X muestra marcas cada hora y etiquetas de horas en punto
cada tres horas según la zona local, incluso cuando el reloj avanza o cambia
el día. La consulta se mantiene cada 120 segundos en CPU1, prioridad 0; el
dibujo se limita a ocho segmentos por intervalo y se invalida solo al
recibir datos o cada diez minutos. No se escribe el histórico en NVS.

La lista de conexiones sí se conserva en NVS y se refresca en segundo plano.
«Gestionar usuarios» permite seleccionar una conexión desde Ajustes.
El cambio se procesa en la tarea de red, se guarda en NVS y limpia
los datos en RAM del paciente anterior. La selección espera a que la tarea
termine la petición HTTPS anterior; la interfaz sigue atendiendo el tacto.

## Interfaz v0.8.1

La gráfica convierte cada marca del eje horizontal a la hora
local del reloj. La portada actual tiene un botón de clima y un engranaje;
«Gestionar usuarios» se encuentra en Ajustes. Tocar la zona superior, por encima de la gráfica, apaga solo la
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
