#!/usr/bin/env bash
# Run EagleGD on Linux from a venv next to the repo.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VENV="${EAGLEGD_VENV:-$ROOT/.venv}"

if [[ ! -d "$VENV" ]]; then
  python3 -m venv "$VENV"
  # shellcheck disable=SC1091
  source "$VENV/bin/activate"
  pip install --upgrade pip
  pip install -r requirements.txt
  pip install -e .
else
  # shellcheck disable=SC1091
  source "$VENV/bin/activate"
fi

export PYTHONPATH="$ROOT${PYTHONPATH:+:$PYTHONPATH}"
exec python -m eaglegd.app.main "$@"
