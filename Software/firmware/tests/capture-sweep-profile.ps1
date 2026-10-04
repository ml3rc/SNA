param(
    [string]$Port = 'COM4',
    [long]$F0 = 50000000,
    [long]$F1 = 6000000000,
    [int]$Points = 201,
    [ValidateSet(50,75)][int]$Z = 50,
    [ValidateRange(20,10000)][int]$LockTimeoutMs = 2000,
    [switch]$StrictLock,
    [Nullable[int]]$Attenuation = $null,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../../docs/sweep-profiles')
)
$ErrorActionPreference = 'Stop'
if ($null -ne $Attenuation -and ($Attenuation -lt 0 -or $Attenuation -gt 127)) {
    throw 'Attenuation must be 0..127'
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$prefix = Join-Path $OutputDirectory "sweep-$stamp"
$serial = [System.IO.Ports.SerialPort]::new($Port,115200)
$serial.DtrEnable = $true
$serial.ReadTimeout = 250
$serial.WriteTimeout = 2000
$rows = [System.Collections.Generic.List[object]]::new()
$summary = $null
$sweepActive = $false
$rawWriter = $null
function Read-Response([int]$RequestId, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $line = $serial.ReadLine() } catch [System.TimeoutException] { continue }
        $record = $line | ConvertFrom-Json
        if ($record.id -eq $RequestId) { return $record }
    }
    throw "No response to request $RequestId"
}
try {
    $serial.Open()
    $serial.WriteLine('{"cmd":"ping","id":700001}')
    $device = Read-Response 700001 5
    if (!$device.ok -or [version]$device.fw -lt [version]'0.1.3') { throw 'Profiling requires firmware 0.1.3 or newer' }
    $device | ConvertTo-Json -Depth 6 | Set-Content "$prefix-device.json" -Encoding utf8
    Write-Output ("Firmware {0}; multicore={1}; CP20={2}" -f $device.fw,$device.multicore,$device.pll_cp20)
    $request = @{cmd='sweep'; id=700002; f0=$F0; f1=$F1; points=$Points; z=$Z; log=$false; params=@('s11','s21','s12','s22'); profile=$true; lock_timeout_ms=$LockTimeoutMs}
    $request.allow_unlocked = !$StrictLock.IsPresent
    if ($null -ne $Attenuation) { $request.atten = [int]$Attenuation }
    $request | ConvertTo-Json -Depth 6 | Set-Content "$prefix-request.json" -Encoding utf8
    $rawWriter = [System.IO.StreamWriter]::new("$prefix.jsonl",$false,[System.Text.UTF8Encoding]::new($false))
    $deadline = [DateTime]::UtcNow.AddSeconds(10 + $Points * ($LockTimeoutMs / 1000.0 + 1))
    $serial.WriteLine(($request | ConvertTo-Json -Compress -Depth 6))
    $sweepActive = $true
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $line = $serial.ReadLine() } catch [System.TimeoutException] { continue }
        $record = $line | ConvertFrom-Json
        if ($record.id -ne 700002) { continue }
        $rawWriter.WriteLine($line)
        $rawWriter.Flush()
        if ($record.done) {
            $summary = $record
            $sweepActive = $false
            break
        }
        if (!$record.timing_us) { throw 'Point has no profiling data' }
        $t = $record.timing_us
        $rows.Add([pscustomobject]@{
            point=$record.pt; frequency_hz=$record.f; valid=$record.valid
            measured=$record.measured; locked=$record.locked
            tune_us=$t.tune; path_us=$t.path; settle_us=$t.settle
            lock_wait_us=$t.lock_wait; lock_acquire_us=$t.lock_acquire
            adc_us=$t.adc; processing_us=$t.processing; total_us=$t.total; adc_reads=$t.adc_reads
        })
        Write-Output ("point {0}/{1}: {2} Hz, lock {3:N3} ms, valid={4}" -f $record.pt,($Points-1),$record.f,($t.lock_acquire/1000),$record.valid)
    }
    if (!$summary) { throw 'Sweep did not finish before its overall deadline' }
    if (!$summary.profile) { throw 'Final response has no profile summary' }
    $summary | ConvertTo-Json -Depth 10 | Set-Content "$prefix-summary.json" -Encoding utf8
    $rows | Export-Csv "$prefix.csv" -NoTypeInformation -Encoding utf8
    Write-Output ("Profile saved: {0}.csv; valid={1}, failed={2}, wall={3:N3}s" -f $prefix,$summary.profile.valid,$summary.profile.failed,($summary.profile.wall_us/1000000))
} finally {
    # Preserve partial evidence on interruption/failure too.
    if ($rawWriter) { $rawWriter.Dispose() }
    if ($rows.Count -gt 0) { $rows | Export-Csv "$prefix.csv" -NoTypeInformation -Encoding utf8 }
    if ($serial.IsOpen) {
        try {
            if ($sweepActive) {
                $serial.WriteLine('{"cmd":"abort","id":700003}')
                $null = Read-Response 700002 5
            }
            $serial.WriteLine('{"cmd":"manual","id":700004,"block":"src_en","value":false}')
            $off = Read-Response 700004 3
            if (!$off.ok) { Write-Warning 'Device did not acknowledge source disable' }
        } catch { Write-Warning "Cleanup response unavailable: $_" }
        $serial.Close()
    }
    $serial.Dispose()
}
