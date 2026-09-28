#include "ship/audio/Audio.h"

#ifdef __APPLE__
#include "ship/audio/CoreAudioAudioPlayer.h"
#endif

#include "ship/Context.h"
#include "ship/config/Config.h"
#include "ship/controller/controldeck/ControlDeck.h"

#include <cstdlib> // ComboShip: std::getenv, for the SHIP_AUDIO_BACKEND override

namespace Ship {

Audio::~Audio() {
    SPDLOG_TRACE("destruct audio");
}

void Audio::InitAudioPlayer() {
    switch (GetCurrentAudioBackend()) {
#ifdef _WIN32
        case AudioBackend::WASAPI:
            mAudioPlayer = std::make_shared<WasapiAudioPlayer>(this->mAudioSettings);
            break;
#endif
#ifdef __APPLE__
        case AudioBackend::COREAUDIO:
            mAudioPlayer = std::make_shared<CoreAudioAudioPlayer>(this->mAudioSettings);
            break;
#endif
        case AudioBackend::SDL:
            mAudioPlayer = std::make_shared<SDLAudioPlayer>(this->mAudioSettings);
            break;
        default:
            mAudioPlayer = std::make_shared<NullAudioPlayer>(this->mAudioSettings);
            break;
    }

    if (mAudioPlayer && !mAudioPlayer->Init()) {
        // Failed to initialize system audio player.
        // Fallback to Null if the native system player does not work.
        SetCurrentAudioBackend(AudioBackend::NUL);
    }
}

void Audio::Init() {
    mConfig = Context::GetRawInstance()->GetConfig();

    mAvailableAudioBackends = std::make_shared<std::vector<AudioBackend>>();
#ifdef _WIN32
    mAvailableAudioBackends->push_back(AudioBackend::WASAPI);
#endif
#ifdef __APPLE__
    mAvailableAudioBackends->push_back(AudioBackend::COREAUDIO);
#endif
    mAvailableAudioBackends->push_back(AudioBackend::SDL);
    mAvailableAudioBackends->push_back(AudioBackend::NUL);

    SetCurrentAudioBackend(GetSavedAudioBackend());
    SetAudioChannels(GetSavedAudioChannelsSetting());
}

std::shared_ptr<AudioPlayer> Audio::GetAudioPlayer() {
    return mAudioPlayer;
}

AudioBackend Audio::GetCurrentAudioBackend() {
    return mAudioBackend;
}

AudioBackend Audio::GetSavedAudioBackend() {
    std::string backendName = mConfig->GetString("Window.AudioBackend");

    // ComboShip: env override, checked before the config so it wins over the macOS coreaudio
    // migration below — which is otherwise a standing override with no way out of it. Same
    // environment-as-config pattern libultraship already uses for SHIP_HOME.
    //
    // It does NOT stay contained to the one run: whatever backend the game ends up on is written
    // back to config by SetCurrentAudioBackend() below, as it is for any other selection. On macOS
    // that self-corrects for the dangerous case — a config left saying "coreaudio" is migrated back
    // to "sdl" on the next launch unless the variable is set again — so forcing CoreAudio stays an
    // opt-in you have to keep making. Any other value simply sticks, like picking it in the menu.
    const char* env = std::getenv("SHIP_AUDIO_BACKEND");
    const bool fromEnv = env != nullptr && *env != '\0';
    if (fromEnv) {
        backendName = env;
    }
    if (backendName == "wasapi") {
        return AudioBackend::WASAPI;
    }

    // Migrate pulse player in config to sdl
    if (backendName == "pulse") {
        mConfig->SetString("Window.AudioBackend", "sdl");
        mConfig->Save();
        return AudioBackend::SDL;
    }

    if (backendName == "coreaudio") {
#ifdef __APPLE__
        // ComboShip: migrate coreaudio -> sdl on macOS, in the same shape as the pulse migration
        // above. A saved "coreaudio" is almost never a deliberate choice — it is what libultraship's
        // own pre-fix default wrote on first launch — and honouring it leaves every existing macOS
        // user one launch away from the device-rate damage described at the fallback below.
        //
        // BE CLEAR ABOUT WHAT THIS COSTS: this runs on every read, so it is not a one-time migration
        // but a standing override. Selecting CoreAudio in the Audio settings appears to work for the
        // session and is silently reverted on the next launch — i.e. CoreAudio is effectively
        // UNSELECTABLE on macOS for as long as this deviation stands. That is deliberate: the failure
        // mode is permanent, system-wide, and hits applications other than this one, so it is not a
        // choice worth honouring until the player itself is fixed. Anyone who truly needs it can set
        // SHIP_AUDIO_BACKEND=coreaudio (checked at the top of this function) on each run.
        if (!fromEnv) {
            mConfig->SetString("Window.AudioBackend", "sdl");
            mConfig->Save();
            SPDLOG_WARN("macOS: forcing Window.AudioBackend sdl (was coreaudio). The CoreAudio player "
                        "reconfigures the output device's sample rate system-wide and persistently. "
                        "Set SHIP_AUDIO_BACKEND=coreaudio to override.");
            return AudioBackend::SDL;
        }
#endif
        return AudioBackend::COREAUDIO;
    }

    if (backendName == "sdl") {
        return AudioBackend::SDL;
    }

    if (backendName == "null") {
        return AudioBackend::NUL;
    }

    SPDLOG_TRACE("Could not find AudioBackend matching value from config file ({}). Returning default AudioBackend.",
                 backendName);
#ifdef _WIN32
    return AudioBackend::WASAPI;
#endif

#ifdef __APPLE__
    // ComboShip: default macOS to SDL, not CoreAudio, until the CoreAudio player stops reconfiguring
    // the user's output device. CoreAudioAudioPlayer opens a kAudioUnitSubType_HALOutput unit — which
    // binds the device directly, unlike DefaultOutput, which converts — and sets a client format at
    // AudioPlayer.h's hardcoded default SampleRate = 44100. macOS PERSISTS a device's chosen format,
    // so first launch left a DisplayPort display stuck at 44100 (it expects 48000) and every other
    // app on the system cut out — surviving both quitting the game and a reboot. A saved "coreaudio"
    // is migrated to "sdl" above on every launch, so only SHIP_AUDIO_BACKEND=coreaudio reaches it.
    // Revert once the player uses DefaultOutput or queries the device's nominal rate. See
    // docs/deviations/resource-mgmt.md.
    return AudioBackend::SDL;
#endif

    return AudioBackend::SDL;
}

void Audio::SetCurrentAudioBackend(AudioBackend backend) {
    mAudioBackend = backend;

    switch (backend) {
        case AudioBackend::WASAPI:
            mConfig->SetString("Window.AudioBackend", "wasapi");
            break;
        case AudioBackend::COREAUDIO:
            mConfig->SetString("Window.AudioBackend", "coreaudio");
            break;
        case AudioBackend::SDL:
            mConfig->SetString("Window.AudioBackend", "sdl");
            break;
        case AudioBackend::NUL:
            mConfig->SetString("Window.AudioBackend", "null");
            break;
        default:
            mConfig->SetString("Window.AudioBackend", "");
    }
    mConfig->Save();

    InitAudioPlayer();
}

std::shared_ptr<std::vector<AudioBackend>> Audio::GetAvailableAudioBackends() {
    return mAvailableAudioBackends;
}

void Audio::SetAudioChannels(AudioChannelsSetting channels) {
    if (mAudioSettings.ChannelSetting != channels) {
        mAudioSettings.ChannelSetting = channels;
        // Reinitialize the existing audio player with the new channel configuration
        if (mAudioPlayer) {
            mAudioPlayer->SetAudioChannels(channels);
        }
    }
}

AudioChannelsSetting Audio::GetAudioChannels() const {
    return mAudioSettings.ChannelSetting;
}

AudioChannelsSetting Audio::GetSavedAudioChannelsSetting() {
    int32_t channelsSetting =
        mConfig->GetInt("CVars." CVAR_AUDIO_CHANNELS_SETTING, static_cast<int32_t>(AudioChannelsSetting::audioMax));
    switch (channelsSetting) {
        case AudioChannelsSetting::audioMatrix51:
            return AudioChannelsSetting::audioMatrix51;
        case AudioChannelsSetting::audioRaw51:
            return AudioChannelsSetting::audioRaw51;
        case AudioChannelsSetting::audioStereo:
        case AudioChannelsSetting::audioMax:
        default:
            return AudioChannelsSetting::audioStereo;
    }
}

} // namespace Ship
