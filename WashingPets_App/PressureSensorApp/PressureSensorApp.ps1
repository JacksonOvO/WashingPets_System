Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'
$script:ConfigPath = Join-Path $PSScriptRoot 'pressure_sensor_config.json'
$script:Config = Get-Content $script:ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$script:Serial = $null
$script:Busy = $false
$script:Samples = New-Object System.Collections.Generic.List[object]
$script:MaxSamples = 240
$script:LastRaw = 0.0
$script:LastCorrected = 0.0
$script:LastComp = 0.0
$script:FilteredCorrected = 0.0
$script:FilterReady = $false
$script:ParamValues = @{}

if (-not ($script:Config.PSObject.Properties.Name -contains 'displayFilterSamples')) {
    $script:Config | Add-Member -NotePropertyName displayFilterSamples -NotePropertyValue 10
}

function Add-Log($text) {
    $stamp = Get-Date -Format 'HH:mm:ss'
    $logBox.AppendText("[$stamp] $text`r`n")
}

function Convert-ToSigned16([int]$value) {
    if ($value -band 0x8000) { return $value - 0x10000 }
    return $value
}

function Get-RegisterOffset([int]$register) {
    if ($register -ge 40001) { return $register - 40001 }
    return $register
}

function Get-Crc16([byte[]]$bytes, [int]$length) {
    [uint16]$crc = 0xFFFF
    for ($i = 0; $i -lt $length; $i++) {
        $crc = $crc -bxor $bytes[$i]
        for ($j = 0; $j -lt 8; $j++) {
            if (($crc -band 1) -ne 0) {
                $crc = ($crc -shr 1) -bxor 0xA001
            } else {
                $crc = $crc -shr 1
            }
        }
    }
    return $crc
}

function Invoke-Modbus([byte[]]$request, [int]$expectedMin) {
    if (-not $script:Serial -or -not $script:Serial.IsOpen) { throw '串口未连接' }
    while ($script:Busy) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 5 }
    $script:Busy = $true
    try {
        $script:Serial.DiscardInBuffer()
        $script:Serial.Write($request, 0, $request.Length)
        $buffer = New-Object byte[] 256
        $read = 0
        $target = [Math]::Max(5, $expectedMin)
        $deadline = (Get-Date).AddMilliseconds($script:Serial.ReadTimeout + 500)
        while ($read -lt $target -and (Get-Date) -lt $deadline) {
            try {
                $n = $script:Serial.Read($buffer, $read, $buffer.Length - $read)
                $read += $n
                if ($read -ge 3) {
                    if (($buffer[1] -band 0x80) -ne 0) {
                        $target = 5
                    } elseif ($buffer[1] -eq 0x03) {
                        $target = 5 + [int]$buffer[2]
                    } elseif ($buffer[1] -eq 0x06) {
                        $target = 8
                    }
                }
            } catch [TimeoutException] {
                break
            }
        }
        if ($read -lt 5) { throw '未收到有效响应' }
        $resp = New-Object byte[] $read
        [Array]::Copy($buffer, $resp, $read)
        $got = [uint16](([int]$resp[$read - 2]) -bor (([int]$resp[$read - 1]) -shl 8))
        $calc = Get-Crc16 $resp ($read - 2)
        if ($got -ne $calc) {
            $hex = (($resp | ForEach-Object { '{0:X2}' -f $_ }) -join ' ')
            throw ('CRC错误: got=0x{0:X4}, calc=0x{1:X4}, len={2}, frame={3}' -f $got, $calc, $read, $hex)
        }
        if (($resp[1] -band 0x80) -ne 0) { throw ('设备异常码: 0x{0:X2}' -f $resp[2]) }
        return $resp
    } finally {
        $script:Busy = $false
    }
}

function Read-HoldingRegisters([int]$slave, [int]$register, [int]$count) {
    $addr = Get-RegisterOffset $register
    $req = [byte[]]@($slave, 0x03, (($addr -shr 8) -band 0xFF), ($addr -band 0xFF), (($count -shr 8) -band 0xFF), ($count -band 0xFF), 0, 0)
    $crc = Get-Crc16 $req 6
    $req[6] = $crc -band 0xFF
    $req[7] = ($crc -shr 8) -band 0xFF
    $resp = Invoke-Modbus $req (5 + $count * 2)
    if ($resp[1] -ne 0x03) { throw '响应功能码不是03' }
    $values = @()
    for ($i = 0; $i -lt $count; $i++) {
        $p = 3 + $i * 2
        $values += ((([int]$resp[$p]) -shl 8) -bor ([int]$resp[$p + 1]))
    }
    return $values
}

