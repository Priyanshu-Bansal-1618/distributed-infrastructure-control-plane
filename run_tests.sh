#!/usr/bin/env bash
# ==============================================================================
# Universal Test Runner: C++ GoogleTest (11 suites) + Python Pytest (5 suites)
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

echo "=========================================================="
echo " Step 1: Running C++20 Core Test Suites (11 Suites)"
echo "=========================================================="
if [ -d "build" ]; then
    cd build
    ctest --output-on-failure
    cd "$DIR"
else
    echo "[!] Build directory missing. Run ./setup.sh first."
    exit 1
fi

echo ""
echo "=========================================================="
echo " Step 2: Running Python Management API Tests"
echo "=========================================================="
PYTHONPATH=. pytest management/tests/test_management_api.py -v

echo ""
echo "=========================================================="
echo " ✅ ALL C++ AND PYTHON TEST SUITES PASSED (100%)"
echo "=========================================================="
