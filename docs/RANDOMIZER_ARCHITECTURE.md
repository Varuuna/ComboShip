# Cross-World Randomizer Architecture

## Overview

ComboShip's primary purpose is to enable a **cross-world randomizer** where:
- Items from OOT can be found in MM locations
- Items from MM can be found in OOT locations
- Both games remain largely separate experiences
- Players switch between games to progress
- The randomizer logic coordinates item placement across both games

This is similar to multiworld randomizers, but both "worlds" are different games running in the same executable.

## Core Concept

Unlike a unified game experience, this is about:
- **Separate Games**: OOT and MM run independently with their own logic
- **Shared Randomizer State**: A unified randomizer tracks item locations across both games
- **Cross-Game Item Placement**: OOT items can be placed in MM checks, and vice versa
- **Game Switching**: Players switch between games to collect items and progress

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────┐
│                  ComboShip Executable                    │
│                                                          │
│  ┌────────────────────────────────────────────────────┐ │
│  │      Cross-World Randomizer Engine                 │ │
│  │  • Unified item pool (OOT + MM items)              │ │
│  │  • Unified location pool (OOT + MM checks)         │ │
│  │  • Cross-game logic rules                          │ │
│  │  • Seed generation                                 │ │
│  │  • Item placement algorithm                        │ │
│  └────────────────────────────────────────────────────┘ │
│              ↓                           ↓               │
│  ┌─────────────────────┐   ┌─────────────────────┐     │
│  │  OOT Randomizer     │   │  MM Randomizer      │     │
│  │  Interface          │   │  Interface          │     │
│  │  • Query checks     │   │  • Query checks     │     │
│  │  • Give items       │   │  • Give items       │     │
│  │  • Track state      │   │  • Track state      │     │
│  └─────────────────────┘   └─────────────────────┘     │
│              ↓                           ↓               │
│  ┌─────────────────────┐   ┌─────────────────────┐     │
│  │   soh.dll (OOT)     │   │  2ship.dll (MM)     │     │
│  │  • Vanilla gameplay │   │  • Vanilla gameplay │     │
│  │  • Rando hooks      │   │  • Rando hooks      │     │
│  └─────────────────────┘   └─────────────────────┘     │
└─────────────────────────────────────────────────────────┘
```

## Key Components

### 1. Cross-World Randomizer Engine

**Location**: `combo/randomizer/CrossWorldRandomizer.h/cpp`

**Responsibilities**:
- Generate unified seed for both games
- Create item pool from both games
- Create location pool from both games
- Apply logic rules that span both games
- Place items across both games
- Track collected items and accessible locations

**Core Data Structures**:

```cpp
class CrossWorldRandomizer {
public:
    struct Item {
        ItemID id;
        GameID sourceGame;      // Which game this item is from
        ItemType type;          // PROGRESSION, JUNK, etc.
        std::string name;
    };
    
    struct Location {
        LocationID id;
        GameID game;            // Which game this check is in
        LocationType type;      // CHEST, NPC, BOSS, etc.
        std::string name;
        std::vector<ItemID> requirements; // Logic requirements
    };
    
    struct RandomizerState {
        uint64_t seed;
        std::map<LocationID, Item> placements;  // Which item at which location
        std::set<LocationID> checkedLocations;  // Locations player has checked
        std::set<ItemID> obtainedItems;         // Items player has obtained
    };
    
    // Seed generation
    void GenerateSeed(uint64_t seed);
    
    // Item placement
    void PlaceItems();
    bool CanPlaceItem(Item item, Location location);
    
    // Logic checking
    bool IsLocationAccessible(LocationID location);
    std::vector<LocationID> GetAccessibleLocations();
    
    // State tracking
    void OnLocationChecked(LocationID location, GameID game);
    void OnItemObtained(ItemID item, GameID game);
};
```

### 2. Game-Specific Randomizer Interfaces

**Purpose**: Bridge between cross-world randomizer and individual game randomizers

**OOT Interface** (`combo/randomizer/OOTRandomizerInterface.h/cpp`):

```cpp
class OOTRandomizerInterface {
public:
    // Query what item is at a location
    Item GetItemAtLocation(LocationID ootLocation);
    
    // Give item to player (may be from MM)
    void GiveItemToPlayer(Item item);
    
    // Check if location has been checked
    bool IsLocationChecked(LocationID location);
    
    // Mark location as checked
    void MarkLocationChecked(LocationID location);
    