function Write-SingleRegister([int]$slave, [int]$register, [int]$value) {
    $addr = Get-RegisterOffset $register
    $u = $value -band 0xFFFF
    $req = [byte[]]@($slave, 0x06, (($addr -shr 8) -band 0xFF), ($addr -band 0xFF), (($u -shr 8) -band 0xFF), ($u -band 0xFF), 0, 0)
    $crc = Get-Crc16 $req 6
    $req[6] = $crc -band 0xFF
    $req[7] = ($crc -shr 8) -band 0xFF
    [void](Invoke-Modbus $req 8)
}

function Get-UnitName([int]$code) {
    switch ($code) {
        1 { 'MPa' }
        2 { 'Kg' }
        3 { 'T' }
        4 { 'g' }
        5 { 'N' }
        6 { 'KN' }
        default { $script:Config.unit }
    }
}

function Get-CorrectedWeight([double]$raw) {
    $pts = @()
    foreach ($row in $compGrid.Rows) {
        if ($row.IsNewRow) { continue }
        $d = 0.0
        $a = 0.0
        if ([double]::TryParse([string]$row.Cells[0].Value, [ref]$d) -and [double]::TryParse([string]$row.Cells[1].Value, [ref]$a)) {
            $pts += [pscustomobject]@{ Display = $d; Actual = $a }
        }
    }
    $pts = $pts | Sort-Object Display
    if ($pts.Count -lt 2) { return $raw }
    $p1 = $pts[0]
    $p2 = $pts[1]
    for ($i = 0; $i -lt $pts.Count - 1; $i++) {
        if ($raw -ge $pts[$i].Display -and $raw -le $pts[$i + 1].Display) {
            $p1 = $pts[$i]
            $p2 = $pts[$i + 1]
            break
        }
        if ($raw -gt $pts[$pts.Count - 1].Display) {
            $p1 = $pts[$pts.Count - 2]
            $p2 = $pts[$pts.Count - 1]
        }
    }
    if ([Math]::Abs($p2.Display - $p1.Display) -lt 0.000001) { return $raw }
    $k = ($p2.Actual - $p1.Actual) / ($p2.Display - $p1.Display)
    $b = $p1.Actual - $k * $p1.Display
    return $k * $raw + $b
}

function Update-CompInfo {
    $pts = @()
    foreach ($row in $compGrid.Rows) {
        if ($row.IsNewRow) { continue }
        $d = 0.0
        $a = 0.0
        if ([double]::TryParse([string]$row.Cells[0].Value, [ref]$d) -and [double]::TryParse([string]$row.Cells[1].Value, [ref]$a)) {
            $pts += [pscustomobject]@{ Display = $d; Actual = $a }
        }
    }
    $pts = $pts | Sort-Object Display
    if ($pts.Count -lt 2) {
        $compInfo.Text = '至少填写两组显示值/实际值'
        return
    }
    $parts = @()
    for ($i = 0; $i -lt $pts.Count - 1; $i++) {
        if ([Math]::Abs($pts[$i + 1].Display - $pts[$i].Display) -lt 0.000001) { continue }
        $k = ($pts[$i + 1].Actual - $pts[$i].Actual) / ($pts[$i + 1].Display - $pts[$i].Display)
        $b = $pts[$i].Actual - $k * $pts[$i].Display
        $parts += ('{0:g}~{1:g}: y={2:N6}x{3:+0.######;-0.######;+0}' -f $pts[$i].Display, $pts[$i + 1].Display, $k, $b)
    }
    $compInfo.Text = $parts -join '    '
}

function Save-Config {
    $script:Config.slaveId = [int]$slaveBox.Value
    $script:Config.baudRate = [int]$baudBox.Text
    $script:Config.parity = [string]$parityBox.Text
    $script:Config.dataBits = [int]$dataBitsBox.Value
    $script:Config.stopBits = [string]$stopBitsBox.Text
    $script:Config.pollIntervalMs = [int]$pollBox.Value
    $script:Config.displayFilterSamples = [int]$filterBox.Value
    $script:Config.decimalPlaces = [int]$decimalBox.Value
    $script:Config.unit = [string]$unitBox.Text
    $script:Config.bipolar = [bool]$bipolarBox.Checked
    $points = @()
    foreach ($row in $compGrid.Rows) {
        if ($row.IsNewRow) { continue }
        $points += [pscustomobject]@{ display = [double]$row.Cells[0].Value; actual = [double]$row.Cells[1].Value }
    }
    $script:Config.compensationPoints = $points
    $script:Config | ConvertTo-Json -Depth 8 | Set-Content $script:ConfigPath -Encoding UTF8
    Add-Log '配置已保存'
}

