#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Manuel - EA7JRS
#
# Wefax · Pone en los ajustes el número de manchas solares que usa VOACAP, sin que
# haya que escribirlo. La misma fuente que S.A.T.-APRS+: los índices solares diarios
# del SWPC de la NOAA (daily-solar-indices.txt: fecha, flujo de 10,7 cm, número de
# manchas). De ese archivo, que trae unos 30 días, se toma la MEDIA del número de
# manchas, más estable que el valor de un día. Si no hubiera número de manchas, se
# estima con el flujo como hace S.A.T.-APRS+: (flujo - 64) x 0,9.
#
# No es el número suavizado de 12 meses que pide VOACAP en rigor, sino la media
# observada del último mes: se le parece mucho y no hay que tocar nada. Quien
# quiera el suavizado puede escribirlo en la web (pestaña Receptor), y manda ese.
#
#   sh tools/actualizar-manchas.sh [http://127.0.0.1:8081]
#
# Necesita Internet y curl. Si algo no cuadra, no cambia nada. El instalador de
# servicios lo programa una vez al día (wefax-manchas.timer).
set -eu
WEFAX="${1:-http://127.0.0.1:8081}"
URL=https://services.swpc.noaa.gov/text/daily-solar-indices.txt
TEXTO="$(curl -fsS --max-time 30 "$URL")" || { echo "No se pudo descargar $URL" >&2; exit 1; }
# Líneas de datos: año mes día flujo manchas ...  (las de # y : son comentarios)
DATO="$(printf '%s\n' "$TEXTO" | awk '
  /^[#:]/ || NF < 5 { next }
  $1 !~ /^[0-9][0-9][0-9][0-9]$/ { next }
  { dia = $1 "-" $2 "-" $3
    if ($5 ~ /^[0-9]+$/ && $5 + 0 <= 400) { s += $5; n++ }
    if ($4 ~ /^[0-9]+$/ && $4 + 0 >= 50 && $4 + 0 <= 500) { f += $4; m++ } }
  END {
    if (n >= 5) printf "%d %s %d manchas\n", int(s / n + 0.5), dia, n
    else if (m >= 5) { v = (f / m - 64) * 0.9; if (v < 0) v = 0; printf "%d %s %d flujo\n", int(v + 0.5), dia, m }
  }')"
[ -n "$DATO" ] || { echo "El archivo de la NOAA no trae datos que se entiendan" >&2; exit 1; }
set -- $DATO
SSN="$1"; DIA="$2"; DIAS="$3"; DE="$4"
[ "$SSN" -ge 0 ] && [ "$SSN" -le 300 ] || { echo "Valor fuera de rango: $SSN" >&2; exit 1; }
curl -fsS -X POST -H "X-Wefax: 1" "$WEFAX/api/settings/prop.ssn_auto/$SSN" >/dev/null
curl -fsS -X POST -H "X-Wefax: 1" "$WEFAX/api/settings/prop.ssn_date/$DIA" >/dev/null
echo "Número de manchas solares: $SSN (media de $DIAS días hasta el $DIA, por $DE; NOAA / SWPC)"
