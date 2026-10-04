# Cambios

## 1.0

- Retira las dos modificaciones de inicialización introducidas inmediatamente antes de que apareciese la pantalla iluminada sin imagen: creación con `Board(config)` y selección de EDMA directo. No se ha aislado cuál de las dos causó el fallo. Restaura la ruta predeterminada `Board()` del perfil Waveshare, con 12 MHz y veinte líneas de rebote, que ya había mostrado imagen. Conserva la recuperación no bloqueante al despertar: solicita reinicio RGB en VSYNC y enciende la retroiluminación 160 ms después; también resincroniza tras limpiar en NVS un diagnóstico de WDT. No altera núcleos, prioridades, intervalos ni formato NVS. Pendiente de validación prolongada en hardware.
- Corrige el desplazamiento RGB observado tras cambiar de usuario: la resincronización pendiente se solicita al terminar la lectura de gráfica, lista de usuarios o clima, cuando ya se han liberado sus buffers, y se ejecuta desde el bucle principal. Las escrituras NVS también usan esa vía única. Si HTTPS anuncia la longitud, el buffer se reserva de una vez para evitar copias por crecimiento en PSRAM. Sin cambios de núcleo, prioridad, intervalos ni formato NVS. Pendiente de prueba en hardware.
- La búsqueda de ubicación conserva solo seis campos por resultado y reduce de 28 a 12 KiB el JSON reservado. Tras completar o fallar el HTTPS, resincroniza una sola vez el panel RGB desde el bucle de interfaz, incluso si se abandona el menú, para recuperar un barrido desajustado sin reiniciar el equipo. Pendiente de comprobar en la placa.
- Las teclas «Más» y «Volver» cambian el mapa al soltar el dedo. Antes el cambio se hacía al presionar y la tecla «abc» de la nueva página, situada en el mismo lugar, devolvía el teclado a letras al terminar el toque.
- Usa selectores de estilo `lv_style_selector_t` para los estados del cursor y las teclas pulsadas; elimina las cinco advertencias de enumeraciones distintas en Arduino-ESP32 3.3.12 sin cambiar su apariencia.
- Mueve «Gestionar usuarios» de la pantalla de glucosa a Ajustes; «Volver» regresa al menú y elegir una persona regresa a glucosa y mantiene el guardado en NVS.
- Añade una segunda página de símbolos del teclado con asterisco, barra invertida y otros signos frecuentes; «Más» y «Volver» solo cambian de página y no se insertan en el texto.
- Limita a 100 ms (10 Hz) el período de repintado de LVGL para reducir la carga de interfaz observada como destellos durante Wi-Fi/HTTPS. Conserva el reloj RGB de 12 MHz: corresponde al barrido continuo del panel y bajarlo a 10 Hz produciría parpadeo visible. Pendiente de validación en la placa.
- Reordena el teclado QWERTY español: ñ en la fila de letras, vocales con tilde y signos en **1#**. Da foco explícito al campo activo y muestra un cursor azul fijo, sin animación adicional.
- Amplía el búfer de rebote RGB de diez a veinte líneas para tolerar mejor las recargas de PSRAM bajo HTTPS. Libera uno de los dos buffers LVGL de dieciocho líneas; el `drawBitmap` RGB es síncrono, de modo que el segundo no es necesario. Balance aproximado: 3,2 KiB más de RAM interna. Mantiene 12 MHz de PCLK y la tarea HTTPS en CPU1/prioridad 0. Pendiente de prueba en la pantalla real.

- Corrige la alineación del teclado táctil de LVGL para que sus cuatro filas queden dentro de la LCD. Añade distribución española con ñ, tildes, símbolos y botón para mostrarlo u ocultarlo.
- Usa la fuente de símbolos LVGL para la flecha del selector de región, y mantiene la fuente castellana para sus opciones.
- El menú principal de Ajustes tiene una sola salida a Glucosa mediante una X.
- Acota los datos JSON retenidos durante el inicio de sesión y la lista de usuarios, separa medio segundo ambas peticiones y reduce las consultas de estado del portal a una cada tres segundos.
- Limpia la fase de diagnóstico al terminar el trabajo HTTPS para evitar atribuir un reinicio posterior al login. Distingue login, usuarios y análisis JSON; la fase por sí sola no identifica la tarea que disparó el WDT. Pendiente de validación en la placa.
- Ante el WDT reproducible unos tres segundos después del login del portal, mueve sus dos puntos de entrada (QR y pantalla táctil) de CPU0 a CPU1, prioridad 0, igual que la tarea periódica estable. Hace lo mismo para las dos entradas de geocodificación. La tarea periódica queda pausada durante ajustes y el bucle de LVGL mantiene mayor prioridad. No altera la configuración del watchdog.

