#!/bin/bash
# PawClean Workbench — Development launcher
cd "$(dirname "$0")"
exec python3 gtk_app.py "$@"
