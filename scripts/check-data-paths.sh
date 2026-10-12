#!/bin/sh
# ComboShip: assert that ComboShip's writable data (save containers, seeds) follows the same base
# directory as the games' own saves — libultraship's Context::GetAppDirectoryPath: SHIP_HOME on
# macOS/Linux, else the working directory.
#
# Why this exists: with no SHIP_HOME, a .app launched from Finder starts at CWD "/" — the read-only
# Signed System Volume on macOS — so every save write failed, SILENTLY, while the game logged "Save
# File Finish". A full playthrough was lost before anyone noticed. The .app's Info.plist therefore
# always sets SHIP_HOME; cases A/B check the paths land under it. Case C checks that without it they
# stay beside the games' saves (CWD), as libultraship's do, rather than splitting off elsewhere.
#
# Run from the repo root:  ./scripts/check-data-paths.sh
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/check.cpp" <<'EOF'
#include "rando/CrossForeign.h"
#include <iostream>
#include <cstdlib>
#include <string>
static int fails = 0;
// Expected base: libultraship's rule (Context::GetAppDirectoryPath) — SHIP_HOME, tilde-expanded, else ".".
static std::filesystem::path expected_base() {
    const char* h = std::getenv("SHIP_HOME");
    if (h == nullptr || *h == '\0')
        return ".";
    std::string p(h);
    if (p[0] == '~')
        p = std::string(std::getenv("HOME")) + p.substr(1);
    return p;
}
static void expect(const char* what, const std::filesystem::path& got, const std::filesystem::path& want) {
    std::cout << "  " << what << " = " << got << "\n";
    if (got != want) { std::cerr << "  FAIL: expected " << want << "\n"; ++fails; }
}
int main() {
    const auto base = expected_base();
    expect("DataDir", ComboRando::DataDir(), base);
    expect("SaveDir", ComboRando::SaveDir(), base / "Save");
    expect("ContainerPath(0)", ComboRando::ContainerPath(0), base / "Save" / "file1.combosav");
    expect("ConsolidatedDir", ComboRando::ConsolidatedDir(), base / "Randomizer");
    return fails ? 1 : 0;
}
EOF

inc=""
for d in /opt/homebrew/include /usr/local/include /usr/include; do
    [ -d "$d" ] && inc="$inc -I$d"
done
# shellcheck disable=SC2086
c++ -std=c++20 -I "$root/combo" $inc "$tmp/check.cpp" -o "$tmp/check"

status=0
echo "A: SHIP_HOME absolute (the .app / AppRun case)"
( cd / && SHIP_HOME="$tmp" "$tmp/check" ) || status=1
echo "B: SHIP_HOME tilde-relative (exactly what combo/macosx/Info.plist stores)"
( cd / && SHIP_HOME='~/Library/Application Support/com.comboship.ComboShip' "$tmp/check" ) || status=1
echo "C: no SHIP_HOME (paths stay CWD-relative, beside the games' own saves)"
( cd / && env -u SHIP_HOME "$tmp/check" ) || status=1

[ "$status" -eq 0 ] && echo "OK: data paths match libultraship's app directory" || echo "FAILED"
exit "$status"
