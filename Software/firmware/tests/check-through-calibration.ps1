param([string]$Port='COM4',[string]$OutputDirectory=(Join-Path $PSScriptRoot '../../docs/calibration-audit'))
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$serial=[System.IO.Ports.SerialPort]::new($Port,115200)
$serial.DtrEnable=$true
$serial.ReadTimeout=250
$serial.WriteTimeout=2000
$requestId=720000
$temporarySets=[System.Collections.Generic.List[string]]::new()
$original=$null
$sweepActive=$false
function Request($request) {
    $script:requestId++
    $request.id=$script:requestId
    $serial.WriteLine(($request | ConvertTo-Json -Compress -Depth 10))
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    $records=[System.Collections.Generic.List[object]]::new()
    if ($request.cmd -eq 'sweep') { $script:sweepActive=$true }
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $line=$serial.ReadLine() } catch [System.TimeoutException] { continue }
        $response=$line | ConvertFrom-Json
        if ($response.id -ne $script:requestId) { continue }
        if ($request.cmd -eq 'sweep') {
            if ($response.done -or $response.ok -eq $false) {
                $script:sweepActive=$false
                if (!$response.ok) { throw "Sweep failed: $line" }
                return ,$records.ToArray()
            }
            $records.Add($response)
        } else { return $response }
    }
    throw "Request timed out: $($request.cmd)"
}
function Capture {
    $rows=Request @{cmd='sweep';f0=50000000;f1=6000000000;points=240;z=75;log=$false;params=@('s21','s12');allow_unlocked=$false;atten=0}
    if ($rows.Count -ne 240 -or @($rows | Where-Object { !$_.locked -or $null -eq $_.s21_db -or $null -eq $_.s12_db }).Count) {
        throw 'Incomplete or unlocked capture'
    }
    return ,$rows
}
try {
    $serial.Open()
    $original=Request @{cmd='status'}
    # Never overwrite an existing calibration. This check requires both sets empty.
    foreach ($set in @('s21_75','s12_75')) {
        $existing=Request @{cmd='cal_get';set=$set}
        if ($existing.ok -or $existing.error -ne 'not_calibrated') { throw "Cannot run temporary test: $set is not confirmed empty" }
    }
    $before=Capture
    $prefix=Join-Path $OutputDirectory ('through-roundtrip-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    $before | ConvertTo-Json -Depth 10 | Set-Content "$prefix-before.json" -Encoding utf8
    foreach ($param in @('s21','s12')) {
        $set="${param}_75"
        # Mark attempted writes for cleanup even if the acknowledgement is lost.
        $temporarySets.Add($set)
        $points=@($before | ForEach-Object { $_."${param}_db" })
        $response=Request @{cmd='cal_set';set=$set;f0=50000000;f1=6000000000;points=$points}
        if (!$response.ok) { throw "Calibration upload failed: $($response.error)" }
        $stored=Request @{cmd='cal_get';set=$set}
        if (!$stored.ok -or $stored.points.Count -ne 240) { throw "Calibration readback failed: $set" }
        $stored | ConvertTo-Json -Depth 10 | Set-Content "$prefix-$set.json" -Encoding utf8
    }
    $after=Capture
    $after | ConvertTo-Json -Depth 10 | Set-Content "$prefix-after.json" -Encoding utf8
    # Protocol omits cal when true; an explicit false means uncalibrated.
    if (@($after | Where-Object { $_.cal -eq $false }).Count) { throw 'Firmware did not report calibration applied' }
    Write-Output "Round-trip saved: $prefix; all 240 corrected points calibrated and locked"
} finally {
    if ($serial.IsOpen) {
        if ($sweepActive) {
            # Drain the interrupted sweep before EEPROM cleanup.
            $serial.WriteLine('{"cmd":"abort","id":729999}')
            $deadline=[DateTime]::UtcNow.AddSeconds(5)
            while ([DateTime]::UtcNow -lt $deadline) {
                try { $line=$serial.ReadLine() } catch [System.TimeoutException] { continue }
                if (($line | ConvertFrom-Json).done) { break }
            }
        }
        foreach ($set in $temporarySets) {
            $cleared=Request @{cmd='cal_clear';set=$set}
            if (!$cleared.ok) { Write-Warning "Temporary calibration cleanup failed: $set" }
            $check=Request @{cmd='cal_get';set=$set}
            if ($check.ok -or $check.error -ne 'not_calibrated') { Write-Warning "Calibration remains: $set" }
        }
        $off=Request @{cmd='manual';block='src_en';value=$false}
        if (!$off.ok) { Write-Warning 'Source disable was not acknowledged' }
        if ($original) {
            foreach ($block in @('atten','z','port')) {
                $value=if ($block -eq 'port') { $original.source_port } else { $original.$block }
                $restored=Request @{cmd='manual';block=$block;value=$value}
                if (!$restored.ok) { Write-Warning "Restore failed: $block" }
            }
        }
        $serial.Close()
    }
    $serial.Dispose()
}
