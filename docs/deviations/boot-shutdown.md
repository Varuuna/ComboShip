# ComboShip deviations — Boot, transition & shutdown

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

## Cross-World Randomizer — Eager MM boot (replaces headless warm-up) (2026-06-05)

The MM rando oracle needs MM's region graph at OOT-generate time, before MM would normally boot.
The Inc3 approach (`MM_InitRandoLogic` → `ShipInit::InitAll()` at startup) faked a headless MM and
crashed: `InitAll()` runs MM's entire UI/cosmetic/audio init surface, which dereferences a null
`GameInteractor::Instance` and then `ResourceManager`-loads MM assets through OOT's RM. Replaced by
**eagerly booting MM for real at startup** — one OOT→MM→OOT transition with MM's game loop skipped,
reusing the existing transition machinery. (Runtime-verified: boots to file-select, generation runs,
round-trip + Inc6 delivery all work.)

**Game-source deviation (additive, COMBO_BUILD-guarded — preserve on future mm merges):**
- `mm/src/code/main.c` (`MM_RunMain` tail): the final `Graph_ThreadEntry(0)` is gated on
  `gComboBootOnly` so `MM_BootForCombo` can run MM's full init without entering the blocking loop.
  `extern int gComboBootOnly;` declared near the `InitOTR` forward-decl.

**MM port code (`mm/2s2h/BenPort.cpp`):**
- `extern "C" int gComboBootOnly` definition; `MM_BootForCombo()` export (sets `sComboTransitionActive`
  + `gComboBootOnly`, runs `MM_RunMain`, clears the flag).
- **Deleted** `MM_InitRandoLogic()` (and the throwaway-singleton workaround from `f54b3cece`), plus its
  lazy-init caller at the top of `Combo_MM_Rando_Reset` (the region graph is now built by eager boot).

**OOT port code (`soh/soh/OTRGlobals.cpp`):**
- `SOH_ResumeForeground()` export = `SOH_ReinitForResume()` + `ImGui::SetCurrentContext`, no game loop
  (reactivates OOT as foreground after the eager MM boot).
- `EnsureOracleInit()` (the OOT oracle init) no longer calls `GenerateItemPool()`. That builds OOT's
  item pool purely for OOT's OWN fill (which the combo layer never runs — the combined cross-world fill
  owns placement) and asserts `itemPool.size() <= locCount`; under headless default settings the pools
  aren't balanced for a real fill, so it aborted on file creation. The oracle only needs reachability
  (`ReachabilitySearch` reads logic/region state + `allLocations` from `GenerateLocationPool`;
  `GenerateStartingInventory` doesn't touch `itemPool`).

**combo (`combo/ComboShip.cpp`):** the warm-up block is replaced by the eager-boot sequence
(`SOH_PrepareForTransition` → `MM_BootForCombo` → `MM_PrepareForTransition` → `SOH_ResumeForeground`);
the main loop starts with `mmBooted = true` so the first portal transition is a `MM_ResumeGame`. The
stale `MM_InitRandoLogic` resolution was removed.

**GUI lifecycle fix (`soh/soh/OTRGlobals.cpp`, found via runtime testing 2026-06-05):** the OOT
transition path tore down + rebuilt the shared Gui every OOT↔MM transition
(`SOH_PrepareForTransition` → `SohGui::Destroy()`, `SOH_ReinitForResume` →
`SohGui::SetupGuiElements()`). MM deliberately does NOT (see `MM_PrepareForTransition`): the shared
Gui persists, each game's windows are set up once. On the OOT rebuild the Gui still held the old
windows, so `AddGuiWindow` rejected the duplicates → the new windows never got `InitElement`'d → their
`calloc`-backed buffers stayed `0xCD` → freeing them on the next rebuild crashed
(`MessageViewer::~MessageViewer`, access violation on the 2nd MM→OOT return). Fixed by making OOT match
MM: removed the `Destroy()`/`SetupGuiElements()` calls from the transition path — OOT's windows persist
(fully initialized) and only the active RM/audio/menu are swapped. **Runtime-verified: multiple
OOT↔MM round-trips now work.**

**Open follow-ups (not blocking, tracked):**
- **Combined-fill performance:** the fill is O(checks²)-ish (~5000 checks, per-item double-oracle
  reachability + JSON round-trips) and takes minutes synchronously on the main thread (window appears
  frozen). Needs incremental reachability / binary interchange + a "Generating…" frame. See the
  combined-logic spec's perf note.
- **Foreign `displayName` polish** (see Inc6 section above).
- **Combo settings window (Increment 7):** without it, both games' rando options sit at defaults, so
  most non-chest checks aren't shuffled at runtime.

## Eager-MM-boot export bug: SOH_PrepareForTransition was never exported (2026-06-11)

**`soh/soh/OTRGlobals.cpp` (`SOH_PrepareForTransition`):** the founding-commit declaration put
`__declspec(dllexport)` on its own line BEFORE `extern "C"` — MSVC silently ignores the declspec
in that arrangement (warning C4091, invisible because soh compiles with `/w`). The function was
never in soh.dll's export table, so ComboShip.exe's eager-MM-boot gate (which requires all four
transition exports) failed on EVERY launch since 2026-06-05, printing one stderr line nobody saw.
Consequences while hidden: `ShipInit::InitAll` never ran at startup, `Rando::Logic::Regions`
stayed empty (0 of 315), the settings-scoped `MM_DumpRandoStaticData` emitted 0 checks, and the
cross-world fill never placed a single cross-game item (`mmCount=0`, `foreign=[]`) — masked by
graceful fallbacks everywhere else (boot-on-first-portal-transition, place-anywhere fill).
Fixed by using the canonical `extern "C" __declspec(dllexport)` form. Verified: eager boot
completes, 315 regions, MM dump 876 checks, spoiler mmCount=876 with populated foreign array.

Note when adding MM-side logs: 2ship.dll's spdlog default logger is never configured in combo (the
shared Context owns logging in soh's module), so SPDLOG_* calls from 2ship.dll go nowhere. (A former
debug-build canary here wrote pool sizes to `saves/combo/debug-mmdump.json` for exactly this reason;
it and the startup `oot_dump.json`/`mm_dump.json` dumps were removed 2026-07-21 as no longer needed.)

## Sturdy shutdown: clean deinit of both games (2026-06-11)

Closing the game on the window's X sometimes crashed or froze, and window resize/position changes
were never saved. Three intertwined causes, all from MM staying resident (eager MM boot) while the
shutdown path only deinitted SOH:

**`soh/soh/OTRGlobals.cpp` + `mm/2s2h/BenPort.cpp` (`OTRAudio_Exit`):** the unconditional
`audio.thread.join()` terminates (`std::system_error`) when the thread was already joined — which
is exactly the case at combo shutdown for whichever game was BACKGROUND (its `*_PrepareForTransition`
already ran `OTRAudio_Exit`). Guarded with `audio.thread.joinable()` in both games. This was the
"crash on X" (deterministic when closing from MM; soh's `DeinitOTR` re-ran `OTRAudio_Exit` on the
already-joined OOT audio thread).

