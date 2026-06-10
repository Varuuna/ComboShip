# scripts/build-comboui.ps1 — build only the comboui target
param([switch]$Release)
$config = if ($Release) { "Release" } else { "Debug" }
cmake --build build/x64 --target comboui --config $config