    // Convert between OOT and cross-world IDs
    LocationID ConvertToGlobalLocation(OOTLocationID local);
    ItemID ConvertToGlobalItem(OOTItemID local);
};
```

**MM Interface** (similar structure):

```cpp
class MMRandomizerInterface {
    // Same interface as OOT, but for MM
};
```

### 3. Minimal Game Code Hooks

**In OOT** (`soh/src/code/z_player.c`):

```cpp
void Player_OpenChest(Player* player, ChestActor* chest) {
    #ifdef COMBO_BUILD
    // Query cross-world randomizer for item
    Item item = ComboRando_GetItemAtLocation(chest->locationId);
    
    if (item.sourceGame == GAME_MM) {
        // This is an MM item in OOT
        ComboRando_GiveMMItemInOOT(item);
        return;
    }
    #endif
    
    // Normal OOT chest logic
    GiveItemFromChest(player, chest);
}
```

**In MM** (`mm/src/code/z_player.c`):

```cpp
void Player_OpenChest(Player* player, ChestActor* chest) {
    #ifdef COMBO_BUILD
    // Query cross-world randomizer for item
    Item item = ComboRando_GetItemAtLocation(chest->locationId);
    
    if (item.sourceGame == GAME_OOT) {
        // This is an OOT item in MM
        ComboRando_GiveOOTItemInMM(item);
        return;
    }
    #endif
    
    // Normal MM chest logic
    GiveItemFromChest(player, chest);
}
```

## Item Handling

### Cross-Game Item Representation

```cpp
// combo/randomizer/ItemDefinitions.h

enum class ItemID : uint32_t {
    // OOT Items (0x0000 - 0x0FFF)
    OOT_KOKIRI_SWORD = 0x0001,
    OOT_MASTER_SWORD = 0x0002,
    OOT_HOOKSHOT = 0x0003,
    // ... all OOT items
    
    // MM Items (0x1000 - 0x1FFF)
    MM_DEKU_MASK = 0x1001,
    MM_GORON_MASK = 0x1002,
    MM_ZORA_MASK = 0x1003,
    // ... all MM items
    
    // Special cross-game items (0x2000+)
    CROSS_PROGRESSIVE_SWORD = 0x2001,
    CROSS_PROGRESSIVE_HOOKSHOT = 0x2002,
};

struct ItemDefinition {
    ItemID id;
    GameID sourceGame;
    std::string name;
    std::string description;
    
    // How to give this item in each game
    std::function<void()> giveInOOT;
    std::function<void()> giveInMM;
    
    // Visual representation
    uint16_t ootGetItemId;  // OOT's get-item ID
    uint16_t mmGetItemId;   // MM's get-item ID
};
```

### Giving Cross-Game Items

**When OOT player gets MM item**:

```cpp
void CrossWorldRandomizer::GiveMMItemInOOT(Item mmItem) {
    // 1. Show custom get-item animation in OOT
    OOT_ShowCustomGetItem(mmItem.name, mmItem.description);
    
    // 2. Store item in cross-world state
    randomizerState.obtainedItems.insert(mmItem.id);
    
    // 3. Item will be available when player switches to MM
    // (MM checks randomizerState.obtainedItems on load)
    
    // 4. Update logic state
    UpdateAccessibleLocations();
}
```

**When MM player gets OOT item**:

```cpp
void CrossWorldRandomizer::GiveOOTItemInMM(Item ootItem) {
    // Same process, but in reverse
    MM_ShowCustomGetItem(ootItem.name, ootItem.description);
    randomizerState.obtainedItems.insert(ootItem.id);
    UpdateAccessibleLocations();
}
```

### Item Synchronization on Game Switch

```cpp
void CrossWorldRandomizer::SyncItemsToGame(GameID targetGame) {
    if (targetGame == GAME_OOT) {
        // Give all obtained OOT items to OOT save
        for (ItemID item : randomizerState.obtainedItems) {
            if (IsOOTItem(item)) {
                OOT_GiveItemToInventory(item);
            }
        }
    } else if (targetGame == GAME_MM) {
        // Give all obtained MM items to MM save
        for (ItemID item : randomizerState.obtainedItems) {
            if (IsMMItem(item)) {
                MM_GiveItemToInventory(item);
            }
        }
    }
}
```

## Location Handling

### Location Definitions

```cpp
// combo/randomizer/LocationDefinitions.h

enum class LocationID : uint32_t {
    // OOT Locations (0x0000 - 0x0FFF)
    OOT_DEKU_TREE_COMPASS_CHEST = 0x0001,
    OOT_DODONGOS_CAVERN_BOSS_HEART = 0x0002,
    // ... all OOT checks
    
    // MM Locations (0x1000 - 0x1FFF)
    MM_WOODFALL_TEMPLE_SMALL_KEY_CHEST = 0x1001,
    MM_DEKU_PALACE_BEAN_SELLER = 0x1002,
    // ... all MM checks
};