**`mm/2s2h/BenPort.cpp` (`MM_Deinit`, new export):** 2ship holds a `shared_ptr` to the SHARED
Context (forward-transition reuse path in the OTRGlobals ctor) and nothing ever released it, so
`~Ship::Context` — the ONLY place window geometry is saved (`SaveWindowToConfig` + `Config::Save`)
and spdlog is shut down — never ran. `MM_Deinit` wraps MM's `DeinitOTR`. ComboShip.exe calls it
BEFORE `SOH_Deinit` (BenGui::Destroy dereferences the live Context), so soh's `DeinitOTR` releases
the LAST reference and `~Context` runs on the main thread. This is what fixed window-resize
persistence.

**`soh/soh/OTRGlobals.cpp` + `mm/2s2h/BenPort.cpp` (`DeinitOTR`), `libultraship`
(`CrossRMRegistry::Unregister`, new):** both resident ResourceManagers were pinned by the
`sOOT/sMMResourceManager` statics and the `CrossRMRegistry` map, deferring their destruction to
DLL-unload static destructors — where `~ResourceManager`'s thread pool joins its worker threads
UNDER THE LOADER LOCK and deadlocks. This was the "freeze on X". Each game's `DeinitOTR` now
unregisters its RM and nulls its static (`#ifdef COMBO_BUILD`), so RM destruction happens during
the explicit deinit calls on the main thread, before any `FreeLibrary`.

Shutdown order (ComboShip.cpp cleanup): `MM_Deinit()` (if MM ever booted) → `SOH_Deinit()` →
`FreeDll(comboui/2ship/soh)`. Everything thread-owning must be dead before the first FreeDll.

**`mm/2s2h/DeveloperTools/MessageViewer.h` (follow-up — freeze moved here after the fixes above):**
with MM_Deinit in place, BenGui::Destroy now actually destroys MM's window objects, and
`~MessageViewerWindow` froze the debug heap: it does `free(mTextIdBuf)` / `free(mCustomMessageBuf)`,
but those are only allocated in `InitElement()` — which NEVER ran in combo, because the shared Gui
rejected MM's window as a duplicate name (OOT registers its own "Message Viewer" first;
`Gui::AddGuiWindow` rejects + skips `Init()`). The members were raw uninitialized `char*` (0xCD
debug fill) and freeing them hung `_free_dbg`. Fixed by null-initializing both members
(`free(nullptr)` is a no-op). Audited all other MM GuiWindow destructors — MessageViewerWindow is
the only one freeing InitElement-allocated raw pointers. The rejected-duplicate window class
(known from the resume path) is worth remembering for any new MM window whose destructor frees
state allocated in `InitElement()`.

**`libultraship` `Gui::ImGuiWMShutdown`/`ImGuiBackendShutdown` (signature change — next crash in the
chain):** with teardown actually reaching `~Context`, `Fast3dGui::ImGui{WM,Backend}Shutdown` AV'd:
they called `Ship::Context::GetInstance()->GetWindow()`, but they run from `~Window` INSIDE
`~Context`, where the Context weak_ptr is already expired (GetInstance() == nullptr). This killed
the process BEFORE `~Context` reached `Config::Save()` — i.e. even with MM_Deinit in place, window
geometry still wasn't persisted. Fixed by threading the `Ship::Window*` (which `ShutDownImGui`
already received) through both virtuals instead of using GetInstance(). Affects Gui.h/Gui.cpp/
Fast3dGui.h/Fast3dGui.cpp; a Gui.h change recompiles nearly everything in soh+mm.

**`soh/soh/OTRGlobals.cpp` + `mm/2s2h/BenPort.cpp` (`DeinitOTR`, GImGui null-out — last crash in the
chain):** after a fully clean main(), the process still AV'd in CRT exit: soh.dll's atexit dtor for
`itemTrackerNotes` (static ImVector) called `ImGui::MemFree`, which dereferences the MODULE-LOCAL
`GImGui` — `ImGui::DestroyContext` (in ~Context) only nulls libultraship's copy; each game DLL's
GImGui still pointed at the freed context. Fixed: both games' DeinitOTR end with
`ImGui::SetCurrentContext(nullptr)` (COMBO_BUILD-guarded). Found via the new last-chance crash
filter in ComboShip.cpp (`ComboLateCrashFilter` -> combo_late_crash.txt) which covers the
post-Context window where lus's CrashHandler is gone/unusable; the filter + cerr shutdown markers
are kept permanently.

**`libultraship` `Context::~Context` + `luslog.cpp` (spdlog registry — 4th X-close crash class,
2026-08-04):** every quit AV'd after `main()` returned (silently — only `combo_late_crash.txt`
showed it). `InitLogging` registers the shared logger in spdlog's registry (a static inside
libultraship.dll) and both game DLLs `set_pattern` it — spdlog.lib is statically linked into every
module, so the per-sink `pattern_formatter` vtables land inside soh.dll/2ship.dll. `~Context` never
unregistered the
logger, so the registry's static dtor destroyed the sinks at `DLL_PROCESS_DETACH`, after
`FreeLibrary` unmapped those vtables. Fixed: `~Context` calls `spdlog::shutdown()`
(COMBO_BUILD-guarded) so the registry (and its `init_thread_pool` worker — a latent loader-lock
join) dies while everything is mapped, then installs a lus-owned null-sink default logger so any
late `SPDLOG_*` call stays safe; `luslog()` also null-guards `default_logger_raw()`. Must live in
lus code — the registry singleton is per-module.

## Foreign-anim caches: cross-DLL shared_ptr teardown (5th X-close crash class, 2026-08-25)

Quitting after a session that DREW at least one foreign MM item model in OOT crashed post-`main()`
(only the late-crash log showed it). `combo/menu/ComboForeignAnim.h` cached foreign resources in
function-local statics (`sSkelCache`/`sTexAnimCache`/`sCache`) holding `shared_ptr<Ship::IResource>`
loaded through the FOREIGN game's RM — so in soh.dll's copy the control blocks and IResource vtables
live in 2ship.dll (its factories `make_shared` them). The launcher frees 2ship.dll before soh.dll,
so when soh's static dtors destroyed the caches, `_Ref_count_base::_Decref`'s virtual `_Destroy()`
dispatched through an unmapped vtable (AV at the `call` through the control-block vtable). Same
class as the spdlog-registry crash above — a cross-module vtable outliving `FreeLibrary` — held by
combo-owned code this time.

