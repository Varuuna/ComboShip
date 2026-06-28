// combo/gui/ComboAudioBridge.h
//
// ComboShip-owned: bridges the Shared tab's audio sliders to MM. The Shared tab's Audio section is
// OOT's, writing gSettings.Volume.* (int 0-100). MM reads its own gSettings.Audio.* (float 0-1) and
// applies per-port volume via AudioSeq_SetPortVolumeScale (not ShipInit), so the Shared sliders would
// otherwise never reach MM. This is a one-way mirror: OOT's Volume.* is canonical, MM's Audio.* is
// derived. See Part A4 of the shared-settings consolidation.
#pragma once

namespace ComboAudio {

// If `cvar` is one of OOT's gSettings.Volume.* sliders, mirror it into MM's matching gSettings.Audio.*
// (int 0-100 -> float 0-1) and, when MM is the active game, apply it live. No-op otherwise. Call from
// the combo widget render apply-step.
void MirrorIfVolumeCVar(const char* cvar);

// Push all canonical OOT volumes into MM's CVars and apply the per-port scales. Call when MM becomes
// the foreground game (boot/resume), so volume changes made while MM was dormant take effect, and
// MM's audio ports match the canonical Shared values on entry.
void SyncAllToMM();

} // namespace ComboAudio
