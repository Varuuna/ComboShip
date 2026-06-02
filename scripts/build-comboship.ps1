# Build the launcher exe: ComboShip.exe  (POST_BUILD copies DLLs, port .o2r, and assets next to it)
# Usage:  .\build-comboship.ps1 [--Debug | --Release]   (defaults to Debug)
# Depends on soh; building this also builds soh if stale. Runnable output: build\x64\combo\<config>\.

$target = 'ComboShip'

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

Write-Host "==> cmake --build `"$buildDir`" --target $target --config $config" -ForegroundColor Cyan
cmake --build $buildDir --target $target --config $config
exit $LASTEXITCODE