- Ajustes táctiles como vía principal: teclado, búsqueda y gestión de redes Wi-Fi cercanas o guardadas, SSID manual, búsqueda de localidad y acceso LibreLinkUp con selección de usuario. Portal QR opcional y cerrado cuando no se muestra.
- Guardados de ajustes en una tarea separada de baja prioridad, serializados con HTTPS para proteger el bucle de LVGL y la pantalla RGB; mismo formato NVS y tabla de particiones.
- Datos de glucosa cada dos minutos con un único reintento espaciado tras fallos transitorios. Lista de usuarios sin bloquear lecturas actuales ante errores ordinarios; clima cada 30 minutos. Histórico válido conservado si llega solo el punto actual.
- Pantalla principal con estado simple de lectura y hora de la última muestra válida. Los fallos técnicos siguen disponibles en el portal y el registro serie.

## 0.8.9

- Corrige la compilación de `providers.cpp`: el filtro JSON para `glucoseMeasurement` y el punto de glucosa actual tenían el mismo nombre `current` en `LibreClient::read`.
- Mantiene el comportamiento de la versión 0.8.8, incluidas las tareas, particiones y claves NVS; no requiere borrado para actualizar.

## 0.8.8

- Sustituye en RAM el histórico de cada respuesta válida de LibreLinkUp: hasta 12 horas y 160 muestras, sin acumulación de valores actuales antiguos. La lectura actual y su flecha se guardan separadas.
- Dibuja una curva visual monótona entre medidas disponibles, sin extremos inventados; une por trazos punteados la última muestra histórica con el punto actual si la separación no supera 30 minutos. Los huecos largos quedan abiertos.
- El eje X se desplaza con el tiempo y etiqueta horas reales en punto cada tres horas. La selección táctil sigue mostrando hora y valor originales.
- Mantiene peticiones cada dos minutos en CPU1/prioridad 0, la separación HTTPS, el formato NVS y las protecciones RGB. Revisión estática sin compilación ni prueba en placa.

## 0.8.7

- Retira escaneos periódicos y cambios de red con la pantalla en funcionamiento para reducir la contención de radio/PSRAM observada como destellos. Las redes adicionales se prueban únicamente tras desconexión y con 30 segundos entre intentos.
- La pestaña Wi-Fi muestra las redes detectadas mediante un escaneo bajo demanda, limitado a ocho segundos; permite elegir el SSID sin teclearlo y conserva la entrada manual para redes ocultas.
- Mantiene el orden de LibreLinkUp, afinidad y prioridad de la tarea de datos, así como particiones y formato NVS. Revisión estática; pendiente de prueba en la placa.

## 0.8.6

- Retira el brillo visual de 0.8.5: el interruptor CH422G no regula la retroiluminación; el toque para apagarla físicamente permanece.
- Hasta cuatro redes Wi-Fi adicionales desde la pestaña del segundo portal QR; persisten en NVS y se examinan fuera de HTTPS mediante un escaneo asíncrono acotado.
- Conserva CPU1 y prioridad 0 para datos. Cuando hay usuario guardado, solicita la gráfica antes que la lista de usuarios, manteniendo el intervalo de 45 segundos entre peticiones.
- Tabla de particiones sin cambios; se pueden cargar ajustes existentes sin borrarlos. Revisión estática, sin compilar ni probar en la placa.

## 0.8.4

- Reduce de nuevo el reloj RGB a 12 MHz durante Wi-Fi y resincroniza el panel
  después de guardar ajustes o la lista de usuarios en NVS. Espressif advierte
  de interrupciones del barrido RGB al escribir flash en este modo de PSRAM.
- La pantalla del QR Wi-Fi deja de redibujar la misma etiqueta cada medio
  segundo; solo la actualiza si cambian la red o la contraseña mostradas.
- El actualizador de Ubuntu copia primero la partición NVS de 393216 bytes y
  cancela el `upload` si no se puede leer; no utiliza `erase`.
- El portal informa si los ajustes NVS faltan o si están presentes pero su
  contenido JSON no se puede interpretar, sin mostrar las contraseñas.
- Amplía la ventana de la gráfica a diez horas mediante acumulación en RAM;
  la marca azul se quita al apagar la pantalla o al recibir una nueva lectura.
