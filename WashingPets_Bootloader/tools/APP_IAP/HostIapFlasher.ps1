Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

[System.Windows.Forms.Application]::EnableVisualStyles()

$script:Head0 = [byte]0x55
$script:Head1 = [byte]0xAA
$script:RspDeviceInfo = [byte]0x90
$script:RspAck = [byte]0xF0
$script:RspNack = [byte]0xF1
$script:CmdQueryInfo = [byte]0x10
$script:CmdEnterUpgrade = [byte]0x11
$script:CmdUpgradeStart = [byte]0x20
$script:CmdUpgradeData = [byte]0x21
$script:CmdUpgradeEnd = [byte]0x22
$script:CmdUpgradeAbort = [byte]0x23
$script:TargetId = [uint32]0x407A0C06
$script:HwVersion = [uint16]0x0001
$script:ChunkSize = [uint16]128

function Add-Log([string]$Text) {
    if ($null -eq $form -or $form.IsDisposed) { return }
    $time = Get-Date -Format "HH:mm:ss.fff"
    $logBox.AppendText("[$time] $Text`r`n")
    $logBox.SelectionStart = $logBox.TextLength
    $logBox.ScrollToCaret()
    [System.Windows.Forms.Application]::DoEvents()
}

function Set-UiState([bool]$Busy, [string]$Status) {
    $portCombo.Enabled = -not $Busy
    $refreshButton.Enabled = -not $Busy
    $browseButton.Enabled = -not $Busy
    $upgradeButton.Enabled = -not $Busy
    $queryButton.Enabled = -not $Busy
    $versionBox.Enabled = -not $Busy
    $statusLabel.Text = $Status
    [System.Windows.Forms.Application]::DoEvents()
}

function Set-Progress([int]$Value, [string]$Status) {
    $progress.Value = [Math]::Max(0, [Math]::Min(100, $Value))
    $statusLabel.Text = $Status
    [System.Windows.Forms.Application]::DoEvents()
}

function Convert-ToHex([byte[]]$Data) {
    if ($null -eq $Data -or $Data.Length -eq 0) { return "" }
    return (($Data | ForEach-Object { $_.ToString("X2") }) -join " ")
}

function Get-U16Bytes([uint16]$Value) {
    return ,[byte[]](
        [byte]($Value -band 0xFF),
        [byte](($Value -shr 8) -band 0xFF)
    )
}

function Get-U32Bytes([uint32]$Value) {
    return ,[byte[]](
        [byte]($Value -band 0xFF),
        [byte](($Value -shr 8) -band 0xFF),
        [byte](($Value -shr 16) -band 0xFF),
        [byte](($Value -shr 24) -band 0xFF)
    )
}

function Get-Crc16Ccitt([byte[]]$Data, [int]$Offset, [int]$Length) {
    [int]$crc = 0xFFFF
    for ($i = 0; $i -lt $Length; $i++) {
        $crc = ($crc -bxor ([int]$Data[$Offset + $i] -shl 8)) -band 0xFFFF
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF
            } else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return [uint16]$crc
}

function Get-Crc32([byte[]]$Data) {
    [uint32]$crc = [uint32]::MaxValue
    foreach ($item in $Data) {
        $crc = [uint32]($crc -bxor [uint32]$item)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 1) -ne 0) {
                $crc = [uint32](($crc -shr 1) -bxor 0xEDB88320)
            } else {
                $crc = [uint32]($crc -shr 1)
            }
        }
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

function Convert-Version([string]$Text) {
    $parts = $Text.Split(".")
    if ($parts.Length -ne 3) { throw "Version must be MAJOR.MINOR.PATCH, for example 1.0.0" }
    [int]$major = [int]$parts[0]
    [int]$minor = [int]$parts[1]
    [int]$patch = [int]$parts[2]
    if ($major -lt 0 -or $major -gt 255 -or $minor -lt 0 -or $minor -gt 255 -or $patch -lt 0 -or $patch -gt 255) {
        throw "Version parts must be in range 0..255"
    }
    return [uint32](($major -shl 24) -bor ($minor -shl 16) -bor ($patch -shl 8))
}

function Format-Version([uint32]$Value) {
    if ($Value -eq [uint32]::MaxValue) { return "none" }
    return "{0}.{1}.{2}" -f (($Value -shr 24) -band 0xFF), (($Value -shr 16) -band 0xFF), (($Value -shr 8) -band 0xFF)
}

