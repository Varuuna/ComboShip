// combo/gui/ComboCosmeticsSync.cpp
//
// ComboShip (#169): copy OOT's randomized cosmetic colors onto MM's semantically-shared elements, so a
// synced config comes out matching in both games. OOT is the source of truth: its randomize-on-gen roll
// is seed-derived (same seed -> same colors), MM's is not. So we copy OOT -> MM, never the other way.
//
// Lives in comboui because it needs libultraship's CVar API directly — the CVar store is one shared
// instance across the exe and every DLL, so the sync is plain reads/writes with no IPC. The launcher
// drives it through the exports at the bottom (sync, its gate, and the per-seed gen-roll latch).
#include <libultraship/libultraship.h> // CVar bridge
#include "ComboExport.h"
#include <ship/Context.h> // SaveConsoleVariablesNextFrame (persist the writes)
#include <spdlog/spdlog.h>
#include "ComboMenuModel.h" // cached MM_MenuApplyCVarChange resolver
// RANDOMIZE_ON_RANDO_GEN_ONLY. Relative path on purpose: enhancementTypes.h is a zero-include enum
// header, and comboui must not take soh/ onto its include path.
#include "../../soh/soh/Enhancements/enhancementTypes.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Comma-separated seeds whose generation-completion roll already happened on this machine (see the
// latch below). A list, not one slot: A -> B -> A must not re-roll A over the user's manual edits.
constexpr const char* kGenRollSeedCVar = "gCombo.Rando.GenRollSeed";
constexpr size_t kGenRollSeedsKept = 8;

// One OOT color feeding one or more MM elements. Up to 4 targets, nullptr-terminated. ootAdvanced
// marks OOT rows its editor only randomizes with AdvancedMode on (they legitimately never fire).
struct CosmeticPair {
    const char* ootId;
    bool ootAdvanced;
    const char* mmIds[4];
};

// Semantically-shared elements only. An advanced OOT row simply doesn't fire and MM keeps its own
// roll — see the per-pair gate in CopyOne.
const CosmeticPair kPairs[] = {
    { "HUD.AButton", false, { "Buttons.A" } },
    { "HUD.BButton", false, { "Buttons.B" } },
    { "HUD.CButtons", false, { "Buttons.CLeft", "Buttons.CDown", "Buttons.CRight" } },
    { "HUD.StartButton", false, { "Buttons.Start" } },
    { "HUD.Dpad", false, { "Buttons.DPad" } },
    { "Consumable.Hearts", false, { "HUD.Hearts" } },
    { "Consumable.Magic", false, { "HUD.Magic" } },
    { "HUD.Minimap", false, { "HUD.Minimap" } },
    // Deliberate: OOT's Kokiri tunic drives all four MM forms. OOT's Goron/Zora tunic rolls are
    // equipment colors with no MM equivalent, so they are intentionally not mirrored.
    { "Link.KokiriTunic", false, { "Player.HumanTunic", "Player.DekuTunic", "Player.GoronTunic", "Player.ZoraTunic" } },
    { "Link.Hair", true, { "Player.HumanHair", "Player.DekuHair" } },
    { "Consumable.GreenRupee", true, { "HUD.RupeeIcon" } },
    { "HUD.KeyCount", true, { "HUD.SmallKey" } },
    { "Arrows.FirePrimary", false, { "Effects.FireArrowPrim" } },
    { "Arrows.FireSecondary", true, { "Effects.FireArrowSec" } },
    { "Arrows.IcePrimary", false, { "Effects.IceArrowPrim" } },
    { "Arrows.IceSecondary", true, { "Effects.IceArrowSec" } },
    { "Arrows.LightPrimary", false, { "Effects.LightArrowPrim" } },
    { "Arrows.LightSecondary", true, { "Effects.LightArrowSec" } },
    // The Primaries are advanced, but OOT derives them from the (non-advanced) Secondary roll, so both
    // sides of each pair are populated even for default users.
    { "SpinAttack.Level1Primary", true, { "Effects.SpinSlashCharge" } },
    { "SpinAttack.Level1Secondary", false, { "Effects.SpinSlashBurst" } },
    { "SpinAttack.Level2Primary", true, { "Effects.GreatSpinCharge" } },
    { "SpinAttack.Level2Secondary", false, { "Effects.GreatSpinBurst" } },
    { "Title.FileChoose", false, { "Menus.FileWindow", "Menus.FilePlates" } },
    { "Trails.KokiriSword", false, { "Trails.KokiriSwordTrail" } },
    { "Trails.Stick", true, { "Trails.DekuStickTrail" } },
    { "Trails.Boomerang", true, { "Trails.ZoraBoomerangTrail" } },
};

