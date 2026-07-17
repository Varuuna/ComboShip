// combo/gui/ComboAnchorRoomWindow.cpp — see ComboAnchorRoomWindow.h
#include "ComboAnchorRoomWindow.h"
#include "ComboForeground.h" // ComboUI::GetForegroundGame() -> same-game teleport gating
#include <imgui.h>
#include <libultraship/libultraship.h> // CVar bridge
#include <ship/window/gui/IconsFontAwesome4.h>
#include <ship/Context.h>
#include <nlohmann/json.hpp>
#include <string>
#include <set>
#include <map>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ComboRando {

namespace {
// Shared Anchor CVar keys (process-global store; the combo panel writes them). comboui has no access to
// soh's CVAR_REMOTE_ANCHOR macro, so the literals are re-declared here (as in ComboMenu.cpp).
constexpr const char* kCvarTeamId = "gRemote.Anchor.TeamId";
constexpr const char* kCvarRoomId = "gRemote.Anchor.RoomId";
constexpr const char* kCvarTeleportMode = "gRemote.Anchor.RoomSettings.TeleportMode";
constexpr const char* kVisibilityCvar = "gCombo.Anchor.RoomWindow";

// Roster + teleport exports, resolved from the already-loaded game DLLs (same idiom as ComboMenu).
typedef const char* (*FnGetRoster)(void);
typedef void (*FnRequestTeleport)(uint32_t);
typedef int (*FnGetConnState)(void);
FnGetRoster sSohGetRoster = nullptr;
FnGetRoster sMmGetRoster = nullptr;
FnRequestTeleport sSohRequestTeleport = nullptr;
FnRequestTeleport sMmRequestTeleport = nullptr;
FnGetConnState sSohGetConnState = nullptr;

void ResolveSyms() {
#ifdef _WIN32
    if (HMODULE h = GetModuleHandleA("soh.dll")) {
        if (!sSohGetRoster)
            sSohGetRoster = (FnGetRoster)GetProcAddress(h, "SOH_Anchor_GetRoster");
        if (!sSohRequestTeleport)
            sSohRequestTeleport = (FnRequestTeleport)GetProcAddress(h, "SOH_Anchor_RequestTeleport");
        if (!sSohGetConnState)
            sSohGetConnState = (FnGetConnState)GetProcAddress(h, "SOH_Anchor_GetConnectionState");
    }
    if (HMODULE h = GetModuleHandleA("2ship.dll")) {
        if (!sMmGetRoster)
            sMmGetRoster = (FnGetRoster)GetProcAddress(h, "MM_Anchor_GetRoster");
        if (!sMmRequestTeleport)
            sMmRequestTeleport = (FnRequestTeleport)GetProcAddress(h, "MM_Anchor_RequestTeleport");
    }
#endif
}

// Low-frequency roster cache: area names only change on a scene transition, so polling the exports a
// couple of times a second is plenty (no per-frame FFI/JSON cost).
double sLastPoll = -1.0;
nlohmann::json sRoster = nlohmann::json::array(); // soh authoritative full roster
std::map<uint32_t, std::string> sMmAreaNames;     // clientId -> MM-resolved area name

void PollRoster() {
    double now = ImGui::GetTime();
    if (sLastPoll >= 0.0 && (now - sLastPoll) < 0.5) {
        return;
    }
    sLastPoll = now;
    try {
        sRoster = sSohGetRoster ? nlohmann::json::parse(sSohGetRoster()) : nlohmann::json::array();
    } catch (...) { sRoster = nlohmann::json::array(); }
    sMmAreaNames.clear();
    try {
        if (sMmGetRoster) {
            for (auto& e : nlohmann::json::parse(sMmGetRoster())) {
                sMmAreaNames[e.value("clientId", (uint32_t)0)] = e.value("areaName", std::string());
            }
        }
    } catch (...) {}
}

std::shared_ptr<ComboAnchorRoomWindow> sWindow;
} // namespace