function Fill-Ports {
    $portBox.Items.Clear()
    [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object | ForEach-Object { [void]$portBox.Items.Add($_) }
    if ($portBox.Items.Count -gt 0 -and -not $portBox.Text) { $portBox.SelectedIndex = 0 }
}

$form = New-Object Windows.Forms.Form
$form.Text = 'BSQ-DG-V2 数字型变送器 Modbus RTU 调试软件'
$form.Size = New-Object Drawing.Size(1220, 780)
$form.StartPosition = 'CenterScreen'
$form.Font = New-Object Drawing.Font('Microsoft YaHei UI', 9)

$tabs = New-Object Windows.Forms.TabControl
$tabs.Dock = 'Fill'
$form.Controls.Add($tabs)

$tabMain = New-Object Windows.Forms.TabPage '实时重量'
$tabParams = New-Object Windows.Forms.TabPage '参数配置'
$tabComp = New-Object Windows.Forms.TabPage '三段补偿'
$tabs.TabPages.AddRange(@($tabMain, $tabParams, $tabComp))

$conn = New-Object Windows.Forms.GroupBox
$conn.Text = '串口'
$conn.SetBounds(10, 10, 1180, 78)
$tabMain.Controls.Add($conn)

$portBox = New-Object Windows.Forms.ComboBox
$portBox.DropDownStyle = 'DropDownList'
$portBox.SetBounds(55, 30, 90, 26)
$conn.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='COM'; Location='15,33'; AutoSize=$true }))
$conn.Controls.Add($portBox)

$refreshBtn = New-Object Windows.Forms.Button
$refreshBtn.Text = '刷新'
$refreshBtn.SetBounds(150, 29, 58, 28)
$conn.Controls.Add($refreshBtn)

$baudBox = New-Object Windows.Forms.ComboBox
$baudBox.Items.AddRange(@('1200','2400','4800','9600','19200','38400','115200'))
$baudBox.Text = [string]$script:Config.baudRate
$baudBox.SetBounds(265, 30, 90, 26)
$conn.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='波特率'; Location='220,33'; AutoSize=$true }))
$conn.Controls.Add($baudBox)

$parityBox = New-Object Windows.Forms.ComboBox
$parityBox.Items.AddRange(@('None','Odd','Even'))
$parityBox.Text = [string]$script:Config.parity
$parityBox.SetBounds(410, 30, 80, 26)
$conn.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='校验'; Location='370,33'; AutoSize=$true }))
$conn.Controls.Add($parityBox)

$stopBitsBox = New-Object Windows.Forms.ComboBox
$stopBitsBox.Items.AddRange(@('One','Two'))
$stopBitsBox.Text = [string]$script:Config.stopBits
$stopBitsBox.SetBounds(545, 30, 70, 26)
$conn.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='停止位'; Location='500,33'; AutoSize=$true }))
$conn.Controls.Add($stopBitsBox)

$dataBitsBox = New-Object Windows.Forms.NumericUpDown
$dataBitsBox.Minimum = 7; $dataBitsBox.Maximum = 8; $dataBitsBox.Value = [int]$script:Config.dataBits
$dataBitsBox.SetBounds(675, 30, 50, 26)
$conn.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='数据位'; Location='630,33'; AutoSize=$true }))
$conn.Controls.Add($dataBitsBox)

$slaveBox = New-Object Windows.Forms.NumericUpDown
$slaveBox.Minimum = 1; $slaveBox.Maximum = 250; $slaveBox.Value = [int]$script:Config.slaveId
$slaveBox.SetBounds(795, 30, 60, 26)
$conn.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='地址'; Location='760,33'; AutoSize=$true }))
$conn.Controls.Add($slaveBox)

