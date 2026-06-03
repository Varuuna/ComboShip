# Regenerate the PORT/custom asset archives: soh.o2r (soh/) and 2ship.o2r (mm/).
# These pack <game>/assets/custom into an .o2r via the ZAPD tool (extract_assets.py --norom).
# They are NOT the ROM-extracted archives (oot.o2r / mm.o2r) -- those come from the user's ROMs.
#
# Usage:  .\generate-o2r.ps1 [--Debug | --Release] [soh | 2ship | both]
#   config  : defaults to Debug
#   game    : defaults to both
#
# Builds the ZAPD executable first (the targets DEPEND on it). Requires Python 3 on PATH
# (OTRExporter/extract_assets.py). Regenerate after assets/custom or libultraship shaders change.
# NOTE: overwrites the tracked soh/soh.o2r and mm/2ship.o2r, and refreshes soh/assets/custom/shaders/.

$config = 'Debug'
$game   = 'both'

foreach ($a in $args) {
    switch -Regex ($a.TrimStart('-')) {
        '^(?i)debug$'   { $config = 'Debug' }
        '^(?i)release$' { $config = 'Release' }
        '^(?i)soh$'     { $game = 'soh' }
        '^(?i)(2ship|mm)$' { $game = '2ship' }
        '^(?i)both$'    { $game = 'both' }
        default {
            Write-Host "Unknown argument '$a'. Use [--Debug|--Release] [soh|2ship|both]." -ForegroundColor Yellow
            exit 2
        }
    }
}

# Python 3 is required by OTRExporter/extract_assets.py.
if (-not (Get-Command python -ErrorAction SilentlyContinue) -and
    -not (Get-Command python3 -ErrorAction SilentlyContinue) -and
    -not (Get-Command py -ErrorAction SilentlyContinue)) {
    Write-Host "Python 3 not found on PATH -- extract_assets.py needs it." -ForegroundColor Red
    exit 1
}

$buildDir = Join-Path $PSScriptRoot '..\build\x64'
if (-not (Test-Path $buildDir)) {
    Write-Host "Build dir not found: $buildDir" -ForegroundColor Red
    exit 1
}
$buildDir = (Resolve-Path $buildDir).Path

$targets = @()
if ($game -eq 'soh'  -or $game -eq 'both') { $targets += 'GenerateSohOtr' }
if ($game -eq '2ship' -or $game -eq 'both') { $targets += 'Generate2ShipOtr' }

foreach ($t in $targets) {
    Write-Host "==> cmake --build `"$buildDir`" --target $t --config $config" -ForegroundColor Cyan
    cmake --build $buildDir --target $t --config $config
    if ($LASTEXITCODE -ne 0) {
        Write-Host "$t failed (exit $LASTEXITCODE)." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Write-Host "Done. Regenerated: $($targets -join ', ')" -ForegroundColor Green
Write-Host "  soh.o2r   -> $(Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\soh')).Path 'soh.o2r')" -ForegroundColor DarkGray
Write-Host "  2ship.o2r -> $(Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\mm')).Path '2ship.o2r')" -ForegroundColor DarkGray
exit 0
