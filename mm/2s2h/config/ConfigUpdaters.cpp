#include "2s2h/config/ConfigUpdaters.h"

#include "2s2h/Enhancements/Enhancements.h"
#include "2s2h/Enhancements/Trackers/DisplayOverlay.h"

namespace Ben {

struct Migration {
    const char* from;
    const char* to;
};

static void ApplyMigrationActions(const Migration* migrations) {
    while (migrations->from != nullptr) {
        if (migrations->to != nullptr) {
            CVarCopy(migrations->from, migrations->to);
        }
        CVarClear(migrations->from);
        migrations++;
    }
}

static const Migration version1Migrations[] = {
    { "gFixes.FixAmmoCountEnvColor", "gFixes.FixButtonEnvColor" },
    { nullptr, nullptr },
};

static bool HasDisplayOverlayModeInConfig(Ship::Config* conf) {
    if (!conf->GetNestedJson().contains("CVars")) {
        return false;
    }

    const auto& cvars = conf->GetNestedJson()["CVars"];
    return cvars.contains("gDisplayOverlay") && cvars["gDisplayOverlay"].is_object() &&
           cvars["gDisplayOverlay"].contains("Mode");
}

static void MigrateDisplayOverlayTimerMode(Ship::Config* conf) {
    if (HasDisplayOverlayModeInConfig(conf)) {
        return;
    }

    int oldVal = CVarGetInteger("gWindows.DisplayOverlay", 0);
    if (oldVal == TIMER_DISPLAY_RTA || oldVal == TIMER_DISPLAY_IGT) {
        CVarSetInteger(CVAR_DISPLAY_OVERLAY_MODE, oldVal);
        CVarSetInteger("gWindows.DisplayOverlay", 1);
    } else {
        CVarSetInteger(CVAR_DISPLAY_OVERLAY_MODE, TIMER_DISPLAY_NONE);
        CVarSetInteger("gWindows.DisplayOverlay", 0);
    }
}

ConfigVersion1Updater::ConfigVersion1Updater() : ConfigVersionUpdater(1) {
}

void ConfigVersion1Updater::Update(Ship::Config* conf) {
    ApplyMigrationActions(version1Migrations);
    MigrateDisplayOverlayTimerMode(conf);
}

} // namespace Ben