- Revisión estática; falta comprobar el resultado en el dispositivo.

## 0.8.3

- Revierte la tarea periódica de datos a CPU1/prioridad 0, como en 0.8.0.
  La 0.8.2 la había movido a CPU0, donde coincidía con Wi-Fi y RGB; este
  cambio coincide con los reinicios al consultar la gráfica de LibreLinkUp.
- Recupera la pausa de lectura HTTP cada 4 KiB y el reloj RGB de 14 MHz de
  0.8.0. El selector táctil de medidas de 0.8.1 se conserva en LVGL.
- Guarda la última fase crítica en RTC NOINIT con marca y suma de comprobación.
  RTC DATA se reinicializaba durante el arranque tras un WDT, por lo que la
  pantalla mostraba «fase anterior desconocida».
- Quita del README las instrucciones para subir el proyecto a GitHub.
- Revisión estática del código; esta versión aún no se ha probado en la placa.

## 0.8.2

- El login LibreLinkUp del portal baja de prioridad 1 a 0; la aplicación
  detecta el núcleo de Arduino/LVGL y fija todas las tareas HTTPS al otro.
  La pila Wi-Fi conserva preferencia. Las transferencias HTTP ceden CPU cada
  2 KiB durante dos ticks de FreeRTOS, con menor presión sobre PSRAM.
- La búsqueda de localidades deja de bloquear el bucle del portal y LVGL:
  petición asíncrona en el núcleo de trabajo/prioridad 0 y consulta de estado
  desde el móvil.
- Las tareas temporales liberan sus documentos y cadenas antes de llamar a
  `vTaskDelete()`, evitando pérdidas de memoria entre varios intentos.
- La lista de usuarios se reescribe en NVS solo cuando su contenido cambia,
  evitando escrituras de flash innecesarias tras cada arranque.
- El modo seguro cuenta únicamente reinicios por watchdog o excepción;
  varios encendidos manuales rápidos ya no lo activan.
- El portal informa del motivo y fase del último reinicio por WDT. La nueva
  guía permite guardar una copia de NVS antes de actualizar. `actualizar.sh`
  ejecuta únicamente `upload`; la tabla de particiones no cambia.
- La causa precisa de un reinicio o pérdida de datos anterior no se puede
  determinar solo con el código. No se ha validado esta versión sobre hardware.

## 0.8.1

- La gráfica táctil marca la medida más cercana al punto elegido, con una línea
  vertical y su fecha, hora y glucosa. No interpola valores y descarta la
  selección al cambiar de usuario o caducar el punto.
- El apagado táctil queda en la parte superior de la pantalla de glucosa,
  por encima de la gráfica; no se activa al inspeccionar el histórico.
- Reduce de 14 a 12 MHz el reloj RGB para dar margen al acceso compartido a
  PSRAM. Las fases de diagnóstico se guardan en RTC durante la ejecución, sin
  escribir NVS en cada consulta. Los ajustes persistentes siguen en NVS.
- El intervalo de LibreLinkUp y Open-Meteo permanece en dos minutos; el toque
  de la gráfica solo redibuja esa tarjeta, sin lanzar nuevas peticiones.

## 0.8.0

- Retoque visual inspirado en la infografía: fondo nocturno azul muy oscuro,
  tarjetas con bordes suaves, botones más legibles y panel de lectura con
  acento del color de glucosa.
- La gráfica marca los tres umbrales con trazos punteados y destaca el último
  punto. Las tarjetas del pronóstico resaltan el primer tramo o día.
- Mantiene la distribución, navegación, red, intervalos de consulta y
  configuración NVS de la versión 0.7.1. No requiere borrar la flash.

## 0.7.1

- Distribución para GitHub: README breve, lista de materiales, instrucciones
  reproducibles para Ubuntu y Windows, `.gitignore` y aviso OFL completo para
  los glifos Montserrat. El portal utiliza un ejemplo genérico de localidad.
  Se conserva la versión del firmware y no se modifican los servicios de red.
- Corrige el recorte de máxima y mínima en la tarjeta meteorológica del reloj
  y presenta localidad, estado, sensación, máxima y mínima también en glucosa.
- En glucosa, deja dos botones grandes y un engranaje pequeño para Ajustes.
  Tocar el fondo o las tarjetas apaga la retroiluminación; tocar otra vez la
  despierta. Los botones mantienen su función.