struct LocationDefinition {
    LocationID id;
    GameID game;
    std::string name;
    LocationType type;
    
    // Logic requirements (can reference items from either game!)
    std::vector<ItemID> requiredItems;
    std::function<bool()> accessLogic;
};
```

### Cross-Game Logic

```cpp
// Example: MM location that requires OOT item
LocationDefinition mmLocationRequiringOOTItem = {
    .id = MM_IKANA_CANYON_CHEST,
    .game = GAME_MM,
    .name = "Ikana Canyon Chest",
    .type = LOCATION_CHEST,
    .requiredItems = {
        MM_GORON_MASK,      // MM item
        OOT_HOOKSHOT        // OOT item!
    },
    .accessLogic = []() {
        return HasItem(MM_GORON_MASK) && 
               HasItem(OOT_HOOKSHOT);
    }
};
```

## Seed Generation

### Unified Item Pool

```cpp
void CrossWorldRandomizer::CreateItemPool() {
    std::vector<Item> itemPool;
    
    // Add all OOT progression items
    itemPool.push_back({OOT_KOKIRI_SWORD, GAME_OOT, PROGRESSION});
    itemPool.push_back({OOT_HOOKSHOT, GAME_OOT, PROGRESSION});
    // ... all OOT items
    
    // Add all MM progression items
    itemPool.push_back({MM_DEKU_MASK, GAME_MM, PROGRESSION});
    itemPool.push_back({MM_GORON_MASK, GAME_MM, PROGRESSION});
    // ... all MM items
    
    // Add junk items to fill remaining locations
    AddJunkItems(itemPool);
    
    return itemPool;
}
```

### Unified Location Pool

```cpp
void CrossWorldRandomizer::CreateLocationPool() {
    std::vector<Location> locationPool;
    
    // Add all OOT checks
    locationPool.push_back({OOT_DEKU_TREE_COMPASS_CHEST, GAME_OOT});
    // ... all OOT locations
    
    // Add all MM checks
    locationPool.push_back({MM_WOODFALL_TEMPLE_SMALL_KEY_CHEST, GAME_MM});
    // ... all MM locations
    
    return locationPool;
}
```

### Placement Algorithm

```cpp
void CrossWorldRandomizer::PlaceItems() {
    // Use assumed fill algorithm (standard for randomizers)
    
    std::vector<Item> itemPool = CreateItemPool();
    std::vector<Location> locationPool = CreateLocationPool();
    
    // Shuffle items
    std::shuffle(itemPool.begin(), itemPool.end(), rng);
    
    // Place items using logic
    for (Item item : itemPool) {
        // Find accessible location that can hold this item
        Location location = FindAccessibleLocation(item);
        
        // Place item
        placements[location.id] = item;
        
        // Update logic state
        assumedItems.insert(item.id);
        UpdateAccessibleLocations();
    }
}
```

## Save System

### Cross-World Save Data

```cpp
struct CrossWorldSave {
    // Randomizer state
    uint64_t seed;
    std::map<LocationID, ItemID> placements;
    std::set<LocationID> checkedLocations;
    std::set<ItemID> obtainedItems;
    
    // Individual game saves
    OOTSaveData ootSave;
    MMSaveData mmSave;
    
    // Metadata
    uint64_t playTime;
    GameID lastActiveGame;
};
```

### Save/Load

```cpp
void CrossWorldRandomizer::SaveState(const std::string& filename) {
    CrossWorldSave save;
    
    // Save randomizer state
    save.seed = currentSeed;
    save.placements = placements;
    save.checkedLocations = checkedLocations;
    save.obtainedItems = obtainedItems;
    
    // Save individual game states
    save.ootSave = ootAdapter->GetSaveData();
    save.mmSave = mmAdapter->GetSaveData();
    
    // Write to file
    WriteSaveFile(filename, save);
}

