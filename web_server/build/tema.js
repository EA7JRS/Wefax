/* Wefax · aspecto de la interfaz. Se carga antes de pintar la página.
 * SPDX-License-Identifier: GPL-3.0-or-later · Copyright (c) 2026 Manuel - EA7JRS
 *
 * El mismo selector que la WebUI del tracker APRS LoRa+:
 *   sistema  el del equipo (de serie)
 *   auto     claro de día y oscuro de noche, por la salida y la puesta del sol en la
 *            posición del receptor; sin posición, claro de 8 a 20 h
 *   claro, oscuro
 *   noche    rojo sobre negro, para no perder la visión nocturna
 * La elección se guarda en este navegador.
 */
window.WEFAX_TEMA = (function () {
  'use strict';
  var MODOS = ['sistema', 'auto', 'claro', 'oscuro', 'noche'];
  function modo() {
    var m = null;
    try { m = localStorage.getItem('wefax-tema'); } catch (e) { /* sin almacenamiento: el del equipo */ }
    return MODOS.indexOf(m) >= 0 ? m : 'sistema';
  }
  // ¿Está el sol por encima del horizonte (-0,833 grados)? Fórmulas simplificadas de la NOAA
  function solArriba(lat, lon, d) {
    var rad = Math.PI / 180, jd = d.getTime() / 86400000 + 2440587.5, n = jd - 2451545.0;
    var L = (280.46 + 0.9856474 * n) % 360, g = ((357.528 + 0.9856003 * n) % 360) * rad;
    var lam = (L + 1.915 * Math.sin(g) + 0.02 * Math.sin(2 * g)) * rad, eps = (23.439 - 0.0000004 * n) * rad;
    var dec = Math.asin(Math.sin(eps) * Math.sin(lam)), ra = Math.atan2(Math.cos(eps) * Math.sin(lam), Math.cos(lam));
    var gmst = (18.697374558 + 24.06570982441908 * n) % 24, ha = ((gmst * 15 + lon) * rad - ra);
    var el = Math.asin(Math.sin(lat * rad) * Math.sin(dec) + Math.cos(lat * rad) * Math.cos(dec) * Math.cos(ha)) / rad;
    return el > -0.833;
  }
  function posicion() {
    try {
      var p = JSON.parse(localStorage.getItem('wefax-sol') || 'null');
      if (p && isFinite(p.lat) && isFinite(p.lon)) return p;
    } catch (e) { /* sin posición guardada */ }
    return null;
  }
  function efectivo(m) {
    m = m || modo();
    if (m === 'sistema') return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'oscuro' : 'claro';
    if (m === 'auto') {
      var p = posicion(), d = new Date();
      return (p ? solArriba(p.lat, p.lon, d) : (d.getHours() >= 8 && d.getHours() < 20)) ? 'claro' : 'oscuro';
    }
    return m;
  }
  function aplicar() {
    var t = efectivo();
    document.documentElement.setAttribute('data-tema', t);
    return t;
  }
  function poner(m) {
    try { localStorage.setItem('wefax-tema', m); } catch (e) { /* no se recuerda, pero se aplica */ }
    return aplicar();
  }
  function ponerPosicion(lat, lon) {
    try { localStorage.setItem('wefax-sol', JSON.stringify({ lat: Math.round(lat * 100) / 100, lon: Math.round(lon * 100) / 100 })); } catch (e) { /* da igual */ }
  }
  try { aplicar(); } catch (e) { /* queda el aspecto del equipo, por la hoja de estilos */ }
  return { MODOS: MODOS, modo: modo, efectivo: efectivo, aplicar: aplicar, poner: poner, ponerPosicion: ponerPosicion, posicion: posicion };
})();