- En reloj y clima coloca Glucosa, Por horas y 4 días en la fila inferior; esa
  pantalla no tiene control para apagar la retroiluminación.
- No cambia las peticiones de red ni el formato de los ajustes guardados.

## 0.7.0

- Vista principal inspirada en la referencia adjunta: reloj y fecha, glucosa
  destacada, flecha de tendencia, hora de la lectura, clima e icono, gráfica
  amplia de ocho horas con etiquetas de horas locales reales.
- Vista meteorológica con dos botones: seis horas o cuatro días. Iconos
  dibujados por LVGL sin ficheros de imagen; usan `is_day` de Open-Meteo para
  mostrar sol o luna cuando corresponde, además de nubes, niebla, lluvia,
  nieve y tormenta. Días con máxima, mínima y probabilidad de lluvia.
- Una sola petición Open-Meteo recibe los pronósticos horarios y diarios.
  Permanece la cola de red y el intervalo mínimo de dos minutos por servicio.
- El diagnóstico de arranque deja la pantalla habitual, pero se conserva en
  el modo seguro si se repiten reinicios por error.
- Sigue sin compilarse el proyecto en este paquete; se conserva NVS al hacer
  la actualización normal y no hay alarmas ni integración Nightscout.

## 0.6.0-alpha.8-con-clima

- Recupera Open-Meteo con pronóstico acotado a ocho horas. Una tarea solicita
  glucosa y clima cada dos minutos, con al menos 45 segundos entre conexiones.
- El perfil Waveshare RGB reduce su frecuencia de 16 a 14 MHz, mantiene un solo
  framebuffer y el buffer de rebote oficial de diez líneas. Evita redibujar la
  gráfica cada minuto si no han llegado puntos nuevos. Es necesario comprobar
  físicamente la mejora en los puntos que parpadean.
- Une en segmentos las lecturas de LibreLinkUp separadas hasta 30 minutos;
  conserva los huecos más largos y no crea medidas artificiales.
- Tras detectar un bloqueo también sin cambiar de usuario, desactiva las
  escrituras periódicas del histórico en NVS y lo vuelve a descargar al
  arrancar. Los ajustes y el usuario siguen persistiendo. La pantalla muestra
  cuánto dura la petición Libre cada 15 segundos.
- El segundo cambio de usuario muestra que espera al HTTPS anterior sin detener
  el reloj ni los botones; deja de borrar NVS al seleccionar otra persona.
- Sustituye los hashes calculados cuatro veces por segundo por un aviso de
  cambio de configuración.
- Conserva la configuración en NVS y el control de reinicios; no requiere borrar
  la memoria de la placa.

## 0.6.0-alpha.7-sin-clima

- Tras registrar en la placa un WDT durante login y dos durante la petición
  de la gráfica, mueve la tarea de datos del núcleo 0 al 1, a prioridad 0.
  La pantalla LVGL mantiene su tarea y Wi-Fi queda principalmente en CPU0.
- Reduce el registro NVS a hitos de Wi-Fi, login, descarga, análisis y guardado,
  con un máximo de seis escrituras por arranque. Menos escrituras durante la
  actividad de la LCD RGB y las consultas HTTPS.
- La tarjeta `Antes:` muestra la fase del último reinicio, no siempre la del
  primero; en modo seguro siguen apareciendo las tres por separado.
- Conserva el modo seguro, el reloj, la glucosa y la configuración almacenada;
  el clima sigue temporalmente desactivado. No modifica el watchdog.

## 0.6.0-alpha.6-sin-clima

- Sin peticiones a Open-Meteo ni actualización del pronóstico. La página de
  reloj indica que el clima está desactivado y mantiene la hora sincronizada.
- La ubicación deja de ser obligatoria para configurar LibreLinkUp. Si ya se
  había guardado, se conserva para una versión futura.
- Conserva el diagnóstico de tres fases de reinicio y la reducción de
  redibujados de alpha.6. Se mantiene una sola cola de red para LibreLinkUp.

## 0.6.0-alpha.6

- Guarda una fase breve por cada reinicio WDT o excepción y muestra las tres
  fases al entrar en modo seguro; permite distinguir los reinicios iniciales
  de la petición de la gráfica. Limita a 24 escrituras de fase por arranque.
- Distingue esperas de Wi-Fi, sincronización de hora, lista de usuarios, login,
  descarga de gráfica, análisis JSON, guardado de histórico y clima.
