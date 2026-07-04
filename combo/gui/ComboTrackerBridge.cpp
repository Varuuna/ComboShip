// combo/gui/ComboTrackerBridge.cpp — see ComboTrackerBridge.h for rationale.
#include "ComboTrackerBridge.h"
#include <libultraship/libultraship.h> // CVar API
#include <algorithm>                   // std::clamp

void ComboTracker::SyncAppearance() {
    // First run: adopt OOT's current appearance as the canonical values, so this build doesn't
    // reset a user's pre-existing tracker setup to combo defaults.
    if (!CVarGetInteger("gCombo.Tracker.Seeded", 0)) {
        CVarSetInteger("gCombo.Tracker.Seeded", 1);
        CVarSetInteger("gCombo.Tracker.IconSize", CVarGetInteger("gTrackers.ItemTracker.IconSize", kDefaultIconSize));
        Color_RGBA8 seed = CVarGetColor("gTrackers.ItemTracker.BgColor.Value", { 0, 0, 0, 0 });
        CVarSetFloat("gCombo.Tracker.Opacity", seed.a / 255.0f);
        CVarSetInteger("gCombo.Tracker.WindowType",
                       CVarGetInteger("gTrackers.ItemTracker.WindowType", kDefaultWindowType));
        CVarSetInteger("gCombo.Tracker.Draggable",
                       CVarGetInteger("gTrackers.ItemTracker.Draggable", kDefaultDraggable));
    }

    // Icon size: OOT takes pixels directly; MM scales its fixed 46px base (ITEM_TEXTURE_SIZE).
    int px = CVarGetInteger("gCombo.Tracker.IconSize", kDefaultIconSize);
    CVarSetInteger("gTrackers.ItemTracker.IconSize", px);
    CVarSetFloat("gSettings.ItemTracker.Scale", std::clamp(px / 46.0f, 0.2f, 3.0f));

    // Opacity: MM has a float CVar; OOT dims via BgColor alpha (RGB preserved).
    float op = std::clamp(CVarGetFloat("gCombo.Tracker.Opacity", kDefaultOpacity), 0.0f, 1.0f);
    CVarSetFloat("gSettings.ItemTracker.Opacity", op);
    Color_RGBA8 bg = CVarGetColor("gTrackers.ItemTracker.BgColor.Value", { 0, 0, 0, 0 });
    bg.a = (uint8_t)(op * 255.0f + 0.5f);
    CVarSetColor("gTrackers.ItemTracker.BgColor.Value", bg);

    // Window type (0 = floating overlay, 1 = docked window) — same enum in both games.
    int wt = CVarGetInteger("gCombo.Tracker.WindowType", kDefaultWindowType);
    CVarSetInteger("gTrackers.ItemTracker.WindowType", wt);
    CVarSetInteger("gSettings.ItemTracker.WindowType", wt);

    // Draggable: OOT native; MM via a COMBO_BUILD seam in its tracker (upstream MM has no such toggle).
    int drag = CVarGetInteger("gCombo.Tracker.Draggable", kDefaultDraggable);
    CVarSetInteger("gTrackers.ItemTracker.Draggable", drag);
    CVarSetInteger("gSettings.ItemTracker.Draggable", drag);
}
