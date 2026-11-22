# Historial de cambios

Las entradas dicen también qué se probó y qué no.

## [0.1.0] - 2026-11-22

Primera versión: el descodificador y el programa receptor.

- **Descodificador** (`receiver/wefax_decoder.c`), nuevo: tono de inicio de 300 Hz (índice 576) y 675 Hz (índice 288), enfasado con pulso blanco del 5 %, 60 / 90 / 120 / 240 líneas por minuto reconocidas por el enfasado, tono de parada de 450 Hz, fin por pérdida de señal. Mide el error de frecuencia del receptor en el tono de inicio y el error de su reloj en el enfasado, y los corrige.
- **Canal** (`receiver/channel.c`): de las muestras del receptor (252 kS/s) a la frecuencia del canal de facsímil (12 kS/s).
- **Programa `wefaxreceiver`**: SDRplay, RTL-SDR o grabación; frecuencia fija (`-k`), o servicio (`-n`) que sigue el horario y los ajustes; cartas en PNG y lista en SQLite; turno con Navtex.
- Las fuentes de RTL-SDR y de grabaciones son las del receptor de [Navtex](https://github.com/EA7JRS/Navtex); la de SDRplay sigue su misma secuencia de llamadas.
- **Probado:** `make check`, 85 comprobaciones, con señales generadas por las propias pruebas y nunca emitidas. **Sin probar:** una señal real, ningún receptor de verdad, y el turno con Navtex. La parte de SDRplay no se ha podido compilar donde se escribió.
- **Sin hacer:** lista de estaciones y horarios, interfaz web, servicios.
