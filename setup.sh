#!/usr/bin/env bash
# ==============================================================================
# DICP Setup Script (Universal / Portable)
# Builds C++20 binaries, prepares Python environment, and runs test validation.
# Works on any Linux distribution (Ubuntu, Debian, Fedora, Arch, WSL2) & macOS.
# ==============================================================================
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$DIR"

echo "=========================================================="
echo " Setting up Distributed Infrastructure Control Plane (DICP)"
echo " Working Directory: $DIR"
echo "=========================================================="

# 1. Setup Python Virtual Environment
if [ ! -d "venv" ] && [ ! -d "$HOME/venv" ]; then
    echo "[+] Creating Python virtual environment (./venv)..."
    python3 -m venv venv
    source venv/bin/activate
elif [ -d "venv" ]; then
    echo "[+] Using local virtual environment (./venv)..."
    source venv/bin/activate
elif [ -d "$HOME/venv" ]; then
    echo "[+] Using user virtual environment ($HOME/venv)..."
    source "$HOME/venv/bin/activate"
fi

echo "[+] Installing/verifying Python dependencies..."
pip install -q --upgrade pip
pip install -q -r management/requirements.txt

# 2. Build C++20 Binaries (dicp_agent, dicp_lb, test suites)
echo "[+] Configuring and building C++20 core binaries..."
mkdir -p build
cd build

BUILD_TOOL="make"
if command -v ninja >/dev/null 2>&1; then
    cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
    ninja
else
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
fi

cd "$DIR"

echo "=========================================================="
echo " DICP Setup Completed Successfully!"
echo " Next Steps:"
echo "   1. Start Web Dashboard:  ./run_dashboard.sh"
echo "   2. Run All Tests:        ./run_tests.sh"
echo "   3. Run Cluster Sim:      ./run_simulator.sh"
echo "   4. Run LB Benchmark:     ./run_benchmark.sh"
echo "=========================================================="
