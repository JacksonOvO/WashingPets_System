#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
VERSION="1.1.0"
BUILD_ROOT="${SCRIPT_DIR}/dist/pawclean-workbench-deb"
PACKAGE_DIR="${BUILD_ROOT}/pawclean-workbench_${VERSION}_amd64"
OUTPUT="${SCRIPT_DIR}/dist/pawclean-workbench_${VERSION}_amd64.deb"

rm -rf "${PACKAGE_DIR}"
mkdir -p \
  "${PACKAGE_DIR}/DEBIAN" \
  "${PACKAGE_DIR}/opt/pawclean-workbench" \
  "${PACKAGE_DIR}/usr/bin" \
  "${PACKAGE_DIR}/usr/share/applications" \
  "${PACKAGE_DIR}/usr/share/icons/hicolor/scalable/apps"

cp "${SCRIPT_DIR}/linux_workbench/server.py" "${PACKAGE_DIR}/opt/pawclean-workbench/"
cp "${SCRIPT_DIR}/linux_workbench/gtk_app.py" "${PACKAGE_DIR}/opt/pawclean-workbench/"
cp "${SCRIPT_DIR}/linux_workbench/pawtopia-logo.svg" "${PACKAGE_DIR}/opt/pawclean-workbench/"
cp "${SCRIPT_DIR}/linux_workbench/pawclean-app-icon.svg" "${PACKAGE_DIR}/opt/pawclean-workbench/"
cp "${SCRIPT_DIR}/linux_workbench/pawclean-app-icon.svg" "${PACKAGE_DIR}/usr/share/icons/hicolor/scalable/apps/pawclean-workbench.svg"
cp "${SCRIPT_DIR}/APP.bin" "${PACKAGE_DIR}/opt/pawclean-workbench/APP.bin"
cp "${SCRIPT_DIR}/linux_workbench/pawclean-workbench" "${PACKAGE_DIR}/usr/bin/pawclean-workbench"
cp "${SCRIPT_DIR}/linux_workbench/pawclean-workbench.desktop" "${PACKAGE_DIR}/usr/share/applications/pawclean-workbench.desktop"

cat > "${PACKAGE_DIR}/DEBIAN/control" <<'CONTROL'
Package: pawclean-workbench
Version: 1.1.0
Section: devel
Priority: optional
Architecture: amd64
Depends: python3, python3-serial, python3-gi, python3-gi-cairo, gir1.2-gtk-3.0
Maintainer: Pawtopia Hardware <support@example.com>
Description: PawClean STM32 Linux debug workbench
 Linux implementation of the PawClean STM32 smart debug workbench.
 It provides a native GTK desktop UI for serial connection, telemetry,
 IO control, parameter editing, IAP query and firmware upgrade.
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

chmod 0755 "${PACKAGE_DIR}/usr/bin/pawclean-workbench"
chmod 0755 "${PACKAGE_DIR}/DEBIAN/postinst"
chmod 0644 "${PACKAGE_DIR}/usr/share/applications/pawclean-workbench.desktop"
find "${PACKAGE_DIR}/opt/pawclean-workbench" -type f -exec chmod 0644 {} +

mkdir -p "${SCRIPT_DIR}/dist"
dpkg-deb --root-owner-group --build "${PACKAGE_DIR}" "${OUTPUT}"
rm -rf "${PACKAGE_DIR}"
rmdir "${BUILD_ROOT}" 2>/dev/null || true
echo "Built: ${OUTPUT}"
