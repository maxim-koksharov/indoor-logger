#!/usr/bin/env bash
# Flash an app to its device.
#
# Usage: scripts/flash.sh client|server

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 <client|server>" >&2
    exit 1
}

[ $# -eq 1 ] || usage
APP="$1"

case "${APP}" in
    client) PORT="/dev/ttyUSB0" ;;
    server) PORT="/dev/ttyUSB1" ;;
    *) usage ;;
esac

APP_DIR="${REPO_ROOT}/apps/${APP}"

: "${IDF_PATH:?IDF_PATH must be set (export IDF_PATH=/esp/ESP8266_RTOS_SDK)}"

(cd "${APP_DIR}" && idf.py -p "${PORT}" flash)