void ComboAnchorRoomWindow::Draw() {
    if (!IsVisible()) {
        return;
    }
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    // comboui's per-module GImGui must point at the shared context (same pattern as ComboMenu/SwapWindow).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());

    ImGui::SetNextWindowSize(ImVec2(320.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Anchor Room", &mIsVisible, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        DrawElement();
    }
    ImGui::End();
    SyncVisibilityConsoleVariable();
}

void ComboAnchorRoomWindow::DrawElement() {
    ResolveSyms();

    int connState = sSohGetConnState ? sSohGetConnState() : 0;
    bool connected = (connState & 2) != 0;
    if (!connected) {
        ImGui::TextDisabled("Not connected.");
        return;
    }

    PollRoster();

    // Global room: only a player count is meaningful (mirrors soh's AnchorRoomWindow).
    if (std::string("soh-global") == CVarGetString(kCvarRoomId, "")) {
        int online = 0;
        for (auto& c : sRoster) {
            if (c.value("online", false)) {
                online++;
            }
        }
        ImGui::Text("Players Online: %d", online);
        return;
    }

    const int activeGame = ComboUI::GetForegroundGame(); // 0 = OOT, 1 = MM
    const std::string ownTeam = CVarGetString(kCvarTeamId, "default");
    const int teleportMode = CVarGetInteger(kCvarTeleportMode, 1);

    // Group by team, each player shown once.
    std::set<std::string> teams;
    for (auto& c : sRoster) {
        teams.insert(c.value("teamId", std::string("default")));
    }

    for (auto& team : teams) {
        if (teams.size() > 1) {
            ImGui::SeparatorText(team.c_str());
        }
        bool isOwnTeam = team == ownTeam;
        for (auto& c : sRoster) {
            if (c.value("teamId", std::string("default")) != team) {
                continue;
            }
            uint32_t clientId = c.value("clientId", (uint32_t)0);
            bool self = c.value("self", false);
            bool online = c.value("online", false);
            std::string name = c.value("name", std::string("???"));
            std::string game = c.value("game", std::string("oot"));

            ImGui::PushID((int)clientId);

            if (c.value("isOwner", false)) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", ICON_FA_GAVEL);
                ImGui::SameLine();
            }

            if (self) {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%s", name.c_str());
            } else if (!online) {
                ImGui::TextColored(ImVec4(1, 1, 1, 0.3f), "%s - offline", name.c_str());
                ImGui::PopID();
                continue;
            } else {
                auto col = c.value("color", nlohmann::json::object());
                ImVec4 nameColor(col.value("r", 255) / 255.0f, col.value("g", 255) / 255.0f,
                                 col.value("b", 255) / 255.0f, 1.0f);
                ImGui::TextColored(nameColor, "%s", name.c_str());
            }

            // Area name: soh gates visibility (ShowLocationsMode) and resolves OOT names; MM area names
            // are supplied by clientId in MM_Anchor_GetRoster and overlaid here for mm-tagged players.
            if (c.value("areaVisible", false)) {
                std::string area = c.value("areaName", std::string());
                if (game == "mm") {
                    auto it = sMmAreaNames.find(clientId);
                    if (it != sMmAreaNames.end() && !it->second.empty()) {
                        area = it->second;
                    }
                }
                if (!area.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1, 1, 1, 0.5f), "- %s", area.c_str());
                }
            }

            // Teleport: same-game only. Never rendered for a player in the other game (cross-game
            // teleport is impossible). The game's export re-validates and no-ops if disallowed.
            bool sameGame = (game == "mm") == (activeGame == 1);
            bool teleportGate = !self && online && c.value("isSaveLoaded", false) && teleportMode != 0 &&
                                (teleportMode == 2 || isOwnTeam);
            if (sameGame && teleportGate) {
                ImGui::SameLine();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                if (ImGui::Button(ICON_FA_LOCATION_ARROW, ImVec2(20.0f, 20.0f))) {
                    if (activeGame == 1) {
                        if (sMmRequestTeleport)
                            sMmRequestTeleport(clientId);
                    } else {
                        if (sSohRequestTeleport)
                            sSohRequestTeleport(clientId);
                    }
                }
                ImGui::PopStyleVar();
            }

            if (c.value("versionMismatch", false)) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", ICON_FA_EXCLAMATION_TRIANGLE);
                ImGui::SetItemTooltip("Incompatible version! Will not work together!");
            }
            if (c.value("seedMismatch", false)) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", ICON_FA_EXCLAMATION_TRIANGLE);
                ImGui::SetItemTooltip("Seed mismatch! Continuing will break things!");
            }

            ImGui::PopID();
        }
    }
}

void RegisterAnchorRoomWindow() {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    if (!sWindow) {
        sWindow = std::make_shared<ComboAnchorRoomWindow>(kVisibilityCvar, "Anchor Room");
    }
    ctx->GetWindow()->GetGui()->AddGuiWindow(sWindow);
}

} // namespace ComboRando
