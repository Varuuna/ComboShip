# Preserved ComboShip deviations

These are net-new ComboShip features/fixes that add `#ifdef COMBO_BUILD`-guarded (or otherwise
load-bearing) changes to the vendored trees. They are **not** tied to one merge pass — preserve them
on every future merge (they conflict only if upstream rewrites the exact functions). Each also
carries a `// ComboShip:` comment at the code site.

Entries are split by subsystem, one file each. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md)
for the merge mechanism and standing policies.

- [`rando.md`](rando.md) — Randomizer & cross-world fill: the cross-world generator, foreign-item delivery, hints, and per-game logic/accessibility honoring.
- [`anchor.md`](anchor.md) — Anchor networking: the launcher-owned connection, MM Anchor adapter phases, and co-op sync hardening.
- [`tracker.md`](tracker.md) — Trackers & notifications: cross-game tracker/toast window collision fixes, active-game gating, and the both-games tracker model.
- [`resource-mgmt.md`](resource-mgmt.md) — Resource manager & rendering: cross-RM display-list resolution, Fast3D segment guards, RM-scoped audio/cosmetics, and shader/texture fixes.
- [`boot-shutdown.md`](boot-shutdown.md) — Boot, transition & shutdown: eager MM boot, sturdy shutdown, unified ROM extraction, cross-game erase, and resume fixes.
- [`ui-menu.md`](ui-menu.md) — Menu & UI: combo-owned menu extraction, dual-game title logos, file-select changes, shared-settings consolidation, and the Advanced Resolution / controller-bindings editors.
