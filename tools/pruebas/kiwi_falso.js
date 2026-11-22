// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Manuel - EA7JRS
// Uso: node tools/pruebas/kiwi_falso.js [puerto] [ok|busy|badp|limit]   y   wefaxreceiver -K 127.0.0.1:8073 -k 7880 -o .
// KiwiSDR de pega para probar el receptor: habla lo justo del protocolo y manda una emisión hecha aquí (nunca emitida).
const net = require('net'), crypto = require('crypto');
const RATE = 12001.135, LINES = 40, PORT = +process.argv[2] || 8073, MODE = process.argv[3] || 'ok';
function frame(buf, op = 2) {
  const n = buf.length, h = n < 126 ? Buffer.from([0x80 | op, n]) : n < 65536 ? Buffer.from([0x80 | op, 126, n >> 8, n & 255]) : Buffer.concat([Buffer.from([0x80 | op, 127, 0, 0, 0, 0]), Buffer.from([n >>> 24, (n >> 16) & 255, (n >> 8) & 255, n & 255])]);
  return Buffer.concat([h, buf]);
}
const msg = (t) => frame(Buffer.concat([Buffer.from('MSG '), Buffer.from(t)]));
function pattern(x, y, w) { if (y < 20) return Math.floor(x / 100) % 2 ? 1 : 0; if (y < 40) return x / w; if (Math.abs(x - y * 4) < 6) return 0; return 1; }
function hz(t) {
  const line = 0.5, w = 1810;
  if (t < 1) return 0; t -= 1;
  if (t < 5) return (t * 300) % 1 < 0.5 ? 400 : -400; t -= 5;
  if (t < 30) { const p = (t / line) % 1; return p < 0.025 || p >= 0.975 ? 400 : -400; } t -= 30;
  if (t < LINES * line) { const l = t / line, y = Math.floor(l); return pattern(Math.floor((l - y) * w), y, w) * 800 - 400; } t -= LINES * line;
  if (t < 5) return (t * 450) % 1 < 0.5 ? 400 : -400;
  return -400;
}
net.createServer((s) => {
  let buf = Buffer.alloc(0), up = false, timer = null, i = 0, ph = 0, seq = 0;
  const said = [];
  s.on('error', () => {}); s.on('close', () => { clearInterval(timer); console.log('cliente fuera; dijo:', JSON.stringify(said)); });
  s.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    if (!up) {
      const e = buf.indexOf('\r\n\r\n'); if (e < 0) return;
      const head = buf.slice(0, e).toString(); buf = buf.slice(e + 4);
      console.log(head.split('\r\n')[0]);
      const key = /Sec-WebSocket-Key: (.+)/.exec(head)[1].trim();
      s.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64') + '\r\n\r\n');
      up = true;
    }
    for (;;) {
      if (buf.length < 6) return;
      let n = buf[1] & 127, h = 2; if (n === 126) { n = buf.readUInt16BE(2); h = 4; }
      if (buf.length < h + 4 + n) return;
      const mask = buf.slice(h, h + 4), p = Buffer.from(buf.slice(h + 4, h + 4 + n)); for (let k = 0; k < n; k++) p[k] ^= mask[k & 3];
      buf = buf.slice(h + 4 + n);
      const t = p.toString(); if (t !== 'SET keepalive') said.push(t);
      if (t.startsWith('SET auth')) {
        if (MODE === 'busy') { s.write(msg('too_busy=4')); return; }
        if (MODE === 'badp') { s.write(msg('badp=1')); return; }
        // la configuración en dos trozos de WebSocket, como un mensaje largo
        const cfg = Buffer.from('MSG load_cfg=' + encodeURIComponent(JSON.stringify({ rx_name: 'de pega', rx_gps: encodeURIComponent('(53.660000, 9.800000)'), relleno: 'x'.repeat(70000) })));
        s.write(Buffer.concat([Buffer.from([0x02, 126, 0x03, 0xe8]), cfg.slice(0, 1000)])); s.write(frame(cfg.slice(1000), 0));
        s.write(msg('version_maj=1 version_min=700')); s.write(msg('freq_offset=0.000')); s.write(msg('audio_init=0 audio_rate=12000')); s.write(msg('sample_rate=' + RATE));
      }
      if (t.startsWith('SET mod=iq') && !timer) {
        const t0 = Date.now();
        timer = setInterval(() => {
          const want = Math.floor((Date.now() - t0) / 1000 * RATE);
          while (i + 512 <= want) {
            const b = Buffer.alloc(3 + 17 + 4 * 512); b.write('SND'); b[3] = 0x08; b.writeUInt32LE(seq++, 4); b.writeUInt16BE(670, 8);
            for (let k = 0; k < 512; k++, i++) { ph += 2 * Math.PI * hz(i / RATE) / RATE; b.writeInt16BE(Math.round(8000 * Math.cos(ph)), 20 + 4 * k); b.writeInt16BE(Math.round(8000 * Math.sin(ph)), 22 + 4 * k); }
            s.write(frame(b));
          }
          if (MODE === 'limit' && i / RATE > 20) { s.write(frame(Buffer.alloc(0), 8)); s.end(); }
        }, 20);
      }
    }
  });
}).listen(PORT, '127.0.0.1', () => console.log('KiwiSDR de pega en', PORT));
