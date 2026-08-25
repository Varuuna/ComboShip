// combo/gui/ComboNotesWindow.cpp — see ComboNotesWindow.h
#include "ComboNotesWindow.h"
#include "ComboTrackerCommon.h" // ComboTracker::OotActiveSlot + SetTracker
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>     // InputTextMultiline over a std::string (vendored with ImGui)
#include <libultraship/libultraship.h> // CVar bridge
#include <ship/Context.h>
#include <string>

namespace {

constexpr const char* kVisibilityCvar = "gCombo.Tracker.NotesWindow";
// The ##Combo suffix keeps our ImGui identity distinct from soh's retired "Personal Notes" window.
constexpr const char* kWindowName = "Personal Notes##Combo";

// Launcher-owned per-slot note store (registered via ComboUI_SetNotesStore).
const char* (*sGet)(int) = nullptr;
void (*sSet)(int, const char*) = nullptr;

std::string sBuf;
int sLoadedSlot = -1;
bool sDirty = false;
int sDirtySlot = -1;
double sLastEdit = 0.0;

std::shared_ptr<ComboNotesWindow> sWindow;

} // namespace

// Callers always flush BEFORE reloading sBuf, so sBuf still holds sDirtySlot's text here.
void ComboNotes::FlushPending() {
    if (sDirty && sSet && sDirtySlot >= 0) {
        sSet(sDirtySlot, sBuf.c_str());
    }
    sDirty = false;
}

void ComboNotesWindow::Draw() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    // comboui's per-module GImGui must point at the shared context (same pattern as ComboMenu/SwapWindow).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());

    if (!IsVisible()) {
        ComboNotes::FlushPending(); // closing the window must not drop a just-typed note
        sLoadedSlot = -1;           // an erase/copy/new file can change the note while we're hidden
        return;
    }

    int slot = ComboTracker::OotActiveSlot();
    if (slot != sLoadedSlot) {
        ComboNotes::FlushPending();
        sLoadedSlot = slot;
        sBuf = (slot >= 0 && sGet) ? sGet(slot) : ""; // copy out of the launcher's thread_local now
    }
    if (slot < 0) {
        return; // no save loaded (title/file select)
    }

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
        DrawElement();
    }
    ImGui::End();
    SyncVisibilityConsoleVariable();
}

void ComboNotesWindow::DrawElement() {
    // No style push: upstream soh DrawNotes leaves the notes box at ImGui's default frame colors (#186).
    bool edited = ImGui::InputTextMultiline("##ComboNotes", &sBuf, ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y),
                                            ImGuiInputTextFlags_AllowTabInput);

    if (edited) {
        sDirty = true;
        sDirtySlot = sLoadedSlot;
        sLastEdit = ImGui::GetTime();
    }
    // Persist on focus loss, or 2s after the last keystroke (SoH's 40-idle-frame equivalent).
    if (ImGui::IsItemDeactivatedAfterEdit() || (sDirty && ImGui::GetTime() - sLastEdit > 2.0)) {
        ComboNotes::FlushPending();
    }
}

bool ComboNotes::WindowShown() {
    return CVarGetInteger(kVisibilityCvar, 0) != 0;
}

void ComboNotes::SetWindowShown(bool shown) {
    ComboTracker::SetTracker({ kVisibilityCvar, kWindowName }, shown);
}

void ComboNotes::RegisterWindow() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    if (!sWindow) {
        sWindow = std::make_shared<ComboNotesWindow>(kVisibilityCvar, kWindowName);
    }
    ctx->GetWindow()->GetGui()->AddGuiWindow(sWindow);
}

// ComboShip (#165): the launcher hands comboui its per-slot notes accessors here (called at startup,
// right before ComboUI_Register).
#ifdef _WIN32
extern "C" __declspec(dllexport) void ComboUI_SetNotesStore(const char* (*getter)(int),
                                                            void (*setter)(int, const char*))
#else
extern "C" void ComboUI_SetNotesStore(const char* (*getter)(int), void (*setter)(int, const char*))
#endif
{
    sGet = getter;
    sSet = setter;
}
