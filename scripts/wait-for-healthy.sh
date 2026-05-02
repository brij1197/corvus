#!/usr/bin/env bash
set -euo pipefail

HOST="${1:-localhost}"
PORT="${2:-8080}"
RETRIES="${3:-30}"
SLEEP="${4:-5}"

echo "Waiting for corvus-core at $HOST:$PORT ..."
for i in $(seq 1 "$RETRIES"); do
    if curl -sf "http://$HOST:$PORT/health" > /dev/null; then
        echo "corvus-core is healthy."
        exit 0
    fi
    echo "  attempt $i/$RETRIES - retrying in ${SLEEP}s"
    sleep "$SLEEP"
done

echo "ERROR: corvus-core did not become healthy after $((RETRIES * SLEEP))s"
exit 1
