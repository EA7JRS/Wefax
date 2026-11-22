# Historial de cambios

Las entradas dicen también qué se probó y qué no.

## [0.3.0] - 2026-11-22

- Recuadro **Hora UTC** en el estado, como en Navtex (con la fecha y la hora local).
- **Predicción de propagación con VOACAP** (opcional; `tools/instalar-voacap.sh` instala voacapl, la versión para Linux del programa del NTIA / ITS). El servidor web calcula para cada estación con posición la fiabilidad y la señal sobre ruido de cada frecuencia en las 24 horas, hasta la posición propia, y lo deja en `propagacion.json`. El receptor elige con esa tabla la frecuencia de cada emisión; la pestaña Estaciones la enseña en tiras de 24 horas. Sin VOACAP, sin posición o sin número de manchas, sigue la regla de día y noche.
- Ajustes nuevos: `prop.ssn` (número de manchas suavizado del mes), `prop.noise`, `prop.snr`. `tools/actualizar-manchas.sh` intenta poner el número de manchas desde el SWPC (sin probar contra el servidor real).
- `GET /api/voacap` da un circuito cualquiera a otros programas; las respuestas de lectura admiten peticiones de otras páginas de la red.
- Potencia de cada transmisor en la lista de estaciones (la que publica la NOAA).
- **Probado:** con voacapl 16.1207W real en el PC: tabla de las nueve estaciones desde Cádiz, elección de frecuencia y consulta de un circuito; `make check`, 127 comprobaciones (el programa no hace falta para ellas). **Sin probar:** el instalador de VOACAP en ningún equipo; nada de esto contra recepciones reales. Los supuestos del cálculo (antenas, ruido, umbral de 40 dB-Hz) están en el README.

## [0.2.0] - 2026-11-22

- Interfaz web (`wefaxwebserver`, puerto 8081): cartas recibidas con visor, carta en curso, horario de las próximas 24 horas con casilla por emisión, estaciones con su frecuencia, ajustes del receptor y del reparto del equipo con Navtex. Mismo aspecto, temas e idiomas (ES / EN) que Navtex. Sin contraseña: solo en una red de confianza.
- Lista de estaciones `web_server/build/estaciones.json` con nueve estaciones, hecha con `tools/estaciones/generar.py`: Boston, Nueva Orleans, Point Reyes, Kodiak y Honolulu, de los horarios en texto por estación del National Weather Service (datos del 12 de diciembre de 2024); Hamburgo / Pinneberg, Northwood, Atenas y Múrmansk (estas dos, dudosas: véase el README), de «Worldwide Marine Radiofacsimile Broadcast Schedules» de la NOAA (edición del 7 de marzo de 2025). Transcrita con un lector automático: sin cotejar línea a línea con el original.
- Posición del GPS: la interfaz la toma de gpsd (`WEFAX_GPSD`, de serie `127.0.0.1:2947`; el mismo lector que Navtex) y la muestra en un recuadro; el aspecto automático usa el sol en esa posición.
- Ayuda «Cómo leer una carta» en la pestaña Cartas: isobaras, viento, frentes, tipos de carta y escala Beaufort (redacción propia).
- **Posición propia** (GPS o escrita): estaciones por distancia, con rumbo, y frecuencia «automática» según el sol en el punto medio del camino (noche, la más baja; día, la más alta; crepúsculo, la de en medio). Regla orientativa, sin contrastar con recepciones reales. Las posiciones de los transmisores son aproximadas.
- Servicios de systemd (`wefax-web`, `wefax-receiver`) y `tools/instalar-servicios.sh`; el receptor no toca ningún equipo hasta que se enciende en la web.
- `make check` carga también la lista instalada (103 comprobaciones).
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