Fixed entirely in combo-owned code, tied to the registry so no deinit call site can forget it: the
three caches are hoisted to header scope, and the first cache touch registers a module-local clear
(`CfaClearCaches`) as a `CrossRMRegistry` teardown listener (`RegisterTeardownListener`, new —
exported automatically by the generated lus .def). Every `Unregister` — i.e. both games'
`DeinitOTR`, which run before any `FreeDll` — fires ALL listeners before dropping the RM, so each
module releases its cross-module refs while every game DLL is still mapped; the second Unregister
re-fires them onto already-empty maps. Listeners are raw fn pointers into the game DLLs, valid
because `Unregister` only ever runs during deinit; they fire outside the registry lock (a released
resource's destructor may re-enter `Get()`). No vendored-file churn.

Diagnosis note: the player's soh.dll was still MAPPED at process exit (its static dtors ran under
`LdrShutdownProcess`, not at `FreeDll`), i.e. something holds an extra loader ref on soh.dll — prime
suspect is the statically-linked SDL's `WH_KEYBOARD_LL` hook (`WIN_UpdateKeyboardHook`, the DLL's
only `SetWindowsHookExW` site), unconfirmed. Immaterial to this fix — the FreeDll order (2ship
before soh) crashes either way — but it means soh.dll static dtors can ALWAYS run at process exit:
never let them depend on another game DLL. Frames were recovered without PDBs by downloading the
matching nightly `ComboShip-windows` artifact, adding the logged displacements to the export RVAs
(`SOH_NotifyComboReturn`/`SOH_RunGameLoop`), and `objdump -d --start-address` at each RVA; the
log's unresolved frames sit BELOW the lowest export RVA (dbghelp's nearest-export synthesis fails
there), which also pins the module base (64K-aligned) and thus the crash PC's RVA.

## Cross-game erase: deleting a slot wipes both OOT and MM saves (issue #1, 2026-06-19)