// MM's live apply (ShipInit re-run). ComboMenuModel already caches it and retries until 2ship.dll
// is loaded, so no second resolver here.
Fn_MenuApplyCVar ResolveMMApply() {
    ComboRando::ComboMenuModel::Get().EnsureLoaded();
    Fn_MenuApplyCVar fn = ComboRando::ComboMenuModel::Get().Mm().applyCVarChange;
    if (!fn) {
        SPDLOG_WARN("[ComboShip] cosmetics sync skipped: 2ship.dll MM_MenuApplyCVarChange not resolved yet");
    }
    return fn;
}

std::string OotKey(const char* id, const char* leaf) {
    return std::string("gCosmetics.") + id + leaf;
}
// MM's prefix is singular on purpose — that is what keeps its keys from colliding with OOT's.
std::string MmKey(const char* id, const char* leaf) {
    return std::string("gCosmetic.") + id + leaf;
}

// Copy one OOT color onto one MM element. Skipped unless the OOT side was randomized and the MM side
// was randomized and unlocked: that also covers OOT's advanced rows (never randomized by default) and
// MM's suppressed options (custom model override -> randomize skipped -> Changed stays 0).
// A locked OOT row is a color the user deliberately pinned, so sync's job is to make MM match it.
bool CopyOne(Fn_MenuApplyCVar apply, const char* ootId, const char* mmId) {
    if (CVarGetInteger(OotKey(ootId, ".Changed").c_str(), 0) != 1 ||
        CVarGetInteger(MmKey(mmId, ".Changed").c_str(), 0) != 1 ||
        CVarGetInteger(MmKey(mmId, ".Locked").c_str(), 0) != 0) {
        return false;
    }
    const std::string colorKey = MmKey(mmId, ".Color");
    const std::string changedKey = MmKey(mmId, ".Changed");
    const std::string rainbowKey = MmKey(mmId, ".Rainbow");
    // A rainbow OOT source has no fixed color to copy — put MM on rainbow too so both keep cycling.
    const bool rainbow = CVarGetInteger(OotKey(ootId, ".Rainbow").c_str(), 0) != 0;
    if (rainbow) {
        CVarSetInteger(rainbowKey.c_str(), 1);
    } else {
        Color_RGB8 src = CVarGetColor24(OotKey(ootId, ".Value").c_str(), Color_RGB8{ 255, 255, 255 });
        // Alpha 255: every mapped MM option is supportsAlpha=false, and MM's draw path takes alpha
        // from the caller rather than the CVar.
        CVarSetColor(colorKey.c_str(), Color_RGBA8{ src.r, src.g, src.b, 255 });
        CVarSetInteger(rainbowKey.c_str(), 0);
    }
    CVarSetInteger(changedKey.c_str(), 1);
    apply(colorKey.c_str());
    apply(changedKey.c_str());
    apply(rainbowKey.c_str());
    return true;
}

