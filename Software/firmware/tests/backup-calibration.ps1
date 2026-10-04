param([string]$Port='COM4', [string]$OutputDirectory=(Join-Path $PSScriptRoot '../../docs/calibration-audit'))
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$serial=[System.IO.Ports.SerialPort]::new($Port,115200)
$serial.DtrEnable=$true
$serial.ReadTimeout=250
$serial.WriteTimeout=2000
$requestId=710000
function Request($request) {
    $script:requestId++
    $request.id=$script:requestId
    $serial.WriteLine(($request | ConvertTo-Json -Compress))
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $line=$serial.ReadLine() } catch [System.TimeoutException] { continue }
        $response=$line | ConvertFrom-Json
        if ($response.id -eq $script:requestId) { return $response }
    }
    throw "Request timed out: $($request.cmd)"
}
try {
    $serial.Open()
    $backup=[ordered]@{captured_at=(Get-Date -Format o); device=(Request @{cmd='ping'}); status=(Request @{cmd='status'}); sets=@()}
    foreach ($z in @(50,75)) {
        foreach ($param in @('s11','s21','s12','s22')) {
            $set=Request @{cmd='cal_get';set="${param}_$z"}
            $set | Add-Member -NotePropertyName requested_set -NotePropertyValue "${param}_$z"
            $backup.sets+= $set
            Write-Output ("{0}: present={1}; points={2}; f0={3}; f1={4}" -f "${param}_$z",$set.ok,$set.points.Count,$set.f0,$set.f1)
        }
    }
    $path=Join-Path $OutputDirectory ('calibration-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'.json')
    $backup | ConvertTo-Json -Depth 10 | Set-Content -Encoding utf8 -Path $path
    Write-Output "Backup saved: $path"
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
