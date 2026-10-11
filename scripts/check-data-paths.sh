#!/bin/sh
# ComboShip: assert that every writable-data path is ABSOLUTE, never CWD-relative.
#
# Why this exists: the save container path was `std::filesystem::path("Save") / "fileN.combosav"`.
# A .app launched from Finder starts at CWD "/" — the read-only Signed System Volume on macOS — so
# every save write failed, and failed SILENTLY, while the game logged "Save File Finish". A full
# playthrough was lost before anyone noticed. Windows has the same exposure through a shortcut's
# "Start in"; the Linux AppImage only escapes it because AppRun cd's to the data dir first.
#
# Run from the repo root:  ./scripts/check-data-paths.sh
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/check.cpp" <<'EOF'
#include "rando/CrossForeign.h"
#include <iostream>
static int fails = 0;
static void expect_absolute(const char* what, const std::filesystem::path& p) {
    std::cout << "  " << what << " = " << p << "\n";
    if (p.is_relative()) { std::cerr << "  FAIL: " << what << " is RELATIVE\n"; ++fails; }
}
int main() {
    expect_absolute("DataDir", ComboRando::DataDir());
    expect_absolute("ContainerPath(0)", ComboRando::ContainerPath(0));
    expect_absolute("SaveDir", ComboRando::SaveDir());
    expect_absolute("ConsolidatedDir", ComboRando::ConsolidatedDir());
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
echo "C: no SHIP_HOME, CWD=/ (the case that silently ate saves)"
( cd / && env -u SHIP_HOME "$tmp/check" ) || status=1

[ "$status" -eq 0 ] && echo "OK: all data paths absolute" || echo "FAILED"
exit "$status"
