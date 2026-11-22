#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Manuel - EA7JRS
#
# Wefax · Instala VOACAP para las predicciones de propagación.
#
#   sudo sh tools/instalar-voacap.sh                  compila e instala
#   sudo sh tools/instalar-voacap.sh --usuario pepe   usuario de los servicios de Wefax
#
# VOACAP es el programa de predicción de onda corta del NTIA / ITS (EE. UU., sin
# derechos de autor allí); voacapl es su versión para Linux, de J. A. Watson
# (CC0). Este guion descarga voacapl de GitHub, a una versión fija, lo compila
# con gfortran, lo instala en /usr/local y deja sus datos en /opt/Wefax/itshfbc
# (1,6 MB), a nombre del usuario de los servicios. Necesita Internet solo aquí.
# Wefax funciona sin él: entonces elige la frecuencia por el día o la noche.
set -eu

VERSION=c12a98b348c39f36f2659b8b3aa97b86284c8a1d    # voacapl, 29-03-2026
USUARIO="${SUDO_USER:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --usuario) shift; USUARIO="${1:?falta el nombre del usuario}" ;;
    -h|--help|--ayuda) sed -n '5,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Opción desconocida: $1 (pruebe --ayuda)" >&2; exit 2 ;;
  esac
  shift
done
[ "$(id -u)" = 0 ] || { echo "Ejecútelo con sudo: sudo sh tools/instalar-voacap.sh" >&2; exit 1; }
[ -n "$USUARIO" ] && [ "$USUARIO" != root ] || { echo "Indique el usuario de los servicios de Wefax: --usuario <nombre> (no root)" >&2; exit 1; }
id "$USUARIO" >/dev/null 2>&1 || { echo "No existe el usuario $USUARIO" >&2; exit 1; }
paso() { printf '\n== %s\n' "$*"; }

paso "Compilador"
apt-get install -y gfortran git autoconf automake make

paso "Código de voacapl ($VERSION)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
git clone https://github.com/jawatson/voacapl.git "$TMP/voacapl"
cd "$TMP/voacapl"
git checkout --quiet "$VERSION"

paso "Compilar e instalar"
automake --add-missing >/dev/null 2>&1 || true
autoreconf >/dev/null 2>&1 || true
./configure >/dev/null
make >/dev/null
make install >/dev/null

paso "Datos en /opt/Wefax/itshfbc"
mkdir -p /opt/Wefax
rm -rf /opt/Wefax/itshfbc
# makeitshfbc los deja en $HOME/itshfbc
HOME=/opt/Wefax makeitshfbc >/dev/null
chown -R "$USUARIO" /opt/Wefax/itshfbc

paso "Prueba"
cd /opt/Wefax/itshfbc/run
if su -s /bin/sh "$USUARIO" -c "voacapl -s /opt/Wefax/itshfbc voacapx.dat prueba.out" >/dev/null 2>&1 && grep -q "REL" prueba.out; then
  rm -f prueba.out
  echo "VOACAP funciona."
else
  echo "VOACAP se instaló pero la prueba no dio resultado: revise /opt/Wefax/itshfbc/run" >&2
  exit 1
fi
systemctl restart wefax-web.service 2>/dev/null || true
echo "Ahora, en la web de Wefax, pestaña Receptor, escriba el número de manchas solares del mes"
echo "(o ejecute: sh tools/actualizar-manchas.sh). La tabla aparece en medio minuto."