$pollBox = New-Object Windows.Forms.NumericUpDown
$pollBox.Minimum = 100; $pollBox.Maximum = 5000; $pollBox.Increment = 100; $pollBox.Value = [int]$script:Config.pollIntervalMs
$pollBox.SetBounds(930, 30, 70, 26)
$conn.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='刷新ms'; Location='875,33'; AutoSize=$true }))
$conn.Controls.Add($pollBox)

$connectBtn = New-Object Windows.Forms.Button
$connectBtn.Text = '连接'
$connectBtn.SetBounds(1025, 28, 70, 30)
$conn.Controls.Add($connectBtn)

$statusLabel = New-Object Windows.Forms.Label
$statusLabel.Text = '未连接'
$statusLabel.ForeColor = [Drawing.Color]::Firebrick
$statusLabel.SetBounds(1110, 34, 60, 24)
$conn.Controls.Add($statusLabel)

$valuePanel = New-Object Windows.Forms.Panel
$valuePanel.SetBounds(10, 95, 300, 250)
$tabMain.Controls.Add($valuePanel)

$rawLabel = New-Object Windows.Forms.Label
$rawLabel.Text = '原始重量 0'
$rawLabel.Font = New-Object Drawing.Font('Microsoft YaHei UI', 18, [Drawing.FontStyle]::Bold)
$rawLabel.SetBounds(5, 10, 290, 44)
$valuePanel.Controls.Add($rawLabel)

$finalLabel = New-Object Windows.Forms.Label
$finalLabel.Text = '补偿后 0'
$finalLabel.Font = New-Object Drawing.Font('Microsoft YaHei UI', 22, [Drawing.FontStyle]::Bold)
$finalLabel.ForeColor = [Drawing.Color]::SeaGreen
$finalLabel.SetBounds(5, 70, 290, 52)
$valuePanel.Controls.Add($finalLabel)

$compLabel = New-Object Windows.Forms.Label
$compLabel.Text = '补偿值 0'
$compLabel.Font = New-Object Drawing.Font('Microsoft YaHei UI', 12)
$compLabel.SetBounds(8, 135, 280, 28)
$valuePanel.Controls.Add($compLabel)

$decimalBox = New-Object Windows.Forms.NumericUpDown
$decimalBox.Minimum = 0; $decimalBox.Maximum = 3; $decimalBox.Value = [int]$script:Config.decimalPlaces
$decimalBox.SetBounds(80, 178, 55, 26)
$valuePanel.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='小数点'; Location='8,181'; AutoSize=$true }))
$valuePanel.Controls.Add($decimalBox)

$unitBox = New-Object Windows.Forms.ComboBox
$unitBox.Items.AddRange(@('MPa','Kg','T','g','N','KN'))
$unitBox.Text = [string]$script:Config.unit
$unitBox.SetBounds(195, 178, 75, 26)
$valuePanel.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='单位'; Location='155,181'; AutoSize=$true }))
$valuePanel.Controls.Add($unitBox)

$bipolarBox = New-Object Windows.Forms.CheckBox
$bipolarBox.Text = '双极性'
$bipolarBox.Checked = [bool]$script:Config.bipolar
$bipolarBox.SetBounds(8, 205, 90, 24)
$valuePanel.Controls.Add($bipolarBox)

$filterBox = New-Object Windows.Forms.NumericUpDown
$filterBox.Minimum = 1; $filterBox.Maximum = 50; $filterBox.Value = [int]$script:Config.displayFilterSamples
$filterBox.SetBounds(195, 205, 75, 26)
$valuePanel.Controls.Add((New-Object Windows.Forms.Label -Property @{ Text='显示滤波'; Location='125,208'; AutoSize=$true }))
$valuePanel.Controls.Add($filterBox)

$zeroBtn = New-Object Windows.Forms.Button
$zeroBtn.Text = '清零'
$zeroBtn.SetBounds(105, 235, 75, 28)
$valuePanel.Controls.Add($zeroBtn)

$tareBtn = New-Object Windows.Forms.Button
$tareBtn.Text = '去皮'
$tareBtn.SetBounds(190, 235, 75, 28)
$valuePanel.Controls.Add($tareBtn)

$chartPanel = New-Object Windows.Forms.Panel
$chartPanel.BorderStyle = 'FixedSingle'
$chartPanel.BackColor = [Drawing.Color]::White
$chartPanel.SetBounds(320, 98, 870, 390)
$tabMain.Controls.Add($chartPanel)

