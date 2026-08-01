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
#ifdef _WIN32
#include <windows.h>
#endif

namespace ComboRando {

namespace {
// Shared Anchor CVar keys (process-global store; the combo panel writes them). comboui has no access to
// soh's CVAR_REMOTE_ANCHOR macro, so the literals are re-declared here (as in ComboMenu.cpp).
constexpr const char* kCvarTeamId = "gRemote.Anchor.TeamId";
constexpr const char* kCvarRoomId = "gRemote.Anchor.RoomId";
constexpr const char* kVisibilityCvar = "gCombo.Anchor.RoomWindow";

// Launcher-owned roster: the exe registers this getter via ComboUI_SetAnchorRosterProvider. It parses
// every packet on a never-dormant thread, so the roster is always live (no per-game staleness).
const char* (*sRosterProvider)() = nullptr;

// Stateless scene-name resolvers + teleport exports, resolved from the game DLLs (dormant-safe).
typedef const char* (*FnResolveScene)(int);
typedef void (*FnRequestTeleport)(uint32_t);
typedef int (*FnGetConnState)(void);
FnResolveScene sSohResolveScene = nullptr;
FnResolveScene sMmResolveScene = nullptr;
FnRequestTeleport sSohRequestTeleport = nullptr;
FnRequestTeleport sMmRequestTeleport = nullptr;
FnGetConnState sSohGetConnState = nullptr;

void ResolveSyms() {
#ifdef _WIN32
    if (HMODULE h = GetModuleHandleA("soh.dll")) {
        if (!sSohResolveScene)
            sSohResolveScene = (FnResolveScene)GetProcAddress(h, "SOH_Anchor_ResolveScene");
        if (!sSohRequestTeleport)
            sSohRequestTeleport = (FnRequestTeleport)GetProcAddress(h, "SOH_Anchor_RequestTeleport");
        if (!sSohGetConnState)
            sSohGetConnState = (FnGetConnState)GetProcAddress(h, "SOH_Anchor_GetConnectionState");
    }
    if (HMODULE h = GetModuleHandleA("2ship.dll")) {
        if (!sMmResolveScene)
            sMmResolveScene = (FnResolveScene)GetProcAddress(h, "MM_Anchor_ResolveScene");
        if (!sMmRequestTeleport)
            sMmRequestTeleport = (FnRequestTeleport)GetProcAddress(h, "MM_Anchor_RequestTeleport");
    }
#endif
}

// Low-frequency snapshot: scene/roster state only changes on transitions, so polling the launcher a
// couple of times a second is plenty (no per-frame FFI/JSON cost).
double sLastPoll = -1.0;
nlohmann::json sSnapshot = nlohmann::json::object(); // { ownClientId, room{...}, clients[...] }

void PollSnapshot() {
    double now = ImGui::GetTime();
    if (sLastPoll >= 0.0 && (now - sLastPoll) < 0.5) {
        return;
    }
    sLastPoll = now;
    try {
        sSnapshot = sRosterProvider ? nlohmann::json::parse(sRosterProvider()) : nlohmann::json::object();
    } catch (...) { sSnapshot = nlohmann::json::object(); }
}

std::shared_ptr<ComboAnchorRoomWindow> sWindow;
} // namespace

void ComboAnchorRoomWindow::Draw() {
    if (!IsVisible()) {
        return;
    }
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    // comboui's per-module GImGui must point at the shared context (same pattern as ComboMenu/SwapWindow).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());

    // Hide the overlay entirely when not connected, rather than showing an empty "Not connected" box.
    ResolveSyms();
    if (!sSohGetConnState || (sSohGetConnState() & 2) == 0) {
        return;
    }

    // Floating overlay: chrome-less, auto-sized to content, translucent — not a resizable window.
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("Anchor Room", nullptr, flags)) {
        DrawElement();
    }
    ImGui::End();
    SyncVisibilityConsoleVariable();
}

