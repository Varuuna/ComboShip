// combo/gui/ComboGenProgress.h
// ComboShip: thread-safe progress state for the cross-world generate worker.
// Written by the worker thread; polled by the UI thread.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
namespace ComboRando {
struct ComboGenProgress {
    std::atomic<int> phase{ 0 }; // 0 Idle,1 Preparing,2 Placing,3 Finalizing
    std::atomic<int> placed{ 0 };
    std::atomic<int> total{ 0 };
    std::atomic<bool> running{ false };
    std::atomic<bool> done{ false };
    std::atomic<bool> success{ false };
    std::atomic<uint32_t> seed{ 0 };
    std::atomic<int> foreignCount{ 0 };
    char error[256] = { 0 };
    void Reset() {
        phase = 0;
        placed = 0;
        total = 0;
        success = false;
        seed = 0;
        foreignCount = 0;
        error[0] = '\0';
    }
    void SetError(const char* m) {
        if (!m) {
            error[0] = '\0';
            return;
        }
        std::strncpy(error, m, sizeof(error) - 1);
        error[sizeof(error) - 1] = '\0';
    }
    static const char* PhaseLabel(int p) {
        switch (p) {
            case 1:
                return "Preparing pools";
            case 2:
                return "Placing items";
            case 3:
                return "Finalizing";
            default:
                return "Idle";
        }
    }
};
} // namespace ComboRando
