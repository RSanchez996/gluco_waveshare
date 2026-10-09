# Componentes, versiones y licencias

La licencia MIT del proyecto original se conserva en `LICENSE` con atribución a RSanchez996. Las bibliotecas mantienen sus avisos en sus fuentes y archivos de licencia. No se cambian sus autores ni se relicencian.

| Componente | Versión | Commit | Licencia principal |
| --- | --- | --- | --- |
| ESP-IDF | v5.5.5 | b774170ff46c393eeb5e495ea37936038d3f4f4f | Apache-2.0 y licencias de componentes |
| Arduino-ESP32 | 3.3.12 | 94afccf35fb1e401facddbcf9e13bcf7c76a31d8 | LGPL-2.1; avisos adicionales por archivo |
| ESP32_Display_Panel | 1.0.4 | 12a01d6569e5c0918389793cee5776924e905975 | Apache-2.0 |
| ESP32_IO_Expander | 1.1.0 | e79a63876a1d8a834cf8ec8f8b698ff9d9374579 | Apache-2.0 |
| esp-lib-utils | 0.2.0 | bb9ea3d6d95d4f0ec9ebc9f65659a4b5d2977663 | Apache-2.0 |
| LVGL | 8.4.0 | 4495f428630cc1741bd8bfd977f080e8460e8e8d | MIT; bibliotecas internas con sus avisos |
| ArduinoJson | 6.21.5 | 40ee05c065ce248192c47eb37af7a70a6935dfa6 | MIT |
| Montserrat | Fuentes generadas de la base | Conservadas | SIL Open Font License 1.1 |

ESP-IDF no se copia entero en el ZIP: se instala desde su distribución oficial para compilar. Las restantes dependencias de aplicación se incluyen en `firmware/components` con sus fuentes y licencias. Los binarios enlazan estas dependencias; el ZIP incluye el código fuente correspondiente de aplicación/bibliotecas y la versión exacta del SDK para reconstruirlos. La licencia de Montserrat está en `third_party/Montserrat-OFL.txt`.

Cambios locales en dependencias: CMake de Display_Panel/IO_Expander declara directamente las dependencias vendorizadas al retirar los manifests del gestor; LVGL usa un registro de componente sencillo y la configuración local común; Arduino conserva su compilación selectiva con las bibliotecas necesarias. Los drivers y las fuentes de biblioteca se mantienen en sus versiones fijadas.