$logBox = New-Object Windows.Forms.TextBox
$logBox.Multiline = $true
$logBox.ScrollBars = 'Vertical'
$logBox.ReadOnly = $true
$logBox.SetBounds(10, 500, 1180, 205)
$tabMain.Controls.Add($logBox)

$paramGrid = New-Object Windows.Forms.DataGridView
$paramGrid.SetBounds(10, 12, 1180, 620)
$paramGrid.AllowUserToAddRows = $false
$paramGrid.AutoSizeColumnsMode = 'Fill'
$paramGrid.SelectionMode = 'FullRowSelect'
$paramGrid.MultiSelect = $false
$tabParams.Controls.Add($paramGrid)
[void]$paramGrid.Columns.Add('name', '参数')
[void]$paramGrid.Columns.Add('register', '寄存器')
[void]$paramGrid.Columns.Add('access', '访问')
[void]$paramGrid.Columns.Add('value', '当前/写入值')
[void]$paramGrid.Columns.Add('range', '范围')
[void]$paramGrid.Columns.Add('note', '说明')
$paramGrid.Columns['name'].ReadOnly = $true
$paramGrid.Columns['register'].ReadOnly = $true
$paramGrid.Columns['access'].ReadOnly = $true
$paramGrid.Columns['range'].ReadOnly = $true
$paramGrid.Columns['note'].ReadOnly = $true

foreach ($p in $script:Config.parameters) {
    $range = '{0}..{1}' -f $p.min, $p.max
    [void]$paramGrid.Rows.Add($p.name, $p.register, $p.access, $p.default, $range, $p.note)
}

$readBtn = New-Object Windows.Forms.Button
$readBtn.Text = '读取选中'
$readBtn.SetBounds(10, 650, 90, 32)
$tabParams.Controls.Add($readBtn)
$writeBtn = New-Object Windows.Forms.Button
$writeBtn.Text = '写入选中'
$writeBtn.SetBounds(110, 650, 90, 32)
$tabParams.Controls.Add($writeBtn)
$readAllBtn = New-Object Windows.Forms.Button
$readAllBtn.Text = '读取常用'
$readAllBtn.SetBounds(210, 650, 90, 32)
$tabParams.Controls.Add($readAllBtn)
$saveBtn = New-Object Windows.Forms.Button
$saveBtn.Text = '保存配置'
$saveBtn.SetBounds(310, 650, 90, 32)
$tabParams.Controls.Add($saveBtn)

$compGrid = New-Object Windows.Forms.DataGridView
$compGrid.SetBounds(20, 20, 520, 170)
$compGrid.AllowUserToAddRows = $false
$compGrid.AutoSizeColumnsMode = 'Fill'
$tabComp.Controls.Add($compGrid)
[void]$compGrid.Columns.Add('display', '传感器显示值')
[void]$compGrid.Columns.Add('actual', '实际重量')
foreach ($pt in $script:Config.compensationPoints) {
    [void]$compGrid.Rows.Add($pt.display, $pt.actual)
}

$compInfo = New-Object Windows.Forms.Label
$compInfo.SetBounds(20, 205, 1150, 60)
$compInfo.Font = New-Object Drawing.Font('Consolas', 10)
$tabComp.Controls.Add($compInfo)
$saveCompBtn = New-Object Windows.Forms.Button
$saveCompBtn.Text = '保存补偿'
$saveCompBtn.SetBounds(20, 280, 95, 32)
$tabComp.Controls.Add($saveCompBtn)

$timer = New-Object Windows.Forms.Timer
$timer.Interval = [int]$pollBox.Value

