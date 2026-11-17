#!/usr/bin/env bash
# ==============================================================================
# Universal Runner: Multi-Node Cluster Fault & Recovery Simulation
# ==============================================================================
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$DIR"

if [ -d "venv" ]; then
    source venv/bin/activate
elif [ -d "$HOME/venv" ]; then
    source "$HOME/venv/bin/activate"
fi

exec python3 simulator/cluster_simulator/simulator.py
