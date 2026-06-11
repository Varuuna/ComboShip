# Cross-Game Item Visuals — Design

**Date:** 2026-06-11 · **Branch:** `fix/cross-item-visuals` · **Status:** Approved (user, 2026-06-11)

## Problem

Cross-game item placement works (foreign items route through the mailbox), but the player can't
tell what anything is:

1. Toasts show raw MM spoiler names (`RI_MAGIC_JAR_SMALL`) instead of human names.
2. OOT shops list MM-bound slots as "Combo Foreign Item" (also leaks in check/item trackers).
3. Foreign items render as the sentinel model — Blue Rupee in OOT, generic custom item in MM —
   so every foreign check looks identical.

## Verified technical ground truth

- Ship-port item models are **OTR path strings**, not compiled symbols:
  `soh/assets/objects/object_gi_bottle/object_gi_bottle.h:6-10` defines `gGiBottleDL` as a
  `char[]` containing `"__OTR__objects/object_gi_bottle/gGiBottleDL"`; `sDrawItemTable`
  (`soh/src/code/z_draw.c:124+`, MM analog `mm/src/code/z_draw.c`) stores these strings.
- The shared Fast3D interpreter resolves them **during end-of-frame command processing** via
  `Ship::Context::GetInstance()->GetResourceManager()`:
  `libultraship/src/fast/interpreter.cpp:3557` (`G_DL_OTR_FILEPATH`) and `:3970`
  (`G_SETTIMG_OTR_FILEPATH`). Textures/vertices inside an extracted DL are themselves embedded
  path commands (`DisplayListFactory.cpp:536-541`) — no N64 segment setup involved.
- Both games' ResourceManagers stay resident for process life (`sMMResourceManager`,
  `BenPort.cpp:149-154`; OOT analog in `OTRGlobals.cpp`), swapped via `Context::SetResourceManager`.
- **Path namespaces collide**: `objects/object_gi_rupy/...` exists in BOTH archives with
  different contents. Routing must therefore be explicit per draw, never fallback-probing.
- Foreign-item identity at runtime comes from the combo-owned foreign map
  (`saves/combo/slot{N}.foreign.json`, loaded via `OOT_LookupForeign` /
  `LoadForeignForGame`); MM-side keys are `StaticData::Checks[].name` ("RC_*"), OOT-side keys
  are pretty `GetName()` names. MM has TWO name tables: `Checks[].name` (RC_* id) and
  `CheckNames[]` (display) — never mix (bug class already hit once).

## Increment 1 — Human display names end to end

- `MM_DumpRandoStaticData` (BenPort.cpp): emit `displayName` per item from
  `Rando::StaticData::Items[id].name` (the existing, unused human field — e.g. "Deku Mask")
  alongside `name` (= `spoilerName`, which MUST stay `RI_*`: grant lookup depends on it).
- `SOH_DumpRandoStaticData` (OTRGlobals.cpp): emit `displayName` too (OOT item names are already
  human English via `RetrieveItem(rg).GetName().GetEnglish()`; displayName = name).
- `RunComboFill` (combo/ComboShip.cpp, before `WriteForeignFromAnnotations` at ~:239): build a
  per-game `name → displayName` map from the two dumps' `items` arrays and stamp `displayName`
  into every foreign marker. `WriteForeignFromAnnotations`'s existing `fm.value("displayName",
  itemName)` fallback then persists real names.
- Toast sites need no changes (they already prefer `displayName`):
  `hook_handlers.cpp:2791`, `CheckQueue.cpp:44`, plus both send toasts.

**Success:** both games' send/receive toasts show "Deku Mask" / "Hookshot"-class names.

## Increment 2 — Real names in OOT shops and trackers

- Primary: `BuildMerchantMessage()` (`soh/.../Messages/MerchantMessages.cpp:40-42`): when
  `rgid == RG_COMBO_FOREIGN`, substitute the item name with the foreign map's `displayName`
  for the check (lookup via `OOT_LookupForeign(slot, GetLocation(rc)->GetName())`). Covers shop
  browse text AND buy-confirm dialog (same template).
- Secondary surfaces, same substitution: check tracker
  (`randomizer_check_tracker.cpp:2073-2074`) and item-tracker tooltips/trap display
  (`randomizer_item_tracker.cpp:815/1289/1311/1365`, `Traps.cpp:1771` — via a small wrapper
  around `SohUtils::GetItemName` or call-site checks).
- MM side: verify whether MM surfaces "RI_COMBO_FOREIGN" text anywhere player-visible (shops
  are Tingle/normal shops; the MM shop item naming path) and apply the same substitution if so.

**Success:** browsing an OOT shop slot holding an MM item shows the MM item's real name.

## Increment 3 — Cross-game model rendering (spike, then generalize)

**Mechanism (the core):**

1. **Namespaced resource paths**: a foreign draw submits DL paths with a game prefix, e.g.
   `"__OTR__@mm/objects/object_gi_..."`. Plain paths behave exactly as today.
2. **Interpreter routing + resolution stack** (shared libultraship, we own it): the
   `G_DL_OTR_FILEPATH` handler recognizes the `@mm/` / `@oot/` prefix, resolves via that game's
   RM (both registered with libultraship at combo boot via a small registry, e.g.
   `Context`-adjacent `Combo_RegisterGameRM(name, shared_ptr<ResourceManager>)`), and pushes
   that RM onto a per-interpreter **resolution stack** for the duration of that DL so the
   texture/vertex paths INSIDE it resolve against the same game's archives; pops on DL return.
   `G_SETTIMG_OTR_FILEPATH` (and vertex equivalents) consult the stack top before falling back
   to the active RM.
3. **Draw-info exports** (C-ABI, per game): `MM_GetItemDrawInfo(const char* spoilerName)` /
   `SOH_GetItemDrawInfo(...)` → POD { dlist path strings (≤8), count, layer layout
   (opa/xlu order) } read from that game's `sDrawItemTable`. The consuming game prefixes the
   returned paths and feeds them to a **generic foreign draw func** (standard matrix/material
   setup + `gSPDisplayList` per layer — matches the majority of `GetItem_DrawOpa*` patterns).
   Items with exotic per-frame draw code render with basic setup; acceptable.
4. **Fallback ladder**: missing foreign-map entry, missing draw info, or any resolution failure
   → current sentinel (Blue Rupee / generic). Never crash, never wrong-game asset.

**Spike first (timeboxed):** hardcode ONE MM item (e.g. Deku Mask) drawn at one OOT site via the
prefix+stack mechanism. Only after the spike renders correctly do we generalize both directions.
If the spike fails on something fundamental, fall back to the lookalike mapping table
(documented in the exploration; ~150 archetype pairs) — Increments 1–2 are unaffected.

**Risks / costs:**
- Interpreter edits live in shared libultraship → upstream-merge surface; keep the change small,
  comment with `// ComboShip:` and document in `docs/UPSTREAM_MERGES.md` (project rule).