**Why:** a ComboShip save *slot* (file 1/2/3) is one combined OOT+MM playthrough, but each game's
"Erase" only deleted its own save, orphaning the other. Issue #1 asks OOT's Erase to also delete MM's
save for the slot; implemented **bidirectionally** (either game's Erase wipes both). Same launcher-owned
seam shape as the cross-item delivery work: the erasing game fires a launcher-registered callback with
the 0-based slot, and the launcher calls the OTHER game's save-only delete export. The launcher does no
index math — MM's 1-based JSON naming (`file{N+1}.json`) is hidden inside `MM_DeleteSaveFile`. No loop:
the delete exports remove files directly and never re-enter a menu erase path.

**soh (`soh/soh/SaveManager.cpp`, all `#ifdef COMBO_BUILD`):**
- `SOH_DeleteSaveFile(int fileNum)` export — calls `DeleteZeldaFile` directly (NOT `Save_DeleteFile`,
  so OOT's own erase seam does not re-fire). Called by the launcher when MM erases.
- `gComboDeleteForeignSave` fn-pointer + `SOH_SetDeleteForeignSave` setter export (mirrors the
  cross-item `SOH_SetCrossDeliver` seam shape).
- `Save_DeleteFile` (the erase-only choke; sole caller is `z_file_copy_erase.c`, NOT `CopyZeldaFile`)
  fires `gComboDeleteForeignSave(fileNum)` after the local delete.

**mm (`mm/2s2h/BenPort.cpp`):**
- `MM_DeleteSaveFile(int fileNum)` export — `fileNum` is the 0-based slot; deletes both
  `SaveManager_GetFileName(fileNum + 1, false)` and the `(…, true)` backup (mirrors
  `Enhancements/DifficultyOptions/DeleteFileOnDeath.cpp`). Called by the launcher when OOT erases.
- `gMMComboDeleteForeignSave` fn-pointer + `MM_SetDeleteForeignSave` setter export.

**mm game-source (`mm/src/overlays/gamestates/ovl_file_choose/z_file_copy_erase.c`, COMBO_BUILD-guarded —
the only vendored-source touch):** `FileSelect_EraseConfirm` fires `gMMComboDeleteForeignSave(selectedFileIndex)`
right after `Sram_EraseSave` on erase confirm. Unavoidable because MM has no port-level erase choke —
`Sram_EraseSave` only zeroes the flash buffer; the JSON is deleted later via the flash-write validation
path. On future merges, keep this guarded block in the erase-confirm branch.

**combo (`combo/ComboShip.cpp`):** resolves the four new symbols, defines `DeleteForeignSaveFromOOT` /
`DeleteForeignSaveFromMM` (route 0-based slot to the other game), and registers them via
`SOH_SetDeleteForeignSave` / `MM_SetDeleteForeignSave` alongside the other startup callbacks.

## Merged per-slot save container `Save/file{N+1}.combosav` (2026-07-21)

**Why:** a slot's state was spread across three files in two folders (OOT `Save/file{N}.sav`, MM
`saves/file{N}.json`, plus the `Randomizer/save{N}-*` sidecars), so a slot wasn't a single portable
artifact. Now one JSON container per slot holds both games' saves verbatim plus combo metadata:
`{comboVersion, slot, oot:<saveBlock>, mm:<2S2H json>, combo:{completion, rando}}`. The launcher owns
the file; each game keeps its own JSON schema/versioning, just nested. No migration of old split saves
(broken cross-version saves are expected — see [save-compat rule]).

**Launcher-mediated IO (the invariant):** every per-slot read/write in both games routes through two
launcher callbacks pushed in at boot — `Combo_ReadGameSave(game, fileNum)` / `Combo_WriteGameSave(game,
fileNum, json)` (`game` 0=OOT/1=MM, `fileNum` 0-based; MM maps `mmFileNum-1`). Registered via new
`SOH_SetComboSaveIO` / `MM_SetComboSaveIO` exports. A single launcher mutex + per-slot in-memory cache
makes the container process-authoritative; write = full-container read-modify-write of only the caller's
section (so an Anchor MM-item write during OOT play can't clobber the OOT section) then temp+atomic
`rename`. An existing-but-unparseable container is renamed aside (`.corrupt-<ts>`), never silently
overwritten. `Combo_CopyContainer` / `SOH_SetCopyContainer` back OOT file-select "copy file" (whole
container, since there's no per-game `.sav` to copy).

**Release-version save gate:** `COMBO_RELEASE_VERSION` (root `CMakeLists.txt`, manual — e.g. `0.1.1`)
is the sole authority for combosave compatibility, enforced at the launcher container level, not in
either game. It is compiled into the `ComboShip` target only. Every container write stamps
`comboRelease`; on load, a container missing it or differing in `major.minor` (patch releases keep
saves) is renamed aside to a timestamped `.bak`, its slot recorded, and a fresh container created. OOT drains the recorded slots on
its main thread (`SOH_SetOutdatedSaveNotice` → `Combo_TakeEvictionNotice`) and shows an "Outdated
ComboShip Save" popup — the launcher never touches ImGui (`Combo_ReadGameSave` may run off-thread). The
old pin-derived `major.minor.patch` triple + MM git commit hash remain for the in-game banner/diagnostics
only (the MM rando `commitHash` strcmp throw is now `#ifndef COMBO_BUILD`). **Why:** the release identity
is a deliberate human decision, and one launcher-level gate is simpler and more correct than two
independent per-game version checks over a merged file.

**OOT (`soh/soh/SaveManager.cpp`):** `SaveFileThreaded` write, `LoadFile` read, and the
`StartupCheckAndInitMeta`/`Init` metadata scan route through the callbacks (fall back to `file{N}.sav`
when unset); a corrupt container section skips the slot instead of `assert`-aborting. `global.sav` and
the legacy raw-SRAM path are untouched (not per-slot).

**MM (`mm/2s2h/SaveManager/SaveManager.cpp`):** interposed at the two IO primitives
`SaveManager_WriteSaveFile` / `SaveManager_ReadSaveFile` (classified by filename: main `file{N}.json` →
container `mm` section; `global.json` → disk; `file{N}backup.json` → dropped, the container's atomic
write makes MM's per-slot backup redundant). One seam catches the native flashrom owl/cycle path,
combo-persist, Saria-hint, and drag-drop. `SaveManager_DeleteSaveFile` clears the section by writing
`null`.

**Baked combo rando (self-contained, no runtime file read):** the consolidated spoiler (foreign
cross-game item map + cross-hints) is stored in `combo.rando` at save creation (`Combo_OnOOTSaveInit`)
and pushed once per save-load into both DLLs via `SOH_LoadComboRando` / `MM_LoadComboRando` →
`ComboRando::Combo_SetForeignJson`. The three `CrossForeign.h` loaders read that in-memory blob, not a
file; the per-slot `save{N}-Randomizer-<hash>.json` sidecar is retired. `Last-Generated-Randomizer.json`
survives only as the pre-save generation output + `ComboShipRandomizer` drop-import vehicle.

**Fallout — MM pictograph photos (issue #91, 2026-07-25).** Because per-slot writes now return before
`create_directories(savesFolderPath)`, MM's `saves/` dir is never created in a combo build (only
`global.json` still falls through, and combo never writes it). `SavePictoPng` in
`mm/2s2h/Enhancements/Items/ColorPictograph.cpp` then got a NULL `fopen` and its
`throw std::runtime_error` unwound out of `2ship.dll` into `std::terminate` (`0xe06d7363`) — a crash on
taking any picture on a fresh install. Photos now live in `Save/` next to the container (via
`PictoDir()`/`PictoPath()`, `COMBO_BUILD`-guarded) with `create_directories` before the write. The
`fopen`/libpng failure paths in both `SavePictoPng` and `LoadPictoPNG` were also converted from `throw`
to cleanup-and-return, since a cosmetic photo must never terminate the process; that part is
unguarded and will conflict if upstream reworks these functions. Note the hook is
`COND_VB_SHOULD(VB_PICTO_TAKE, true, ...)` — a literal `true`, so the ColorPictograph CVar does not
gate it.

## Resume the game the slot was last played in (issues #89/#87/#83, 2026-07-25)

A slot always resumed OOT even when the player's last session was MM, and leaving MM landed them at
Link's House instead of the Mask Shop.

**`combo.lastGame` (`combo/ComboShip.cpp`).** New container field (GameId: `0`=OOT, `1`=MM); absent
means OOT, so older saves behave as before. Written by `Combo_SetLastGame` at the **two transitions
only** — MM when the portal hands off, OOT on a portal return.

It is deliberately **not** derived from save writes. Two separate write classes make that unworkable:
background writes into the *dormant* game's section (cross-game grants, Anchor packets,
`SOH_MarkForeignObtained`), and — the one that actually broke it — OOT's own **load-time** writes.
Loading an OOT save persists sections from the rando and check-tracker `OnLoadGame` handlers, which run
*before* the launcher's `Combo_OnOOTSaveLoad`, so a foreground-filtered write-stamp set `lastGame` to OOT
microseconds before the resume decision read it, and the slot never resumed MM.

Transition stamps alone are sufficient: entering MM stamps MM (so an owl-save quit, which fires no
transition, still resumes MM), a portal return stamps OOT (so quitting from OOT resumes OOT), and an
absent field defaults to OOT for a first-ever session. A reset or owl-save quit deliberately leaves the
field alone — see the return-kind note below.

**The intercept.** `Combo_OnOOTSaveLoad` already fires at the ideal moment: `FileChoose_LoadGame` runs
`GameInteractor_ExecuteOnLoadGame` at its tail (`soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c`),
i.e. after `Sram_OpenSave` rebuilt `gRandoContext` and after the launcher loaded the dormant MM save,
but before OOT executes a single Play frame. `Combo_ResumeMMIfLastSavedThere` decides there, so no
vendored file-select edit is needed. It is gated on `SOH_IsOnFileSelect()` because `OnLoadGame` also
fires from `TitleSetup` on the MM→OOT return and from in-game reloads (`Warping`, `BetterSaveMenu`) —
ungated, walking out of MM after an owl save would bounce the player straight back into MM forever. It
also ignores `fileNum` outside 0..2, since debug-select (`0xFF`) and Boss Rush (`0xFE`) share
`FileChoose_LoadGame`.

**Leaving OOT's loop (`SOH_ParkForComboMMResume`, `soh/soh/OTRGlobals.cpp`).** Sets
`gGameState->init = nullptr` **and** `running = false`. `RunFrame`'s outer loop is
`while (runFrameContext.nextOvl)` and only falls through to `WindowClose()` when
`Graph_GetNextGameState` returns NULL. The Mask-Shop switch gets away with `running = false` alone
because `GameState_Init` already nulled `init`, but `FileChoose_LoadGame` has *queued* `Play_Init` — so
without clearing it OOT would build Play and keep running, and the handoff would never happen. It
deliberately does **not** save the file (that would stamp `lastGame` back to OOT, making the resume work
exactly once) and does **not** fire `OnExitGame` (the dormant tracker peek needs the rando context that
`OnLoadGame` just built). On the return, `SOH_ResumeGame` → `SOH_ResetFrameLoopForResume` re-seeds
`RunFrame` from overlay[0] (`TitleSetup`), which then takes the normal `gComboReturnFileNum` path.

**MM spawn point.** South Clock Town is the arrival for both portal entry and a boot resume; the
resume only differs when Remember Save Location is on, in which case it uses
`gSaveContext.save.shipSaveInfo.pauseSaveEntrance` (where that enhancement stores the spot — *not*
`save.entrance`, and combo never runs `Sram_OpenSave`, which is what normally applies it).
`gComboEntryIsResume` (`MM_SetComboEntryIsResume`) tells MM which kind of entry it is. Deliberately
**not** MM's authentic owl-statue resume: Clock Town is the intended combo default.

**Owl save (`mm/src/code/z_play.c`).** MM's owl save sets `GAMEMODE_OWL_SAVE` (`z_message.c`) and
`z_play.c` then does `SET_NEXT_GAMESTATE(TitleSetup_Init)` — "save and quit to MM's file select". In
combo that re-ran MM's whole boot (the combo entry block is skipped because `gComboStartFileNum` is back
to `-1`), so the attract path's `Sram_InitNewSave` wiped the save and MM's file select then wrote the
wipe into the container: **the slot's MM section was corrupted by an ordinary owl save.** A
`COMBO_BUILD` seam calls `Combo_RequestOwlSaveQuit()` instead, which routes through the existing return
hook and `SOH_SetComboBootToTitle` so the session lands on **OOT's title/file select**. No extra persist
is needed — the owl save's own flashrom write already went through the container seam. `lastGame` stays
MM, so reselecting the slot resumes MM.

**#87 entrance clobber (`soh/src/code/title_setup.c`).** The Mask-Shop arrival entrance is now assigned
*after* `GameInteractor_ExecuteOnLoadGame`, because the rando handler's `Entrance_SetSavewarpEntrance()`
recomputes from `savedSceneNum` (never set by the portal handoff) and overwrote it with
`ENTR_LINKS_HOUSE_CHILD_SPAWN`. Wrapped in `Entrance_OverrideNextIndex()` so it stays correct once
entrance shuffle is generated for combo seeds. (Safe only because every combo save forces
`QUEST_RANDOMIZER`, so `Entrance_Init` always ran.)

**#89(b) attract-demo guard (`mm/2s2h/BenPort.cpp`).** MM's title path runs `Sram_InitNewSave()` and
then a live `Play` for the attract demo, which scene-hops through the Clock Tower interior — tripping
the MM→OOT return trigger and persisting the *wiped* save over the slot. Both the trigger and the
return's persist call now require `gSaveContext.gameMode == GAMEMODE_NORMAL`. The guard sits at the
return call site, **not** inside `SaveManager_SaveCurrentForCombo`: that function has six callers, and
four of them (new-rando-save creation, dormant cross-game grants, `MM_MarkForeignObtained`, Anchor
dormant persist) legitimately run while MM is dormant with a stale `gameMode`.

**#83 shared settings.** MM keeps targeting/audio/language in `global.json`, which combo never writes
(its file select is never reached), so `SaveContext_Init`'s defaults applied — notably Switch targeting.
`Combo_AdoptOOTGlobalOptions` (`mm/2s2h/BenPort.cpp`) pulls OOT's values via the new
`SOH_GetGlobalOptions` export after `Sram_LoadGlobalOptions`. **Language is excluded on purpose:** the
enums disagree (OOT `ENG=0`, MM `JPN=0`), and MM's runtime reads `options.language`, not the
`options.languageSetting` field that gets serialized.

**Dormant check-tracker recalc (part of #81).** `InternalRecalculateAvailableChecks`
(`randomizer_check_tracker.cpp`) no longer early-returns when `gPlayState == nullptr`; it starts the
traversal from `gSaveContext.entranceIndex` instead. Required by the intercept, which leaves OOT with no
play state at all, and it also fixes the pre-existing frozen-tracker complaint while MM is foreground.
It now returns `bool` so the caller only clears `recalculateAvailable` when the recalc really ran,
instead of consuming and dropping the request.

## ComboShip-owned unified ROM extraction (OoT + MM) (2026-06-21)

**Why:** ComboShip needs BOTH an OoT and an MM ROM. The old launcher extracted them headlessly
(per-game `SOH_Extract`/`MM_Extract`, native OS dialogs, no progress bar) before any window existed.
Upstream's friendly ImGui extraction (`RunExtract`'s "ROM Extraction" modal) is per-game and can't host
a single "give me both ROMs" gate. ComboShip now owns a unified screen that asks for both ROMs up
front (auto-scan + Browse), requires both, and extracts each with a progress bar.

**Vendored (additive, `COMBO_BUILD`-guarded, minimal):**
- `soh/soh/OTRGlobals.cpp` — `InitOTR` split into `SOH_InitWindowOnly()` (the `OTRGlobals` ctor:
  window + ImGui + `SohGui::SetupMenu`, needs only the bundled `soh.o2r`, **no ROM**) and
  `SOH_FinishInit()` (the ROM-dependent `Initialize()` + managers + `SetupGuiElements`). Non-combo
  `InitOTR` keeps the original ctor → `RunExtract` → finish ordering (so `SOH_Init` is unchanged for the
  fast path). Added UI-less primitives `SOH_ValidateRom` / `SOH_StartExtraction` (background
  `std::async` → `Extractor::CallZapd`) / `SOH_GetExtractionProgress` (poll atomics; bool-ish values as
  `int` for a clean C ABI).
- `mm/2s2h/BenPort.cpp` — `MM_ValidateRom` / `MM_StartExtraction` / `MM_GetExtractionProgress` mirror
  (no init split — MM has no window of its own; `CallZapd` is context-independent so MM extracts fine
  against OOT's shared window). The `*Extract` workers catch exceptions → `done && !success`, never
  crashing the launcher.

**Combo-owned:**
- `combo/ComboExtract.h` — the C ABI (callback fn-ptr typedefs + `ComboExtractCallbacks`).
- `combo/gui/ComboExtractScreen.cpp/.h` (in comboui) — `ComboUI_RunExtraction(cb)`: owns the
  libultraship frame loop (`HandleEvents`/`StartDraw`/`StartFrame`/`RunGuiOnly`/`EndDraw`/`EndFrame`,
  same sequence as `RunExtract`) and the screen — auto-scans the working dir and classifies ROMs via the
  validate callbacks, per-slot Browse (native `GetOpenFileNameA`), Extract gated on both valid, Quit
  exits, then **sequential** single progress bar per game. Must `ImGui::SetCurrentContext` (per-module
  `GImGui`).
- `combo/ComboShip.cpp` — new ordering: detect missing ROM archives → if any, `SOH_InitWindowOnly()` →
  load comboui early → `ComboUI_RunExtraction()` (exit 1 on quit/failure) → `SOH_FinishInit()`; else the
  monolithic `SOH_Init()` fast path. Also **fixed `OOTArchivesExist()`**: it counted the PORT archive
  `soh.o2r` (always present) as the OoT ROM — so a real first run skipped OOT extraction and then
  hard-exited in `Initialize()`. It now checks only `oot.o2r` / `oot-mq.o2r`.

Verified: fast path (archives present) boots straight to title unchanged; first-run path opens the
extraction screen (`OoT=1 MM=1`). The old `SOH_Extract`/`MM_Extract` exports remain for non-combo use
but the launcher no longer calls them.

**`CallZapd` must return `true` on success (re-survive on every re-vendor).** Upstream `CallZapd`
returns `false` unconditionally (native flow gates on exceptions, not the return value), but
`SOH_/MM_StartExtraction` use it as the combo screen's success flag — so a `false` return makes a
*successful* extract read as "failed". The soh re-vendor `19427b200` reverted this and OOT extraction
broke; restored in `soh/soh/Extractor/Extract.cpp` to mirror the MM sibling (catch throw → verify
archive exists → `return true`). Also Release links `/SUBSYSTEM:WINDOWS` (no console window; Debug
keeps it), `+/ENTRY:mainCRTStartup` since ComboShip has its own `main()` and doesn't link SDL2main.

**Combined config renamed to `comboship.json` (issue 24).** OOT + MM share one libultraship Context, so
there is a single config file. `OTRGlobals.cpp` now names it `comboship.json` (COMBO_BUILD-guarded; `#else`
keeps `shipofharkinian.json` for standalone soh) to make the combined nature explicit and to gate the
first-launch settings import (absent file = fresh install). New combo-owned export `SOH_ApplyImportedConfig`
installs a launcher-merged config into the live `Config` (`SetBlock` + `Save` + `CVarLoad` + controller
reload). MM's `2ship2harkinian.json` literals are untouched (standalone-only, off the combo path).

## MM resume: reset magicLevel like Sram_OpenSave (magic meter outline, 2026-07-03)

**Why:** the combo resume shortcut (`title_setup.c` `gComboStartFileNum` block) loads the save
directly, skipping `Sram_OpenSave`'s post-load `magicLevel = 0` (z_sram_NES.c) that re-arms the
magic-meter grow animation. A save written mid-game stores `magicLevel` 1/2, so on every re-entry
the trigger (`Interface_Update`: `isMagicAcquired && magicLevel == 0`) never fired and runtime
`magicCapacity` stayed 0 — outline drawn at zero width while the fill showed correctly. First
entry was fine because it CREATES the save (default `magicLevel` 0).

**Vendored (inside the existing `COMBO_BUILD` block):** `mm/src/code/title_setup.c` — one line,
`magicLevel = 0` after the save load, mirroring `Sram_OpenSave`.

## Crash-handler tracebacks: honour SymFromAddr, always print module + RVA (2026-08-03)

**Why:** a player's Release traceback named `MM_Anchor_RequestTeleport` three times in a crash with no
Anchor involvement, and sent the investigation into the Anchor code for about an hour. Three
compounding defects in `PrintStack`:

1. `SymFromAddr`'s return value was ignored while a single `SYMBOL_INFO` buffer was reused, so a
   FAILED lookup silently reprinted the **previous** frame's name.
2. The printed address was `symbol->Address` (the symbol's base), not the PC — so every frame that
   resolved to the same symbol showed one identical address, and no RVA could be derived.
3. A Release build ships no PDBs, so dbghelp synthesizes symbols from the **export table** only.
   Every `static` function (all of the Fast3D interpreter) collapses onto an unrelated neighbouring
   export, and 2ship.dll exports only its handful of `MM_*` combo entry points — so an entire MM call
   chain reports as ~3 names, each wildly far from the real function.

**`libultraship/src/ship/debug/CrashHandler.cpp` (COMBO_BUILD-guarded; upstream preserved verbatim
under `#else` so future lus merges stay mechanical):**
- `SymSetOptions` gains `SYMOPT_LOAD_LINES | SYMOPT_UNDNAME`. Without `LOAD_LINES`,
  `SymGetLineFromAddr` can fail even when PDBs are present, silently degrading Debug builds too.
- Per frame: reset `displacement` and `symbol->Name[0]`, keep `SymFromAddr`'s result, and print
  `<unresolved>` when it fails — never a stale name.
- `symbol->Flags & SYMFLAG_EXPORT` is surfaced as a `~export(approx)` marker. That flag is set exactly
  when dbghelp invented the name from the export table, i.e. on every Release frame. Defect 3 is
  unfixable without shipping PDBs, so the fix is to make it *visible* rather than silently trusted.
- The module lookup is hoisted so **both** print branches carry the real PC, the module path and an
  RVA. The file/line branch had the same stale-name hazard (it printed `symbol->Name` too), which was
  the most misleading output of the three: this frame's file and line beside the previous frame's name.
- `AppendStrTrunc` no longer reads past the source string's terminator, and `AppendLine` no longer
  writes its newline unchecked. The latter was a genuine 1-byte heap overflow: `AppendStr` caps the
  index at `gMaxBufferSize - 1`, so `AppendLine` could push it to `gMaxBufferSize` and the next
  `AppendStrTrunc` wrote the terminator one byte past the allocation — inside the crash handler, i.e.
  corrupting the very log we needed. Reachable for real on a stack-overflow crash with thousands of
  frames.

**Reading a Release traceback offline** (the whole point of the RVA):

    llvm-symbolizer --obj=<matching dll> --relative-address <rva>

`--relative-address` is mandatory — without it PE addresses are interpreted against the image base
(`0x180000000`) and every lookup silently returns `??`. Verified end to end: a frame printed as
`MM_Anchor_RequestTeleport +0x50E4C ... RVA 0x28096C` mapped back to `CfaBindSeg` at
`combo/menu/ComboForeignAnim.h:403`, matching the Debug trace exactly.

**Deliberately NOT shared with `combo/ComboShip.cpp`'s late filter**, which does the same job
correctly. `ComboShip.exe` does not link libultraship on purpose — its filter has to survive the
window after `FreeLibrary`, when `libultraship.dll` may be gone. The ~15 duplicated lines are the
price of that; do not "DRY" them.

**Known gap:** the RVA is only actionable where we hold the matching linker PDB. Release currently
produces PDBs for soh/2ship (via the unconditional `/DEBUG` in `mm/CMakeLists.txt`) but **not** for
libultraship or ComboShip.exe — which is where the crash above actually faulted. PDBs must never ship
(they are ~5x the download), but they should be generated and archived per tagged release.

## Extraction-screen ROM drop: FileDropMgr early init + backend null-guards (2026-08-08)

**Why:** dragging a ROM onto the extraction screen crashed on a null `this` in
`FileDropMgr::SetDroppedFile` (reported on the Linux AppImage; reproduced with an injected
`SDL_DROPFILE`, and the identical latent crash on Windows via `WM_DROPFILES`). Root cause:
`SOH_InitWindowOnly()` is only the OTRGlobals ctor, but upstream creates the FileDropMgr later in
`Initialize()` — so for the whole extraction screen (which runs between the two)
`GetFileDropMgr()` returns null, and both gfx backends called `->SetDroppedFile()` on it
unguarded. Dropping a ROM there is the natural first move a new player makes.

**`libultraship/src/fast/backends/gfx_sdl2.cpp` + `gfx_dxgi.cpp` (COMBO_BUILD-fenced; upstream
line preserved verbatim under `#else`):** null-guard around the `SetDroppedFile` call at both
drop sites (`SDL_DROPFILE` / `WM_DROPFILES`).

**`soh/soh/OTRGlobals.cpp` ctor (COMBO_BUILD-guarded):** `context->InitFileDropMgr()` after
`InitConsole()`, so the manager exists during the extraction screen. `Initialize()`'s own later
call is idempotent (`Context::InitFileDropMgr` returns early if one exists), so the full-boot
path is unchanged.

**combo-owned, no fence (`combo/gui/ComboExtractScreen.cpp`):** the screen registers a drop
handler for its lifetime and routes dropped files into the OoT/MM slot via the same header-only
`SOH_ClassifyRom` / `MM_ClassifyRom` callbacks the auto-scan uses — content decides, not
filename; a drop that classifies for neither game parks in the first unfilled slot so the
"Not a valid … ROM" line explains what happened. The handler returns true so FileDropMgr skips
its "Unsupported file dropped" overlay.

Verified end to end on Linux with an `LD_PRELOAD` shim pushing synthetic `SDL_DROPFILE` events:
the pre-fix segfault reproduced on demand; post-fix, `oot.z64` routes to the OoT slot
(`SOH_ClassifyRom` accepts, MM never consulted) and `mm.z64` to the MM slot (SOH rejects, then
`MM_ClassifyRom` accepts). Also verified by hand on Windows: ROMs dragged onto the extraction
screen route through the `WM_DROPFILES`/DXGI path into the correct slots.

## Three more quit-to-MM-title seams (2026-08-22)

Every path that reaches `TitleSetup_SetupTitleScreen` runs `Sram_InitNewSave()` (a `SAVETYPE_VANILLA`
wipe) and then fires `OnSaveLoad`, which unregisters every `IS_RANDO` hook. The owl-save seam above
covered one such path; three others were still unguarded, and all now route through the existing return
hooks instead. Neither `Combo_RequestOwlSaveQuit()` nor `MM_RequestComboReturn()` stops the gamestate —
the return hook drives the handoff — so none of these sites may `STOP_GAMESTATE`.

- `z_kaleido_scope_NES.c` **save-prompt state 6** → `Combo_RequestOwlSaveQuit()`. Dead code in this tree;
  guarded defensively so it cannot resurface as a save wipe.
- `z_kaleido_scope_NES.c` **game-over Continue → "No"** → `Combo_RequestOwlSaveQuit()`. Vanilla discards
  unsaved progress here too, so the semantics match. Both sites already set `pauseCtx->state =
  PAUSE_STATE_OFF` first, so the branch cannot re-fire.
- `DebugConsole.cpp` **`reset`** → `MM_RequestComboReturn()` (Ctrl+R semantics: persists only when
  autosave is on). Transitively fixes `Ship_HandleConsoleCrashAsReset` and its callers. The
  `gGameState == nullptr` early return is kept.

**MM entry is never refused.** The return kinds stay 0/1/2; there is deliberately no "entry failed" kind.
Nor is it repaired: an unusable MM half is logged loudly, the fail-closed load sentinel keeps stray writes
and tracker draws off the slot, and play proceeds — re-creating the file is the remedy, and the legacy
population is retired by the 0.3.0 container gate. See `rando.md` for the load-side failure codes.

## MM owl saves on combo resume (issue #182, 2026-08-24)

**Why:** ComboShip enters MM without a file select — `Setup_InitImpl`'s `COMBO_BUILD` block
(`mm/src/code/title_setup.c`) hand-rolls a subset of `Sram_OpenSave` and loads through
`SaveManager_LoadSaveFile`, which read only the `newCycleSave` key. Owl saves (owl statue, Pause Save,
Autosave) are still written correctly into the sibling `owlSave` key, so both coexist and the resume
always took the stale one — discarding everything since the last cycle save. Vanilla is fine:
`Sram_OpenSave` picks the owl page when `fileSelect->isOwlSave[...]` is set, then consumes it.

**The invariant:** *if `owlSave` exists, it is at least as new as `newCycleSave`.* Vanilla holds it
with `VB_DELETE_OWL_SAVE` on continue plus the `DeleteOwlSave()` hooks on `BeforeEndOfCycleSave` /
`BeforeMoonCrash`. Combo breaks it because `SaveManager_SaveCurrentForCombo` writes `newCycleSave` from
11 call sites — four of them while MM is **dormant** and the player is in OOT (cross-item grants,
Anchor, dormant Triforce credits).

**Fix:** `gComboOwlBlobSlot` (`SaveManager.cpp`, declared in `BenPort.h`'s C-only region) records the
slot whose owl blob `gSaveContext` descends from.

- `SaveManager_LoadSaveFile` prefers `owlSave` when the key is present — a whole `SaveContext`, so it
  restores the cycle extras (`eventInf`, bottle timers, `pictoPhotoI5`) that `newCycleSave` lacks.
  Presence of the key is the discriminator; `save.isOwlSave` is **not** reliable, every owl writer
  restores it in RAM afterwards. Stays a pure read — the dormant tracker peek shares this function.
  Its owl branches return the load-failure codes like every other page: a slot with neither key, or an
  unparseable `owlSave` and no `newCycleSave`, is `-4` (fail-closed sentinel; entry still proceeds).
- `SaveManager_SaveCurrentForCombo` read-modify-writes: it **refreshes** the blob when the flag matches,
  otherwise **erases** it. Never leaves it untouched — blanket preservation would let a dormant grant
  write a newer `newCycleSave` behind a stale blob, and the granted item would vanish. The refresh
  rewrites the **whole** `SaveContext`, the same shape the owl writer emits: refreshing only `["save"]`
  would leave the blob's `eventInf` and bottle timers frozen while `save` moved on, a mix no vanilla
  writer can produce (a bottle timer would then resume against a start time from an earlier process).
- The new-cycle branch of `SaveManager_SysFlashrom_WriteData` preserves `owlSave`, so it gets the same
  erase-unless-it-is-ours treatment. Reached by the game-over save prompt and by a pause save with
  Pause Menu Save off — Song of Time and moon crash are already safe via `DeleteOwlSave`.
- `MM_InvalidateOwlBlobSlot` clears the flag when the launcher replaces a slot's `mm` section behind
  MM's back (`EraseComboContainer`, `Combo_CopyContainer`) — it cannot reach a DLL global otherwise.
- A portal entry arrives in South Clock Town, which runs neither owl-arrival path, so `title_setup.c`
  clears `save.isOwlSave` there. Left set it would persist into `newCycleSave` and keep owl-save write
  timing armed for the whole session (a 2s stall and an `eventInf` wipe on every cycle reset).
- `Combo_ApplyOwlSaveOpen` (`z_sram_NES.c`, next to `Sram_OpenSave` because `sOwlWarpEntrances` is
  static there) mirrors the owl branch: pause entrance beats owl warp id, the `owlWarpId > OWL_WARP_MAX`
  quirk is kept verbatim, post-temple swamp/mountain rewrites, scarecrow song.
- `Combo_MMDropOwlSaveBlob` consumes the blob on continue. `func_80147314` can't be used — it needs
  `sramCtx->saveBuf` and `gPlayState`, neither of which exists at `Setup_InitImpl`.

**Entry kinds:** an owl blob wins on both, but only a resume follows it to where it was saved; a portal
entry still arrives at South Clock Town.

**Not a gap:** the combo block never zeroes `eventInf` or resets the timers the way `Sram_OpenSave`'s
non-owl branch does — it doesn't need to. `Setup_InitImpl` calls `SaveContext_Init()`, which `memset`s
all of `gSaveContext` on every MM entry. Vanilla only needs those resets because its file select
re-uses a dirty `gSaveContext`.

**Known hole, left alone:** with Autosave on and Pause Menu Save off, a pause save takes
`Sram_SetFlashPagesDefault` into the new-cycle branch, which preserves `owlSave` — so a stale autosave
blob shadows it until the next combo write. Vanilla 2S2H has the identical bug through its file select.

## Flash-save tables indexed with a raw or sentinel `fileNum` (issue #184, 2026-08-26)

**Why:** MM addresses save storage through the `gFlashSave*` / `gFlashOwlSave*` lookup tables in
`z_sram_NES.c`, whose correct index is `fileNum * FLASH_SAVE_MAIN_MULTIPLIER` (+
`FLASH_SAVE_BACKUP_OFFSET` for the backup row) — each slot owns two consecutive rows. Two kaleido
sites dropped the multiplier, so the index still resolved to a *valid but wrong* row: slot 2 wrote
`file1backup.json` (which `SaveManager_WriteSaveFile` drops under `COMBO_BUILD`, so the save vanished)
and slot 3 wrote `file2.json`, i.e. slot 2's `mm` section. Upstream's backup write is real, so only a
ComboShip build loses the write. Separately, four sites indexed with `gSaveContext.fileNum` without
testing the `0xFF` "no slot" sentinel, reading 255 or 510 entries past 14- and 6-entry arrays. That
usually just fails the `SaveManager_GetFlashSaveFromPages` reverse lookup and drops the write, but if
the garbage happens to alias a real (startPage, numPages) pair it resolves a valid `FlashSave` and
overwrites another slot's `mm` section — deterministic per build, so it can flip on any unrelated
`.data` change.

ComboShip reaches `0xFF` deliberately: `SaveManager_LoadFailedForCombo` sets it whenever a
container's `mm` half fails to load, because a failed half is never refused and never repaired — play
proceeds (see the entry above). `gSaveContext.flashSaveAvailable` is true in that session, so the
existing `!flashSaveAvailable` guards do not cover it; the `fileNum` test is the only thing that can.
Playing the Song of Time was enough to hit it: `Sram_SaveEndOfCycle` fires `BeforeEndOfCycleSave` →
`DeleteOwlSave` → `func_80147314(0xFF)` → two synchronous writes off `gFlashOwlSaveStartPages[510]` on
a six-entry array. Upstream 2S2H is exposed too (map select and BootToWarpPoint both play with
`fileNum == 0xFF`), which is why the sibling guards are already commented "Don't let them save if they
are in debug save" — so these are correctness fixes, not deviations, and none is fenced.

**`mm/src/code/z_sram_NES.c` (unguarded, `// ComboShip:`):** a `fileNum != 0xFF` test around the
storage access only — never the function entry — in `Sram_SaveSpecialEnterClockTown`,
`Sram_SaveSpecialNewDay`, `Sram_ResetSaveFromMoonCrash`, and `func_80147314` (which tests its
`fileNum` argument, so one guard covers both callers). The in-memory transitions these functions
perform first all still run: the `isFirstCycle`/`isOwlSave` sets, `Sram_SaveEndOfCycle`, the `newf`
zero-and-restore dance, and the cycle-flag and timer resets. All four use synchronous writes and never
touch `sramCtx->status`, so skipping leaves only a stale `curPage`/`numPages` that nothing reads while
`status` is 0 — no save state machine can observe the skip. `Sram_ResetSaveFromMoonCrash` is a
read/restore rather than a write, so its post-guard invariant is stated explicitly: the live save is
left exactly as it was and no restore is attempted, since there is nothing to restore from. That
replaces the old behaviour, where both reads failed and the function still copied the zeroed buffer
over `gSaveContext.save` — blanking the player's items, hearts and masks in RAM.

**`mm/src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope_NES.c` (unguarded,
`// ComboShip:`):** the missing multiplier on the pause-menu save (Pause Menu Save off) and on the
game-over "Save" prompt, plus a `fileNum == 255` term added to the game-over prompt's existing
`!flashSaveAvailable` condition — its pause-save sibling already had that test, this one did not.
Extending that condition rather than nesting a new `if` routes the skip to `PAUSE_STATE_GAMEOVER_8` —
the exact state the pre-existing `!flashSaveAvailable` skip already used, so the guard introduces no
new state. `GAMEOVER_7` would be wrong: it polls `sramCtx->status`, which a skipped write leaves at 0.
Note that `GAMEOVER_8` does draw the "Saved!" banner (`gPauseSaveConfirmationENGTex`, and
`GAMEOVER_7` is drawn nowhere), so a no-slot game over claims a save it never made — an upstream quirk
inherited here, shared with the `!flashSaveAvailable` path and `PAUSE_SAVEPROMPT_STATE_5`, not
something this guard introduces. Both
branches are unreachable in this tree — the pause save prompt is only entered behind
`VB_SAVE_ON_B_BUTTON_IN_PAUSE_MENU`, whose sole hook requires the very CVar the buggy branch tests as
off, and the game-over prompt needs `GAMEOVER_INACTIVE`, which `z_game_over.c` clears on the frame it
starts the sequence. Fixed anyway so they cannot resurface as a cross-slot write. Both calls re-wrap
under clang-format; the formatter was run, nothing was hand-wrapped.

**`combo/ComboShip.cpp` (combo-owned, no fence):** `Combo_ReadGameSave` and `Combo_WriteGameSave` now
early-out on `!ComboIsValidSlot(fileNum)` like every other container callback. They were the only two
that skipped it, despite `ComboIsValidSlot`'s own comment saying callbacks reached from a game's
`gSaveContext.fileNum` must never create a phantom container. With `fileNum == 0xFF`,
`SaveManager_SaveCurrentForCombo` targeted `file256.json` → slot 255 → a real
`Save/file256.combosav` on disk; two MM callers reached it unguarded (`BenPort.cpp`'s portal-return
persist and `CheckQueue.cpp`'s per-check save), while every other caller already tested the sentinel.
Guarding the boundary closes all of them, present and future, so neither MM caller needed touching.

**Declined residuals.** `Sram_OpenSave`'s `0xFF` branch never assigns `phi_t1`, which then feeds a
`memcpy` length twice — real, but dead code: its only caller assigns 0-2 one line earlier and combo
never enters MM file select, and any fix would have to invent the intended index. `func_80147414`
only ever receives file-select indices, never `gSaveContext.fileNum`. The backup write in
`func_80147314` uses `gFlashOwlSaveNumPages[...MAIN_MULTIPLIER]` without `+ FLASH_SAVE_BACKUP_OFFSET`
(upstream's own `//!` note flags it) — a no-op, since both entries are `0x80`, and an upstream
decomp-accuracy question. The guards test `0xFF` equality rather than a `FILE_NUM_MAX` range, matching
the six pre-existing sibling guards in `z_message.c`, `MoonCrashSave.cpp` and the pause-save prompt:
`0xFE` only exists inside a three-statement window in `WarpPoint.cpp` and `0xFEDC` is commented out in
`z_title.c`, so neither is observable by a save path, and the one place that does want a range check is
the launcher boundary, where `ComboIsValidSlot` already is one. No assert was added: the obvious
candidate — checking in `Sram_StartWriteToFlashDefault` that `curPage` matches `gSaveContext.fileNum` —
would false-fire, because file-select copy/erase/nameset legitimately call it for `copyDestFileIndex`,
`selectedFileIndex` and the SRAM header. The dead kaleido branches were left in place rather than
removed, matching the quit-to-title seams above. Also noticed but not touched: the comment at
`BenPort.cpp`'s `Combo_LoadMMSaveFile` claims the caller rebuilds on a negative code — `title_setup.c`
discards the return and rebuilds nothing. Not sent upstream.
