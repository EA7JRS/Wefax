/*
 * Wefax · interfaz web
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * Lee /api/status, /api/charts, /api/settings y estaciones.json, y cambia los ajustes con
 * POST /api/settings/<nombre>/<valor>. Todo lo que viene del servidor se escribe como texto.
 */
(function () {
  'use strict';
  var I18N = window.WEFAX_I18N, tr = I18N.tr;
  function $(id) { return document.getElementById(id); }
  function el(tag, clase, texto) {
    var e = document.createElement(tag);
    if (clase) e.className = clase;
    if (texto !== undefined && texto !== null) e.textContent = texto;
    return e;
  }
  function dos(n) { return (n < 10 ? '0' : '') + n; }
  function coma(n, dec) { var s = Number(n).toFixed(dec); return I18N.idioma === 'en' ? s : s.replace('.', ','); }
  function khz(k) { return coma(k, Math.abs(k * 10 - Math.round(k * 10)) < 0.01 && Math.round(k * 10) % 10 === 0 ? 0 : 1) + ' kHz'; }

  var estado = {}, ajustes = {}, cartas = [], datos = { stations: [] }, version = '', gps = null, tabla = {}, tablaEstado = null;
  var filtro = { estacion: '', texto: '', guardadas: false };
  var repintarAspecto = function () {};

  function leer(ruta) {
    return fetch(ruta, { cache: 'no-store' }).then(function (r) {
      if (!r.ok) throw new Error(tr('el servidor respondió ') + r.status);
      return r.json();
    });
  }
  function enviar(ruta) {
    return fetch(ruta, { method: 'POST', cache: 'no-store', headers: { 'X-Wefax': '1' } }).then(function (r) {
      if (!r.ok) throw new Error(tr('el servidor respondió ') + r.status);
    });
  }
  function guardar(nombre, valor, idAviso) {
    ajustes[nombre] = valor;
    return enviar('/api/settings/' + encodeURIComponent(nombre) + '/' + (valor === '' ? '-' : encodeURIComponent(valor))).then(function () {
      avisar(idAviso, true, tr('Guardado. El receptor lo aplica en unos segundos.'));
    }).catch(function (e) { avisar(idAviso, false, tr('No se pudo guardar: ') + e.message + '.'); });
  }
  function avisar(id, bien, texto) {
    var a = $(id);
    a.hidden = false;
    a.className = bien ? 'aviso bien' : 'aviso';
    a.textContent = texto;
  }
  function st(nombre) { return estado[nombre] ? estado[nombre].value : ''; }
  function ajuste(nombre, def) { return ajustes[nombre] !== undefined ? ajustes[nombre] : def; }
  function lista(nombre) { return ajuste(nombre, '').split(',').filter(function (x) { return x; }); }
  function estacion(id) {
    for (var i = 0; i < datos.stations.length; i++) if (datos.stations[i].id === id) return datos.stations[i];
    return null;
  }
  function nombreEstacion(id) { var s = estacion(id); return s ? s.name : (id || tr('sin identificar')); }

  // ---- horas
  function horaUtc(d) { return dos(d.getUTCHours()) + ':' + dos(d.getUTCMinutes()); }
  function horaLocal(d) { return dos(d.getHours()) + ':' + dos(d.getMinutes()); }
  function fechaUtc(d) { return d.getUTCFullYear() + '-' + dos(d.getUTCMonth() + 1) + '-' + dos(d.getUTCDate()); }
  function minutos(hhmm) { return Number(hhmm.slice(0, 2)) * 60 + Number(hhmm.slice(2)); }
  // ¿está la frecuencia en el aire a ese minuto UTC del día?
  function enElAire(f, min) {
    if (!f.from || !f.to) return true;
    var a = minutos(f.from), b = minutos(f.to);
    return a <= b ? (min >= a && min < b) : (min >= a || min < b);
  }
  function haceCuanto(ms) {
    var s = Math.round((Date.now() - ms) / 1000);
    if (s < 90) return tr('ahora mismo');
    if (s < 5400) return tr('hace ') + Math.round(s / 60) + ' min';
    if (s < 129600) return tr('hace ') + Math.round(s / 3600) + ' h';
    return tr('hace ') + Math.round(s / 86400) + tr(' días');
  }

  // ---- posición del GPS (gpsd), como en Navtex
  function posicionGps() {
    return gps && gps.connected && typeof gps.lat === 'number' && typeof gps.lon === 'number' && !(gps.age > 120) ? { lat: gps.lat, lon: gps.lon } : null;
  }
  function localizador(p) {
    var lo = p.lon + 180, la = p.lat + 90;
    if (!(lo >= 0 && lo < 360 && la >= 0 && la < 180)) return '';
    return String.fromCharCode(65 + Math.floor(lo / 20)) + String.fromCharCode(65 + Math.floor(la / 10))
      + Math.floor((lo % 20) / 2) + Math.floor(la % 10)
      + String.fromCharCode(97 + Math.floor((lo % 2) * 12)) + String.fromCharCode(97 + Math.floor((la % 1) * 24));
  }
  // "37° 36,46' N  000° 59,09' O": como se lee en una carta
  function gradosMinutos(v, positivo, negativo, ancho) {
    var a = Math.abs(v), g = Math.floor(a), m = (a - g) * 60;
    if (m >= 59.995) { g += 1; m = 0; }
    var gs = String(g); while (gs.length < ancho) gs = '0' + gs;
    return gs + '° ' + (m < 10 ? '0' : '') + coma(m, 2) + "' " + (v >= 0 ? positivo : negativo);
  }
  function enCarta(p) { return gradosMinutos(p.lat, 'N', 'S', 2) + '\n' + gradosMinutos(p.lon, 'E', I18N.idioma === 'en' ? 'W' : 'O', 3); }
  function datoGps() {
    var p = posicionGps(), v, x = '';
    if (!gps) v = '—';
    else if (!gps.enabled) { v = tr('sin GPS'); x = tr('no hay gpsd configurado'); }
    else if (!gps.connected) { v = tr('sin GPS'); x = tr('gpsd no contesta'); }
    else {
      var sat = typeof gps.used === 'number' ? tr('{a} de {b} satélites').replace('{a}', gps.used).replace('{b}', gps.visible) : '';
      if (!p) { v = tr('sin posición'); x = tr('buscando satélites') + (sat ? ' · ' + sat : ''); }
      else {
        v = enCarta(p);
        x = localizador(p) + ' · ' + (gps.fix >= 3 ? '3D' : '2D') + (sat ? ' · ' + sat : '') + (typeof gps.alt === 'number' ? ' · ' + Math.round(gps.alt) + ' m' : '');
      }
    }
    var d = dato(tr('Posición GPS'), v, x);
    d.querySelector('.valor').className = 'valor mono gps';
    return d;
  }

  // ---- posición propia: la del GPS o la escrita; distancia, rumbo y sol en el camino
  function leerPosicion(texto) {
    var t = String(texto).trim();
    var m = /^(-?\d{1,2}(?:[.,]\d+)?)\s*[,; ]\s*(-?\d{1,3}(?:[.,]\d+)?)$/.exec(t);
    if (m) {
      var lat = parseFloat(m[1].replace(',', '.')), lon = parseFloat(m[2].replace(',', '.'));
      return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180 ? { lat: lat, lon: lon } : null;
    }
    var g = /^([A-Ra-r])([A-Ra-r])(\d)(\d)(?:([A-Xa-x])([A-Xa-x])(?:(\d)(\d))?)?$/.exec(t);
    if (!g) return null;
    var c = function (x) { return x.toUpperCase().charCodeAt(0) - 65; };
    var lo = c(g[1]) * 20 - 180 + Number(g[3]) * 2, la = c(g[2]) * 10 - 90 + Number(g[4]);
    var anchoLo = 2, anchoLa = 1;
    if (g[5]) { lo += c(g[5]) * (2 / 24); la += c(g[6]) * (1 / 24); anchoLo = 2 / 24; anchoLa = 1 / 24; }
    if (g[7]) { lo += Number(g[7]) * (2 / 240); la += Number(g[8]) * (1 / 240); anchoLo = 2 / 240; anchoLa = 1 / 240; }
    return { lat: la + anchoLa / 2, lon: lo + anchoLo / 2 };   // centro del cuadro
  }
  function posicion() {
    var g = posicionGps(), m = /^(-?[0-9.]+),(-?[0-9.]+)$/.exec(ajuste('position', ''));
    if (ajuste('position_source', 'gps') === 'gps' && g) return g;
    return m ? { lat: Number(m[1]), lon: Number(m[2]) } : null;
  }
  var RAD = Math.PI / 180;
  function distanciaKm(a, b) {
    var x = Math.sin((b.lat - a.lat) * RAD / 2), y = Math.sin((b.lon - a.lon) * RAD / 2);
    return 2 * 6371 * Math.asin(Math.sqrt(Math.min(1, x * x + Math.cos(a.lat * RAD) * Math.cos(b.lat * RAD) * y * y)));
  }
  function rumbo(a, b) {
    var dl = (b.lon - a.lon) * RAD;
    var r = Math.atan2(Math.sin(dl) * Math.cos(b.lat * RAD), Math.cos(a.lat * RAD) * Math.sin(b.lat * RAD) - Math.sin(a.lat * RAD) * Math.cos(b.lat * RAD) * Math.cos(dl)) / RAD;
    return (Math.round(r) + 360) % 360;
  }
  // Altura del sol en grados (fórmulas simplificadas de la NOAA): las mismas que usa el receptor
  function alturaSol(lat, lon, ms) {
    var n = ms / 86400000 + 2440587.5 - 2451545.0;
    var L = (280.46 + 0.9856474 * n) % 360, g = ((357.528 + 0.9856003 * n) % 360) * RAD;
    var lam = (L + 1.915 * Math.sin(g) + 0.02 * Math.sin(2 * g)) * RAD, eps = (23.439 - 0.0000004 * n) * RAD;
    var dec = Math.asin(Math.sin(eps) * Math.sin(lam)), ra = Math.atan2(Math.cos(eps) * Math.sin(lam), Math.cos(lam));
    var gmst = (18.697374558 + 24.06570982441908 * n) % 24, ha = (gmst * 15 + lon) * RAD - ra;
    return Math.asin(Math.sin(lat * RAD) * Math.sin(dec) + Math.cos(lat * RAD) * Math.cos(dec) * Math.cos(ha)) / RAD;
  }
  // La regla del receptor (schedule_pick): devuelve { khz, por } con por = 'n', 't', 'd' o 'm'
  function elegir(frecs, p, s, ms) {
    var f = frecs.slice().sort(function (a, b) { return a - b; }), i;
    if (!f.length) return { khz: 0, por: 'm' };
    if (!p || typeof s.lat !== 'number') return { khz: frecs[Math.floor(frecs.length / 2)], por: 'm' };
    var x = Math.cos(p.lat * RAD) * Math.cos(p.lon * RAD) + Math.cos(s.lat * RAD) * Math.cos(s.lon * RAD);
    var y = Math.cos(p.lat * RAD) * Math.sin(p.lon * RAD) + Math.cos(s.lat * RAD) * Math.sin(s.lon * RAD);
    var z = Math.sin(p.lat * RAD) + Math.sin(s.lat * RAD);
    var sol = alturaSol(Math.atan2(z, Math.sqrt(x * x + y * y)) / RAD, Math.atan2(y, x) / RAD, ms);
    if (sol < -6) { for (i = 0; i < f.length; i++) if (f[i] >= 3000) return { khz: f[i], por: 'n' }; return { khz: f[0], por: 'n' }; }
    if (sol <= 6) return { khz: f[Math.floor(f.length / 2)], por: 't' };
    if (distanciaKm(p, s) < 3000) for (i = f.length - 1; i >= 0; i--) if (f[i] <= 14500 || i === 0) return { khz: f[i], por: 'd' };
    return { khz: f[f.length - 1], por: 'd' };
  }
  var CORTO = { n: 'noche', t: 'crepúsculo', d: 'día' };
  // Tabla de VOACAP: fiabilidad (0..1) de una frecuencia de una estación a una hora UTC; null si no la da
  function tablaVale() {
    var d = new Date();
    return tabla && tabla.stations && tabla.year === d.getUTCFullYear() && tabla.month === d.getUTCMonth() + 1;
  }
  function fiabilidad(id, k, hora) {
    var e = tablaVale() ? tabla.stations[id] : null, i;
    if (!e) return null;
    for (i = 0; i < e.khz.length; i++) if (Math.abs(e.khz[i] - k) < 0.05) return typeof e.rel[i][hora] === 'number' ? e.rel[i][hora] : null;
    return null;
  }
  function senal(id, k, hora) {
    var e = tablaVale() ? tabla.stations[id] : null, i;
    if (!e) return -1e9;
    for (i = 0; i < e.khz.length; i++) if (Math.abs(e.khz[i] - k) < 0.05) return typeof e.snr[i][hora] === 'number' ? e.snr[i][hora] : -1e9;
    return -1e9;
  }
  function elegirTabla(frecs, id, ms) {
    var h = new Date(ms).getUTCHours(), mejor = null;
    frecs.forEach(function (k) {
      var r = fiabilidad(id, k, h), q = senal(id, k, h);
      // la fiabilidad primero; entre iguales, la señal más fuerte (como el receptor)
      if (r !== null && (mejor === null || r > mejor.rel + 0.005 || (r > mejor.rel - 0.005 && q > mejor.snr))) mejor = { khz: k, por: 'v', rel: r, snr: q };
    });
    return mejor;
  }
  function pct(r) { return Math.round(r * 100) + ' %'; }
  var POR = { n: 'de noche en el camino', t: 'crepúsculo en el camino', d: 'de día en el camino', m: 'la de en medio' };
  function pintarPosicion() {
    var g = posicionGps(), p = posicion(), a = $('posicion-actual');
    $('usar-gps').checked = ajuste('position_source', 'gps') === 'gps';
    if (!p) a.textContent = tr('Sin posición: escríbala aquí si no hay GPS.');
    else a.textContent = (ajuste('position_source', 'gps') === 'gps' && g ? tr('En uso: la del GPS, ') : tr('En uso: la escrita, ')) + coma(p.lat, 2) + ', ' + coma(p.lon, 2) + ' (' + localizador(p) + ').';
  }

  // ---- estado
  var ESTADOS = {
    'off': 'Apagado', 'stopped': 'Parado', 'listening': 'A la escucha', 'receiving a chart': 'Recibiendo una carta',
    'no receiver': 'Sin equipo', 'tuned, waiting for the broadcast': 'Sintonizado, a la espera de la emisión',
    'listening on a fixed frequency': 'A la escucha en una frecuencia fija', 'no station followed': 'No sigue ninguna estación',
    'nothing in the schedule for the stations followed': 'Sin emisiones en el horario', 'waiting: the receiver is with Navtex': 'En espera: el equipo lo tiene Navtex',
    'Navtex does not answer: cannot take the shared receiver': 'Navtex no responde: no se puede tomar el equipo compartido'
  };
  var DECOD = { chart: 'carta', phasing: 'sincronía', start: 'tono de inicio', idle: 'en espera' };
  function dato(etiqueta, valor, detalle, punto) {
    var d = el('div', 'dato'), v = el('span', 'valor');
    d.appendChild(el('span', 'etiqueta', etiqueta));
    if (punto) v.appendChild(el('span', 'punto ' + punto));
    v.appendChild(document.createTextNode(valor));
    d.appendChild(v);
    if (detalle) d.appendChild(el('span', 'detalle', detalle));
    return d;
  }
  function vivo() { var a = estado.alive ? Number(estado.alive.at) : 0; return a && Date.now() / 1000 - a < 30; }
  function recibiendo() { return vivo() && st('decoder') === 'chart'; }
  // Clima espacial de ahora (escalas de la NOAA que manda S.A.T.-APRS+): lo que la predicción del mes no ve
  function pintarClimaEspacial() {
    var a = $('aviso-espacial'), m = /^R([0-5])S([0-5])G([0-5])T(\d{9,11})$/.exec(ajuste('space.wx', '')), partes = [];
    // vale tres horas; después, sin noticias, no se dice nada
    if (!m || Date.now() / 1000 - Number(m[4]) > 3 * 3600 || (m[1] === '0' && m[2] === '0' && m[3] === '0')) { a.hidden = true; return; }
    if (m[1] !== '0') partes.push(tr('Apagón de radio R') + m[1] + tr(': en el lado de día las frecuencias bajas se absorben; pruebe las más altas.'));
    if (m[3] !== '0') partes.push(tr('Tormenta geomagnética G') + m[3] + tr(': peor recepción, sobre todo de las estaciones del norte, y frecuencias altas menos fiables.'));
    if (m[2] !== '0') partes.push(tr('Tormenta de radiación S') + m[2] + tr(': los caminos por latitudes altas pueden cerrarse.'));
    a.hidden = false;
    a.textContent = tr('Clima espacial ahora (NOAA, ') + horaUtc(new Date(Number(m[4]) * 1000)) + ' UTC). ' + partes.join(' ') + ' ' + tr('La predicción de VOACAP es la mediana del mes y no lo tiene en cuenta.');
  }
  function pintarHora() {
    var d = new Date(), u = $('hora-utc'), l = $('hora-local');
    if (u) u.textContent = dos(d.getUTCHours()) + ':' + dos(d.getUTCMinutes()) + ':' + dos(d.getUTCSeconds());
    if (l) l.textContent = fechaUtc(d) + ' · ' + tr('local ') + horaLocal(d);
  }
  function pintarEstado() {
    var c = $('estado'), s = st('state'), ok = vivo();
    c.textContent = '';
    var texto = ESTADOS[s] ? tr(ESTADOS[s]) : (s || tr('Sin datos del receptor'));
    if (!ok && s && s !== 'off' && s !== 'stopped' && s !== 'no receiver' && !/Navtex|schedule|followed/.test(s)) texto = tr('El receptor no responde');
    c.appendChild(dato(tr('Receptor'), texto, estado.state ? haceCuanto(estado.state.at * 1000) : '', ok ? 'ok' : (s === 'off' || !s ? '' : 'mal')));
    var k = Number(st('tuned_khz'));
    c.appendChild(dato(tr('Frecuencia'), ok && k ? khz(k) : '—', ok && k ? tr('en banda lateral superior: ') + khz(k - 1.9) : ''));
    var dec = st('decoder');
    c.appendChild(dato(tr('Decodificador'), ok && dec ? tr(DECOD[dec] || dec) : '—',
      recibiendo() ? Math.round(Number(st('lines'))) + tr(' líneas') + ' · ' + tr('desvío ') + coma(Number(st('offset_hz')), 0) + ' Hz' : ''));
    var prox = st('next_start');
    if (prox) {
      var d = new Date(prox);
      c.appendChild(dato(tr('Próxima emisión'), horaUtc(d) + ' UTC · ' + nombreEstacion(st('next_station')), st('next_title') + (Number(st('next_khz')) ? ' · ' + khz(Number(st('next_khz'))) : '')
        + (st('next_why') === 'v' && Number(st('next_rel')) >= 0 ? ' (VOACAP ' + pct(Number(st('next_rel'))) + ')' : '')
        + (POR[st('next_why')] && st('next_why') !== 'm' && ajuste('freq.' + st('next_station'), 'auto') === 'auto' ? ' (' + tr(POR[st('next_why')]) + ')' : '')));
    } else c.appendChild(dato(tr('Próxima emisión'), '—', ''));
    c.appendChild(dato(tr('Cartas guardadas'), String(cartas.length), cartas.length ? tr('la última, ') + haceCuanto(Date.parse(cartas[0].received)) : ''));
    // Hora UTC, como en Navtex: los horarios de las emisiones van en UTC
    var reloj = dato(tr('Hora UTC'), '', '');
    reloj.querySelector('.valor').className = 'valor mono'; reloj.querySelector('.valor').id = 'hora-utc';
    reloj.appendChild(el('span', 'detalle')); reloj.lastChild.id = 'hora-local';
    c.appendChild(reloj);
    pintarHora();
    c.appendChild(datoGps());
  }

  // ---- cartas
  var FIN = { stop: 'completa', long: 'cortada a los 30 minutos', lost: 'señal perdida', cut: 'cortada' };
  function pintarDirecto() {
    var d = $('directo');
    if (!recibiendo()) { d.hidden = true; return; }
    d.hidden = false;
    $('directo-texto').textContent = nombreEstacion(st('station')) + (st('title') ? ' · ' + st('title') : '') + ' · ' + khz(Number(st('tuned_khz'))) + ' · ' + Math.round(Number(st('lines'))) + tr(' líneas');
    $('directo-img').src = '/live.png?t=' + Math.floor(Date.now() / 20000);
  }
  function pintarFiltroEstaciones() {
    var g = $('filtro-estaciones'), vistas = {}, ids = [''];
    cartas.forEach(function (c) { if (!vistas[c.station]) { vistas[c.station] = 1; ids.push(c.station); } });
    g.textContent = '';
    ids.forEach(function (id, i) {
      if (i > 0 && id === '') return;
      var b = el('button', 'chip' + (filtro.estacion === (i === 0 ? '' : id) && (i === 0 || filtro.estacion !== '') ? ' activa' : ''), i === 0 ? tr('Todas') : nombreEstacion(id));
      b.type = 'button';
      b.addEventListener('click', function () { filtro.estacion = i === 0 ? '' : id; pintarCartas(); });
      g.appendChild(b);
    });
  }
  function pintarCartas() {
    var ol = $('cartas'), n = 0, q = filtro.texto.toLowerCase();
    pintarFiltroEstaciones();
    ol.textContent = '';
    cartas.forEach(function (c) {
      if (filtro.estacion && c.station !== filtro.estacion) return;
      if (filtro.guardadas && !c.keep) return;
      if (q && (c.title + ' ' + nombreEstacion(c.station)).toLowerCase().indexOf(q) < 0) return;
      n++;
      var li = el('li', 'carta'), b = el('button'), img = el('img', 'mini'), pie = el('div', 'pie'), d = new Date(c.received);
      b.type = 'button';
      img.loading = 'lazy'; img.alt = ''; img.src = '/chart/' + c.id + '.png';
      pie.appendChild(el('span', 'titulo', c.title || tr('Carta sin título')));
      pie.appendChild(el('span', 'meta', fechaUtc(d) + ' ' + horaUtc(d) + ' UTC · ' + nombreEstacion(c.station) + ' · ' + khz(c.khz)));
      pie.appendChild(el('span', 'meta', c.height + tr(' líneas') + ' · ' + tr(FIN[c.ended] || c.ended) + (c.phased ? '' : ' · ' + tr('sin sincronía')) + (c.keep ? ' · ★ ' + tr('conservada') : '')));
      b.appendChild(img); b.appendChild(pie);
      b.addEventListener('click', function () { abrir(c); });
      li.appendChild(b); ol.appendChild(li);
    });
    $('resumen').textContent = cartas.length ? n + tr(' de ') + cartas.length + tr(' cartas') + '. ' + tr('Las que no se conservan se borran a los ') + ajuste('retention_days', '30') + tr(' días') + '.' : '';
    $('vacio').hidden = n > 0;
    $('vacio').textContent = cartas.length ? tr('Ninguna carta con ese filtro.') : tr('Aún no se ha recibido ninguna carta. Elija las estaciones y encienda el receptor.');
  }
  var abierta = null;
  function abrir(c) {
    var d = new Date(c.received);
    abierta = c;
    $('visor-titulo').textContent = (c.title || tr('Carta sin título')) + ' · ' + nombreEstacion(c.station);
    $('visor-img').src = '/chart/' + c.id + '.png';
    $('visor-descargar').href = '/chart/' + c.id + '.png';
    $('visor-descargar').setAttribute('download', 'wefax_' + fechaUtc(d).replace(/-/g, '') + '_' + horaUtc(d).replace(':', '') + '_' + (c.station || 'x') + '.png');
    $('visor-conservar').checked = !!c.keep;
    $('visor-datos').textContent = fechaUtc(d) + ' ' + horaUtc(d) + ' UTC · ' + khz(c.khz) + ' · ' + c.lpm + tr(' líneas por minuto') + ' · ' + tr('índice ') + c.ioc +
      ' · ' + c.width + ' × ' + c.height + ' · ' + tr(FIN[c.ended] || c.ended) + ' · ' + tr('desvío ') + coma(c.offset_hz, 0) + ' Hz · ' + tr('reloj ') + coma(c.clock_ppm, 0) + ' ppm';
    $('visor').showModal();
  }

  // ---- horario
  function emisiones(horas) {
    var ahora = Date.now(), sigue = lista('follow'), salta = lista('skip'), dia = Date.UTC(new Date().getUTCFullYear(), new Date().getUTCMonth(), new Date().getUTCDate()), out = [];
    datos.stations.forEach(function (s) {
      if (sigue.indexOf(s.id) < 0) return;
      var bs = s.broadcasts;
      for (var d = -1; d <= 1; d++) bs.forEach(function (b, i) {
        var ini = dia + d * 86400000 + minutos(b.utc) * 60000, sig = bs[(i + 1) % bs.length];
        var dur = ((minutos(sig.utc) - minutos(b.utc) + 1440) % 1440 || 1440) * 60000;
        dur = Math.max(8 * 60000, Math.min(30 * 60000, dur));
        if (ini + dur < ahora || ini > ahora + horas * 3600000) return;
        out.push({ s: s, b: b, ini: ini, fin: ini + dur, no: salta.indexOf(s.id + ':' + b.utc) >= 0 });
      });
    });
    out.sort(function (a, b) { return a.ini - b.ini; });
    return out;
  }
  function pintarHorario() {
    var tb = $('horario'), es = emisiones(24), ahora = Date.now();
    tb.textContent = '';
    es.forEach(function (e) {
      var trw = el('tr', (e.ini <= ahora ? 'ahora' : '') + (e.no ? ' no' : '')), d = new Date(e.ini), td = el('td'), c = el('input');
      trw.appendChild(el('td', 'hora', horaUtc(d)));
      trw.appendChild(el('td', '', horaLocal(d)));
      trw.appendChild(el('td', '', e.s.name));
      trw.appendChild(el('td', '', e.b.title));
      c.type = 'checkbox'; c.checked = !e.no; c.setAttribute('aria-label', tr('Recibir'));
      c.addEventListener('change', function () {
        var clave = e.s.id + ':' + e.b.utc, l = lista('skip').filter(function (x) { return x !== clave; });
        if (!c.checked) l.push(clave);
        guardar('skip', l.join(','), 'estado-horario').then(pintarHorario);
      });
      td.appendChild(c); trw.appendChild(td); tb.appendChild(trw);
    });
    $('horario-vacio').hidden = es.length > 0;
    $('horario-vacio').textContent = lista('follow').length ? tr('Sin emisiones en las próximas 24 horas.') : tr('No sigue ninguna estación: elíjalas en Estaciones.');
  }

  // ---- estaciones
  function pintarEstaciones() {
    var c = $('estaciones'), sigue = lista('follow'), min = new Date().getUTCHours() * 60 + new Date().getUTCMinutes();
    var yo = posicion(), ahoraMs = Date.now();
    pintarPosicion();
    c.textContent = '';
    datos.stations.map(function (s) {
      return { s: s, d: yo && typeof s.lat === 'number' ? distanciaKm(yo, s) : null };
    }).sort(function (a, b) { return a.d === null || b.d === null ? 0 : a.d - b.d; }).forEach(function (par) {
      var s = par.s;
      var t = el('div', 'tarjeta'), lab = el('label', 'interruptor'), chk = el('input'), ul = el('ul', 'frecs'), aj = el('label', 'ajuste'), sel = el('select');
      t.appendChild(el('h2', '', s.name));
      t.appendChild(el('p', 'nota', s.country + ' · ' + s.id + ' · ' + s.broadcasts.length + tr(' emisiones al día') + (s.deviation !== 400 ? ' · ±' + s.deviation + ' Hz' : '')));
      if (par.d !== null) t.appendChild(el('p', 'nota', '≈ ' + (Math.round(par.d / 50) * 50) + ' km · ' + tr('rumbo ') + ('00' + rumbo(yo, s)).slice(-3) + '° · ' + tr('posición aproximada del transmisor')));
      chk.type = 'checkbox'; chk.checked = sigue.indexOf(s.id) >= 0;
      chk.addEventListener('change', function () {
        var l = lista('follow').filter(function (x) { return x !== s.id; });
        if (chk.checked) l.push(s.id);
        guardar('follow', l.join(','), 'estado-estaciones').then(pintarHorario);
      });
      lab.appendChild(chk); lab.appendChild(document.createTextNode(' ' + tr('Seguir esta estación')));
      t.appendChild(lab);
      s.freqs.forEach(function (f) {
        var si = enElAire(f, min), li = el('li', si ? '' : 'apagada');
        li.appendChild(el('span', 'mono', khz(f.khz) + (f.call ? ' · ' + f.call : '')));
        li.appendChild(el('span', '', f.from ? f.from + '–' + f.to + ' UTC' + (si ? '' : ' · ' + tr('ahora no')) : tr('todo el día')));
        ul.appendChild(li);
        if (fiabilidad(s.id, f.khz, 0) !== null) {
          // 24 horas UTC: cuanto más lleno, más días del mes llega bien a esa hora
          var tira = el('li', 'tira'), hh = new Date().getUTCHours();
          for (var h = 0; h < 24; h++) {
            var r = fiabilidad(s.id, f.khz, h), c1 = el('span', 'h' + (h === hh ? ' ya' : ''));
            c1.style.setProperty('--r', r === null ? 0 : r);
            c1.title = dos(h) + ' UTC · ' + (r === null ? '—' : pct(r));
            tira.appendChild(c1);
          }
          ul.appendChild(tira);
        }
      });
      t.appendChild(ul);
      aj.appendChild(document.createTextNode(tr('Frecuencia que se usa')));
      var enAire = s.freqs.filter(function (f) { return enElAire(f, min); }).map(function (f) { return f.khz; });
      var ele = elegirTabla(enAire, s.id, ahoraMs) || elegir(enAire, yo, s, ahoraMs);
      [['auto', tr('Automática') + (ele.khz ? ': ' + khz(ele.khz) + (ele.por === 'v' ? ' · VOACAP ' + pct(ele.rel) : ele.por !== 'm' ? ' · ' + tr(CORTO[ele.por]) : '') : '')]].concat(s.freqs.map(function (f) { return [String(f.khz), khz(f.khz)]; })).forEach(function (o) {
        var op = el('option', '', o[1]); op.value = o[0]; sel.appendChild(op);
      });
      sel.value = ajuste('freq.' + s.id, 'auto');
      sel.addEventListener('change', function () { guardar('freq.' + s.id, sel.value, 'estado-estaciones'); });
      aj.appendChild(sel); t.appendChild(aj);
      c.appendChild(t);
    });
    $('fuente-estaciones').textContent = datos.source ? tr('Horarios: ') + datos.source + '. ' +
      tr('Transcritos de forma automática: pueden tener errores y los horarios cambian; compruebe una hora con la fuente antes de fiarse de ella.') : tr('No se pudo leer la lista de estaciones.');
  }

  // ---- receptor
  function campo(etiqueta, control) { var l = el('label', 'ajuste'); l.appendChild(document.createTextNode(etiqueta)); l.appendChild(control); return l; }
  function selector(nombre, def, opciones) {
    var s = el('select');
    opciones.forEach(function (o) { var op = el('option', '', o[1]); op.value = o[0]; s.appendChild(op); });
    s.value = ajuste(nombre, def);
    s.addEventListener('change', function () { guardar(nombre, s.value, 'estado-receptor').then(pintarReceptor); });
    return s;
  }
  function texto(nombre, def, marcador) {
    var i = el('input'); i.type = 'text'; i.value = ajuste(nombre, def); i.spellcheck = false; i.autocomplete = 'off';
    if (marcador) i.placeholder = marcador;
    i.addEventListener('change', function () { guardar(nombre, i.value.trim(), 'estado-receptor'); });
    return i;
  }
  function casilla(nombre, etiqueta) {
    var l = el('label', 'interruptor'), c = el('input');
    c.type = 'checkbox'; c.checked = ajuste(nombre, '0') === '1';
    c.addEventListener('change', function () { guardar(nombre, c.checked ? '1' : '0', 'estado-receptor').then(pintarReceptor); });
    l.appendChild(c); l.appendChild(document.createTextNode(' ' + etiqueta));
    return l;
  }
  function pintarReceptor() {
    var c = $('receptor'), t, r, fuente = ajuste('rx.source', 'sdrplay');
    c.textContent = '';
    t = el('div', 'tarjeta'); t.appendChild(el('h2', '', tr('Equipo')));
    t.appendChild(casilla('rx.enabled', tr('Encendido')));
    r = el('div', 'rejilla');
    r.appendChild(campo(tr('Equipo'), selector('rx.source', 'sdrplay', [['sdrplay', 'SDRplay'], ['rtlsdr', 'RTL-SDR']])));
    r.appendChild(campo(tr('Número de serie (si hay varios)'), texto('rx.serial', '', tr('cualquiera'))));
    if (fuente === 'sdrplay') r.appendChild(campo(tr('Toma de antena (RSPdx)'), selector('rx.port', 'A', [['A', 'A'], ['B', 'B'], ['C', 'C']])));
    else {
      r.appendChild(campo(tr('Muestreo directo'), selector('rx.direct', 'a', [['a', tr('Automático, según el modelo')], ['2', tr('Rama Q')], ['1', tr('Rama I')], ['0', tr('Apagado (conversor)')]])));
      r.appendChild(campo(tr('Conversor externo (Hz; 0 = ninguno)'), texto('rx.upconv', '0')));
      r.appendChild(campo(tr('Ganancia (dB, o auto)'), texto('rx.gain', 'auto')));
      r.appendChild(campo(tr('Corrección (ppm)'), texto('rx.ppm', '0')));
    }
    t.appendChild(r);
    t.appendChild(casilla('rx.bias', tr('Alimentar la antena por el coaxial')));
    c.appendChild(t);

    t = el('div', 'tarjeta'); t.appendChild(el('h2', '', tr('Qué se recibe')));
    r = el('div', 'rejilla');
    r.appendChild(campo(tr('Frecuencia fija (kHz; 0 = seguir el horario)'), texto('rx.fixed_khz', '0')));
    r.appendChild(campo(tr('Líneas por minuto en la frecuencia fija'), selector('rx.fixed_lpm', '120', [['60', '60'], ['90', '90'], ['120', '120'], ['240', '240']])));
    r.appendChild(campo(tr('Días que se guarda una carta sin conservar'), texto('retention_days', '30')));
    t.appendChild(r);
    t.appendChild(el('p', 'nota', tr('Con una frecuencia fija el receptor se queda en ella y guarda todo lo que oiga, sin mirar el horario. Escriba la frecuencia asignada (la central del canal), no la del dial en banda lateral.')));
    c.appendChild(t);

    t = el('div', 'tarjeta'); t.appendChild(el('h2', '', tr('Predicción de propagación (VOACAP)')));
    var MOTIVO = { 'voacap not installed': 'VOACAP no está instalado en este equipo: ejecute tools/instalar-voacap.sh.', 'no sunspot number': 'Falta el número de manchas solares: se lee solo de la NOAA una vez al día (sh tools/actualizar-manchas.sh lo lee ahora), o escríbalo.', 'no position': 'Falta la posición (pestaña Estaciones).' };
    t.appendChild(el('p', 'nota', !tablaEstado ? '' : tablaEstado.ok
      ? (tablaVale() ? tr('En uso: tabla de ') + dos(tabla.month) + '/' + tabla.year + tr(', manchas solares ') + tabla.ssn + '.' : tr('Calculando la tabla…'))
      : tr(MOTIVO[tablaEstado.why] || tablaEstado.why)));
    r = el('div', 'rejilla');
    r.appendChild(campo(tr('Número de manchas solares (vacío: el automático)'), texto('prop.ssn', '', ajuste('prop.ssn_auto', '') ? tr('automático: ') + ajuste('prop.ssn_auto', '') : tr('sin dato'))));
    r.appendChild(campo(tr('Ruido en su antena'), selector('prop.noise', '145', [['136', tr('Ciudad, mucho ruido')], ['145', tr('Residencial')], ['150', tr('Rural')], ['160', tr('Lugar tranquilo, en la mar')]])));
    r.appendChild(campo(tr('Señal sobre ruido necesaria (dB-Hz)'), texto('prop.snr', '40')));
    t.appendChild(r);
    if (ajuste('prop.ssn_auto', '')) t.appendChild(el('p', 'nota', tr('Manchas solares leídas de la NOAA: ') + ajuste('prop.ssn_auto', '') + tr(' (media de unos 30 días hasta el ') + ajuste('prop.ssn_date', '?') + ').' + (ajuste('prop.ssn', '') ? ' ' + tr('Se usa el valor escrito a mano.') : '')));
    t.appendChild(el('p', 'nota', tr('VOACAP calcula, para cada estación, frecuencia y hora, en qué parte de los días del mes la señal llegará con esa relación sobre el ruido. Es la mediana del mes: no ve una tormenta geomagnética ni un apagón de hoy. Supone un transmisor sin ganancia con la potencia publicada (5 kW si no se publica) y una antena corta de látigo en recepción. El número de manchas se lee solo, una vez al día, de los índices solares de la NOAA (la media de sus últimos 30 días, como referencia la misma fuente que S.A.T.-APRS+); si escribe uno, manda el suyo.')));
    c.appendChild(t);

    t = el('div', 'tarjeta'); t.appendChild(el('h2', '', tr('Compartir el equipo con Navtex')));
    t.appendChild(casilla('share.on', tr('Tomar el equipo de Navtex durante cada emisión')));
    r = el('div', 'rejilla');
    r.appendChild(campo(tr('Dirección de Navtex'), texto('share.url', '', 'http://127.0.0.1:8080')));
    r.appendChild(campo(tr('Receptor de Navtex que usa ese equipo'), selector('share.rx', '1', [['1', '1'], ['2', '2']])));
    t.appendChild(r);
    t.appendChild(el('p', 'nota', tr('Un minuto antes de cada emisión Wefax apaga ese receptor de Navtex, recibe la carta y lo vuelve a encender. Mientras tanto Navtex no recibe con él. Con un equipo para cada programa, déjelo apagado.')));
    c.appendChild(t);
  }

  // ---- todo
  function leerTodo() {
    return Promise.all([leer('/api/status'), leer('/api/charts?limit=500'), leer('/api/settings'), leer('/api/gps').catch(function () { return null; }),
      leer('/api/propagation').catch(function () { return {}; }), leer('/api/propagation/state').catch(function () { return null; })]).then(function (r) {
      var antesTabla = (tabla && tabla.made) || 0, antesEst = JSON.stringify(tablaEstado);
      tabla = r[4] || {}; tablaEstado = r[5];
      if (((tabla.made || 0) !== antesTabla || JSON.stringify(tablaEstado) !== antesEst) && !document.activeElement.matches('input[type="text"], select')) { pintarEstaciones(); pintarReceptor(); }
      gps = r[3] && typeof r[3] === 'object' ? r[3] : null;
      var pg = posicionGps();
      if (pg) { window.WEFAX_TEMA.ponerPosicion(pg.lat, pg.lon); repintarAspecto(); }
      var antes = cartas.length ? cartas[0].id : -1, antesAj = JSON.stringify(ajustes);
      estado = r[0]; cartas = r[1]; 
      $('error-general').hidden = true;
      if (JSON.stringify(r[2]) !== antesAj && !document.activeElement.matches('input[type="text"], select')) { ajustes = r[2]; pintarEstaciones(); pintarReceptor(); pintarHorario(); }
      pintarEstado(); pintarDirecto(); pintarClimaEspacial();
      if ((cartas.length ? cartas[0].id : -1) !== antes || !$('cartas').childNodes.length) pintarCartas();
    }).catch(function (e) {
      $('error-general').hidden = false;
      $('error-general').textContent = tr('No se puede leer el estado: ') + e.message + '.';
    });
  }

  // ---- pestañas
  Array.prototype.forEach.call(document.querySelectorAll('.pestana'), function (p) {
    p.addEventListener('click', function () {
      var v = p.getAttribute('data-vista');
      Array.prototype.forEach.call(document.querySelectorAll('.pestana'), function (q) { q.classList.toggle('activa', q === p); });
      ['cartas', 'horario', 'estaciones', 'receptor', 'acerca'].forEach(function (n) { $('vista-' + n).hidden = n !== v; });
      if (v === 'horario') pintarHorario();
      if (v === 'estaciones') pintarEstaciones();
      try { history.replaceState(null, '', '#' + v); } catch (e) { /* da igual */ }
    });
  });
  $('usar-gps').addEventListener('change', function () {
    guardar('position_source', $('usar-gps').checked ? 'gps' : 'manual', 'estado-estaciones').then(pintarEstaciones);
  });
  $('form-posicion').addEventListener('submit', function (ev) {
    ev.preventDefault();
    var p = leerPosicion($('posicion').value);
    if (!p) { avisar('estado-estaciones', false, tr('No entiendo esa posición. Ejemplos: 36.53, -6.29 o IM66uq.')); return; }
    ajustes.position_source = 'manual';
    enviar('/api/settings/position_source/manual').then(function () { return guardar('position', p.lat.toFixed(2) + ',' + p.lon.toFixed(2), 'estado-estaciones'); })
      .then(function () { $('posicion').value = ''; pintarEstaciones(); });
  });
  $('filtro-texto').addEventListener('input', function (e) { filtro.texto = e.target.value; pintarCartas(); });
  $('filtro-guardadas').addEventListener('change', function (e) { filtro.guardadas = e.target.checked; pintarCartas(); });
  $('visor-cerrar').addEventListener('click', function () { $('visor').close(); });
  $('visor').addEventListener('click', function (e) { if (e.target === $('visor')) $('visor').close(); });
  $('visor-conservar').addEventListener('change', function () {
    var c = abierta, si = $('visor-conservar').checked;
    if (!c) return;
    enviar('/api/charts/keep/' + c.id + '/' + (si ? 1 : 0)).then(function () { c.keep = si; pintarCartas(); })
      .catch(function () { $('visor-conservar').checked = !si; });
  });

  // Aspecto: botón con menú, como en Navtex. Los iconos son de Bootstrap Icons (MIT).
  (function () {
    var SVG = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16" aria-hidden="true">';
    var LUNA = '<path d="M6 .278a.77.77 0 0 1 .08.858 7.2 7.2 0 0 0-.878 3.46c0 4.021 3.278 7.277 7.318 7.277q.792-.001 1.533-.16a.79.79 0 0 1 .81.316.73.73 0 0 1-.031.893A8.35 8.35 0 0 1 8.344 16C3.734 16 0 12.286 0 7.71 0 4.266 2.114 1.312 5.124.06A.75.75 0 0 1 6 .278"/>';
    var ICONOS = {
      sistema: SVG + '<path d="M0 4s0-2 2-2h12s2 0 2 2v6s0 2-2 2h-4q0 1 .25 1.5H11a.5.5 0 0 1 0 1H5a.5.5 0 0 1 0-1h.75Q6 13 6 12H2s-2 0-2-2zm1.398-.855a.76.76 0 0 0-.254.302A1.5 1.5 0 0 0 1 4.01V10c0 .325.078.502.145.602q.105.156.302.254a1.5 1.5 0 0 0 .538.143L2.01 11H14c.325 0 .502-.078.602-.145a.76.76 0 0 0 .254-.302 1.5 1.5 0 0 0 .143-.538L15 9.99V4c0-.325-.078-.502-.145-.602a.76.76 0 0 0-.302-.254A1.5 1.5 0 0 0 13.99 3H2c-.325 0-.502.078-.602.145"/></svg>',
      auto: SVG + '<path d="M8 15A7 7 0 1 0 8 1zm0 1A8 8 0 1 1 8 0a8 8 0 0 1 0 16"/></svg>',
      claro: SVG + '<path d="M8 11a3 3 0 1 1 0-6 3 3 0 0 1 0 6m0 1a4 4 0 1 0 0-8 4 4 0 0 0 0 8M8 0a.5.5 0 0 1 .5.5v2a.5.5 0 0 1-1 0v-2A.5.5 0 0 1 8 0m0 13a.5.5 0 0 1 .5.5v2a.5.5 0 0 1-1 0v-2A.5.5 0 0 1 8 13m8-5a.5.5 0 0 1-.5.5h-2a.5.5 0 0 1 0-1h2a.5.5 0 0 1 .5.5M3 8a.5.5 0 0 1-.5.5h-2a.5.5 0 0 1 0-1h2A.5.5 0 0 1 3 8m10.657-5.657a.5.5 0 0 1 0 .707l-1.414 1.415a.5.5 0 1 1-.707-.708l1.414-1.414a.5.5 0 0 1 .707 0m-9.193 9.193a.5.5 0 0 1 0 .707L3.05 13.657a.5.5 0 0 1-.707-.707l1.414-1.414a.5.5 0 0 1 .707 0m9.193 2.121a.5.5 0 0 1-.707 0l-1.414-1.414a.5.5 0 0 1 .707-.707l1.414 1.414a.5.5 0 0 1 0 .707M4.464 4.465a.5.5 0 0 1-.707 0L2.343 3.05a.5.5 0 1 1 .707-.707l1.414 1.414a.5.5 0 0 1 0 .708"/></svg>',
      oscuro: SVG + LUNA + '</svg>',
      noche: SVG + LUNA + '<path d="M10.794 3.148a.217.217 0 0 1 .412 0l.387 1.162c.173.518.579.924 1.097 1.097l1.162.387a.217.217 0 0 1 0 .412l-1.162.387a1.73 1.73 0 0 0-1.097 1.097l-.387 1.162a.217.217 0 0 1-.412 0l-.387-1.162A1.73 1.73 0 0 0 9.31 6.593l-1.162-.387a.217.217 0 0 1 0-.412l1.162-.387a1.73 1.73 0 0 0 1.097-1.097z"/></svg>'
    };
    var NOMBRES = { sistema: tr('Sistema'), auto: tr('Automático'), claro: tr('Claro'), oscuro: tr('Oscuro'), noche: tr('Noche') };
    var T = window.WEFAX_TEMA, boton = $('tema-boton'), menu = $('tema-menu');
    function cerrar() { menu.classList.remove('abierto'); boton.setAttribute('aria-expanded', 'false'); }
    function pintar() {
      var m = T.modo(), t = T.aplicar();
      boton.innerHTML = ICONOS[m];   // constantes de este archivo, no datos recibidos
      boton.title = tr('Aspecto: ') + NOMBRES[m] + (m === 'sistema' || m === 'auto' ? tr(' (ahora ') + NOMBRES[t].toLowerCase() + ')' : '');
      boton.setAttribute('aria-label', boton.title);
      Array.prototype.forEach.call(menu.querySelectorAll('button[data-modo]'), function (b) {
        var si = b.getAttribute('data-modo') === m;
        b.classList.toggle('elegido', si);
        b.setAttribute('aria-checked', si ? 'true' : 'false');
        b.querySelector('.ti').innerHTML = ICONOS[b.getAttribute('data-modo')];
      });
    }
    pintar();
    boton.addEventListener('click', function (e) { e.stopPropagation(); boton.setAttribute('aria-expanded', menu.classList.toggle('abierto') ? 'true' : 'false'); });
    menu.addEventListener('click', function (e) {
      var b = e.target.closest('button[data-modo]');
      if (!b) return;
      T.poner(b.getAttribute('data-modo')); pintar(); cerrar();
    });
    document.addEventListener('click', function (e) { if (!menu.contains(e.target)) cerrar(); });
    document.addEventListener('keydown', function (e) { if (e.key === 'Escape') cerrar(); });
    if (window.matchMedia) {
      var mq = window.matchMedia('(prefers-color-scheme: dark)');
      var alCambiar = function () { if (T.modo() === 'sistema') pintar(); };
      if (mq.addEventListener) mq.addEventListener('change', alCambiar); else if (mq.addListener) mq.addListener(alCambiar);
    }
    setInterval(function () { if (T.modo() === 'auto') pintar(); }, 60000);
    repintarAspecto = pintar;
  })();

  // Idioma: el botón enseña el otro idioma y cambia a él
  (function () {
    var b = $('idioma-boton'), otro = I18N.idioma === 'en' ? 'es' : 'en';
    b.textContent = otro.toUpperCase();
    b.title = otro === 'en' ? 'English' : 'Español';
    b.setAttribute('aria-label', b.title);
    b.setAttribute('lang', otro);
    b.addEventListener('click', function () { I18N.poner(otro); });
  })();

  leer('/api/about').then(function (a) { version = a.version; $('version').textContent = version; }).catch(function () {});
  leer('estaciones.json').then(function (d) { datos = d; }).catch(function () {}).then(function () {
    return leer('/api/settings').then(function (a) { ajustes = a; }).catch(function () {});
  }).then(function () {
    pintarEstaciones(); pintarReceptor(); pintarHorario();
    var v = (location.hash || '').slice(1), p = v && document.querySelector('.pestana[data-vista="' + v.replace(/[^a-z]/g, '') + '"]');
    if (p) p.click();
    leerTodo();
    setInterval(leerTodo, 5000);
    setInterval(pintarHora, 1000);
    setInterval(function () { if (!$('vista-horario').hidden) pintarHorario(); }, 60000);
  });
})();
