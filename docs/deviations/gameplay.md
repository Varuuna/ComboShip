# ComboShip deviations — Gameplay enhancements

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

## Deku Nuts Explode Bombs in MM, ported from Ship of Harkinian (2026-10-16)

**Why:** SoH's "Deku Nuts Explode Bombs" (Enhancements → Items) had no 2Ship equivalent.

**What:** MM gets the same checkbox, under Enhancements → Gameplay → Explosives, after "Remove Explosive Limit"
(`gEnhancements.Items.NutsExplodeBombs`; SoH's is `gEnhancements.NutsExplodeBombs`, so each game is set separately).
A Deku Nut's flash (`EnMFire1`, which carries `DMG_DEKU_NUT`) sets off a bomb, like it already does a bombchu. Like
SoH, it adds `DMG_DEKU_NUT` to the bomb's hurtbox (`collider1`) when the bomb spawns; Powder Kegs are left alone.

**Vendored:** none. `mm/2s2h/Enhancements/Items/NutsExplodeBombs.cpp` (an `OnActorInit` hook on `ACTOR_EN_BOM`) and
the checkbox in `mm/2s2h/BenGui/BenMenu.cpp`.

## Improved Roll in MM, ported from Ship of Harkinian (2026-10-12)

**Why:** SoH's "Improved Roll" and "Improved Roll Steering" (Time Savers) had no 2Ship equivalent, so rolling felt
different between the two games.

**What:** MM gets the same two options, under Enhancements → Gameplay → Player, after "Dash After Roll":
- **Improved Roll** (`gEnhancements.Player.ImprovedRoll`): pressing A from frame 15 of a roll starts a new roll
  straight away, keeping full roll speed (not on `FLOOR_TYPE_7`, like a normal roll).
- **Improved Roll Steering** (`gEnhancements.Player.ImprovedRollSteering`, needs Improved Roll): the control stick
  turns a roll by up to 0x200 a frame, except while Z is held, so Z-target roll setups still work.

The CVars are MM's own (SoH's are `gEnhancements.ImprovedRoll*`), so each game is set separately.

**Vendored:**
- `mm/src/overlays/actors/ovl_player_actor/z_player.c` — `Player_Action_26` (the roll) asks
  `VB_PLAYER_ROLL_CHAIN` after the bonk check and `VB_PLAYER_ROLL_STEER` before the roll's speed update, the same
  places as SoH's hooks.
- `mm/2s2h/GameInteractor/GameInteractor_VanillaBehavior.h` — the two VB ids, with SoH's arguments.
- `mm/2s2h/Enhancements/Player/ImprovedRoll.cpp` — the hooks (SoH's code, with MM's roll start `func_80836B3C`).
- `mm/2s2h/BenGui/BenMenu.cpp` + `MenuTypes.h` — the two checkboxes and `DISABLE_FOR_IMPROVED_ROLL_OFF` (steering is
  greyed out while Improved Roll is off). `BenMenu::EvalDisabledByIndex` (the combo menu's disable query) now also
  honors a preFunc's `activeDisables`, as `Menu::DrawElement` does, so this and other widgets that disable that way
  (e.g. Koume's health while she is invincible) are greyed out in the combo menu too.
