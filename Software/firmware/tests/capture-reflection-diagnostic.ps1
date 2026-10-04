param(
    [string]$Port = 'COM4',
    [string]$Label = 'port2-load',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../../docs/port2-load-diagnosis')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$serial = [System.IO.Ports.SerialPort]::new($Port,115200)
$serial.DtrEnable = $true
$serial.ReadTimeout = 250
$serial.WriteTimeout = 2000
$requestId = 930000
$original = $null
$active = $false
function Request($request) {
    $script:requestId++
    $request.id = $script:requestId
    if ($request.cmd -eq 'sweep') { $script:active = $true }
    $serial.WriteLine(($request | ConvertTo-Json -Compress -Depth 10))
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $records = [System.Collections.Generic.List[object]]::new()
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $line = $serial.ReadLine() } catch [System.TimeoutException] { continue }
        $record = $line | ConvertFrom-Json
        if ($record.id -ne $script:requestId) { continue }
        $records.Add($record)
        if ($record.done -or $record.ok -eq $false -or $request.cmd -ne 'sweep') {
            if ($request.cmd -eq 'sweep') { $script:active = $false }
            if ($record.ok -eq $false) { throw "Device error: $line" }
            return ,$records.ToArray()
        }
    }
    throw "No completion for $($request.cmd)"
}
try {
    $serial.Open()
    $device = (Request @{cmd='ping'})[0]
    if (!$device.sweep_raw_adc) { throw 'Firmware lacks raw ADC sweep diagnostics' }
    $original = (Request @{cmd='status'})[0]
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $prefix = Join-Path $OutputDirectory "$Label-$stamp"
    @{device=$device; original=$original; label=$Label} | ConvertTo-Json -Depth 10 | Set-Content "$prefix-device.json"
    foreach ($z in @(50,75)) {
        foreach ($atten in @(0,64)) {
            $rows = Request @{cmd='sweep'; f0=50000000; f1=6000000000; points=201; z=$z; atten=$atten; raw_adc=$true; allow_unlocked=$false; params=@('s11','s21','s12','s22')}
            $rows | ForEach-Object { $_ | ConvertTo-Json -Compress -Depth 10 } | Set-Content "$prefix-z$z-atten$atten.jsonl"
            $points = @($rows | Where-Object { $null -ne $_.pt })
            if ($points.Count -ne 201 -or @($points | Where-Object { !$_.locked -or !$_.raw_adc }).Count) { throw 'Incomplete diagnostic capture' }
            foreach ($param in @('s11_db','s22_db')) {
                $sorted = @($points.$param | Sort-Object)
                Write-Output "$Label z=$z atten=$atten $param median=$($sorted[100]) dB"
            }
        }
    }
    Write-Output "Saved $prefix"
} finally {
    if ($serial.IsOpen) {
        if ($active) {
            $serial.WriteLine('{"cmd":"abort","id":939999}')
            $deadline = [DateTime]::UtcNow.AddSeconds(5)
            while ([DateTime]::UtcNow -lt $deadline) {
                try { $record = $serial.ReadLine() | ConvertFrom-Json } catch [System.TimeoutException] { continue }
                if ($record.done) { break }
            }
        }
        $null = Request @{cmd='manual';block='src_en';value=$false}
        if ($original) {
            foreach ($block in @('atten','z','port')) {
                $value = if ($block -eq 'port') { $original.source_port } else { $original.$block }
                $null = Request @{cmd='manual';block=$block;value=$value}
            }
        }
    }
    $serial.Dispose()
}
