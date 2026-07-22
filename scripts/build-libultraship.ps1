# Build the shared engine: libultraship.dll
# Usage:  .\build-libultraship.ps1 [--Debug | --Release]   (defaults to Debug)
# Build order when several targets changed: libultraship -> soh -> 2ship -> ComboShip.

$target = 'libultraship'

# --- pick config from args: accepts --Debug / -Debug / Debug (any dash, case-insensitive) ---
$config = 'Debug'
foreach ($a in $args) {
    switch -Regex ($a.TrimStart('-')) {
        '^(?i)debug$'   { $config = 'Debug' }
        '^(?i)release$' { $config = 'Release' }
        default {
            Write-Host "Unknown argument '$a'. Use --Debug or --Release." -ForegroundColor Yellow
            exit 2
        }
    }
}

$buildDir = Join-Path $PSScriptRoot '..\build\x64'
if (-not (Test-Path $buildDir)) {
    Write-Host "Build dir not found: $buildDir" -ForegroundColor Red
    exit 1
}
$buildDir = (Resolve-Path $buildDir).Path

# Cap concurrent cl.exe; full /MP (16) lets heavy TUs (UIWidgets/rando) exhaust memory (C1060/C1076).
$jobs = 8

Write-Host "==> cmake --build `"$buildDir`" --target $target --config $config (max $jobs parallel cl)" -ForegroundColor Cyan
cmake --build $buildDir --target $target --config $config -- /m:1 /p:CL_MPCount=$jobs
exit $LASTEXITCODE
