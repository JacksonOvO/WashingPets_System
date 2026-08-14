#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="${SCRIPT_DIR}/linux_workbench"

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required." >&2
  exit 1
fi

if ! python3 -c 'import cairo, gi, serial; gi.require_version("Gtk", "3.0"); gi.require_foreign("cairo")' >/dev/null 2>&1; then
  cat >&2 <<'EOF'
Linux workbench dependencies are required.

Install it with:
  sudo apt install python3-serial python3-gi python3-gi-cairo gir1.2-gtk-3.0
EOF
  exit 1
fi

cd "${APP_DIR}"
exec python3 gtk_app.py
