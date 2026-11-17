#!/usr/bin/env bash
# ==============================================================================
# Universal Runner: Load Balancer Throughput & Failover Benchmark
# ==============================================================================
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$DIR"

if [ -d "venv" ]; then
    source venv/bin/activate
elif [ -d "$HOME/venv" ]; then
    source "$HOME/venv/bin/activate"
fi

exec python3 simulator/traffic_generator/benchmark.py
