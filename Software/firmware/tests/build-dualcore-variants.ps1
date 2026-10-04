param([string]$BuildDir = 'build3')
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $outDir = 'artifacts/0.1.8'
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    foreach ($variant in @('baseline', 'cp20')) {
        $cp20 = if ($variant -eq 'cp20') { 'ON' } else { 'OFF' }
        & cmake -S . -B $BuildDir "-DSNA_PLL_CP20_EXPERIMENT=$cp20" *> "$outDir/$variant-configure.log"
        if ($LASTEXITCODE -ne 0) { Get-Content "$outDir/$variant-configure.log"; throw 'Configure failed' }
        & cmake --build $BuildDir *> "$outDir/$variant-build.log"
        if ($LASTEXITCODE -ne 0) { Get-Content "$outDir/$variant-build.log"; throw 'Build failed' }
        $name = if ($variant -eq 'cp20') { 'sna-dualcore-cp20' } else { 'sna-dualcore' }
        foreach ($extension in @('elf','uf2')) {
            Copy-Item "$BuildDir/sna_firmware.$extension" "$outDir/$name.$extension"
        }
        Write-Output "Built $name (CP20=$cp20)"
    }
    $cache = Get-Content "$BuildDir/CMakeCache.txt"
    $sourceGate = ($cache | Where-Object { $_ -match '^SNA_BRINGUP_NO_RF_SOURCE:BOOL=' }) -replace '^.*=', ''
    $files = Get-ChildItem "$outDir/*.elf", "$outDir/*.uf2" | ForEach-Object {
        @{file=$_.Name; sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash}
    }
    @{firmware='0.1.8'; source_gate=$sourceGate; cp_current_code=15; adc_samples=16; detector_settle_us=500; files=@($files)} |
        ConvertTo-Json -Depth 5 | Set-Content "$outDir/manifest.json" -Encoding utf8
} finally {
    # Leave the normal flash target on the hardware-validated CP20 settings.
    & cmake -S . -B $BuildDir -DSNA_PLL_CP20_EXPERIMENT=ON *> "$outDir/restore-configure.log"
    if ($LASTEXITCODE -eq 0) {
        & cmake --build $BuildDir *> "$outDir/restore-build.log"
        if ($LASTEXITCODE -ne 0) { Write-Warning 'CP20 rebuild failed; inspect restore-build.log' }
    } else { Write-Warning 'CP20 restore failed; inspect restore-configure.log' }
    Pop-Location
}
