# Cambios

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
