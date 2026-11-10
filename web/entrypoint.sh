#!/usr/bin/env bash
# Starts the virtual screen, window manager, VNC server, the web gateway and Eden itself.
set -euo pipefail

if [ -z "${EDEN_WEB_PASSWORD:-}" ] && [ "${EDEN_WEB_ALLOW_NO_PASSWORD:-}" != "1" ]; then
  echo "Set EDEN_WEB_PASSWORD (the login for the website). For a private network only you can set" >&2
  echo "EDEN_WEB_ALLOW_NO_PASSWORD=1 instead." >&2
  exit 1
fi

# Everything Eden stores (library, notes, API keys) lives in the /data volume
export HOME=/data XDG_DATA_HOME=/data/share XDG_CONFIG_HOME=/data/config XDG_CACHE_HOME=/data/cache
mkdir -p /data/files /data/share /data/config /data/cache

export DISPLAY=:0 QT_QPA_PLATFORM=xcb QT_X11_NO_MITSHM=1 LIBGL_ALWAYS_SOFTWARE=1 QT_XCB_GL_INTEGRATION=none
export EDEN_FILES_DIR=/data/files

Xvfb :0 -screen 0 "${EDEN_WEB_SCREEN:-1360x860x24}" -nolisten tcp &
for _ in $(seq 1 50); do
  xdpyinfo -display :0 >/dev/null 2>&1 && break
  sleep 0.2
done
xdpyinfo -display :0 >/dev/null 2>&1 || { echo "virtual screen did not start" >&2; exit 1; }

openbox --config-file /opt/eden-web/openbox-rc.xml &
x11vnc -display :0 -localhost -rfbport 5900 -nopw -forever -shared -noxdamage -quiet &
python3 /opt/eden-web/gateway.py &

# Eden's file dialogs open in the folder that the website's Files page uploads to / downloads from.
cd /data/files
( while true; do /opt/eden/eden_reader || true; sleep 1; done ) &

wait -n
