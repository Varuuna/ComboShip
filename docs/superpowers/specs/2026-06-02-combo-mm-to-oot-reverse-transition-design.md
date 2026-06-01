# ComboShip — Reverse MM→OOT Transition (bidirectional portal)

**Date:** 2026-06-02
**Status:** Design approved, pending spec review
**Branch:** shared-libraries

## Goal

Add the reverse of the existing OOT→MM transition so the two games can be entered
back and forth indefinitely (full bidirectional loop).

- **Forward (already works):** in OOT, entering Mido's House → switch to MM (spawns
  in South Clock Town).
- **Reverse (this work):** in MM, entering the Clock Tower interior scene
  `Z2_INSIDETOWER` (`SCENE_INSIDETOWER`) → switch back to OOT, spawning in Kokiri
  Forest at the Mido's-House door (as if exiting Mido's House).
- Repeatable: OOT ↔ MM any number of times.

## Constraints / principles

- **Shared `libultraship.dll`, one persistent `Context`.** The whole design reuses
  the single shared window/context/resource-manager; nothing is torn down and
  recreated on a switch (that would reintroduce the "MM opens a new window" bug).
- **HM64:** keep changes in port code (`soh/soh/OTRGlobals.cpp`, `mm/2s2h/BenPort.cpp`,
  `combo/ComboShip.cpp`) and hooks/enhancements. Touch decompiled game source
  (`soh/src`, `mm/src`) as little as possible; the unavoidable bit is splitting each
  game's one-time boot from its re-runnable game-state loop.

## Existing forward mechanism (reference)

1. OOT `GameInteractor::OnSceneInit` hook: `sceneNum == SCENE_MIDOS_HOUSE` →
   `sComboSwitchPending = true` (`OTRGlobals.cpp:1306`).
2. OOT `OnGameFrameUpdate` hook: on pending → `SaveManager::SaveFile`,
   `gComboSceneSwitchCallback(fileNum)`, `gGameState->running = false`
   (`OTRGlobals.cpp:1313`).
3. `SOH_RunMain` returns (under COMBO_BUILD it does NOT `DeinitOTR` — context stays
   alive). `main()` then calls `SOH_PrepareForTransition` (stop audio, destroy gui,
   keep context), `MM_NotifyComboTransition`, `MM_RunGame(fileNum)`.
4. MM (`BenPort.cpp` `sComboTransitionActive` branch): reuses OOT's context, swaps
   archives to MM, re-arms the window (`Fast3dWindow::SetIsRunning(true)`), runs its
   `while (WindowIsRunning())` loop.

## Architecture

### 1. Control flow & signaling (`combo/ComboShip.cpp`)

`main()` becomes a game-switch loop driven by two pending-transition signals:

```
SOH_Init(); register forward + reverse callbacks
Game current = OOT;  bool ootBooted = false, mmBooted = false;

while (true) {
    if (current == OOT) {
        g_pendingMMFileNum = -1;
        if (!ootBooted) { SOH_RunMain(argc,argv); ootBooted = true; }  // one-time boot
        else            { SOH_ResumeGame(); }                          // reuse path (new)
        if (g_pendingMMFileNum >= 0) { SOH_PrepareForTransition(); MM_NotifyComboTransition(); current = MM; }
        else break;                                                    // real quit → exit app
    } else { // MM
        g_pendingOOTReturn = false;
        if (!mmBooted) { MM_RunGame(g_pendingMMFileNum); mmBooted = true; }  // one-time boot
        else           { MM_ResumeGame(g_pendingMMFileNum); }               // reuse path (new)
        if (g_pendingOOTReturn) { MM_PrepareForTransition(); SOH_NotifyComboReturn(); current = OOT; }
        else break;                                                    // real quit → exit app
    }
}
SOH_Deinit();
```

Signals:
- **Forward** (exists): OOT hook → `gComboSceneSwitchCallback` → `g_pendingMMFileNum`, stop OOT loop.
- **Reverse** (new): MM hook → `gComboReturnCallback()` → `g_pendingOOTReturn`, stop MM loop.

The **quit-vs-switch** distinction (pending flag set ⇒ switch; not set ⇒ real window
close ⇒ break → `SOH_Deinit`) is new and must work from either game.

