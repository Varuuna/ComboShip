# Verifies combo/core/ComboDllApi.{h,cpp} stay in sync: every declared export pointer is resolved,
# every resolve has a declaration, and each pointer's name matches the symbol string it resolves from.
# That name-identity invariant is what lets you grep one name and find the export, its GetSym and every
# call site. Run locally or from CI; exits 1 on any mismatch.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$hdr = Join-Path $root 'combo/core/ComboDllApi.h'
$src = Join-Path $root 'combo/core/ComboDllApi.cpp'

foreach ($f in @($hdr, $src)) {
    if (-not (Test-Path $f)) { Write-Host "missing: $f" -ForegroundColor Red; exit 1 }
}

# Declarations: `extern <Type> <Name>;`
$declared = [regex]::Matches((Get-Content $hdr -Raw), '(?m)^\s*extern\s+\w+\s+(\w+)\s*;') |
    ForEach-Object { $_.Groups[1].Value }

# Resolves: `<Name> = (<Type>)GetSym(<module>, "<Symbol>");`  (may wrap across lines).
# ComboShip.cpp too: the two comboui extraction/import pointers are resolved lazily in main().
$resolveText = (Get-Content $src -Raw) + (Get-Content (Join-Path $root 'combo/ComboShip.cpp') -Raw)
$resolves = [regex]::Matches($resolveText, '(\w+)\s*=\s*\(\w+\)GetSym\(\s*\w+\s*,\s*"([^"]+)"\s*\)')

$fail = 0

foreach ($m in $resolves) {
    $name = $m.Groups[1].Value
    $sym = $m.Groups[2].Value
    if ($name -cne $sym) {
        Write-Host "name/symbol mismatch: $name resolves `"$sym`"" -ForegroundColor Red
        $fail = 1
    }
}

$resolvedNames = $resolves | ForEach-Object { $_.Groups[1].Value }
$missingResolve = $declared | Where-Object { $resolvedNames -notcontains $_ }
$missingDecl = $resolvedNames | Where-Object { $declared -notcontains $_ }

foreach ($n in $missingResolve) { Write-Host "declared but never resolved: $n" -ForegroundColor Red; $fail = 1 }
foreach ($n in $missingDecl) { Write-Host "resolved but not declared: $n" -ForegroundColor Red; $fail = 1 }

if ($fail) { exit 1 }
Write-Host "export bindings OK ($($declared.Count) declared, $($resolvedNames.Count) resolved)" -ForegroundColor Green
