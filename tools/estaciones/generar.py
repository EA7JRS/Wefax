#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Manuel - EA7JRS
"""Wefax · hace web_server/build/estaciones.json a partir de los archivos de esta carpeta.

Cada archivo <ID>.txt es la transcripción de una estación de «Worldwide Marine Radiofacsimile
Broadcast Schedules» (NOAA / National Weather Service, edición del 7 de marzo de 2025):

    # comentarios
    STATION|ID|nombre|país|desviación en Hz
    FREQ|kHz|indicativo|HHMM-HHMM   (horas en que esa frecuencia está en el aire; vacío = siempre)
    HHMM|qué se emite               (una línea por emisión, hora UTC; 120 líneas por minuto, índice 576)

Uso:  python3 tools/estaciones/generar.py
"""
import json, os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, '..', '..', 'web_server', 'build', 'estaciones.json')
stations, errors = [], []
for name in sorted(os.listdir(here)):
    if not name.endswith('.txt'):
        continue
    st, seen = None, set()
    for n, line in enumerate(open(os.path.join(here, name), encoding='utf-8'), 1):
        line = line.rstrip('\n')
        if not line.strip() or line.startswith('#'):
            if st is None and line.startswith('# ') and 'note' not in locals(): pass
            continue
        f = line.split('|')
        where = f'{name}:{n}'
        if f[0] == 'STATION' and len(f) == 5:
            st = {'id': f[1], 'name': f[2], 'country': f[3], 'deviation': int(f[4]), 'freqs': [], 'broadcasts': []}
        elif st is None:
            errors.append(f'{where}: falta la línea STATION')
        elif f[0] == 'FREQ' and len(f) == 4:
            fr = {'khz': float(f[1]), 'call': f[2]}
            if not 1500 <= fr['khz'] <= 30000: errors.append(f'{where}: frecuencia fuera de onda corta')
            if f[3]:
                m = re.fullmatch(r'(\d{4})-(\d{4})', f[3])
                if not m: errors.append(f'{where}: horas mal escritas')
                else: fr['from'], fr['to'] = m.group(1), m.group(2)
            st['freqs'].append(fr)
        elif re.fullmatch(r'\d{4}', f[0]) and len(f) == 2 and f[1]:
            if int(f[0][:2]) > 23 or int(f[0][2:]) > 59: errors.append(f'{where}: hora imposible')
            if f[0] in seen: errors.append(f'{where}: hora repetida')
            seen.add(f[0])
            st['broadcasts'].append({'utc': f[0], 'title': f[1], 'lpm': 120, 'ioc': 576})
        else:
            errors.append(f'{where}: no se entiende: {line[:40]}')
    if st:
        st['broadcasts'].sort(key=lambda b: b['utc'])
        if not st['freqs'] or not st['broadcasts']: errors.append(f'{name}: sin frecuencias o sin emisiones')
        stations.append(st)
if errors:
    print('\n'.join(errors)); sys.exit(1)
data = {
    'source': 'Worldwide Marine Radiofacsimile Broadcast Schedules, NOAA / National Weather Service',
    'source_url': 'https://www.weather.gov/media/marine/rfax.pdf',
    'dated': '2025-03-07',
    'note': 'Transcribed through an automatic reader of the PDF: check a time against the source before relying on it. Frequencies are the assigned ones (centre of the channel).',
    'stations': stations,
}
with open(out, 'w', encoding='utf-8') as f:
    json.dump(data, f, ensure_ascii=False, indent=1)
    f.write('\n')
print(f'{out}: {len(stations)} estaciones, {sum(len(s["broadcasts"]) for s in stations)} emisiones')
