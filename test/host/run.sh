#!/bin/sh
# Host tests for the om_lite_l2 tree. Non-zero exit on any failure; no device and no extra
# packages needed (system python3 only).
set -eu
cd "$(dirname "$0")"
# -B: no __pycache__ left behind in the tree the reviewer diffs.
python3 -B test_shot.py
