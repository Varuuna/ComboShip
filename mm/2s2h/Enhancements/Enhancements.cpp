#include "Enhancements.h"

void InitEnhancements() {
    // Cycle & Saving
    RegisterSavingEnhancements();
    RegisterAutosave();

    // Uncomment to enable the demo behavior, this shows of different modding capabilities
    // void RegisterDemoBehavior();
    // RegisterDemoBehavior();
}
