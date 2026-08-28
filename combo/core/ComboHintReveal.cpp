#include "core/ComboHintReveal.h"

#include <iostream>
#include <string>

#include "core/ComboContainer.h"
#include "core/ComboDllApi.h"

// ComboShip (#164): push the slot's hints slice + read state into comboui's Hint Tracker. Reads the
// container under the mutex, then calls out with it released (the DLL setters must never re-enter it).
void Combo_PushHintTrackerData(int slot) {
    if (!ComboUI_SetHintTrackerData)
        return;
    if (!ComboIsValidSlot(slot)) {
        ComboUI_SetHintTrackerData(-1, "", "");
        return;
    }
    const ComboHintSlice slice = ComboReadHintSlice(slot);
    ComboUI_SetHintTrackerData(slot, slice.hints.c_str(), slice.read.c_str());
}

// Persist one reveal and re-push the read state. Runs on the reporting game's thread; the container
// write is locked inside ComboInsertHintRead and the comboui push happens after it returns.
void Combo_RecordHintRead(int fileNum, const char* bucket, const nlohmann::json& value, const char* matchField) {
    if (ComboInsertHintRead(fileNum, bucket, value, matchField))
        Combo_PushHintTrackerData(fileNum);
}

// OOT reported a revealed hint (keyed by the combo checkName it was applied from). OnRandoHintRevealed
// can fire repeatedly per textbox — the set-semantics insert makes that free.
void Combo_OnOotHintRevealed(int fileNum, const char* comboKey) try {
    if (!ComboIsValidSlot(fileNum) || !comboKey || comboKey[0] == '\0')
        return;
    Combo_RecordHintRead(fileNum, "oot", std::string(comboKey));
} catch (const std::exception& e) {
    std::cerr << "[ComboShip] Combo_OnOotHintRevealed threw: " << e.what() << std::endl;
} catch (...) { std::cerr << "[ComboShip] Combo_OnOotHintRevealed threw a non-std exception" << std::endl; }

// MM reported a revealed hint. kind: 0 = cross gossipPool pick (poolIndex), 1 = native MM stone hint
// (no upfront list, so the tracker shows these as a revealed-only group), 2 = NPC itemLocations hint.
void Combo_OnMmHintRevealed(int fileNum, int kind, int poolIndex, const char* key, const char* text) try {
    if (!ComboIsValidSlot(fileNum))
        return;
    switch (kind) {
        case 0:
            if (poolIndex >= 0)
                Combo_RecordHintRead(fileNum, "mmPool", poolIndex);
            return;
        case 1:
            if (key && key[0] != '\0')
                Combo_RecordHintRead(fileNum, "mmNative",
                                     nlohmann::json{ { "check", key }, { "text", text ? text : "" } }, "check");
            return;
        case 2:
            if (key && key[0] != '\0')
                Combo_RecordHintRead(fileNum, "mmNpc", std::string(key));
            return;
        default:
            return;
    }
} catch (const std::exception& e) {
    std::cerr << "[ComboShip] Combo_OnMmHintRevealed threw: " << e.what() << std::endl;
} catch (...) { std::cerr << "[ComboShip] Combo_OnMmHintRevealed threw a non-std exception" << std::endl; }