- Texture-cache keying: verify Fast3D's texture cache keys can't collide across same-named
  resources from different RMs (the two games' `object_gi_rupy` textures must not alias).
  Part of the spike's checklist.
- Memory: cross-game resources cached in their owning RM (lazy, shared_ptr) — no preload.

**Success:** an OOT chest/shop/drop holding an MM item shows the MM item's actual model, and
vice versa; sentinel only for unmapped/exotic cases.

## Out of scope

Get-item cutscene text boxes beyond the shop/tracker surfaces, item icons in ImGui trackers,
spoiler-log formatting, and the rando-logic correctness work tracked separately
(oracle options, assumed-fill soundness, portal gate).

## Increment 3b — Animated foreign items (approved 2026-06-11, after Task 9)

User rejected lookalikes for MM's animated items (stray fairies); investigation confirmed real
cross-game animated rendering is feasible. Decisive facts: the games' skeleton/animation structs
are byte-identical (SkelAnime 0x44, StandardLimb/LodLimb, FlexSkeletonHeader, AnimationHeader,
identical SkelAnime_InitFlex/Update/DrawFlex implementations) — the HOST game's engine can run the
FOREIGN game's skeleton as pure data. MM-hosted drawing is dead (GraphicsContext layouts diverged
0x10 bytes past offset 0x1A0 — MM code writing OOT's frame corrupts it); OOT-hosted runs no MM code.

**Architecture (user directive: minimal port seam, running code combo-owned):**
1. **Interpreter bracket commands** `G_COMBO_RM_PUSH("<game>")` / `G_COMBO_RM_POP` (free opcodes in
   0x2A-0x30): raw-pointer DL spans (skeletal limb DLs) get scoped cross-RM resolution at command-
   processing time — the existing exec-stack routing can't cover raw submissions. Bracket overrides
   carry a sentinel depth so ENDDL pops never remove them; only POP does; frame-start clear applies.
2. **ABI extension** (combo/menu/ComboItemDrawABI.h): an animated variant — skeleton path, anim
   path, texanim path, scale, billboard flag, XLU layer — so the owning game DESCRIBES the item and
   the host's combo-owned code draws it. MM fills it for the stray-fairy class first.
3. **Combo-owned TU-glue header** `combo/render/ComboForeignAnim.h` (included by the host game's
   draw TU, menu-extraction pattern, zero CMake churn): loads foreign skeleton/anim/texanim via
   `CrossRMRegistry::Get(game)` (load through the OWNING RM = its factories resolve limb DL pointers
   correctly), drives the HOST's SkelAnime engine, ports the needed AnimatedMat handler subset
   (~330 lines of engine-agnostic math+gfx; OOT has no AnimatedMat system), emits brackets around
   the draw, restores segments 7-16 after.
4. **soh seam** (~10 lines): animated branch in Randomizer_DrawComboForeign.

Known risks to verify at the gate: interpreter-time inner hash refs inside limb DLs resolve under
the bracket (the whole point); segment 7-16 restoration vs OOT's per-scene segment expectations;
animation timing constants (R_UPDATE_RATE vs MM divisors) — cosmetic if off.
