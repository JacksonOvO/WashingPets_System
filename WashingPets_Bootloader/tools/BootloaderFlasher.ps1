param(
    [string]$Port,
    [string]$Image,
    [string]$Version = "1.0.0"
)

if ([string]::IsNullOrWhiteSpace($Port)) {
    $Port = Read-Host "USART1 COM port (for example COM5)"
}
if ([string]::IsNullOrWhiteSpace($Image)) {
    $Image = Read-Host "App.bin path"
}

$uploader = Join-Path $PSScriptRoot "bootloader_upload.py"
& py $uploader $Port $Image --version $Version
if ($LASTEXITCODE -ne 0) {
    throw "Bootloader upload failed with exit code $LASTEXITCODE"
}
