# scripts/build-comboui.ps1 — build only the comboui target
param([switch]$Release)
$config = if ($Release) { "Release" } else { "Debug" }
# Cap concurrent cl.exe; full /MP (16) lets heavy TUs exhaust memory (C1060/C1076).
cmake --build build/x64 --target comboui --config $config -- /m:1 /p:CL_MPCount=8
