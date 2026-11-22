# Historial de cambios

Las entradas dicen también qué se probó y qué no.

## [0.2.0] - 2026-11-22

- Interfaz web (`wefaxwebserver`, puerto 8081): cartas recibidas con visor, carta en curso, horario de las próximas 24 horas con casilla por emisión, estaciones con su frecuencia, ajustes del receptor y del reparto del equipo con Navtex. Mismo aspecto, temas e idiomas (ES / EN) que Navtex. Sin contraseña: solo en una red de confianza.
- Lista de estaciones `web_server/build/estaciones.json` con siete estaciones, hecha con `tools/estaciones/generar.py`: Boston, Nueva Orleans, Point Reyes, Kodiak y Honolulu, de los horarios en texto por estación del National Weather Service (datos del 12 de diciembre de 2024); Hamburgo / Pinneberg y Northwood, de «Worldwide Marine Radiofacsimile Broadcast Schedules» de la NOAA (edición del 7 de marzo de 2025). Transcrita con un lector automático: sin cotejar línea a línea con el original.
- Posición del GPS: la interfaz la toma de gpsd (`WEFAX_GPSD`, de serie `127.0.0.1:2947`; el mismo lector que Navtex) y la muestra en un recuadro; el aspecto automático usa el sol en esa posición.
- Ayuda «Cómo leer una carta» en la pestaña Cartas: isobaras, viento, frentes, tipos de carta y escala Beaufort (redacción propia).
- Servicios de systemd (`wefax-web`, `wefax-receiver`) y `tools/instalar-servicios.sh`; el receptor no toca ningún equipo hasta que se enciende en la web.
- `make check` carga también la lista instalada (91 comprobaciones).
- README con la instalación paso a paso, servicios, comandos, API, seguridad y solución de problemas, al modo de Navtex.
- Comprobado en un equipo real (Debian 13, API de SDRplay y librtlsdr): compila, pasa `make check`, se instala y los servicios arrancan. **Sin probar con un receptor ni con una señal real.**

## [0.1.0] - 2026-11-22

Primera versión: el descodificador y el programa receptor.

- **Descodificador** (`receiver/wefax_decoder.c`), nuevo: tono de inicio de 300 Hz (índice 576) y 675 Hz (índice 288), enfasado con pulso blanco del 5 %, 60 / 90 / 120 / 240 líneas por minuto reconocidas por el enfasado, tono de parada de 450 Hz, fin por pérdida de señal. Mide el error de frecuencia del receptor en el tono de inicio y el error de su reloj en el enfasado, y los corrige.
- **Canal** (`receiver/channel.c`): de las muestras del receptor (252 kS/s) a la frecuencia del canal de facsímil (12 kS/s).
- **Programa `wefaxreceiver`**: SDRplay, RTL-SDR o grabación; frecuencia fija (`-k`), o servicio (`-n`) que sigue el horario y los ajustes; cartas en PNG y lista en SQLite; turno con Navtex.
- Las fuentes de RTL-SDR y de grabaciones son las del receptor de [Navtex](https://github.com/EA7JRS/Navtex); la de SDRplay sigue su misma secuencia de llamadas.
- **Probado:** `make check`, 85 comprobaciones, con señales generadas por las propias pruebas y nunca emitidas. **Sin probar:** una señal real, ningún receptor de verdad, y el turno con Navtex. La parte de SDRplay no se ha podido compilar donde se escribió.
- **Sin hacer:** lista de estaciones y horarios, interfaz web, servicios.
