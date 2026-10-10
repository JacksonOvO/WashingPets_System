# PawClean Windows

This is the Qt Widgets Windows front end for the existing GTK-free `../server.py`
business backend. The Linux GTK application remains available.

## Build

On Windows 10/11 x64 with Python 3.12 x64 installed, run in PowerShell:

```powershell
cd WashingPets_Bootloader\tools\Workbench\windows
.\build_windows.ps1
```

The result is `dist\PawClean.exe`. Copy the EXE to the desktop and double-click it.
Python is not needed on the destination computer. Dependencies are pinned in
`requirements-windows.txt`. A normal USB serial driver for the connected adapter
is still required.

For source development, install those dependencies and run from the parent
`Workbench` directory with `python -m windows.main`.

## Upgrade workflow

Choose PawClean or Store, COM port, baud rate, APP version, and the matching raw
`App.bin`. Query the device. The tool checks the profile's flash layout and
Bootloader family. Start upgrade to see the file size, CRC32, target ID, and a
final confirmation. Raw BIN files do not contain a model identifier; always
verify the selected file before accepting. Store is supported only on this page.

The upgrade page owns its COM port during the operation, then restores an
existing PawClean business connection when the APP is available. The test page
exports CSV and PNG files to the Windows Desktop `测试曲线` folder.

Run protocol tests without Qt or hardware:

```powershell
cd WashingPets_Bootloader\tools\Workbench
python -m unittest -v windows.test_iap
```
