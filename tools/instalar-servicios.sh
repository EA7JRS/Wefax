#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Manuel - EA7JRS
#
# Wefax · Instala el receptor y la interfaz web como servicios.
#
#   sudo sh tools/instalar-servicios.sh                 instala y arranca
#   sudo sh tools/instalar-servicios.sh --usuario pepe  usuario que los ejecuta
#   sudo sh tools/instalar-servicios.sh --quitar        los para y los retira
#
# Antes hay que haber compilado e instalado los programas (./configure, make,
# sudo make install). Qué hace:
#   1. deja /opt/Wefax (cartas y base de datos) a nombre del usuario de servicio;
#   2. crea /etc/default/wefax (puerto de la interfaz web) si no existe;
#   3. instala y arranca la interfaz web y el receptor, sin root. El receptor
#      no toca ningún equipo hasta que se enciende en la web, pestaña Receptor.
set -eu

USUARIO="${SUDO_USER:-}"
QUITAR=0
while [ $# -gt 0 ]; do
  case "$1" in
    --usuario) shift; USUARIO="${1:?falta el nombre del usuario}" ;;
    --quitar) QUITAR=1 ;;
    -h|--help|--ayuda) sed -n '5,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Opción desconocida: $1 (pruebe --ayuda)" >&2; exit 2 ;;
  esac
  shift
done

[ "$(id -u)" = 0 ] || { echo "Ejecútelo con sudo: sudo sh tools/instalar-servicios.sh" >&2; exit 1; }
DIR="$(cd "$(dirname "$0")/.." && pwd)"
UNIDADES=/etc/systemd/system
paso() { printf '\n== %s\n' "$*"; }

if [ "$QUITAR" = 1 ]; then
  systemctl disable --now wefax-receiver.service wefax-web.service wefax-manchas.timer 2>/dev/null || true
  rm -f "$UNIDADES/wefax-receiver.service" "$UNIDADES/wefax-web.service" "$UNIDADES/wefax-manchas.service" "$UNIDADES/wefax-manchas.timer"
  rm -rf /usr/local/share/wefax
  systemctl daemon-reload
  echo "Servicios retirados. Se conservan /opt/Wefax (cartas y ajustes), /etc/default/wefax y los programas."
  exit 0
fi

[ -n "$USUARIO" ] && [ "$USUARIO" != root ] || { echo "Indique el usuario que ejecutará los servicios: --usuario <nombre> (no root)" >&2; exit 1; }
id "$USUARIO" >/dev/null 2>&1 || { echo "No existe el usuario $USUARIO" >&2; exit 1; }
for p in wefaxreceiver wefaxwebserver; do
  [ -x "/usr/local/bin/$p" ] || { echo "Falta /usr/local/bin/$p: compile e instale antes (./configure && make && sudo make install)" >&2; exit 1; }
done
[ -f /opt/Wefax/build/index.html ] || { echo "Falta /opt/Wefax/build: ejecute antes sudo make install" >&2; exit 1; }

paso "Carpeta de datos para $USUARIO"
mkdir -p /opt/Wefax/charts
chown "$USUARIO" /opt/Wefax
chown -R "$USUARIO" /opt/Wefax/charts
[ ! -f /opt/Wefax/Wefax.db ] || chown "$USUARIO" /opt/Wefax/Wefax.db
chmod 775 /opt/Wefax /opt/Wefax/charts
# Los RTL-SDR se abren con el grupo plugdev
if getent group plugdev >/dev/null 2>&1; then usermod -aG plugdev "$USUARIO"; fi

paso "Ajustes"
if [ -f /etc/default/wefax ]; then
  echo "Se conserva /etc/default/wefax"
else
  install -m 644 "$DIR/systemd/wefax.default" /etc/default/wefax
  echo "Creado /etc/default/wefax"
fi

paso "Servicios"
sed "s/@USUARIO@/$USUARIO/" "$DIR/systemd/wefax-receiver.service" > "$UNIDADES/wefax-receiver.service"
sed "s/@USUARIO@/$USUARIO/" "$DIR/systemd/wefax-web.service" > "$UNIDADES/wefax-web.service"
chmod 644 "$UNIDADES/wefax-receiver.service" "$UNIDADES/wefax-web.service"
# Número de manchas solares para VOACAP: se lee de la NOAA una vez al día (necesita curl e Internet)
mkdir -p /usr/local/share/wefax
install -m 644 "$DIR/tools/actualizar-manchas.sh" /usr/local/share/wefax/actualizar-manchas.sh
sed "s/@USUARIO@/$USUARIO/" "$DIR/systemd/wefax-manchas.service" > "$UNIDADES/wefax-manchas.service"
install -m 644 "$DIR/systemd/wefax-manchas.timer" "$UNIDADES/wefax-manchas.timer"
systemctl daemon-reload
systemctl enable --now wefax-manchas.timer
command -v curl >/dev/null 2>&1 || echo "Falta curl para leer el número de manchas solares: sudo apt install curl"
# restart, y no solo arrancar: si ya estaba en marcha seguiría con el programa anterior
systemctl enable wefax-web.service wefax-receiver.service
systemctl restart wefax-web.service wefax-receiver.service

paso "Estado"
sleep 3
PUERTO="$(sed -n 's/^WEB_PORT=\([0-9][0-9]*\).*/\1/p' /etc/default/wefax | head -1)"
echo "wefax-web:       $(systemctl is-active wefax-web.service 2>/dev/null || true)  (http://<este equipo>:${PUERTO:-8081}/)"
echo "wefax-receiver:  $(systemctl is-active wefax-receiver.service 2>/dev/null || true)"
echo "El receptor espera a que elija estaciones y lo encienda en la web (pestañas Estaciones y Receptor)."
if command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -q '^Status: active'; then
  if ! ufw status | grep -q "^${PUERTO:-8081}/tcp"; then
    echo "El cortafuegos está activo y no abre el puerto ${PUERTO:-8081}. Para la red local, por ejemplo:"
    echo "  sudo ufw allow from 192.168.0.0/16 to any port ${PUERTO:-8081} proto tcp"
  fi
fi
# la primera lectura, ya (sin esperar: si falla, el servicio lo reintenta solo cada 30 s, hasta seis veces)
systemctl start --no-block wefax-manchas.service 2>/dev/null || true
echo "Registro:  journalctl -u wefax-receiver -f"
