# ComboShip deviations — Anchor networking

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

## Anchor transport moved to the launcher (combo-owned connection) — Phase 1 (2026-06-17)

**Why:** Anchor (upstream SoH online co-op) owned its own TCP socket + receive thread inside
`soh.dll`, so the connection died on every OOT↔MM portal transition and could never be shared with
MM. ComboShip now owns ONE persistent connection in `ComboShip.exe` that survives transitions and
will later route packets to whichever game is active. OOT's Anchor is **not relocated** — only its
transport is redirected through a minimal COMBO_BUILD seam; all packet/handler/menu/dummy-player
logic stays byte-intact in `soh/soh/Network/Anchor/`. A relay probe (since removed) confirmed the
public hm64 server relays our packet types peer-to-peer, so no server changes are needed.

**`soh/soh/Network/Network.h` + `Network.cpp` (vendored, COMBO_BUILD-guarded — preserve on future
soh merges):** under `COMBO_BUILD`, `Enable`/`Disable` no longer open a socket or spawn
`ReceiveFromServer`; they call launcher-registered hooks `gComboAnchorConnect`/`gComboAnchorDisconnect`.
`SendDataToRemote` routes to `gComboAnchorSend` instead of `SDLNet_TCP_Send`. Two new members feed
the launcher's receive thread back in: `InjectIncomingJson` (reuses `HandleRemoteJson`) and
`SetConnectedFromCombo` (drives `OnConnected`/`OnDisconnected`). The original socket bodies are kept
intact under `#else` for non-combo builds. No original lines deleted.

**`soh/soh/Network/Anchor/Anchor.cpp` (vendored, COMBO_BUILD-guarded):** `SendJsonToRemote` sends
immediately via `Network::SendJsonToRemote` under `COMBO_BUILD` (the launcher owns the thread-safe
outgoing queue), instead of pushing to the game-side `outgoingPacketQueue` that nothing would drain
without the local receive thread. Non-combo path unchanged.

**`soh/soh/OTRGlobals.cpp` (vendored, COMBO_BUILD-guarded):** six new exports — `SOH_SetAnchorSend`,
`SOH_SetAnchorConnect`, `SOH_SetAnchorDisconnect` (store the launcher's transport hooks),
`SOH_Anchor_RecvJson`, `SOH_Anchor_OnConnected`, `SOH_Anchor_OnDisconnected` (drive the in-place
Anchor). `declspec` follows `extern "C"` (the export-visibility ordering trap).

**Combo-owned (no further vendored churn):**
- `combo/ComboShip.cpp` — `namespace ComboAnchor` owns the SDL_net socket + receive thread + a
  thread-safe outgoing queue, framing NUL-delimited JSON exactly like the old `Network`. It
  registers `Send`/`Connect`/`Disconnect` into soh at boot and dispatches inbound packets via
  `SOH_Anchor_RecvJson`. `ComboAnchor::Shutdown()` joins the thread BEFORE any `FreeDll` (the thread
  calls into soh.dll). `#define SDL_MAIN_HANDLED` precedes the SDL include so SDL doesn't hijack
  `main`.
- `combo/CMakeLists.txt` — `ComboShip` now links `SDL2_net` (`-static` on the static-md triplet),
  mirroring soh's linkage; SDL2main is intentionally not linked.

On future merges: if upstream restructures `Network`/`Anchor` transport, re-apply the COMBO_BUILD
`#else` split. The launcher-side connection and dispatch are combo-owned and merge-independent.

## MM Anchor adapter — Phase 2a (MM joins the shared connection) (2026-06-17)

**Why:** MM had no online presence — when a co-op player crossed into MM, peers saw their stale last
OOT location ("Happy Mask Shop") because OOT's Anchor went dormant and nothing on the MM side sent
updates. Phase 2a adds an MM-side Anchor adapter that piggybacks on the launcher-owned connection
(no second socket, no MM Anchor menu) and sends MM client-state with a namespaced scene id.

**Combo-owned / new (no vendored churn):**
- `mm/2s2h/Network/Anchor/MMAnchor.{h,cpp}` (new) — standalone MM Anchor adapter. Sends via the
  launcher callback `gMMComboAnchorSend`, receives via the `MM_Anchor_RecvJson` export, is
  activated/deactivated by the launcher on transitions. Emits JSON shapes matching soh's Anchor
  (`UPDATE_CLIENT_STATE`/`ALL_CLIENT_STATE`) for cross-client interop. Reads the same process-global
  `gRemote.Anchor.*` CVars OOT's menu wrote (literals spelled out — MM lacks soh's prefix macro).
  Scene ids are namespaced (`MM_ANCHOR_SCENE_NAMESPACE = 1000`, fits `s16`) so MM and OOT scene
  numbers never collide in the shared roster / presence matching. Exports: `MM_SetAnchorSend`,
  `MM_Anchor_RecvJson`, `MM_Anchor_Activate`, `MM_Anchor_Deactivate`. Picked up by MM's existing
  `GLOB_RECURSE 2s2h/*.cpp` (CMake reconfigure required after adding the files).
