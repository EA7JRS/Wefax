#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Manuel - EA7JRS
#
# Wefax · Pone en los ajustes el número de manchas solares suavizado previsto para
# este mes, que es el que usa VOACAP. Lo toma de la predicción del ciclo solar del
# SWPC de la NOAA. Necesita Internet, curl y python3. Si algo no cuadra, no cambia nada.
#
#   sh tools/actualizar-manchas.sh [http://127.0.0.1:8081]
#
# SIN PROBAR contra el servidor real: el formato esperado es una lista de objetos con
# "time-tag": "AAAA-MM" y "predicted_ssn". Una vez al mes basta (por ejemplo, con cron).
set -eu
WEFAX="${1:-http://127.0.0.1:8081}"
URL=https://services.swpc.noaa.gov/json/solar-cycle/predicted-solar-cycle.json
SSN="$(curl -fsS --max-time 30 "$URL" | python3 -c '
import json, sys, time
mes = time.strftime("%Y-%m", time.gmtime())
for r in json.load(sys.stdin):
    if isinstance(r, dict) and r.get("time-tag") == mes and isinstance(r.get("predicted_ssn"), (int, float)):
        v = round(r["predicted_ssn"])
        if 0 <= v <= 300:
            print(v)
        break
')"
[ -n "$SSN" ] || { echo "No se pudo leer el número de manchas de este mes en $URL" >&2; exit 1; }
curl -fsS -X POST -H "X-Wefax: 1" "$WEFAX/api/settings/prop.ssn/$SSN" >/dev/null
echo "Número de manchas solares de este mes: $SSN"