void ComboAnchorRoomWindow::DrawElement() {
    // Draw() already gated on connected (the overlay is hidden otherwise), so no not-connected branch here.
    PollSnapshot();
    static const nlohmann::json kEmptyArr = nlohmann::json::array();
    const nlohmann::json& clients = sSnapshot.contains("clients") ? sSnapshot["clients"] : kEmptyArr;
    const nlohmann::json room = sSnapshot.value("room", nlohmann::json::object());

    // Global room: only a player count is meaningful (mirrors soh's AnchorRoomWindow).
    if (std::string("soh-global") == CVarGetString(kCvarRoomId, "")) {
        int online = 0;
        for (auto& c : clients) {
            if (c.value("online", false)) {
                online++;
            }
        }
        ImGui::Text("Players Online: %d", online);
        return;
    }

    const int activeGame = ComboUI::GetForegroundGame();    // 0 = OOT, 1 = MM
    const int teleportMode = room.value("teleportMode", 1); // launcher-owned room state (not the local CVar)

    // Own team: prefer the self entry's teamId (== gRemote.Anchor.TeamId), fall back to the CVar.
    std::string ownTeam = CVarGetString(kCvarTeamId, "default");
    std::string selfVersion;
    uint32_t selfSeed = 0;
    int selfGame = 0;
    bool haveSelf = false;
    for (auto& c : clients) {
        if (c.value("self", false)) {
            ownTeam = c.value("teamId", ownTeam);
            selfVersion = c.value("clientVersion", std::string());
            selfSeed = c.value("seed", (uint32_t)0);
            selfGame = (c.value("game", std::string("oot")) == "mm") ? 1 : 0;
            haveSelf = true;
            break;
        }
    }

    // Group by team, each player shown once.
    std::set<std::string> teams;
    for (auto& c : clients) {
        teams.insert(c.value("teamId", std::string("default")));
    }

    for (auto& team : teams) {
        if (teams.size() > 1) {
            ImGui::SeparatorText(team.c_str());
        }
        bool isOwnTeam = team == ownTeam;
        for (auto& c : clients) {
            if (c.value("teamId", std::string("default")) != team) {
                continue;
            }
            uint32_t clientId = c.value("clientId", (uint32_t)0);
            bool self = c.value("self", false);
            bool online = c.value("online", false);
            std::string name = c.value("name", std::string("???"));
            std::string game = c.value("game", std::string("oot"));
            int peerGame = (game == "mm") ? 1 : 0;

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

            // Area name: the launcher gates visibility (areaVisible); comboui resolves the display name
            // from the raw scene id via the owning game's stateless resolver (works even while dormant).
            if (c.value("areaVisible", false)) {
                int rawScene = c.value("rawScene", 0);
                std::string area;
                if (peerGame == 1) {
                    if (sMmResolveScene)
                        area = sMmResolveScene(rawScene);
                } else {
                    if (sSohResolveScene)
                        area = sSohResolveScene(rawScene);
                }
                if (!area.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1, 1, 1, 0.5f), "- %s", area.c_str());
                }
            }

            // Teleport: same-game only. Never rendered for a player in the other game (cross-game
            // teleport is impossible). The game's export re-validates and no-ops if disallowed.
            bool sameGame = (peerGame == activeGame);
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

            // Version/seed mismatch vs the self entry. Seed compare only within the same game (cross-game
            // seed verification is out of scope; MM/OOT report their own seeds).
            bool versionMismatch =
                haveSelf && !self && !selfVersion.empty() && c.value("clientVersion", std::string()) != selfVersion;
            bool seedMismatch = haveSelf && !self && peerGame == selfGame && online && c.value("isSaveLoaded", false) &&
                                c.value("seed", (uint32_t)0) != selfSeed;
            if (versionMismatch) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", ICON_FA_EXCLAMATION_TRIANGLE);
                ImGui::SetItemTooltip("Incompatible version! Will not work together!");
            }
            if (seedMismatch) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", ICON_FA_EXCLAMATION_TRIANGLE);
                ImGui::SetItemTooltip("Seed mismatch! Continuing will break things!");
            }

            ImGui::PopID();
        }
    }
}

void RegisterAnchorRoomWindow() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    if (!sWindow) {
        sWindow = std::make_shared<ComboAnchorRoomWindow>(kVisibilityCvar, "Anchor Room");
    }
    ctx->GetWindow()->GetGui()->AddGuiWindow(sWindow);
}

} // namespace ComboRando

// ComboShip: the launcher hands comboui its always-live Anchor roster getter here (called at startup,
// right after ComboUI_Register). The room window reads it via sRosterProvider.
#ifdef _WIN32
extern "C" __declspec(dllexport) void ComboUI_SetAnchorRosterProvider(const char* (*getter)())
#else
extern "C" void ComboUI_SetAnchorRosterProvider(const char* (*getter)())
#endif
{
    ComboRando::sRosterProvider = getter;
}
