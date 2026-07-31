// combo/gui/ComboAudioBridge.cpp — see ComboAudioBridge.h for rationale.
#include "ComboAudioBridge.h"
#include "ComboForeground.h"           // ComboUI::IsMmActive
#include <libultraship/libultraship.h> // CVarGetInteger / CVarSetFloat
#include "ComboResolve.h"
#include <cstring>

namespace {

// OOT Volume.* (int 0-100, canonical) -> MM Audio.* (float 0-1, derived). seqPlayer is MM's
// SEQ_PLAYER index for the per-port apply call; -1 = master (read live by MM each note-init, so
// setting the CVar is enough — no apply call). Indices match both games' SEQ_PLAYER_* enums.
struct VolMap {
    const char* ootCVar;
    const char* mmCVar;
    int seqPlayer;
    int defaultPct; // OOT slider default — must match SohMenuSettings.cpp so an untouched slider isn't
                    // read as 0 (which would silence MM). Master 40, the rest 100.
};
const VolMap kMap[] = {
    { "gSettings.Volume.Master", "gSettings.Audio.MasterVolume", -1, 40 },
    { "gSettings.Volume.MainMusic", "gSettings.Audio.MainMusicVolume", 0, 100 }, // SEQ_PLAYER_BGM_MAIN
    { "gSettings.Volume.SubMusic", "gSettings.Audio.SubMusicVolume", 3, 100 },   // SEQ_PLAYER_BGM_SUB
    { "gSettings.Volume.SFX", "gSettings.Audio.SoundEffectsVolume", 2, 100 },    // SEQ_PLAYER_SFX
    { "gSettings.Volume.Fanfare", "gSettings.Audio.FanfareVolume", 1, 100 },     // SEQ_PLAYER_FANFARE
};

// Resolved once from 2ship.dll. MM applies per-port volume via AudioSeq_SetPortVolumeScale, exposed
// as MM_ApplyAudioVolume (BenPort.cpp, COMBO_BUILD). Safe to call even when MM is dormant — gAudioCtx
// persists across transitions — but we only call it when warranted (see MirrorRow).
typedef void (*Fn_ApplyAudioVolume)(int seqPlayer, float volume);
Fn_ApplyAudioVolume ResolveApply() {
    static Fn_ApplyAudioVolume sFn = nullptr;
    static bool sTried = false;
    if (!sTried) {
        sTried = true;
        sFn = (Fn_ApplyAudioVolume)Combo_ResolveSym("2ship", "MM_ApplyAudioVolume");
    }
    return sFn;
}

// Mirror one row. `forceApply` pushes the per-port scale regardless of the active game (used by the
// boot/resume sync, where MM's audio is about to play); otherwise the apply only fires when MM is the
// foreground game (a live slider while playing OOT just updates the CVar — SyncAllToMM re-applies it
// on the next MM entry).
void MirrorRow(const VolMap& m, bool forceApply) {
    int vi = CVarGetInteger(m.ootCVar, m.defaultPct);
    float f = vi / 100.0f;
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    CVarSetFloat(m.mmCVar, f);
    if (m.seqPlayer >= 0 && (forceApply || ComboUI::IsMmActive())) {
        if (auto fn = ResolveApply()) {
            fn(m.seqPlayer, f);
        }
    }
}

} // namespace

void ComboAudio::MirrorIfVolumeCVar(const char* cvar) {
    if (!cvar || !cvar[0]) {
        return;
    }
    for (const auto& m : kMap) {
        if (std::strcmp(cvar, m.ootCVar) == 0) {
            MirrorRow(m, /*forceApply=*/false);
            return;
        }
    }
}

void ComboAudio::SyncAllToMM() {
    for (const auto& m : kMap) {
        MirrorRow(m, /*forceApply=*/true);
    }
}