function New-Packet([byte]$Command, [byte]$Sequence, [byte[]]$Payload) {
    if ($null -eq $Payload) { $Payload = [byte[]]@() }
    $length = [uint16]$Payload.Length
    $packet = New-Object byte[] (2 + 1 + 1 + 2 + $Payload.Length + 2)
    $packet[0] = $script:Head0
    $packet[1] = $script:Head1
    $packet[2] = $Command
    $packet[3] = $Sequence
    $lenBytes = Get-U16Bytes $length
    $packet[4] = $lenBytes[0]
    $packet[5] = $lenBytes[1]
    if ($Payload.Length -gt 0) {
        [Array]::Copy($Payload, 0, $packet, 6, $Payload.Length)
    }
    $crc = Get-Crc16Ccitt $packet 2 (4 + $Payload.Length)
    $crcBytes = Get-U16Bytes $crc
    $packet[$packet.Length - 2] = $crcBytes[0]
    $packet[$packet.Length - 1] = $crcBytes[1]
    return ,$packet
}

function Read-Exact([System.IO.Ports.SerialPort]$Serial, [int]$Count) {
    $buffer = New-Object byte[] $Count
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Serial.Read($buffer, $offset, $Count - $offset)
        if ($read -le 0) { throw "Serial read timeout" }
        $offset += $read
    }
    return ,$buffer
}

function Read-Frame([System.IO.Ports.SerialPort]$Serial) {
    $raw = New-Object System.Collections.Generic.List[byte]
    $prev = [byte]0
    while ($true) {
        $b = (Read-Exact $Serial 1)[0]
        [void]$raw.Add($b)
        if ($prev -eq $script:Head0 -and $b -eq $script:Head1) { break }
        $prev = $b
    }

    $header = Read-Exact $Serial 4
    $raw.AddRange($header)
    $len = [BitConverter]::ToUInt16($header, 2)
    $payload = Read-Exact $Serial $len
    $raw.AddRange($payload)
    $crcBytes = Read-Exact $Serial 2
    $raw.AddRange($crcBytes)

    $crcInput = New-Object byte[] (4 + $payload.Length)
    [Array]::Copy($header, 0, $crcInput, 0, 4)
    if ($payload.Length -gt 0) { [Array]::Copy($payload, 0, $crcInput, 4, $payload.Length) }
    $actual = Get-Crc16Ccitt $crcInput 0 $crcInput.Length
    $expected = [BitConverter]::ToUInt16($crcBytes, 0)
    if ($actual -ne $expected) {
        Add-Log ("RX: " + (Convert-ToHex $raw.ToArray()))
        throw ("RX CRC16 mismatch, actual=0x{0:X4}, expected=0x{1:X4}" -f $actual, $expected)
    }

    Add-Log ("RX: " + (Convert-ToHex $raw.ToArray()))
    return [PSCustomObject]@{
        Cmd = $header[0]
        Seq = $header[1]
        Len = $len
        Payload = $payload
    }
}

function Send-Frame([System.IO.Ports.SerialPort]$Serial, [byte]$Command, [byte]$Sequence, [byte[]]$Payload) {
    $packet = New-Packet $Command $Sequence $Payload
    Add-Log ("TX: " + (Convert-ToHex $packet))
    $Serial.Write($packet, 0, $packet.Length)
}

function Invoke-CommandFrame([System.IO.Ports.SerialPort]$Serial, [byte]$Command, [byte]$Sequence, [byte[]]$Payload, [byte]$ExpectedResponse) {
    Send-Frame $Serial $Command $Sequence $Payload
    $frame = Read-Frame $Serial
    if ($frame.Seq -ne $Sequence) {
        throw ("Unexpected sequence: rx={0}, expected={1}" -f $frame.Seq, $Sequence)
    }
    if ($frame.Cmd -eq $script:RspNack) {
        if ($frame.Payload.Length -lt 8) { throw "Short NACK payload" }
        $req = $frame.Payload[0]
        $err = $frame.Payload[1]
        $expectedSeq = $frame.Payload[2]
        $nextOffset = [BitConverter]::ToUInt32($frame.Payload, 4)
        throw ("NACK: request=0x{0:X2}, err=0x{1:X2}, expected_seq={2}, next_offset={3}" -f $req, $err, $expectedSeq, $nextOffset)
    }
    if ($frame.Cmd -ne $ExpectedResponse) {
        throw ("Unexpected response CMD 0x{0:X2}" -f $frame.Cmd)
    }
    return ,[byte[]]$frame.Payload
}

