# Seguridad y limites

- Las conexiones a LibreLinkUp y Open-Meteo usan HTTPS y validan certificados
  con el paquete CA incluido en `firmware/certs/`.
- El primer portal usa HTTP dentro de una red WPA2 temporal. El segundo usa HTTP
  dentro de la red local domestica y solo existe mientras su QR esta visible.
- Cada fase solo acepta peticiones recibidas por su interfaz correspondiente,
  deja de servir tras guardar/cerrar/10 minutos y no devuelve contrasenas
  guardadas.
- Wi-Fi, correo y contrasena LibreLinkUp quedan en NVS sin cifrado de flash.
  Protege fisicamente el equipo y no compartas una copia completa de su flash.
- La API LibreLinkUp no es publica. Las credenciales se envian directamente del
  ESP32 a los servidores regionales, no a un servidor de este proyecto.
- Solo se admiten regiones LibreLinkUp predefinidas y los dominios de clima son
  fijos en el codigo.
- No se implementan alarmas clinicas, predicciones, dosificacion ni Alexa.
- Una lectura antigua (mas de 10 minutos) se muestra atenuada; eso es una regla
  visual, no un umbral medico.
- Comprueba siempre el valor, tendencia y alarmas en la aplicacion o receptor
  oficial antes de actuar.