- La pantalla no vuelve a dibujar la gráfica si solo cambia un texto de estado;
  tampoco copia el estado repetidamente mientras Wi-Fi mantiene el mismo estado.
- Mantiene el watchdog, las credenciales guardadas y las frecuencias de consulta.

## 0.6.0-alpha.5

- Diagnóstico observado en dispositivo: `WDT tarea` al intentar continuar las
  consultas tras seleccionar usuario. Evitamos la inanición del núcleo 0:
  la tarea de datos pasa a prioridad 0 y el búfer HTTP deja un tick libre cada
  4 KB escritos. No se desactiva ni amplía el watchdog.
- La pantalla muestra `Consultando gráfica LibreLinkUp...` durante esa fase.
- Al seleccionar el mismo usuario no se borran sus lecturas ni se vuelve a
  grabar la configuración en NVS.
- Contador de fallos WDT persistente: tres reinicios seguidos activan un modo
  seguro visible aunque el RTC pierda su contador; un arranque en frío reinicia
  la cuenta. Un funcionamiento estable de cinco minutos borra el contador.

## 0.6.0-alpha.4

- Cambios de página desde botones diferidos al ciclo de UI, fuera del evento
  táctil: no se destruye el botón de usuario mientras LVGL procesa su evento.
- Indicación en pantalla del motivo del último reinicio y número de arranques
  durante la sesión de alimentación; en modo seguro también se indica motivo.
- Seleccionar otro paciente ya no invalida el token de la cuenta LibreLinkUp.
- El búfer HTTP crece desde 16 KB según el tamaño recibido en vez de reservar
  hasta 512 KB de PSRAM para cada petición de gráfica.
- Cerrar el portal del segundo QR no reconfigura el modo Wi-Fi de la STA.
- Aclarado que HTTP 000 en `/api/libre/status` es esperable con el portal cerrado.

## 0.6.0-alpha.3

- Desactivado el ahorro Wi-Fi para reducir la latencia de recepción HTTPS en
  este dispositivo alimentado por USB. Consume más energía.
- Espera de respuesta de LibreLinkUp aumentada de 20 a 30 segundos sin cambiar
  los intervalos de consulta, las credenciales guardadas ni el cliente TLS.
- El error HTTPS `-11` del portal indica duración del intento, RSSI, RAM
  interna libre y tamaño del mayor bloque libre, sin mostrar secretos.
- El timeout de HTTP sigue siendo diagnóstico, no una confirmación de fallo de
  credenciales ni una prueba de reinicio de la placa.

## 0.6.0-alpha.2

- Corregido un defecto demostrado de la alpha.1: las cinco fuentes estaban
  comprimidas, pero `LV_USE_FONT_COMPRESSED` seguía desactivado. LVGL devolvía
  un bitmap nulo para cada carácter; por eso la pantalla no mostraba letras.
- Comprobación temprana de glifos ASCII y españoles en los cinco tamaños.
- Los errores de la web indican ahora si falló el login, la lista de usuarios,
  NTP o el transporte HTTPS; el puerto serie muestra host, respuesta y memoria
  disponible, nunca la contraseña ni el token.
- Se detuvo la conmutación automática de `eu` a `eu2` al recibir HTTP 403:
  un rechazo del servidor no identifica por sí mismo una región alternativa.
  Se aceptan las redirecciones de región declaradas en el propio login.
- Ante HTTP 403 de LibreLinkUp se recomienda esperar 15 minutos antes de
  repetir, evitando una sucesión de intentos que pueda provocar HTTP 430.

## 0.6.0-alpha.1

- Cola única de red: LibreLinkUp y clima se alternan, cada proveedor se consulta
  cada dos minutos y se dejan al menos 45 segundos entre operaciones TLS.
- Eliminada la desconexión forzada en los reintentos Wi-Fi; se usa reconexión
  automática y un único reintento de respaldo cada 60 segundos.
- Espera de ocho segundos después de recuperar Wi-Fi antes de abrir HTTPS.
- Ahorro de consumo habilitado en la radio Wi-Fi para reducir picos de corriente.
- Respuestas HTTP dimensionadas por servicio en lugar de reservar siempre el
  mismo bloque; la gráfica LibreLinkUp se filtra a los campos de glucosa usados.
- Límite amplio para la respuesta fragmentada de la gráfica y documentos JSON
  filtrados para reducir el uso máximo de PSRAM.