void CrossWorldRandomizer::LoadState(const std::string& filename) {
    CrossWorldSave save = ReadSaveFile(filename);
    
    // Restore randomizer state
    currentSeed = save.seed;
    placements = save.placements;
    checkedLocations = save.checkedLocations;
    obtainedItems = save.obtainedItems;
    
    // Restore individual game states
    ootAdapter->LoadSaveData(save.ootSave);
    mmAdapter->LoadSaveData(save.mmSave);
    
    // Sync items to current game
    SyncItemsToGame(save.lastActiveGame);
}
```

## Game Switching

### Why Players Switch Games

In a cross-world randomizer:
1. **Progression Blocked**: Can't progress in current game without items from other game
2. **Exploration**: Want to check locations in other game
3. **Preference**: Simply prefer playing one game over the other at the moment

### Switching Mechanism

```cpp
void CrossWorldRandomizer::SwitchToGame(GameID targetGame) {
    // 1. Save current game state
    if (currentGame == GAME_OOT) {
        ootAdapter->SaveState();
    } else {
        mmAdapter->SaveState();
    }
    
    // 2. Switch games (using transition system)
    transitionManager->SwitchGame(targetGame);
    
    // 3. Load target game state
    if (targetGame == GAME_OOT) {
        ootAdapter->LoadState();
        SyncItemsToGame(GAME_OOT);
    } else {
        mmAdapter->LoadState();
        SyncItemsToGame(GAME_MM);
    }
    
    // 4. Update current game
    currentGame = targetGame;
}
```

## UI/UX Considerations

### Item Get Messages

```cpp
void ShowCrossGameItemGet(Item item) {
    if (item.sourceGame != currentGame) {
        // Show special message for cross-game item
        std::string message = StringHelper::Sprintf(
            "You got %s!\nThis item is from %s!",
            item.name.c_str(),
            GetGameName(item.sourceGame).c_str()
        );
        ShowMessage(message);
    } else {
        // Normal item get message
        ShowNormalItemGet(item);
    }
}
```

### Location Tracker

```cpp
class LocationTracker {
public:
    // Show which locations are accessible in each game
    void ShowAccessibleLocations() {
        auto ootLocations = GetAccessibleLocations(GAME_OOT);
        auto mmLocations = GetAccessibleLocations(GAME_MM);
        
        ImGui::Text("OOT Accessible: %d", ootLocations.size());
        ImGui::Text("MM Accessible: %d", mmLocations.size());
        
        // Show detailed list
        for (auto loc : ootLocations) {
            ImGui::Text("  - %s", GetLocationName(loc).c_str());
        }
    }
    
    // Show which items are needed for progression
    void ShowRequiredItems() {
        auto requiredItems = GetRequiredItemsForProgression();
        
        ImGui::Text("Items needed:");
        for (auto item : requiredItems) {
            ImGui::Text("  - %s (%s)", 
                GetItemName(item).c_str(),
                GetGameName(GetItemGame(item)).c_str()
            );
        }
    }
};
```

### Game Switch Menu

```cpp
void ShowGameSwitchMenu() {
    ImGui::Begin("Switch Game");
    
    if (ImGui::Button("Switch to OOT")) {
        SwitchToGame(GAME_OOT);
    }
    
    if (ImGui::Button("Switch to MM")) {
        SwitchToGame(GAME_MM);
    }
    
    // Show accessible locations in each game
    ImGui::Separator();
    ImGui::Text("Accessible Locations:");
    ImGui::Text("  OOT: %d", GetAccessibleLocationCount(GAME_OOT));
    ImGui::Text("  MM: %d", GetAccessibleLocationCount(GAME_MM));
    
    ImGui::End();
}
```

## Example Gameplay Flow

```
1. Player starts in OOT
   - Opens chest in Deku Tree
   - Gets MM Deku Mask (cross-game item!)
   - Item stored in cross-world state

2. Player continues in OOT
   - Explores more locations
   - Gets stuck - needs Hookshot to progress
   - Hookshot is in MM somewhere

3. Player switches to MM
   - MM loads with Deku Mask already in inventory
   - Explores Woodfall Temple
   - Opens chest, gets OOT Hookshot!

4. Player switches back to OOT
   - OOT loads with Hookshot now in inventory
   - Can now progress past previous obstacle
   - Continues OOT dungeons

5. Repeat switching as needed to complete both games
```

## Integration with Existing Randomizers

### OOT Randomizer Integration

```cpp
// Wrap existing OOT randomizer
class OOTRandomizerWrapper {
public:
    void Initialize() {
        // Initialize existing OOT randomizer
        // But override item placement with cross-world placements
    }
    
    Item GetItemAtCheck(OOTLocationID location) {
        // Convert to global location ID
        LocationID globalLoc = ConvertToGlobalLocation(location);
        
        // Query cross-world randomizer
        return crossWorldRando->GetItemAtLocation(globalLoc);
    }
};
```

### MM Randomizer Integration

```cpp
// Similar wrapper for MM randomizer
class MMRandomizerWrapper {
    // Same pattern as OOT
};
```

## Summary

The cross-world randomizer architecture:

1. **Keeps games separate** - OOT and MM run independently
2. **Unified randomizer logic** - Single seed, unified item/location pools
3. **Cross-game item placement** - Items from either game can be in either game
4. **Minimal game modifications** - Small hooks to query randomizer
5. **Game switching** - Players switch between games to progress
6. **Shared state** - Randomizer tracks all items/locations across both games

This is fundamentally different from a unified game experience - it's about creating a **cross-world randomizer** where both games remain separate but share a randomizer seed and item pool.