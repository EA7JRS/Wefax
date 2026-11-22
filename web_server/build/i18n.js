/* Wefax · idioma de la interfaz: español o inglés. Se carga antes que app.js.
 * SPDX-License-Identifier: GPL-3.0-or-later · Copyright (c) 2026 Manuel - EA7JRS
 *
 * Los textos están escritos en español en la página y en app.js; aquí está su versión inglesa.
 * tr('texto') devuelve el texto en el idioma elegido. Los títulos de las cartas no se traducen:
 * son los del horario. La elección se guarda en este navegador; de serie, el idioma del navegador.
 */
window.WEFAX_I18N = (function () {
  'use strict';
  var EN = {
 "por el sol en la posición del GPS; sin ella, claro de 8 a 20 h": "by the sun at the GPS position; without it, light 8-20 h",
 "Posición GPS": "GPS position",
 "sin GPS": "no GPS",
 "no hay gpsd configurado": "no gpsd configured",
 "gpsd no contesta": "gpsd does not answer",
 "{a} de {b} satélites": "{a} of {b} satellites",
 "sin posición": "no position",
 "buscando satélites": "searching for satellites",
 "claro de día y oscuro de noche (el sol en la posición del receptor)": "light by day, dark at night (the sun at the receiver's position)",
 "Cartas meteorológicas por onda corta": "Weather charts on short wave",
 "Secciones": "Sections",
 "Cartas": "Charts",
 "Horario": "Schedule",
 "Estaciones": "Stations",
 "Receptor": "Receiver",
 "Acerca de": "About",
 "Aspecto": "Theme",
 "Idioma": "Language",
 "Sistema": "System",
 "el del equipo": "follows the device",
 "Automático": "Automatic",
 "claro de 8 a 20 h y oscuro el resto": "light 8-20 h, dark otherwise",
 "Claro": "Light",
 "Oscuro": "Dark",
 "Noche": "Night",
 "rojo, conserva la visión nocturna": "red, keeps night vision",
 "Estado": "Status",
 "Recibiendo ahora": "Receiving now",
 "Carta que se está recibiendo": "Chart being received",
 "Estación": "Station",
 "Buscar en el título": "Search the title",
 "Solo las conservadas": "Only the kept ones",
 "Emisiones de las próximas 24 horas de las estaciones que sigo, en hora UTC. El receptor sintoniza un minuto antes de cada una. Quite la marca de las que no quiera recibir. Solo se puede recibir una emisión a la vez: si dos estaciones coinciden, se recibe la que empieza antes.": "Broadcasts of the next 24 hours from the stations I follow, in UTC. The receiver tunes one minute before each one. Untick those you do not want. Only one broadcast can be received at a time: if two stations overlap, the one that starts first is received.",
 "UTC": "UTC",
 "Local": "Local",
 "Carta": "Chart",
 "Recibir": "Receive",
 "Elija las estaciones que quiere recibir. La frecuencia «automática» es la central de las que están en el aire a esa hora; si una llega mejor, elíjala. Desde España, las de 8 a 17 MHz suelen llegar mejor de día y las de 2 a 6 MHz de noche.": "Choose the stations you want to receive. The “automatic” frequency is the middle one of those on the air at that time; if one arrives better, choose it. From Spain, 8 to 17 MHz usually arrive better by day and 2 to 6 MHz at night.",
 "El receptor usa un equipo SDRplay o RTL-SDR y sigue el horario de las estaciones elegidas. Los cambios se aplican solos en unos segundos.": "The receiver uses an SDRplay or RTL-SDR device and follows the schedule of the chosen stations. Changes apply by themselves in a few seconds.",
 "Un equipo solo lo puede usar un programa a la vez. Si el receptor dice «sin equipo», puede estar desconectado o en uso por otro programa (por ejemplo, SDRconnect o SDR++): se sigue intentando cada 15 segundos. Esta página no tiene contraseña: solo en una red de confianza.": "A device can only be used by one program at a time. If the receiver says “no device”, it may be unplugged or in use by another program (for example, SDRconnect or SDR++): it keeps trying every 15 seconds. This page has no password: only on a trusted network.",
 "Receptor de radiofacsímil meteorológico (WEFAX) por software para SDRplay y RTL-SDR: sigue el horario de las estaciones de onda corta, recibe sus cartas en segundo plano y las guarda como imágenes.": "Software weather radiofacsimile (WEFAX) receiver for SDRplay and RTL-SDR: it follows the schedule of the short wave stations, receives their charts in the background and stores them as images.",
 "Wefax": "Wefax",
 "Horarios: NOAA / National Weather Service (horarios por estación y «Worldwide Marine Radiofacsimile Broadcast Schedules»). Los horarios cambian: compruebe la edición vigente.": "Schedules: NOAA / National Weather Service (station schedules and “Worldwide Marine Radiofacsimile Broadcast Schedules”). Schedules change: check the current edition.",
 "La capa del receptor deriva de": "The receiver layer derives from",
 ", de bartelvdh (MIT). Este programa es software libre, bajo licencia GPL-3.0 o posterior, y se ofrece sin ninguna garantía.": ", by bartelvdh (MIT). This program is free software, under the GPL-3.0-or-later licence, and comes with no warranty.",
 "La señal sigue el Manual del Sistema Mundial de Telecomunicación de la OMM (n.º 386). El detalle está en": "The signal follows the WMO Manual on the Global Telecommunication System (No. 386). The detail is in",
 "· parte del ecosistema APRS LoRa+": "· part of the APRS LoRa+ ecosystem",
 "Donar con PayPal": "Donate with PayPal",
 "Las cartas se muestran tal como se reciben. Este programa es una ayuda y no sustituye a la información meteorológica oficial ni a los equipos obligatorios a bordo.": "Charts are shown as received. This program is an aid and does not replace official weather information or the equipment required on board.",
 "Conservar": "Keep",
 "Guardar": "Save",
 "Cerrar": "Close",
 "el servidor respondió ": "the server answered ",
 "Guardado. El receptor lo aplica en unos segundos.": "Saved. The receiver applies it in a few seconds.",
 "No se pudo guardar: ": "Could not save: ",
 "sin identificar": "not identified",
 "ahora mismo": "just now",
 "hace ": "",
 " días": " days",
 "Apagado": "Off",
 "Parado": "Stopped",
 "A la escucha": "Listening",
 "Recibiendo una carta": "Receiving a chart",
 "Sin equipo": "No device",
 "Sintonizado, a la espera de la emisión": "Tuned, waiting for the broadcast",
 "A la escucha en una frecuencia fija": "Listening on a fixed frequency",
 "No sigue ninguna estación": "No station followed",
 "Sin emisiones en el horario": "Nothing in the schedule",
 "En espera: el equipo lo tiene Navtex": "Waiting: Navtex has the device",
 "Navtex no responde: no se puede tomar el equipo compartido": "Navtex does not answer: the shared device cannot be taken",
 "carta": "chart",
 "sincronía": "phasing",
 "tono de inicio": "start tone",
 "en espera": "idle",
 "Sin datos del receptor": "No data from the receiver",
 "El receptor no responde": "The receiver does not answer",
 "Frecuencia": "Frequency",
 "en banda lateral superior: ": "in upper sideband: ",
 "Decodificador": "Decoder",
 " líneas": " lines",
 "desvío ": "offset ",
 "Próxima emisión": "Next broadcast",
 "Cartas guardadas": "Charts stored",
 "la última, ": "the last one, ",
 "completa": "complete",
 "cortada a los 30 minutos": "cut at 30 minutes",
 "señal perdida": "signal lost",
 "cortada": "cut",
 "Todas": "All",
 "Carta sin título": "Untitled chart",
 "sin sincronía": "not phased",
 "conservada": "kept",
 " de ": " of ",
 " cartas": " charts",
 "Las que no se conservan se borran a los ": "Those not kept are removed after ",
 "Ninguna carta con ese filtro.": "No chart with that filter.",
 "Aún no se ha recibido ninguna carta. Elija las estaciones y encienda el receptor.": "No chart has been received yet. Choose the stations and turn the receiver on.",
 " líneas por minuto": " lines per minute",
 "índice ": "index ",
 "reloj ": "clock ",
 "Sin emisiones en las próximas 24 horas.": "No broadcasts in the next 24 hours.",
 "No sigue ninguna estación: elíjalas en Estaciones.": "No station followed: choose them in Stations.",
 " emisiones al día": " broadcasts a day",
 "Seguir esta estación": "Follow this station",
 "ahora no": "not now",
 "todo el día": "all day",
 "Frecuencia que se usa": "Frequency used",
 "Automática": "Automatic",
 "Horarios: ": "Schedules: ",
 ", edición del ": ", edition of ",
 "Transcritos de forma automática: pueden tener errores y los horarios cambian; compruebe una hora con la fuente antes de fiarse de ella.": "Transcribed automatically: they may contain errors and schedules change; check a time against the source before relying on it.",
 "No se pudo leer la lista de estaciones.": "The list of stations could not be read.",
 "Equipo": "Device",
 "Encendido": "On",
 "Número de serie (si hay varios)": "Serial number (if there are several)",
 "cualquiera": "any",
 "Toma de antena (RSPdx)": "Antenna port (RSPdx)",
 "Muestreo directo": "Direct sampling",
 "Automático, según el modelo": "Automatic, by model",
 "Rama Q": "Q branch",
 "Rama I": "I branch",
 "Apagado (conversor)": "Off (upconverter)",
 "Conversor externo (Hz; 0 = ninguno)": "External upconverter (Hz; 0 = none)",
 "Ganancia (dB, o auto)": "Gain (dB, or auto)",
 "Corrección (ppm)": "Correction (ppm)",
 "Alimentar la antena por el coaxial": "Power the antenna through the coax",
 "Qué se recibe": "What is received",
 "Frecuencia fija (kHz; 0 = seguir el horario)": "Fixed frequency (kHz; 0 = follow the schedule)",
 "Líneas por minuto en la frecuencia fija": "Lines per minute on the fixed frequency",
 "Días que se guarda una carta sin conservar": "Days a chart is stored unless kept",
 "Con una frecuencia fija el receptor se queda en ella y guarda todo lo que oiga, sin mirar el horario. Escriba la frecuencia asignada (la central del canal), no la del dial en banda lateral.": "With a fixed frequency the receiver stays on it and stores everything it hears, without looking at the schedule. Write the assigned frequency (the centre of the channel), not the sideband dial one.",
 "Compartir el equipo con Navtex": "Sharing the device with Navtex",
 "Tomar el equipo de Navtex durante cada emisión": "Take the Navtex device during each broadcast",
 "Dirección de Navtex": "Navtex address",
 "Receptor de Navtex que usa ese equipo": "Navtex receiver that uses that device",
 "Un minuto antes de cada emisión Wefax apaga ese receptor de Navtex, recibe la carta y lo vuelve a encender. Mientras tanto Navtex no recibe con él. Con un equipo para cada programa, déjelo apagado.": "One minute before each broadcast Wefax turns that Navtex receiver off, receives the chart and turns it on again. Meanwhile Navtex does not receive with it. With one device for each program, leave this off.",
 "No se puede leer el estado: ": "The status cannot be read: ",
 "Aspecto: ": "Theme: ",
 " (ahora ": " (now "
};
  function elegido() {
    var l = null;
    try { l = localStorage.getItem('wefax-idioma'); } catch (e) { /* sin almacenamiento */ }
    if (l === 'es' || l === 'en') return l;
    return /^es\b/i.test(navigator.language || 'es') ? 'es' : 'en';
  }
  var idioma = elegido();
  function tr(texto) {
    if (idioma !== 'en') return texto;
    return Object.prototype.hasOwnProperty.call(EN, texto) ? EN[texto] : texto;
  }
  // El texto de un nodo, sin sus espacios de alrededor, que se conservan
  function traducir(valor) {
    var m = /^(\s*)([\s\S]*?)(\s*)$/.exec(valor);
    var clave = m[2].replace(/\s+/g, ' ');
    return clave && Object.prototype.hasOwnProperty.call(EN, clave) ? m[1] + EN[clave] + m[3] : null;
  }
  function pagina() {
    document.documentElement.lang = idioma;
    if (idioma !== 'en') return;
    var w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null), n, nuevo;
    while ((n = w.nextNode())) {
      if (n.parentNode && /^(SCRIPT|STYLE|PRE)$/.test(n.parentNode.nodeName)) continue;
      nuevo = traducir(n.nodeValue);
      if (nuevo !== null) n.nodeValue = nuevo;
    }
    Array.prototype.forEach.call(document.querySelectorAll('[placeholder],[aria-label],[title],[alt]'), function (e) {
      ['placeholder', 'aria-label', 'title', 'alt'].forEach(function (a) {
        var v = e.getAttribute(a), t = v ? traducir(v) : null;
        if (t !== null) e.setAttribute(a, t);
      });
    });
  }
  function poner(l) {
    try { localStorage.setItem('wefax-idioma', l === 'en' ? 'en' : 'es'); } catch (e) { /* no se recuerda */ }
    location.reload();
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', pagina); else pagina();
  return { idioma: idioma, tr: tr, poner: poner };
})();