$chartPanel.Add_Paint({
    param($sender, $e)
    $g = $e.Graphics
    $g.SmoothingMode = 'AntiAlias'
    $rect = $sender.ClientRectangle
    $padL = 58; $padT = 18; $padR = 20; $padB = 34
    $plot = New-Object Drawing.Rectangle($padL, $padT, ($rect.Width - $padL - $padR), ($rect.Height - $padT - $padB))
    $g.Clear([Drawing.Color]::White)
    $axisPen = New-Object Drawing.Pen([Drawing.Color]::FromArgb(90,90,90), 1)
    $gridPen = New-Object Drawing.Pen([Drawing.Color]::FromArgb(230,230,230), 1)
    for ($i = 0; $i -le 5; $i++) {
        $y = $plot.Top + $plot.Height * $i / 5
        $g.DrawLine($gridPen, $plot.Left, $y, $plot.Right, $y)
    }
    $g.DrawRectangle($axisPen, $plot)
    if ($script:Samples.Count -lt 2) { return }
    $vals = @($script:Samples | ForEach-Object { $_.Value })
    $min = ($vals | Measure-Object -Minimum).Minimum
    $max = ($vals | Measure-Object -Maximum).Maximum
    if ([Math]::Abs($max - $min) -lt 0.0001) { $max += 1; $min -= 1 }
    $font = New-Object Drawing.Font('Consolas', 9)
    $brush = [Drawing.Brushes]::DimGray
    $g.DrawString(('{0:N3}' -f $max), $font, $brush, 4, $plot.Top - 4)
    $g.DrawString(('{0:N3}' -f $min), $font, $brush, 4, $plot.Bottom - 14)
    $points = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
    for ($i = 0; $i -lt $script:Samples.Count; $i++) {
        $x = $plot.Left + ($plot.Width * $i / [Math]::Max(1, $script:Samples.Count - 1))
        $y = $plot.Bottom - (($script:Samples[$i].Value - $min) / ($max - $min) * $plot.Height)
        $points.Add((New-Object Drawing.PointF($x, $y)))
    }
    $linePen = New-Object Drawing.Pen([Drawing.Color]::SeaGreen, 2)
    $g.DrawLines($linePen, $points.ToArray())
    $g.DrawString('补偿后实时曲线', (New-Object Drawing.Font('Microsoft YaHei UI', 10, [Drawing.FontStyle]::Bold)), [Drawing.Brushes]::SeaGreen, $plot.Left, 2)
})

$timer.Add_Tick({
    $timer.Interval = [int]$pollBox.Value
    try {
        $vals = Read-HoldingRegisters ([int]$slaveBox.Value) 40001 2
        $rawInt = $vals[0]
        if ($bipolarBox.Checked) { $rawInt = Convert-ToSigned16 $rawInt }
        $decimals = [int]$decimalBox.Value
        if ($vals.Count -gt 1 -and $vals[1] -ge 0 -and $vals[1] -le 3) {
            $decimals = [int]$vals[1]
            $decimalBox.Value = $decimals
        }
        $raw = $rawInt / [Math]::Pow(10, $decimals)
        $corrected = Get-CorrectedWeight $raw
        $filterSamples = [Math]::Max(1, [int]$filterBox.Value)
        if ($filterSamples -le 1 -or -not $script:FilterReady) {
            $script:FilteredCorrected = $corrected
            $script:FilterReady = $true
        } else {
            $alpha = 1.0 / $filterSamples
            $script:FilteredCorrected = $script:FilteredCorrected + $alpha * ($corrected - $script:FilteredCorrected)
        }
        $script:LastRaw = $raw
        $script:LastCorrected = $script:FilteredCorrected
        $script:LastComp = $script:FilteredCorrected - $raw
        $unit = $unitBox.Text
        $rawLabel.Text = ('原始重量 {0:N3} {1}' -f $raw, $unit)
        $finalLabel.Text = ('补偿后 {0:N3} {1}' -f $script:FilteredCorrected, $unit)
        $compLabel.Text = ('补偿值 {0:+0.000;-0.000;0.000} {1}' -f $script:LastComp, $unit)
        $script:Samples.Add([pscustomobject]@{ Time = Get-Date; Value = $script:FilteredCorrected })
        while ($script:Samples.Count -gt $script:MaxSamples) { $script:Samples.RemoveAt(0) }
        $chartPanel.Invalidate()
    } catch {
        Add-Log $_.Exception.Message
    }
})

$refreshBtn.Add_Click({ Fill-Ports })
$filterBox.Add_ValueChanged({ $script:FilterReady = $false })

