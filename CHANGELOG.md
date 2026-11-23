# Historial de cambios

Las entradas dicen también qué se probó y qué no.

## [Sin publicar]

- **HackRF One como receptor.** Cuarto tipo de equipo (Receptor > Equipo, o `-s hackrf` con `-L`, `-V`, `-A` y `-S`). Recibe desde 1 MHz: las cartas de onda corta sin conversor; las de onda larga no. Se le piden 2 016 000 muestras por segundo y un filtro (64 coeficientes, ventana de Kaiser) deja una de cada ocho; lo que caería sobre el canal queda a más de 80 dB. Ganancias de FI y de banda base a mano, amplificador de RF y alimentación de antena. Ajustes `rx.hackrf_lna`, `rx.hackrf_vga`, `rx.hackrf_amp`. Es opcional: sin `libhackrf-dev` el programa compila igual. Solo recibe.
- **Probado:** `make check`, 191 comprobaciones (el filtro, y una emisión entera de 8 bits con ruido y una emisora fuerte donde se plegaría sobre el canal); compila contra libhackrf 2023.01.1 y sin HackRF conectado lo dice y reintenta. **Sin probar con un HackRF de verdad.**

## [0.4.0] - 2026-11-22

- **KiwiSDR como receptor.** Tercer tipo de equipo: un KiwiSDR de la red (Receptor > Equipo, o `-K equipo[:puerto]` y `-W contraseña`). Wefax abre su WebSocket, pide el canal en modo I/Q y lleva sus muestras (unas 12 000 por segundo, a la velocidad exacta que dice el KiwiSDR) a las 252 000 de los demás equipos. Siguiendo el horario solo se conecta un minuto antes de cada emisión; si está lleno o corta, lo dice y reintenta al minuto. La página enseña la posición que declara el KiwiSDR, con un botón para calcular desde ahí. Ajustes `rx.kiwi_host`, `rx.kiwi_port`, `rx.kiwi_pass`.
- **Probado:** `make check`, 183 comprobaciones (tramas WebSocket, mensajes, muestras, remuestreo con la imagen a −86 dB y una emisión entera por esa vía); el programa contra un KiwiSDR de pega (conexión, carta, «ocupado», contraseña mala, corte). **Sin probar con un KiwiSDR de verdad.**

## [0.3.0] - 2026-11-22

- **Todas las estaciones de la publicación de la NOAA**: de 9 a 21 (903 emisiones). Nuevas: Tokio (JMH, con el horario de la propia JMA de enero de 2022), Kagoshima, Pevek (onda larga, dudosa), Seúl, Bangkok, Kyodo News, Cantón, Valparaíso, Punta Arenas, Sídney de Nueva Escocia, Charleville y Wiluna. Las cerradas (Iqaluit, Resolute, Inuvik, Wellington) se nombran aparte en la pestaña Estaciones. Cada archivo de `tools/estaciones` dice su fecha y sus dudas: varias son antiguas, y la posición de las nuevas es la de la ciudad, no la del transmisor. **Sin recibir ninguna.**
- **Enfasado simétrico** (mitad de la línea en blanco, mitad en negro): el descodificador lo reconoce, con su velocidad y el error del reloj, y la carta dice «fase simétrica». El comienzo de la línea (donde el negro pasa a blanco) es un supuesto sin comprobar con la recomendación ni con una señal real. Probado con señales generadas; `make check`, 137 comprobaciones.
- **Cabecera como la de APRS LoRa+**: su logotipo, la versión bajo el nombre y la raya de verde a cian.
- **Aviso de clima espacial**: si S.A.T.-APRS+ 1.52.0 está en la red y conoce la dirección de Wefax, le manda las escalas oficiales de la NOAA (ajuste `space.wx`); con un apagón de radio (R), una tormenta geomagnética (G) o de radiación (S) en las últimas tres horas, la página lo avisa arriba. Sin S.A.T.-APRS+ no cambia nada.
- Recuadro **Hora UTC** en el estado, como en Navtex (con la fecha y la hora local).
- **Predicción de propagación con VOACAP** (opcional; `tools/instalar-voacap.sh` instala voacapl, la versión para Linux del programa del NTIA / ITS). El servidor web calcula para cada estación con posición la fiabilidad y la señal sobre ruido de cada frecuencia en las 24 horas, hasta la posición propia, y lo deja en `propagacion.json`. El receptor elige con esa tabla la frecuencia de cada emisión; la pestaña Estaciones la enseña en tiras de 24 horas. Sin VOACAP, sin posición o sin número de manchas, sigue la regla de día y noche.
- **El número de manchas solares se lee solo**, una vez al día (`wefax-manchas.timer`, `tools/actualizar-manchas.sh`): media de los índices solares diarios de la NOAA (la fuente de S.A.T.-APRS+), o estimado con el flujo, (flujo − 64) × 0,9, si faltara. Ajustes `prop.ssn_auto` y `prop.ssn_date`; `prop.ssn`, escrito a mano, manda. **Probado en un equipo real** contra el servidor de la NOAA (22-11-2026: 70, media de 30 días). Si la primera lectura falla (la web aún arrancando, sin red), el servicio lo reintenta cada 30 segundos, hasta seis veces.
- Otros ajustes nuevos: `prop.noise`, `prop.snr`.
- `GET /api/voacap` da un circuito cualquiera a otros programas; las respuestas de lectura admiten peticiones de otras páginas de la red.
- Potencia de cada transmisor en la lista de estaciones (la que publica la NOAA).
- **Probado:** con voacapl 16.1207W real en el PC: tabla de las nueve estaciones desde Cádiz, elección de frecuencia y consulta de un circuito; `make check`, 129 comprobaciones (el programa no hace falta para ellas). **Sin probar:** el instalador de VOACAP en ningún equipo; nada de esto contra recepciones reales. Los supuestos del cálculo (antenas, ruido, umbral de 40 dB-Hz) están en el README.

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
