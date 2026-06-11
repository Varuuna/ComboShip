# ComboShip Progress — updated 2026-06-11 (evening)

Quick-resume notes. Newest work first. Branch state at last update:
**`fix/cross-item-visuals`** (active, ~30 commits ahead of `develop`), `develop` is 79 commits
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
5. **Generalized OOT←MM rendering** (Task 9, `9dbc375e9`, boot-smoked, in-game NOT yet
   verified beyond the spike item): every foreign OOT check renders the real MM item model.
   EnGirlA announces its check → `Randomizer_DrawComboForeign` → foreign map →
   `MM_GetItemDrawInfo` (C ABI, `combo/menu/ComboItemDrawABI.h`) → routed `@mm:` paths,
   OPA/XLU layers mirrored. Non-portable MM draw funcs → sentinel (Blue Rupee).
6. **Increment 3b — ANIMATED foreign items** (user-directed; spec/plan amended; Tasks 12-13
   landed `adf8feabf` + `2b99749` + review fixes `17972f874`; **GATE PENDING — user has not
   yet seen it in-game**): MM stray fairies render fully animated inside OOT.
   - Investigation verdict: games' skeleton/animation structs BYTE-IDENTICAL → host game's
     SkelAnime runs foreign skeletons as data. MM-hosted draw is dead (GraphicsContext layouts
     diverged 0x10 bytes past 0x1A0).
   - New interpreter bracket commands `G_COMBO_RM_PUSH("mm")`/`POP` (opcodes 0x2A/0x2B) scope
     raw-pointer DL spans to a game's RM. REVIEW CAUGHT A CRITICAL: the sentinel-depth unwind
     condition was a tautology (plan text had it inverted) — brackets were eaten by the first
     ENDDL; fixed with explicit sentinel exclusion.
   - `combo/menu/ComboForeignAnim.h` (TU-glue, host-compiled): loads MM skel/anim/texanim via
     CrossRMRegistry (ResourceManagerScope RAII around loads — review fix), drives the HOST's
     SkelAnime, ports MM's ColorChangeLagrange texanim handler (the only type the fairies use,
     verified by hex-parsing the resources), rewrites limb DLs to `@mm:` routed strings,
     restores segments 8/9 after. `MM_GetItemAnimDrawInfo` export describes the 5 fairy areas.

### Remaining on this branch

- **GATE (next step)**: user verifies in-game — a foreign OOT check/shop slot holding a
  stray fairy shows MM's animated fairy (billboarded, wing animation, per-area colors);
  OOT visuals intact afterward; no crash. Also general Task 9 verification: various MM items
  in shops show real models or sentinel, never blank/crash.
- **Task 9 reviews** were superseded-in-part by the Inc3b review (which covered draw.cpp);
  a focused review of the Task 9 commit itself is still outstanding.
- **Task 10**: reverse direction — MM renders real OOT item models (`SOH_GetItemDrawInfo` +
  `RI_COMBO_FOREIGN` case in `mm/2s2h/Rando/DrawItem.cpp` with `@oot:` paths; the animated
  machinery from Inc3b is symmetric if needed). Plan has the steps.
- **Task 11/14**: `docs/UPSTREAM_MERGES.md` entries (interpreter routing, bracket commands,
  ArchiveManager hash-resolution fix, z_draw accessors, AnimatedMat port note); sweep leftover
  `SPIKE` markers; final full build + tests.
- Cleanup: TEMPORARY actor-init crash tracer still in `mm/src/code/z_actor.c` (uncommitted;
  crash never reproduced). `soh/src/boot/build.c` build stamp. `UI_COMPARISON.png` untracked.

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
