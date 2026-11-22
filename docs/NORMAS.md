# Normas y reglamentos que contempla Wefax

© 2026 Manuel - EA7JRS · versión 0.2.0 · comprobado el 22 de noviembre de 2026

Este documento dice qué normas describen el radiofacsímil meteorológico por onda corta, qué parte de ellas aplica el programa y cuáles **no** tienen que ver, aunque a veces se citen. Wefax es una ayuda: no está homologado ni ensayado contra ninguna norma.

## La norma técnica: Manual n.º 386 de la OMM

El **Manual del Sistema Mundial de Telecomunicación** de la Organización Meteorológica Mundial (OMM-N.º 386), parte III, apartado 5, «Características técnicas del equipo para transmisiones meteorológicas por facsímil (analógico)», es donde están los parámetros de la señal. Consultada la edición de 2009, [publicada por la NOAA](https://www.weather.gov/media/marine/WMO_386_Vol_I_2009_en.pdf); hay edición de 2023, que no se ha podido contrastar apartado por apartado.

| Requisito | Apartado | En el programa |
| :--- | :--- | :--- |
| Velocidades: 60, 90, 120 y 240 líneas por minuto | 5.1.5 | Las cuatro; se reconocen por el enfasado |
| Índice de cooperación 576 o 288 | 5.1.2 | Los dos; 1810 y 905 puntos por línea (índice × π) |
| Señal de inicio: 5 a 10 s de negro y blanco alternando a 300 Hz (índice 576) o 675 Hz (índice 288) | 5.2.2.1 | Reconocida con 1,5 s de tono; de ella sale también el error de frecuencia del receptor |
| Señal de enfasado: 30 s, con línea asimétrica (blanco el 5 %, negro el 95 %) | 5.2.3 | Reconocida; da el comienzo de las líneas y el error del reloj del receptor |
| Señal de enfasado simétrica (mitad blanco, mitad negro) | 5.2.3.2 | **No hecho:** la carta se toma igualmente, pero puede salir desplazada |
| Señal de parada: 5 s a 450 Hz y después 10 s de negro | 5.2.5.1 | Reconocida; las líneas del tono no se guardan |
| Por radio en onda corta: negro a fo − 400 Hz y blanco a fo + 400 Hz | 5.5.2 | Así se descodifica; ±425 Hz para las estaciones que lo indican |
| Subportadora: centro 1900 Hz, negro 1500 Hz, blanco 2300 Hz | 5.5.1 | Es lo mismo visto desde un receptor de banda lateral superior sintonizado 1,9 kHz por debajo |
| Estabilidad de la exploración: ±5 partes por millón | 5.1.5 | Un RTL-SDR no la cumple: por eso el programa mide el error en el enfasado y lo corrige |
| Onda larga: ±150 Hz | 5.5.2 | No hecho |

## Los horarios y frecuencias

Los horarios, frecuencias e indicativos de cada estación los publica el Servicio Meteorológico Nacional de EE. UU. en [«Worldwide Marine Radiofacsimile Broadcast Schedules»](https://www.weather.gov/media/marine/rfax.pdf) (edición del 7 de marzo de 2025), con los datos que le da cada país. De ahí salen, en la lista del programa (`web_server/build/estaciones.json`, hecha con `tools/estaciones/generar.py`), Hamburgo / Pinneberg (datos del 24 de julio de 2024) y Northwood (5 de enero de 2022). Las cinco de EE. UU. (Boston, Nueva Orleans, Point Reyes, Kodiak y Honolulu) salen del horario en texto de cada una que publica el mismo servicio en [weather.gov/marine/radiofax_charts](https://www.weather.gov/marine/radiofax_charts), con datos del 12 de diciembre de 2024, más recientes que los del PDF. Todo se transcribió con un lector automático y **no se ha cotejado línea a línea con el original**; además los horarios cambian. La interfaz lo avisa. Northwood no da velocidad ni índice en esa publicación: se supone 120 líneas por minuto e índice 576.

## Reglamentación marítima

Lo que sigue es un resumen del marco, no una consulta de los textos: **no se han leído para este documento** y hay que tomarlo como orientación.

- El radiofacsímil **no es un equipo obligatorio** del SMSSM. La información de seguridad marítima obligatoria llega por NAVTEX y por los servicios de llamada intensificada a grupos por satélite. El radiofacsímil es un medio complementario, útil lejos de la cobertura de otros medios.
- En España, el reglamento de radiocomunicaciones marítimas a bordo de los buques civiles (Real Decreto 1185/2006) fija el equipo obligatorio por zona de navegación; el receptor de radiofacsímil queda como equipo voluntario.

## Documentos que no tratan de esto

Se consultaron porque se citan a veces junto al radiofacsímil, y no son de aplicación:

| Documento | De qué trata en realidad |
| :--- | :--- |
| Recomendación UIT-R M.1170 | Procedimientos de telegrafía Morse en el servicio móvil marítimo. No fija parámetros del facsímil |
| Recomendación UIT-R SA.1158-3 | Compartición de la banda 1670–1710 MHz entre los satélites meteorológicos y el servicio móvil por satélite. El «WEFAX» que nombra es el de satélite (1691 MHz), otro servicio |
| Documento de la OMM «Distribución de los datos procedentes de satélites meteorológicos» (UIT, seminario de Manta) | Difusión por satélite; el WEFAX que describe es también el de satélite, ya sustituido por LRIT |
| Recomendación UIT-T G.343 | Sistemas telefónicos de portadoras de 4 MHz en pares coaxiales |
| Manual de la UIT-R «Utilización del espectro radioeléctrico en meteorología» | Satélites meteorológicos, radiosondas, radares y teledetección. Su apartado 2.2.2.3, «Facsímil meteorológico (WEFAX)», es el de satélite geoestacionario; no trata las emisiones de facsímil por onda corta para buques |

## Lo que no se aplica o no se cumple

- **Homologación.** Sin ensayos ni aprobación de tipo de ninguna clase.
- **Sensibilidad y selectividad:** dependen del receptor y de la antena; no se han medido con una señal real.
- **Enfasado simétrico** y **onda larga:** no hechos.
- **Satélite:** no recibe imágenes de satélite. El WEFAX analógico de los geoestacionarios (1691 MHz) se sustituyó por LRIT / HRIT, digitales; es otro sistema y otro descodificador.
- **Seguridad de la interfaz web:** sin contraseña ni cifrado; solo para una red de confianza.