$connectBtn.Add_Click({
    try {
        if ($script:Serial -and $script:Serial.IsOpen) {
            $timer.Stop()
            $script:Serial.Close()
            $connectBtn.Text = '连接'
            $statusLabel.Text = '未连接'
            $statusLabel.ForeColor = [Drawing.Color]::Firebrick
            Add-Log '串口已断开'
            return
        }
        if (-not $portBox.Text) { throw '请选择COM端口' }
        $script:Serial = New-Object System.IO.Ports.SerialPort
        $script:Serial.PortName = $portBox.Text
        $script:Serial.BaudRate = [int]$baudBox.Text
        $script:Serial.Parity = [System.IO.Ports.Parity]::$($parityBox.Text)
        $script:Serial.DataBits = [int]$dataBitsBox.Value
        $script:Serial.StopBits = [System.IO.Ports.StopBits]::$($stopBitsBox.Text)
        $script:Serial.ReadTimeout = 450
        $script:Serial.WriteTimeout = 450
        $script:Serial.Open()
        $script:FilterReady = $false
        $connectBtn.Text = '断开'
        $statusLabel.Text = '已连接'
        $statusLabel.ForeColor = [Drawing.Color]::SeaGreen
        $timer.Start()
        Add-Log ('已连接 {0} {1},{2},{3},{4}' -f $portBox.Text, $baudBox.Text, $parityBox.Text, $dataBitsBox.Value, $stopBitsBox.Text)
    } catch {
        [Windows.Forms.MessageBox]::Show($_.Exception.Message, '连接失败') | Out-Null
        Add-Log $_.Exception.Message
    }
})

$zeroBtn.Add_Click({
    try {
        Write-SingleRegister ([int]$slaveBox.Value) 40023 0x0011
        $script:FilterReady = $false
        Add-Log '已发送清零: 40023 = 0x0011'
    } catch { Add-Log $_.Exception.Message }
})

$tareBtn.Add_Click({
    try {
        Write-SingleRegister ([int]$slaveBox.Value) 40018 1
        $script:FilterReady = $false
        Add-Log '已发送去皮: 40018 = 1'
    } catch { Add-Log $_.Exception.Message }
})

$readBtn.Add_Click({
    try {
        if ($paramGrid.SelectedRows.Count -eq 0) { return }
        $row = $paramGrid.SelectedRows[0]
        $reg = [int]$row.Cells['register'].Value
        $val = (Read-HoldingRegisters ([int]$slaveBox.Value) $reg 1)[0]
        if ($reg -in 40010,40011,40012,40013,40014,40015,40016,40019,40020) { $val = Convert-ToSigned16 $val }
        $row.Cells['value'].Value = $val
        Add-Log ("读取 {0}({1}) = {2}" -f $row.Cells['name'].Value, $reg, $val)
    } catch { Add-Log $_.Exception.Message }
})

$writeBtn.Add_Click({
    try {
        if ($paramGrid.SelectedRows.Count -eq 0) { return }
        $row = $paramGrid.SelectedRows[0]
        if ([string]$row.Cells['access'].Value -eq 'R') { throw '只读参数不能写入' }
        $reg = [int]$row.Cells['register'].Value
        $val = [int]$row.Cells['value'].Value
        Write-SingleRegister ([int]$slaveBox.Value) $reg $val
        Add-Log ("写入 {0}({1}) = {2}" -f $row.Cells['name'].Value, $reg, $val)
    } catch { Add-Log $_.Exception.Message }
})

$readAllBtn.Add_Click({
    try {
        $vals = Read-HoldingRegisters ([int]$slaveBox.Value) 40001 25
        foreach ($row in $paramGrid.Rows) {
            $reg = [int]$row.Cells['register'].Value
            if ($reg -ge 40001 -and $reg -le 40025) {
                $idx = $reg - 40001
                $v = $vals[$idx]
                if ($reg -in 40010,40011,40012,40013,40014,40015,40016,40019,40020) { $v = Convert-ToSigned16 $v }
                $row.Cells['value'].Value = $v
            }
        }
        if ($vals[1] -ge 0 -and $vals[1] -le 3) { $decimalBox.Value = $vals[1] }
        $unitBox.Text = Get-UnitName $vals[2]
        $bipolarBox.Checked = ($vals[23] -eq 2)
        Add-Log '已读取40001~40025常用参数'
    } catch { Add-Log $_.Exception.Message }
})

$saveBtn.Add_Click({ Save-Config })
$saveCompBtn.Add_Click({ Update-CompInfo; Save-Config })
$compGrid.Add_CellValueChanged({ Update-CompInfo })
$compGrid.Add_RowsAdded({ Update-CompInfo })
$compGrid.Add_RowsRemoved({ Update-CompInfo })

$form.Add_FormClosing({
    Save-Config
    if ($script:Serial -and $script:Serial.IsOpen) { $script:Serial.Close() }
})

Fill-Ports
Update-CompInfo
[void]$form.ShowDialog()