The return destination is an OOT entrance; MM does not know OOT entrance constants, so
the reverse callback carries no entrance — `SOH_ResumeGame()` chooses the Kokiri/Mido's-door
entrance on the OOT side.

### 2. Reverse trigger (MM side — port code in `BenPort.cpp`, `#ifdef COMBO_BUILD`)

Mirror OOT's forward hook:
- `GameInteractor` `OnSceneInit` → if `sceneId == SCENE_INSIDETOWER`, set pending + capture slot.
- Next-frame hook → flush MM save, call `gComboReturnCallback()`, then
  `Fast3dWindow::SetIsRunning(false)` so `while (WindowIsRunning())` exits and
  `MM_RunGame`/`MM_ResumeGame` returns to `main()`.

New MM exports: `MM_ResumeGame(fileNum)`, `MM_PrepareForTransition()`, plus the
`gComboReturnCallback` setter consumed by ComboShip.

### 3. OOT resume path (new `SOH_ResumeGame`, counterpart to MM's reuse path)

1. Reuse the shared `Context` (alive).
2. Swap `ArchiveManager` back to OOT archives, unload MM resources, re-register OOT
   resource factories.
3. Re-arm window (`SetIsRunning(true)`); re-init what `SOH_PrepareForTransition` tore
   down (SohGui, OTRAudio); re-sync ImGui (`SetCurrentContext`). `RebuildFontTexture`
   (already present) handles the font atlas.
4. Reload OOT's save (written at the forward switch); set `entranceIndex` to the
   Kokiri Forest / Mido's-House-door entrance (`ENTR_KOKIRI_FOREST_*` — exact spawn
   index pinned during implementation).
5. Re-run **only** OOT's game-state loop — not `Heaps_Alloc`/`InitOTR`.

New OOT exports: `SOH_ResumeGame()`, `SOH_NotifyComboReturn()`, `gComboReturnCallback`
setter.

### 4. Loop re-entry refactor (core / riskiest)

Neither `SOH_RunMain` (`Heaps_Alloc`, threads, IRQ, `InitOTR`) nor `MM_RunMain` is safe
to run twice. Split each into:
- **one-time boot** (heaps/threads/IRQ/init), and
- a **re-runnable game-state loop** (entered by `*_ResumeGame`).

This is the only part touching game source (`soh/src/code/main.c`, `mm/src/code/main.c`
/ `graph.c`); keep it minimal.

### 5. Handoff (symmetric both directions)

Leaving game: **save** + stop loop. `*_PrepareForTransition`: stop audio + destroy gui,
keep shared context/window/resource-manager. Entering game: swap archives, re-register
factories, re-arm window, re-init gui/audio, re-sync ImGui (+ `RebuildFontTexture`), load
its save, set its entrance, re-run its loop.

## Error handling / edge cases

- **Quit vs switch:** pending flag discriminates; no flag ⇒ real quit ⇒ `SOH_Deinit`.
- **Save-on-exit failure:** log loudly (write path already hardened), don't block the switch.
- **Duplicate factory/window/command registration on re-entry:** tolerated today on the MM
  side (benign warnings); OOT resume re-registration must be equally tolerant.
- **Legit `SCENE_INSIDETOWER`:** in the combo the Clock Tower interior is always the return
  portal — no "real" visit.
- **Audio-thread teardown crash (known):** repeated audio stop/re-init across round-trips
  exercises the joinable-`std::thread`-at-shutdown bug far more; may need handling as part
  of this work.

## Testing (runtime/gameplay — no unit harness)

- Diagnostic logging at every boundary (leaving/entering each game, archive swap, entrance set).
- Milestones: (1) single MM→OOT return lands at Mido's door; (2) full ping-pong several times,
  no new window, fonts intact, no crash; (3) saves persist across round-trips both directions.
- Watch `combo_abort_stack.txt` + `x64/Debug/logs/Ship of Harkinian.log` each run.

## Out of scope

- Resuming MM/OOT at the exact spot/state you left (we always spawn at the fixed portal
  entrances). Persisting full mid-scene state across round-trips is not required.
- Fixing the audio-thread exit crash beyond what these round-trips require.
