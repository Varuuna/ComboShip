# Build the OOT game DLL: soh.dll  (also builds shared deps OTRExporter/ZAPD)
# Usage:  .\build-soh.ps1 [--Debug | --Release]   (defaults to Debug)
# Build BEFORE 2ship -- both share OTRExporter/ZAPD; never build the two games in parallel.

$target = 'soh'

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
