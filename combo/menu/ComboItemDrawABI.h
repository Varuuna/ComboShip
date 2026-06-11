/* combo/menu/ComboItemDrawABI.h — ComboShip: cross-game item draw info (C ABI, POD only).
 * The owning game returns its sDrawItemTable data for one item so the OTHER game can render
 * the model via "@<game>:"-routed resource paths. Strings point into the owning game's static
 * storage (path literals from asset headers — process-lifetime). */
#ifndef COMBO_ITEM_DRAW_ABI_H
#define COMBO_ITEM_DRAW_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CW_DRAW_MAX_DLISTS 8

typedef struct {
    const char* dlists[CW_DRAW_MAX_DLISTS]; /* OTR path strings, in SUBMISSION order */
    int32_t     dlistCount;
    int32_t     xluStartIndex; /* dlists[0..xluStart-1] are OPA layers, rest XLU; -1 = all OPA */
} CwItemDrawInfo;

/* Returns 1 and fills out on success; 0 if the item is unknown/undrawable.
 * itemName is in the owning game's item namespace (MM: RI_* spoilerName; OOT: English name). */
typedef int32_t (*Fn_GetItemDrawInfo)(const char* itemName, CwItemDrawInfo* out);

#ifdef __cplusplus
}
#endif
#endif /* COMBO_ITEM_DRAW_ABI_H */
