$ErrorActionPreference = 'Stop'
$workbench = Split-Path -Parent $PSScriptRoot
$venv = Join-Path $PSScriptRoot '.venv'
$python = Join-Path $venv 'Scripts\python.exe'

if (-not (Test-Path $python)) {
    py -3.12-64 -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.12 x64 is required.' }
}

& $python -m pip install -r (Join-Path $PSScriptRoot 'requirements-windows.txt')
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }

Push-Location $workbench
try {
    & $python -m unittest -v windows.test_iap windows.test_ui
    if ($LASTEXITCODE -ne 0) { throw 'Protocol or UI tests failed.' }
    & $python -m windows.main --smoke
    if ($LASTEXITCODE -ne 0) { throw 'Qt smoke test failed.' }

    & $python -m PyInstaller --noconfirm --clean --onefile --windowed `
        --name PawClean `
        --icon (Join-Path $workbench 'pawclean-app-icon.png') `
        --add-data "$(Join-Path $workbench 'pawclean-app-icon.png'):." `
        --add-data "$(Join-Path $PSScriptRoot 'assets'):windows/assets" `
        --paths $workbench `
        --distpath (Join-Path $PSScriptRoot 'dist') `
        --workpath (Join-Path $PSScriptRoot 'build') `
        --specpath (Join-Path $PSScriptRoot 'build') `
        (Join-Path $PSScriptRoot 'main.py')
    if ($LASTEXITCODE -ne 0) { throw 'PawClean.exe build failed.' }
    & (Join-Path $PSScriptRoot 'dist\PawClean.exe') --smoke
    if ($LASTEXITCODE -ne 0) { throw 'Packaged PawClean.exe smoke test failed.' }
    Write-Host "Built: $(Join-Path $PSScriptRoot 'dist\PawClean.exe')"
} finally {
    Pop-Location
}
