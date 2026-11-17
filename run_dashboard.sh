#!/usr/bin/env bash
# ==============================================================================
# Universal Runner: DICP Web Dashboard & Management API
# ==============================================================================
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$DIR"

# Activate Python environment
if [ -d "venv" ]; then
    source venv/bin/activate
elif [ -d "$HOME/venv" ]; then
    source "$HOME/venv/bin/activate"
fi

PORT="${PORT:-8000}"
HOST="${HOST:-0.0.0.0}"

echo "=========================================================="
echo " Starting DICP Management & Observability Dashboard"
echo " 👉 Web UI:     http://localhost:${PORT}"
echo " 👉 Swagger:    http://localhost:${PORT}/docs"
echo " (Press Ctrl+C to stop)"
echo "=========================================================="

exec uvicorn management.api.app:app --host "$HOST" --port "$PORT"
