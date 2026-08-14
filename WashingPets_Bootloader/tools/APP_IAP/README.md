# PawClean Linux Workbench

Native GTK3 implementation of the PawClean STM32 debug workbench.

## Development run

```bash
cd tools/APP_IAP
./run_linux_workbench.sh
```

## Build Debian package

```bash
cd tools/APP_IAP
./package_linux_workbench.sh
```

The package is written to `dist/`. Runtime dependencies are installed by APT.