- `combo/ComboShip.cpp` — `ComboAnchor` now tracks `sActiveGame`, routes inbound packets to the
  active game, registers `MM_SetAnchorSend`, and calls `SetActiveGame(0|1)` at each transition
  (activates MM's adapter on OOT→MM, deactivates on the way back). OOT self-reactivates via its own
  GameInteractor hooks, so it needs no explicit activate.

**`soh/soh/Network/Anchor/AnchorRoomWindow.cpp` (vendored, COMBO_BUILD-guarded — preserve on future
soh merges):** the room window labels a peer whose `sceneNum >= 1000` (an MM peer) as "Majora's Mask"
instead of running its namespaced id through OOT's `SohUtils::GetSceneName` (which would render a
bogus OOT scene name). It also suppresses the seed-mismatch warning for MM peers (`sceneNum >= 1000`)
— OOT's rando seed and MM's seed aren't comparable, so the check would always false-positive;
real cross-game seed verification (both games reporting the shared combo masterSeed) is Phase 3.
Minimal stopgap; Phase 4 moves the room window into the unified combo UI with real MM scene names.
No original lines deleted.

## MM Anchor adapter — Phase 2b (remote-player puppet + PLAYER_UPDATE) (2026-06-17)

**Why:** make co-op partners visible in MM. Adds per-frame pose broadcast and a "puppet" actor that
renders remote players' Link across all five transformation forms.

**Combo-owned / new (no vendored churn):**
- `mm/2s2h/Network/Anchor/MMAnchor.{h,cpp}` extended to the canonical Anchor field set + `PLAYER_UPDATE`
  send/receive, `RefreshClientActors`, and the `ShouldActorInit`/`OnActorUpdate` hooks.