- Listado de usuarios LibreLinkUp actualizado en segundo plano y guardado en NVS.
- Nuevo botón `Usuario` en la pantalla de glucosa para cambiar de persona sin
  abrir Ajustes; el histórico anterior se borra para no mezclar pacientes.
- Fuentes LVGL directas con ASCII y caracteres españoles en todos los tamaños de
  texto; eliminadas las fuentes Montserrat duplicadas que ya no se usan.
- Diagnóstico serie del número de registros de gráfica recibidos y aceptados.
- HTTP 430 ya no provoca un segundo login regional inmediato: se respeta una
  pausa común de 15 minutos para todas las operaciones de LibreLinkUp.

## 0.5.2-alpha.1

- Inicio de sesión LibreLinkUp ejecutado en una tarea independiente: el servidor
  web local sigue respondiendo mientras se realiza la conexión TLS.
- La web consulta el estado del login cada segundo y conserva abierto el portal.
- Tildes, eñes, signos españoles y punto medio añadidos a la pantalla mediante
  fuentes de respaldo mínimas para los cinco tamaños que muestran texto.
- Textos de la pantalla y del portal web corregidos en español.
- Documentado el tamaño real disponible: cada partición de aplicación dispone
  de 6.815.744 bytes; un firmware de alrededor de 1,8 MB no está cerca del límite.

## 0.5.1-alpha.1

- Cabeceras actuales de la aplicacion Android para LibreLinkUp.
- Reutilizacion de la sesion validada en el portal, sin segundo login inmediato.
- Reutilizacion de esa sesion al volver a Ajustes; solo se renueva si el servidor
  la rechaza con 401/403.
- Respaldo europeo `eu2` cuando `eu` responde 403/430.
- Espera de 15 minutos ante HTTP 430 para no agravar un bloqueo temporal.
- El guardado cierra el portal sin reiniciar la placa.
- Historico reducido a 8 horas y 120 puntos agrupados cada cinco minutos.
- Redibujado limitado a cambios reales o una vez por minuto para evitar parpadeo.
- Localidad abreviada y sin caracteres ausentes en las fuentes LVGL.
- Tarjetas horarias con descripciones meteorologicas cortas.
- Diagnostico serie de cambios de estado Wi-Fi y URL HTTPS sin credenciales.
- Tiempo del portal contado desde la ultima peticion y mensaje claro si el movil
  pierde la conexion local, en lugar del generico `Failed to fetch`.

## 0.5.0-alpha.1

- Asistente dividido en dos fases y dos QR.
- El primer portal solo guarda la red Wi-Fi en NVS.
- El segundo portal se abre en la IP local para LibreLinkUp, usuario y clima.
- Pagina inicial de Ajustes para elegir entre cambiar red o servicios.
- Recuperacion automatica del punto de acceso si la red guardada no conecta.
- Tarea periodica de datos pausada durante la configuracion para evitar TLS
  simultaneo y reducir el uso de memoria.
- Lista de usuarios LibreLinkUp movida de la pila a PSRAM.
- Pila del bucle web ampliada a 16 KB para el inicio HTTPS de LibreLinkUp.
- Portal web adaptado al tema oscuro.

## 0.4.1-alpha.2

- Corregida la inclusion de la API Wi-Fi requerida por el diagnostico HTTPS.

## 0.4.1-alpha.1

- Espera explicita a la sincronizacion NTP antes de iniciar TLS.
- Mensaje HTTPS con el codigo de transporte real en web y monitor serie.
- Tiempos de espera TLS ampliados para conexiones lentas.
- Version LibreLinkUp predeterminada actualizada a 5.1.1.
- Tema oscuro real con fondo negro y tarjetas de gris casi negro.
- Boton para apagar la retroiluminacion y reactivacion con un toque seguro.

## 0.4.0-alpha.1

- Reconstruccion desde el ejemplo LVGL oficial de Waveshare para 4.3B.
- Migracion a Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5.
- Arranque LCD antes de Wi-Fi, diagnostico serie por fases y modo seguro.
- Portal temporal por QR con prueba Wi-Fi, login LibreLinkUp, seleccion de
  usuario y ubicacion.
- Lectura actual, flecha, delta y grafica persistente de 24 horas.
- Rangos rojo, verde, amarillo y naranja solicitados.
- Reloj, clima actual y seis tramos horarios mediante Open-Meteo.
- Eliminacion completa de Alexa y alarmas.
- Distribucion de codigo fuente; la compilacion y la carga se hacen con
  PlatformIO siguiendo la configuracion del perfil oficial Waveshare 4.3B.
