#!/usr/bin/env bash
# Downloads a full-day Nasdaq TotalView-ITCH 5.0 sample capture into ./data.
# Browse available files at https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/
set -euo pipefail

FILE="${1:-01302019.NASDAQ_ITCH50}"
BASE_URL="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH"
DATA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/data"

mkdir -p "${DATA_DIR}"
cd "${DATA_DIR}"

if [[ -f "${FILE}" ]]; then
  echo "${DATA_DIR}/${FILE} already exists"
  exit 0
fi

echo "Downloading ${FILE}.gz (several GB compressed)..."
curl --fail --location --continue-at - --output "${FILE}.gz" "${BASE_URL}/${FILE}.gz"

echo "Decompressing..."
gunzip "${FILE}.gz"
echo "Ready: ${DATA_DIR}/${FILE}"