// Persist our CVar writes — mirrors MM's own CosmeticEditorSave, so they survive a restart.
void RequestCVarSave() {
    if (auto* ctx = Ship::Context::GetRawInstance()) {
        ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

// Upstream-rename tripwire: after a roll, a live non-advanced OOT row has at least one of these keys;
// neither present (-1 sentinel) means the id in kPairs no longer exists upstream.
void WarnIfDeadOotId(const char* ootId) {
    if (CVarGetInteger(OotKey(ootId, ".Changed").c_str(), -1) == -1 &&
        CVarGetInteger(OotKey(ootId, ".Locked").c_str(), -1) == -1) {
        SPDLOG_WARN("[ComboShip] cosmetics sync: OOT option '{}' has no CVars — renamed upstream?", ootId);
    }
}

// True when at least one OOT OnGenerationCompletion subscriber (cosmetics, audio) would actually roll.
bool AnyOotGenRollEnabled() {
    return CVarGetInteger("gCosmetics.RandomizeCosmeticsGenModes", RANDOMIZE_OFF) == RANDOMIZE_ON_RANDO_GEN_ONLY ||
           CVarGetInteger("gAudioEditor.RandomizeAudioGenModes", RANDOMIZE_OFF) == RANDOMIZE_ON_RANDO_GEN_ONLY;
}

} // namespace

// Gate: the combo toggle plus BOTH games' randomize-on-generation options. The File-Load randomize
// modes deliberately don't count — they re-roll OOT after the sync and would drift the games apart.
extern "C" COMBO_EXPORT int ComboUI_CosmeticsSyncGateEnabled(void) {
    if (CVarGetInteger("gCombo.Rando.SyncCosmetics", 0) != 1) {
        return 0;
    }
    if (CVarGetInteger("gCosmetics.RandomizeCosmeticsGenModes", RANDOMIZE_OFF) != RANDOMIZE_ON_RANDO_GEN_ONLY) {
        return 0;
    }
    return CVarGetInteger("gCosmetics.RandomizeOnSeedGen", 0) == 1 ? 1 : 0;
}

extern "C" COMBO_EXPORT void ComboUI_SyncRandomizedCosmetics(void) {
    if (!ComboUI_CosmeticsSyncGateEnabled()) {
        return;
    }
    Fn_MenuApplyCVar apply = ResolveMMApply();
    if (!apply) {
        return;
    }
    int copied = 0;
    std::vector<const char*> suspects;
    for (const auto& pair : kPairs) {
        int pairCopied = 0;
        for (const char* mmId : pair.mmIds) {
            if (!mmId) {
                break;
            }
            if (CopyOne(apply, pair.ootId, mmId)) {
                ++pairCopied;
            }
        }
        copied += pairCopied;
        if (pairCopied == 0 && !pair.ootAdvanced) {
            suspects.push_back(pair.ootId);
        }
    }
    // Only meaningful when something copied: a rename kills one pair, an OOT "Reset All" or no roll
    // at all kills every pair and must stay silent.
    if (copied > 0) {
        for (const char* ootId : suspects) {
            WarnIfDeadOotId(ootId);
        }
    }
    RequestCVarSave();
    SPDLOG_INFO("[ComboShip] cosmetics sync: {} MM elements took OOT's colors", copied);
}

// Per-seed roll latch (#169): the generation-completion hooks must roll once per seed per machine,
// else the silent auto-load on every boot would re-roll over the user's manual cosmetic edits.
// Returns 1 (and claims the seed) only when this seed is not among the last kGenRollSeedsKept claimed
// and some subscriber is actually enabled to roll. Hex strings, not int CVars: the int store is 32-bit.
extern "C" COMBO_EXPORT int ComboUI_ClaimGenRollSeed(unsigned long long seed) {
    // Claiming with every option off would burn the seed, so enabling them mid-seed would do nothing.
    if (!AnyOotGenRollEnabled()) {
        return 0;
    }
    char hex[24];
    std::snprintf(hex, sizeof(hex), "%016llx", seed);
    std::vector<std::string> claimed;
    const std::string stored = CVarGetString(kGenRollSeedCVar, "");
    for (size_t pos = 0; pos < stored.size();) {
        size_t comma = stored.find(',', pos);
        if (comma == std::string::npos) {
            comma = stored.size();
        }
        std::string tok = stored.substr(pos, comma - pos);
        if (!tok.empty()) {
            if (tok == hex) {
                return 0;
            }
            claimed.push_back(tok);
        }
        pos = comma + 1;
    }
    claimed.emplace_back(hex);
    if (claimed.size() > kGenRollSeedsKept) {
        claimed.erase(claimed.begin(), claimed.begin() + (claimed.size() - kGenRollSeedsKept));
    }
    std::string out;
    for (const std::string& tok : claimed) {
        if (!out.empty()) {
            out += ',';
        }
        out += tok;
    }
    CVarSetString(kGenRollSeedCVar, out.c_str());
    RequestCVarSave();
    return 1;
}