function Invoke-AckCommand([System.IO.Ports.SerialPort]$Serial, [byte]$Command, [byte]$Sequence, [byte[]]$Payload) {
    $rsp = Invoke-CommandFrame $Serial $Command $Sequence $Payload $script:RspAck
    if ($rsp.Length -lt 6) { throw "Short ACK payload" }
    if ($rsp[0] -ne $Command -or $rsp[1] -ne 0) { throw "ACK content mismatch" }
    return [BitConverter]::ToUInt32($rsp, 2)
}

function Invoke-QueryInfo([System.IO.Ports.SerialPort]$Serial, [byte]$Sequence) {
    $rsp = Invoke-CommandFrame $Serial $script:CmdQueryInfo $Sequence ([byte[]]@()) $script:RspDeviceInfo
    if ($rsp.Length -ne 28) { throw "Invalid device info length $($rsp.Length)" }
    return [PSCustomObject]@{
        RunMode = $rsp[0]
        ProtocolMajor = $rsp[1]
        ProtocolMinor = $rsp[2]
        AppValid = $rsp[3]
        TargetId = [BitConverter]::ToUInt32($rsp, 4)
        HardwareVersion = [BitConverter]::ToUInt16($rsp, 8)
        BootVersion = [BitConverter]::ToUInt32($rsp, 10)
        AppVersion = [BitConverter]::ToUInt32($rsp, 14)
        AppStart = [BitConverter]::ToUInt32($rsp, 18)
        AppCapacity = [BitConverter]::ToUInt32($rsp, 22)
        MaxChunk = [BitConverter]::ToUInt16($rsp, 26)
    }
}

function Refresh-Ports {
    $selected = $portCombo.Text
    $portCombo.Items.Clear()
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    foreach ($port in $ports) { [void]$portCombo.Items.Add($port) }
    if ($ports -contains $selected) {
        $portCombo.SelectedItem = $selected
    } elseif ($portCombo.Items.Count -gt 0) {
        $portCombo.SelectedIndex = 0
    }
}

$form = New-Object System.Windows.Forms.Form
$form.Text = "STM32 RS232 IAP Updater"
$form.Size = New-Object System.Drawing.Size(860, 620)
$form.StartPosition = "CenterScreen"

$portLabel = New-Object System.Windows.Forms.Label
$portLabel.Text = "Port:"
$portLabel.Location = New-Object System.Drawing.Point(20, 24)
$portLabel.AutoSize = $true
$form.Controls.Add($portLabel)

$portCombo = New-Object System.Windows.Forms.ComboBox
$portCombo.Location = New-Object System.Drawing.Point(80, 20)
$portCombo.Size = New-Object System.Drawing.Size(130, 28)
$portCombo.DropDownStyle = "DropDownList"
$form.Controls.Add($portCombo)

$refreshButton = New-Object System.Windows.Forms.Button
$refreshButton.Text = "Refresh"
$refreshButton.Location = New-Object System.Drawing.Point(225, 19)
$refreshButton.Size = New-Object System.Drawing.Size(90, 28)
$refreshButton.Add_Click({ Refresh-Ports })
$form.Controls.Add($refreshButton)

$versionLabel = New-Object System.Windows.Forms.Label
$versionLabel.Text = "Version:"
$versionLabel.Location = New-Object System.Drawing.Point(340, 24)
$versionLabel.AutoSize = $true
$form.Controls.Add($versionLabel)

$versionBox = New-Object System.Windows.Forms.TextBox
$versionBox.Text = "1.0.0"
$versionBox.Location = New-Object System.Drawing.Point(405, 20)
$versionBox.Size = New-Object System.Drawing.Size(100, 28)
$form.Controls.Add($versionBox)

$fileLabel = New-Object System.Windows.Forms.Label
$fileLabel.Text = "BIN:"
$fileLabel.Location = New-Object System.Drawing.Point(20, 70)
$fileLabel.AutoSize = $true
$form.Controls.Add($fileLabel)

$fileBox = New-Object System.Windows.Forms.TextBox
$fileBox.Location = New-Object System.Drawing.Point(80, 66)
$fileBox.Size = New-Object System.Drawing.Size(620, 28)
$fileBox.ReadOnly = $true
$form.Controls.Add($fileBox)

$browseButton = New-Object System.Windows.Forms.Button
$browseButton.Text = "Browse..."
$browseButton.Location = New-Object System.Drawing.Point(715, 65)
$browseButton.Size = New-Object System.Drawing.Size(95, 28)
$browseButton.Add_Click({
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Filter = "App binary (*.bin)|*.bin|All files (*.*)|*.*"
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        $fileBox.Text = $dialog.FileName
    }
})
$form.Controls.Add($browseButton)

