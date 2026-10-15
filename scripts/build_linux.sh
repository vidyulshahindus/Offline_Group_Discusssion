#!/usr/bin/env bash
# Install EagleGD for Linux (editable) and run unit tests.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VENV="${EAGLEGD_VENV:-$ROOT/.venv}"

echo "==> Creating venv at $VENV"
python3 -m venv "$VENV"
# shellcheck disable=SC1091
source "$VENV/bin/activate"

echo "==> Installing dependencies"
pip install --upgrade pip wheel
pip install -r requirements.txt
pip install -e .

echo "==> Checking imports"
python -c "from eaglegd import __version__; print('eaglegd', __version__)"
python -c "from eaglegd.app.main import main; print('entry OK')"

echo "==> Running pytest (if present)"
if [[ -d tests/python ]]; then
  # Avoid broken system pytest plugins (e.g. ROS launch_testing without PyYAML).
  PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 pytest -q tests/python || true
fi

echo ""
echo "Build complete."
echo "  Activate:  source $VENV/bin/activate"
echo "  Run:       ./scripts/run_linux.sh"
echo "  Or:        python -m eaglegd.app.main"
echo ""
echo "System packages (Ubuntu/Debian) often needed for Qt Multimedia:"
echo "  sudo apt install libpulse0 libxcb-cursor0 libegl1 libgl1 libxkbcommon0"
echo ""
echo "Optional tools (not pip): place whisper-server and piper under ./tools/"