- `mm/2s2h/Network/Anchor/DummyPlayer.cpp` (new) — the puppet actor. **Ported from the canonical
  2S2H Anchor PR (HarbourMasters/2ship2harkinian#1349, by the SoH Anchor author)**, adapted to
  ComboShip's launcher-owned transport (`MMAnchor` instead of a socket-owning `Anchor`) and
  `gRemote.Anchor.*` CVar keys. Spawns `ACTOR_PLAYER` → re-tags to `ACTOR_ITEM_INBOX`/`ACTORCAT_NPC`
  with `DummyPlayer_*` funcs; inits with `gPlayerSkeletons[transformation]` + a mask segment; reuses
  vanilla `Player_DrawGameplay`; respawns on form change. All five forms render through stock code.
- Implementation notes vs canonical: joint buffers are serialized as plain int arrays (nlohmann
  reserves `std::vector<u8>` for its binary type); `posRot` is read via the existing `Vec3f`/`Vec3s`
  converters (no `from_json<PosRot>` in this project's `BenJsonConversions.hpp`); client-state carries
  BOTH a namespaced `sceneNum` (OOT roster display) and a raw `sceneId` (MM same-scene puppet match).

No vendored MM source was modified for 2b (unlike the canonical PR, which added `OnSceneSpawnActors`/
`OnPlayerSfx` hooks to `z_actor.c` — ComboShip uses the existing `OnSceneInit`/`OnActorUpdate` hooks
instead, avoiding any vendored edit).

## MM Anchor adapter — Phase 2c (shared-progression item sync) (2026-06-18)

**Why:** make a check collected by one co-op player benefit the whole team. ComboShip chose
*shared-progression* co-op (decided 2026-06-17), not the canonical PR's *multiworld/routed-ownership*
model — so a locally-obtained check's item is broadcast to all teammates rather than released to an
owner. The apply path still mirrors the canonical (`ConvertItem` → junk fallback → `Rando::GiveItem`).

**Combo-owned (MMAnchor):** `SendPacket_GiveItem`/`HandlePacket_GiveItem` + `GIVE_ITEM` dispatch. The
wire carries the **raw** `randoItemId` + its `randoCheckId`; each receiver runs `ConvertItem` against
its *own* progressive state (so progressive items resolve to the receiver's correct tier) and marks
`RANDO_SAVE_CHECKS[rc]` obtained to avoid double-collection. `applyingRemoteItem` guards the
grant→broadcast loop; `targetTeamId` scopes to the team; self-broadcasts are ignored by clientId.
Flag sync is intentionally deferred (mirrors the canonical, whose `HandlePacket_SetFlag` is stubbed).

**`mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp` (vendored MM rando, COMBO_BUILD-guarded — preserve on
future mm merges):** at the existing local check-grant point, one guarded call
`MMAnchor_BroadcastCheckItem((int)CUSTOM_ITEM_PARAM, (int)randoSaveCheck.randoItemId)` (placed while
`CUSTOM_ITEM_PARAM` still holds the checkId, before it's overwritten with the item id on the next
line) + one extern declaration in the file's existing COMBO_BUILD include block. No original lines
moved/deleted. The function is a no-op unless Anchor is active, so non-co-op rando play is unaffected.

## MM Anchor adapter — Phase 2d (late-join / reconnect resync) (2026-06-18)

**Why:** bring a late-joining or reconnecting co-op client up to the team's current progression.

**Combo-owned (MMAnchor, no vendored churn):** ported the canonical `UPDATE_TEAM_STATE` /
`REQUEST_TEAM_STATE` (2S2H PR #1349) onto the launcher transport. On save (`AfterEndOfCycleSave`) and
in reply to a `REQUEST_TEAM_STATE`, a client serializes its whole `gSaveContext.save` (via
`BenJsonConversions`, with the rando-check array compacted — **7 fields**, dropping the canonical's
`multiWorldTeamIndex` since ComboShip is shared-progression, not multiworld). A client requests team
state on `OnSaveLoad` and on connect-while-in-game. On receive it restores receiver-local fields
(bottle contents, non-zero ammo, checksum, fileCreatedAt, `newf`, dpad/button layout, playerName),
then commits **only** `saveInfo` + `shipSaveInfo` — top-level `Save` fields (scene/entrance/time/day/
`playerForm`/cycle) are intentionally left untouched so the receiver isn't relocated — then re-runs
`Rando::CheckTracker::OnFileLoad` / `ActorBehavior::OnFileLoad` / `ShipInit::Init("IS_RANDO")`. Queued
packets ride along and are replayed through the normal incoming queue. Known canonical tradeoff
(accepted): the resync overwrites the receiver's HP/magic/rupees/respawn/scene-flags with the team's.
Same-game only (MM `permanentSceneFlags`/commit-hash layout). No vendored MM source modified for 2d.

## Anchor auto-reconnect on boot restored (2026-07-16)

**Why:** `Combo_FinishInit` (OTRGlobals.cpp) had a `COMBO_BUILD` branch that `CVarClear`ed
`gAnchor.Enabled` on every boot ("Anchor always starts DISABLED") instead of auto-connecting, so
Anchor stayed disconnected after a restart. That predated the launcher wiring the Anchor connect
transport before `SOH_Init`; with the transport now registered first, boot-time `Enable()` opens a
real socket rather than wedging on "Connecting…". Dropped the combo-only branch so boot auto-connects
from the persisted `gAnchor.Enabled` flag, matching upstream SoH (a deviation removed, not added).

## Anchor co-op sync hardening, bug 3: MM time-travel duplicate grants (2026-07-16)

**Why:** MM's `RandoSaveCheck` has two flags: `cycleObtained` (wiped every Song of Time,
`OnCycleSave.cpp`) and `obtained` (permanent). Co-op broadcast and cross-game delivery were driven by
the give-lambda running again each cycle, re-sharing/re-delivering an already-permanent check.

Fixes, all `COMBO_BUILD`:
- `CheckQueue.cpp`: capture `obtained` BEFORE the grant; only call `MMAnchor_BroadcastCheckItem` /
  `SendForeignCheck` (cross-deliver) the first time a check becomes permanently obtained. Local grant
  (`Rando::GiveItem`) is untouched — renewables still re-give locally, only re-SHARING is suppressed.
- `gComboCrossDeliver`/`gMMComboCrossDeliver` gained a `srcCheckName` parameter. The launcher's
  `DeliverCrossItem` (`combo/ComboShip.cpp`) dedups on it: the same wire `COMBO_CROSS_ITEM` packet
  reaches both DLLs' queues (an explicit `originGame` filter exception), so whichever games later
  process their own copy could each independently deliver — one shared in-memory set closes that.
- `MMAnchor::HandlePacket_UpdateTeamState` / OOT's `UpdateTeamState.cpp`: resync now unions rather than
  replaces permanent progress — MM snapshots local `obtained` flags before the wholesale
  `shipSaveInfo` assignment and restores any the incoming state lacked; OOT only advances
  `RandomizerCheckStatus` (progressive enum) instead of unconditionally overwriting it, so a
  stale/incomplete peer's resync can't un-collect a check.

## Anchor co-op sync hardening, bug 1: MM shop buys never broadcast (2026-07-16)

**Why:** MM broadcast co-op progress only from `CheckQueue.cpp` (the physical rando check path);
Bomb/Curiosity shop buys grant directly through `EnGirlA_RandoBuyFunc` (`EnGirlA.cpp`, `EnFsn.cpp`
just forwards to the same `buyFunc`) without ever calling `MMAnchor_BroadcastCheckItem`, so shop
purchases never reached teammates.

Fix: factored the bug-3 first-time-obtained broadcast guard into a shared seam,
`Rando::MiscBehavior::BroadcastCheckObtainedIfFirst` (`MiscBehavior.h`/`CheckQueue.cpp`), and wired
`EnGirlA_RandoBuyFunc` to call it (both the normal buy and the OOT-bound foreign-item buy branch,
which gets the same wasObtained guard as `CheckQueue.cpp`'s foreign path). `CheckQueue.cpp`'s own
broadcast call now goes through the same seam instead of calling `MMAnchor_BroadcastCheckItem`
directly, so future MM grant paths have one shared, idempotent broadcast point to hook into.

## Anchor co-op sync hardening, bug 2: launcher-owned both-games resync (2026-07-16)

**Why:** the resync button was OOT-only and foreground-only (`soh/soh/Network/Anchor/Menu.cpp:132`);
`REQUEST_TEAM_STATE`'s dormant answer path was broken in both games (OOT's `PumpDormant` REQUEST
branch didn't set `isDormantApply` like its `GIVE_ITEM` branch did, so `IsSaveLoaded()` always failed;
MM's `SendTeamStateFromSave` gated on `IsSaveLoaded()`, which requires `gPlayState` — always null while
MM is dormant); and nothing let a dormant sibling itself REQUEST a resync (MM's
`SendPacket_RequestTeamState` is `isActive`-gated).

Fixes, all `COMBO_BUILD`:
- `Anchor::PumpDormant` (`soh/soh/Network/Anchor/Anchor.cpp`) now wraps the `REQUEST_TEAM_STATE`
  branch in `isDormantApply` like the `GIVE_ITEM` branch already did.
- `MMAnchor::SendTeamStateFromSave` (`mm/2s2h/Network/Anchor/MMAnchor.cpp`) now judges by
  `gSaveContext.fileNum` instead of `IsSaveLoaded()`, so it answers even while MM is dormant.
- New dormant-safe request seam per game: `Anchor::RequestResyncDormantSafe()` /
  `MMAnchor::RequestResyncDormantSafe()`, exported as `SOH_Anchor_RequestResync()` /
  `MM_Anchor_RequestResync()`. MM's bypasses `SendJson`'s `isActive` gate (constructs+sends the
  `REQUEST_TEAM_STATE` JSON directly) since the whole point is a dormant MM asking for a resync too.
- Launcher orchestration (`combo/ComboShip.cpp`): both exports are called, unconditionally, on every
  (re)connect — a late-joiner/reconnect resync now pulls a peer's OOT AND MM progress, and this
  client's own dormant sibling gets asked too. Per-game `originGame` packet isolation is untouched;
  orchestration happens at the launcher, not inside either game's filter.
- Manual control: a "Resync team state" button in the combo-owned Shared > Settings > Network panel
  (`combo/gui/ComboMenu.cpp`), resolving both exports the same way the existing combo-gen syms are
  resolved (`GetModuleHandleA`/`GetProcAddress` — comboui.dll has no other way to call into the game
  DLLs) and calling both. This is NOT the full "Ship of Harkinian -> Network settings" migration to
  combo-owned UI (separate follow-up) — just the resync control. The existing OOT Menu.cpp button is
  unchanged and still works.

## Anchor co-op sync: code-review fixes on bugs 1-3 (2026-07-16)

**Why:** review of the above three entries found the bug-3 union was incomplete (MM still lost
permanent progress on resync) and three smaller issues in the bug-2 plumbing.

Fixes, all `COMBO_BUILD`:
- `MMAnchor::HandlePacket_UpdateTeamState` (`mm/2s2h/Network/Anchor/MMAnchor.cpp`): the bug-3 union
  only covered `RANDO_SAVE_CHECKS[i].obtained`; the wholesale `saveInfo`/`shipSaveInfo` assignment
  still let a stale peer erase `weekEventReg`, owned masks, quest items, upgrade tiers, and heart
  containers. Now snapshots those before the assignment and OR/max-merges them back in: `weekEventReg`
  (byte-wise OR, MM's analog of OOT's `eventChkInf`), `inventory.items[24..47]` (mask ownership slots,
  restore-if-local-non-empty), `inventory.questItems` (OR), `inventory.upgrades` (per-field max via
  `gUpgradeMasks`/`gUpgradeShifts`), `playerData.healthCapacity` (max).
- `soh/soh/Network/Anchor/Packets/UpdateTeamState.cpp`: `SetIsSkipped` was unconditional next to the
  now-progressive `SetCheckStatus`; a stale peer with `isSkipped=false` could un-skip a local skip. Now
  only applies the incoming skip when it's `true` and local isn't already.
- `SOH_Anchor_RequestResync`/`MM_Anchor_RequestResync` (`OTRGlobals.cpp`/`MMAnchor.cpp`): wrapped in
  try/catch — both call into JSON/CVar code with no prior guard, and are `extern "C"` exports the
  launcher calls, so a throw would have unwound across the DLL boundary.
- `combo/ComboShip.cpp`: the auto-resync-on-connect call moved off the network `ReceiveLoop` thread. It
  now sets an `std::atomic<bool> sResyncPending` flag; the existing per-frame `PumpDormant` (already
  running on the active game's thread) drains it once and fires both resync exports there, avoiding a
  race with `PumpDormant`'s own `isDormantApply`/`gPlayState` use. Also scoped the cross-item dedup set
  (`sAppliedCrossChecks`) to the active seed — cleared via `ResetCrossItemDedupForSeed` whenever
  `masterSeed` changes (regen or reload-from-file), so a check name reused across seeds isn't dropped
  as a false duplicate. `ResetCrossItemDedupForSeed` runs on the generation worker thread while
  `DeliverCrossItem` runs on the game thread, so both now take `sAppliedCrossChecksMutex`.

## Anchor co-op sync: toast-burst + name-tag color (2026-07-17)

**Why:** the "Save updated from team" toast fired 2-3x on every connect and on every manual resync
button press, even solo. Root cause: `Anchor::OnConnected` (soh) sent its own
`REQUEST_TEAM_STATE` in addition to the launcher's on-connect resync (`SOH_Anchor_RequestResync` +
`MM_Anchor_RequestResync`), so a solo/no-teammate reply (server answers directly when no teammates
are online) lands once per send, and `ReceiveLoop` forwards every reply to BOTH game DLLs
unconditionally — the foreground game applies+toasts on each one it receives (the dormant game's
`PumpDormant` already silently drops `UPDATE_TEAM_STATE`, so it never double-toasts).

Fixes, all `COMBO_BUILD`:
- `soh/soh/Network/Anchor/Anchor.cpp` (`OnConnected`): removed the redundant direct
  `SendPacket_RequestTeamState()` call — the launcher's resync is the sole on-connect source now.
- `soh/soh/Network/Anchor/Packets/UpdateTeamState.cpp` + `mm/2s2h/Network/Anchor/MMAnchor.cpp`
  (`HandlePacket_UpdateTeamState`): debounce the toast (2s, `steady_clock`) — the state merge still
  runs every time (idempotent), only the notification is deduped. Sender-count-agnostic, so it also
  covers the manual "Resync team state" button (which legitimately sends both an OOT and an MM
  request) and any other multi-reply burst.
- `soh/soh/Network/Anchor/DummyPlayer.cpp` + `mm/2s2h/Network/Anchor/DummyPlayer.cpp`: remote puppet
  name tags now use the client's Anchor color (`client.color`) instead of the dark default
  (`NameTagOptions.textColor` alpha 0).

## MM team-state merge + deferred cycle-save broadcast (2026-08-22)

**Why:** MM's `HandlePacket_UpdateTeamState` replaced `saveInfo`/`shipSaveInfo` wholesale where OOT
max-merges (`soh/soh/Network/Anchor/Packets/UpdateTeamState.cpp:283-290`), so a resync could truncate
Small Keys, Stray Fairies and dungeon items — and, worse, hand us the peer's `saveType`, flipping
`IS_RANDO` and silently unregistering every rando hook (including the Song of Time key restore).
Separately, MMAnchor broadcast from `AfterEndOfCycleSave`, which can run *before* the rando restore hook:
`RegisteredGameHooks<H>::functions` is an `std::unordered_map`, so hook order is not registration order
and no ordering-based fix is sound.

Fixes, receiver-only, no wire change (`mm/2s2h/Network/Anchor/MMAnchor.{h,cpp}`):
- **Two early drop-guards**, neither conditioned on `IS_RANDO` — the handler is also reached with
  `isDormantApply == true` from `PumpDormant`, which persists immediately afterwards, so an `IS_RANDO`-gated
  guard would have skipped exactly the dormant path. (1) Local save not `SAVETYPE_RANDO` → warn and return;
  there is no vanilla mode in ComboShip, so that means nothing usable is loaded, and preserving that
  `saveType` through the merge *is* the original key-eating bug. (2) No `rando` block in the incoming
  `shipSaveInfo` → warn and return: a non-rando peer serializes a zeroed `rando` struct
  (`BenJsonConversions.hpp`), which the wholesale assign would turn into an empty `RANDO_SAVE_CHECKS`.
- **Local `saveType` preserved** alongside `fileCreatedAt` in the receiver-local restore block.
- **Max/OR-merge** `inventory.dungeonKeys`, `inventory.strayFairies`, `rando.foundDungeonKeys` (all `s8`,
  `-1` when fresh, so signed max handles the sentinel) and OR `inventory.dungeonItems` (u8 bitfield).
  Accepted, per the OOT precedent: a stale peer resync can re-grant a locally-spent Small Key.
- **Deferred broadcast**: `AfterEndOfCycleSave` sets `pendingCycleSaveBroadcast`; MMAnchor's existing
  `OnGameStateUpdate` hook sends it. `GameState_Update` runs `main` (where `Sram_SaveEndOfCycle` and all
  `AfterEndOfCycleSave` hooks complete synchronously) before `GameInteractor_ExecuteOnGameStateUpdate`
  (`mm/src/code/game.c:151-168`), so the send still lands the same frame, strictly after every restore.
  A quit on that same frame drops the send harmlessly — peers re-request on connect. `Deactivate()` clears
  the flag so an unsent broadcast can't fire unsolicited on the next activation.

`Rando::MiscBehavior::OnFileLoad()` is still deliberately absent from the post-merge re-init block: it
calls `CheckQueueReset()`, which would drop queued in-flight grants. Preserving the local `saveType`
keeps its already-registered hooks valid, so it is not needed.