$queryButton = New-Object System.Windows.Forms.Button
$queryButton.Text = "Query"
$queryButton.Location = New-Object System.Drawing.Point(80, 110)
$queryButton.Size = New-Object System.Drawing.Size(120, 38)
$form.Controls.Add($queryButton)

$upgradeButton = New-Object System.Windows.Forms.Button
$upgradeButton.Text = "Upgrade"
$upgradeButton.Location = New-Object System.Drawing.Point(220, 110)
$upgradeButton.Size = New-Object System.Drawing.Size(120, 38)
$form.Controls.Add($upgradeButton)

$progress = New-Object System.Windows.Forms.ProgressBar
$progress.Location = New-Object System.Drawing.Point(360, 117)
$progress.Size = New-Object System.Drawing.Size(450, 24)
$form.Controls.Add($progress)

$statusLabel = New-Object System.Windows.Forms.Label
$statusLabel.Text = "Ready"
$statusLabel.Location = New-Object System.Drawing.Point(20, 165)
$statusLabel.Size = New-Object System.Drawing.Size(790, 24)
$form.Controls.Add($statusLabel)

$logBox = New-Object System.Windows.Forms.TextBox
$logBox.Location = New-Object System.Drawing.Point(20, 200)
$logBox.Size = New-Object System.Drawing.Size(790, 350)
$logBox.Multiline = $true
$logBox.ReadOnly = $true
$logBox.ScrollBars = "Vertical"
$logBox.Font = New-Object System.Drawing.Font("Consolas", 9)
$form.Controls.Add($logBox)

