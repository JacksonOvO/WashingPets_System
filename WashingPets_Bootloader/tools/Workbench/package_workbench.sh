#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
VERSION="2.0.0"
BUILD_ROOT="${SCRIPT_DIR}/dist/pawclean-workbench-deb"
PACKAGE_DIR="${BUILD_ROOT}/pawclean-workbench_${VERSION}_amd64"
OUTPUT="${SCRIPT_DIR}/dist/pawclean-workbench_${VERSION}_amd64.deb"

rm -rf "${PACKAGE_DIR}"
mkdir -p \
  "${PACKAGE_DIR}/DEBIAN" \
  "${PACKAGE_DIR}/opt/pawclean-workbench/ui/pages" \
  "${PACKAGE_DIR}/usr/bin" \
  "${PACKAGE_DIR}/usr/share/applications" \
  "${PACKAGE_DIR}/usr/share/icons/hicolor/scalable/apps" \
  "${PACKAGE_DIR}/usr/share/icons/hicolor/512x512/apps" \
  "${PACKAGE_DIR}/usr/share/icons/hicolor/128x128/apps"

# Core files
cp "${SCRIPT_DIR}/server.py" "${PACKAGE_DIR}/opt/pawclean-workbench/"
cp "${SCRIPT_DIR}/gtk_app.py" "${PACKAGE_DIR}/opt/pawclean-workbench/"
cp "${SCRIPT_DIR}/pawtopia-logo.svg" "${PACKAGE_DIR}/opt/pawclean-workbench/"
cp "${SCRIPT_DIR}/pawclean-app-icon.svg" "${PACKAGE_DIR}/opt/pawclean-workbench/"
cp "${SCRIPT_DIR}/pawclean-app-icon.svg" "${PACKAGE_DIR}/usr/share/icons/hicolor/scalable/apps/pawclean-workbench.svg"
cp "${SCRIPT_DIR}/pawclean-app-icon.png" "${PACKAGE_DIR}/opt/pawclean-workbench/pawclean-app-icon.png"
cp "${SCRIPT_DIR}/pawclean-app-icon.png" "${PACKAGE_DIR}/usr/share/icons/hicolor/512x512/apps/pawclean-workbench.png"
cp "${SCRIPT_DIR}/pawclean-app-icon-128.png" "${PACKAGE_DIR}/usr/share/icons/hicolor/128x128/apps/pawclean-workbench.png"
cp "${SCRIPT_DIR}/APP.bin" "${PACKAGE_DIR}/opt/pawclean-workbench/APP.bin"

# UI module
cp "${SCRIPT_DIR}/ui/__init__.py" "${PACKAGE_DIR}/opt/pawclean-workbench/ui/"
cp "${SCRIPT_DIR}/ui/theme.py" "${PACKAGE_DIR}/opt/pawclean-workbench/ui/"
cp "${SCRIPT_DIR}/ui/widgets.py" "${PACKAGE_DIR}/opt/pawclean-workbench/ui/"
cp "${SCRIPT_DIR}/ui/sidebar.py" "${PACKAGE_DIR}/opt/pawclean-workbench/ui/"
cp "${SCRIPT_DIR}/ui/header.py" "${PACKAGE_DIR}/opt/pawclean-workbench/ui/"
cp "${SCRIPT_DIR}/ui/statusbar.py" "${PACKAGE_DIR}/opt/pawclean-workbench/ui/"
cp "${SCRIPT_DIR}/ui/i18n.py" "${PACKAGE_DIR}/opt/pawclean-workbench/ui/"

# UI pages
for f in "${SCRIPT_DIR}/ui/pages/"*.py; do
  cp "$f" "${PACKAGE_DIR}/opt/pawclean-workbench/ui/pages/"
done

# Desktop entry and launcher
if [ -f "${SCRIPT_DIR}/pawclean-workbench" ]; then
  cp "${SCRIPT_DIR}/pawclean-workbench" "${PACKAGE_DIR}/usr/bin/pawclean-workbench"
fi
cp "${SCRIPT_DIR}/pawclean-workbench.desktop" "${PACKAGE_DIR}/usr/share/applications/pawclean-workbench.desktop"

cat > "${PACKAGE_DIR}/DEBIAN/control" <<'CONTROL'
Package: pawclean-workbench
Version: 2.0.0
Section: devel
Priority: optional
Architecture: amd64
Depends: python3, python3-serial, python3-gi, python3-gi-cairo, gir1.2-gtk-3.0
Maintainer: Pawtopia Hardware <support@example.com>
Description: PawClean Pet Washing Machine Workbench
 Modern Apple/macOS-inspired GTK3 workbench for PawClean pet washing machine.
 Features: dashboard control center, real-time telemetry, fault center,
 parameter configuration, IO control, debug tools, and IAP firmware upgrade.
CONTROL

cat > "${PACKAGE_DIR}/DEBIAN/postinst" <<'POSTINST'
#!/usr/bin/env bash
set -e
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi
POSTINST
chmod 755 "${PACKAGE_DIR}/DEBIAN/postinst"

dpkg-deb --build "${PACKAGE_DIR}" "${OUTPUT}"
echo "Package built: ${OUTPUT}"
