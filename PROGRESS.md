# ComboShip Progress — updated 2026-06-11

Quick-resume notes. Newest work first. Branch state at last update:
**`fix/cross-item-visuals`** (active, 24 commits ahead of `develop`), `develop` is 79 commits
ahead of `origin/develop` (nothing pushed).

## Active branch: `fix/cross-item-visuals` — cross-game item visuals

Spec: `docs/superpowers/specs/2026-06-11-cross-item-visuals-design.md`
Plan: `docs/superpowers/plans/2026-06-11-cross-item-visuals.md` (tasks checked off as landed)

### Done (all build-verified; in-game-verified where noted)

1. **Human display names end to end** (Tasks 1–2): both dumps emit `displayName`
   (MM's unused `Items[].name` field was the unlock), combo stamps it into
   `saves/combo/slot{N}.foreign.json` at generate time. Toasts pick it up automatically.
   ⚠ Needs a REGENERATE + NEW FILE to take effect on a save.
2. **Real names in OOT shops + trackers** (Tasks 3–4): `BuildMerchantMessage` interception
   (browse + buy dialog), check tracker (seen AND collected states). `hook_handlers.h` was
   re-added as a combo-owned header (upstream deleted it — see UPSTREAM_MERGES.md; do not let
   merges remove it).
3. **Real names in MM** (Task 5): central `GetItemName` chokepoint covers shops/hints/bank
   sign/smithy; MM check tracker too. Known tradeoff: articles ("the/a") dropped for foreign
   names (fix = carry article through dumps; parked).
4. **Cross-game MODEL rendering — gate PASSED in-game** (Tasks 6–8 + fixes):
   - `Ship::CrossRMRegistry` (libultraship): named per-game ResourceManagers.
   - Fast3D interpreter routing: `"__OTR__@mm:..."` paths resolve against MM's RM with a
     scoped override stack so everything INSIDE a routed DL (paths + hashes) resolves there too.
   - Two structural fixes the spike crashes exposed: soh's GBI wrapper resolves DL paths at
     submission time (marker paths now emitted as raw G_DL_OTR_FILEPATH commands), and a LATENT
     UPSTREAM BUG: every archive's `LoadFile(hash)` resolved hash→name via the globally-active
     RM (wrong manager under routing) — fixed in `ArchiveManager::LoadFile(hash)`.
   - User-verified: MM's Kafei's Mask renders on foreign slots in KF Shop, no crashes.
5. **Generalized OOT←MM rendering** (Task 9, committed `9dbc375e9`, boot-smoked, NOT yet
   in-game-verified): every foreign OOT check renders the real MM item model. Mechanism:
   EnGirlA announces its check → `Randomizer_DrawComboForeign` → foreign map →
   `MM_GetItemDrawInfo` (C ABI, `combo/menu/ComboItemDrawABI.h`) → routed `@mm:` paths,
   OPA/XLU layers mirrored. Non-portable MM draw funcs (animated materials, texture scrolls,
   object-bank segments: RecoveryHeart/Fish/Potion/Poes/Remains...) → Blue Rupee sentinel.

### Remaining on this branch

- **Task 9 reviews** (spec + quality subagent reviews not yet run on `9dbc375e9`) and
  **in-game verification**: shop with various MM items (rupees, hearts, masks, wallet) —
  expect real models or sentinel, never blank/crash.
- **Task 10**: the reverse direction — MM renders real OOT item models (`SOH_GetItemDrawInfo`
  from soh's `sDrawItemTable` + `RI_COMBO_FOREIGN` case in `mm/2s2h/Rando/DrawItem.cpp` with
  `@oot:` paths). Mirrors Task 9; the plan has the steps.
- **Task 11**: `docs/UPSTREAM_MERGES.md` entries for the interpreter routing + ArchiveManager
  fix + z_draw accessors; sweep leftover `SPIKE` markers; final full build + tests.
- Cleanup: TEMPORARY actor-init crash tracer still in `mm/src/code/z_actor.c` (uncommitted,
  from the one-off Termina Field crash investigation — remove once confident; crash never
  reproduced). `soh/src/boot/build.c` is just the build stamp. `UI_COMPARISON.png` untracked.

## Earlier today (merged to develop)

- **`fix-cross-rando-placements` (merged)**: cross-game item placement actually works now.
  Root causes fixed: `SOH_PrepareForTransition` was NEVER exported (declspec-before-extern-C
  silently ignored under /w → eager MM boot never ran → empty MM pool → `mmCount=0` forever);
  combo save-load now fires `OnSaveLoad` (arms MM's whole rando runtime — grass/pot behaviors,
  trackers); MM foreign-map lookup key mismatch (display name vs RC_* id) dropped ALL
  MM-side foreign items. All user-verified in-game (mm=876/1727 checks, items cross).
- **`feat/combo-unified-menu` (merged)**: combo-owned menu code extraction — serializer
  template, shared DrawContent renderer, shared ImGui-context helper (vendored menu diff
  ~1,630 → ~907 lines). Plus the whole unified-menu CwMenu C-ABI architecture (earlier).

## Known open items (not on any branch)

- Rando logic-respect holes (assessed, unfixed; priority order): MM oracle evaluates logic
  with ZEROED save options; combined fill's assumed set includes already-placed items (not
  valid assumed fill); silent place-anywhere fallback; portal gate disabled (`portalCheckName=""`).
- Within-category count/sub-rule settings bypassed by the wholesale fill (shopsanity counts/
  prices, scrubs, cows, tokens).
- OOT check tracker has never worked (pre-existing, predates everything above).
- MM cutscenes: possibly just 2ship cutscene-skip enhancement CVars not set in combo's config.
- User saw ONE possible OOT-side foreign miss (no logs; later retests all worked) — watch for
  a drop-type OOT check granting outside the RC queue.

## How to verify things quickly

- Generate writes `x64\Debug\saves\combo\slot0.spoiler.json` (+ `slot0.foreign.json`,
  `debug-mmdump.json` canary — `regions:0` there means MM's eager boot broke again).
- `[ComboShip] Eager MM boot: begin/complete` must appear at startup (stderr/console).
- MM-side diagnostics: file writes only (2ship's spdlog goes nowhere in combo).
- Tests: `build\x64\libultraship\tests\Debug\lus_tests.exe` (436/437; the 1 failure is
  pre-existing `ResourceLoader.RegisterDuplicateKeyRejected`).
- Builds: `scripts\build-{soh,2ship,libultraship,comboui,comboship}.ps1` (individually).