$queryButton.Add_Click({
    if ([string]::IsNullOrWhiteSpace($portCombo.Text)) {
        [System.Windows.Forms.MessageBox]::Show("Select a serial port.") | Out-Null
        return
    }
    Set-UiState $true "Querying..."
    try {
        $serial = New-Object System.IO.Ports.SerialPort($portCombo.Text, 115200, "None", 8, "One")
        $serial.ReadTimeout = 2000
        $serial.WriteTimeout = 2000
        $serial.Open()
        try {
            $info = Invoke-QueryInfo $serial ([byte]0)
            Add-Log ("INFO: mode={0}, protocol={1}.{2}, target=0x{3:X8}, hw={4}, boot={5}, app={6}, app_valid={7}, app_start=0x{8:X8}, capacity={9}, max_chunk={10}" -f `
                $info.RunMode, $info.ProtocolMajor, $info.ProtocolMinor, $info.TargetId, $info.HardwareVersion, `
                (Format-Version $info.BootVersion), (Format-Version $info.AppVersion), $info.AppValid, `
                $info.AppStart, $info.AppCapacity, $info.MaxChunk)
            Set-Progress 0 "Query complete"
        } finally {
            if ($serial.IsOpen) { $serial.Close() }
            $serial.Dispose()
        }
    } catch {
        Add-Log "ERROR: $($_.Exception.Message)"
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Query failed", "OK", "Error") | Out-Null
    } finally {
        Set-UiState $false "Ready"
    }
})

$upgradeButton.Add_Click({
    if ([string]::IsNullOrWhiteSpace($portCombo.Text)) {
        [System.Windows.Forms.MessageBox]::Show("Select a serial port.") | Out-Null
        return
    }
    if (-not (Test-Path -LiteralPath $fileBox.Text)) {
        [System.Windows.Forms.MessageBox]::Show("Select an App.bin file.") | Out-Null
        return
    }

    Set-UiState $true "Upgrading..."
    try {
        $image = [IO.File]::ReadAllBytes($fileBox.Text)
        if ($image.Length -lt 8) { throw "App.bin is too small." }
        $version = Convert-Version $versionBox.Text
        $imageCrc = Get-Crc32 $image
        Add-Log ("FILE: {0}, size={1}, crc32=0x{2:X8}, version={3}" -f $fileBox.Text, $image.Length, $imageCrc, $versionBox.Text)

        $serial = New-Object System.IO.Ports.SerialPort($portCombo.Text, 115200, "None", 8, "One")
        $serial.ReadTimeout = 30000
        $serial.WriteTimeout = 30000
        $seq = [byte]0
        try {
            $serial.Open()
            $info = Invoke-QueryInfo $serial $seq
            $seq = [byte](($seq + 1) -band 0xFF)
            Add-Log ("INFO: mode={0}, target=0x{1:X8}, capacity={2}, max_chunk={3}" -f $info.RunMode, $info.TargetId, $info.AppCapacity, $info.MaxChunk)
            if ($info.RunMode -eq 0) {
                Add-Log "INFO: device is running App, requesting bootloader..."
                $oldReadTimeout = $serial.ReadTimeout
                $serial.ReadTimeout = 2000
                try {
                    $enterPayload = [Text.Encoding]::ASCII.GetBytes("IAP1")
                    [void](Invoke-AckCommand $serial $script:CmdEnterUpgrade $seq $enterPayload)
                } catch {
                    Add-Log "INFO: enter-upgrade response not received, waiting for reboot..."
                } finally {
                    $serial.ReadTimeout = $oldReadTimeout
                }
                if ($serial.IsOpen) { $serial.Close() }
                Start-Sleep -Milliseconds 1200
                $seq = [byte]0
                $serial.Open()
                $info = Invoke-QueryInfo $serial $seq
                $seq = [byte](($seq + 1) -band 0xFF)
                Add-Log ("INFO: mode={0}, target=0x{1:X8}, capacity={2}, max_chunk={3}" -f $info.RunMode, $info.TargetId, $info.AppCapacity, $info.MaxChunk)
            }
            if ($info.RunMode -ne 1) { throw "Device is not in Bootloader mode." }
            if ($info.TargetId -ne $script:TargetId) { throw "Target mismatch." }
            if ($image.Length -gt $info.AppCapacity) { throw "Image size exceeds App capacity." }
            $chunk = [Math]::Min([int]$script:ChunkSize, [int]$info.MaxChunk)

            $start = New-Object byte[] 22
            [Array]::Copy((Get-U32Bytes $script:TargetId), 0, $start, 0, 4)
            [Array]::Copy((Get-U16Bytes $script:HwVersion), 0, $start, 4, 2)
            [Array]::Copy((Get-U16Bytes $script:HwVersion), 0, $start, 6, 2)
            [Array]::Copy((Get-U32Bytes $version), 0, $start, 8, 4)
            [Array]::Copy((Get-U32Bytes ([uint32]$image.Length)), 0, $start, 12, 4)
            [Array]::Copy((Get-U32Bytes $imageCrc), 0, $start, 16, 4)
            [Array]::Copy((Get-U16Bytes ([uint16]$chunk)), 0, $start, 20, 2)
            [void](Invoke-AckCommand $serial $script:CmdUpgradeStart $seq $start)
            $seq = [byte](($seq + 1) -band 0xFF)

            for ($offset = 0; $offset -lt $image.Length; $offset += $chunk) {
                $count = [Math]::Min($chunk, $image.Length - $offset)
                $payload = New-Object byte[] (6 + $count)
                [Array]::Copy((Get-U32Bytes ([uint32]$offset)), 0, $payload, 0, 4)
                [Array]::Copy((Get-U16Bytes ([uint16]$count)), 0, $payload, 4, 2)
                [Array]::Copy($image, $offset, $payload, 6, $count)
                $next = Invoke-AckCommand $serial $script:CmdUpgradeData $seq $payload
                if ($next -ne ($offset + $count)) { throw "Unexpected next_offset $next." }
                $seq = [byte](($seq + 1) -band 0xFF)
                $percent = [int]($next * 100 / $image.Length)
                Set-Progress $percent ("Writing... {0}%" -f $percent)
            }

            $end = New-Object byte[] 8
            [Array]::Copy((Get-U32Bytes ([uint32]$image.Length)), 0, $end, 0, 4)
            [Array]::Copy((Get-U32Bytes $imageCrc), 0, $end, 4, 4)
            [void](Invoke-AckCommand $serial $script:CmdUpgradeEnd $seq $end)
            Set-Progress 100 "Upgrade complete, Bootloader will jump to App"
            Add-Log "DONE: Upgrade complete."
        } catch {
            Add-Log "ERROR: $($_.Exception.Message)"
            try {
                if ($serial.IsOpen) {
                    Send-Frame $serial $script:CmdUpgradeAbort $seq ([byte[]]@())
                }
            } catch {
            }
            throw
        } finally {
            if ($serial.IsOpen) { $serial.Close() }
            $serial.Dispose()
        }
    } catch {
        Add-Log "ERROR: $($_.Exception.Message)"
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Upgrade failed", "OK", "Error") | Out-Null
    } finally {
        Set-UiState $false "Ready"
    }
})

Refresh-Ports
[void]$form.ShowDialog()
