#!/usr/bin/env python3
"""
tools/gfx_regression_gate.py
FirstOS Graphics Performance & Quality Regression Gate.

Returns:
  0: PASS (All criteria within stable baseline bounds)
  1: FAIL (Regression detected: build/test fails)
"""

import sys
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def run_regression_gate():
    bench_script = REPO_ROOT / "tools" / "test_gfx_stabilization_bench.py"
    print("==================================================")
    print("   FirstOS Graphics Performance Regression Gate   ")
    print("==================================================")
    print(f"[gate] Running benchmark matrix: {bench_script}")

    proc = subprocess.run([sys.executable, str(bench_script)])
    if proc.returncode != 0:
        print("\n[gate] >>> REGRESSION GATE FAILED! <<<")
        print("[gate] Performance or visual quality fell below v1 baseline standards.")
        return False

    print("\n[gate] >>> REGRESSION GATE PASSED! <<<")
    print("[gate] Graphics stack verified clean, responsive, and stable.")
    return True

if __name__ == "__main__":
    passed = run_regression_gate()
    sys.exit(0 if passed else 1)
