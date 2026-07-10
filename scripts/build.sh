#!/usr/bin/env bash
# Two-pass build wrapper for ESP8266 client / server apps.
#
# Pass 1: generate partitions CSV with factory=1MB fallback, build, measure .bin.
# Pass 2: regenerate CSV with factory sized to actual .bin + margin, build again.
#
# Usage: scripts/build.sh client [extra idf.py args...]
#        scripts/build.sh server [extra idf.py args...]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GENERATOR="${REPO_ROOT}/cmake/generate_partitions.py"

usage() {
    echo "Usage: $0 <client|server> [extra idf.py args...]" >&2
    exit 1
}

[ $# -ge 1 ] || usage
APP="$1"
shift

case "${APP}" in
    client|server) ;;
    *) usage ;;
esac

APP_DIR="${REPO_ROOT}/apps/${APP}"
SDKCONFIG="${APP_DIR}/sdkconfig"
PARTITIONS_CSV="${APP_DIR}/partitions_${APP}.csv"
APP_BIN="${APP_DIR}/build/${APP}.bin"
SIZE_FILE="${APP_DIR}/build/app_bin_size.txt"

if [ ! -d "${APP_DIR}" ]; then
    echo "App directory not found: ${APP_DIR}" >&2
    exit 1
fi

: "${IDF_PATH:?IDF_PATH must be set (export IDF_PATH=/esp/ESP8266_RTOS_SDK)}"

run_idf() {
    (cd "${APP_DIR}" && idf.py "$@")
}

echo "=== ${APP}: pass 1 (factory=1MB fallback) ==="
python3 "${GENERATOR}" \
    --app "${APP}" \
    --sdkconfig "${SDKCONFIG}" \
    --output "${PARTITIONS_CSV}"

run_idf build "$@"

if [ ! -f "${APP_BIN}" ]; then
    echo "App bin not produced: ${APP_BIN}" >&2
    exit 1
fi

BIN_SIZE=$(stat -c %s "${APP_BIN}")
echo "${BIN_SIZE}" > "${SIZE_FILE}"
echo "=== ${APP}: pass 1 bin size = ${BIN_SIZE} bytes ==="

echo "=== ${APP}: pass 2 (factory sized to .bin + margin) ==="
python3 "${GENERATOR}" \
    --app "${APP}" \
    --sdkconfig "${SDKCONFIG}" \
    --app-bin "${APP_BIN}" \
    --output "${PARTITIONS_CSV}"

cat "${PARTITIONS_CSV}"

run_idf build "$@"

if [ ! -f "${APP_BIN}" ]; then
    echo "App bin not produced on pass 2: ${APP_BIN}" >&2
    exit 1
fi

NEW_BIN_SIZE=$(stat -c %s "${APP_BIN}")
echo "${NEW_BIN_SIZE}" > "${SIZE_FILE}"
echo "=== ${APP}: pass 2 bin size = ${NEW_BIN_SIZE} bytes ==="
echo "=== ${APP}: build OK ==="
