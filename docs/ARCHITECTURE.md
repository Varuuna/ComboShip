# ComboShip Architecture

## Overview

ComboShip is designed to create a unified OoTMM-style game experience that combines **Ocarina of Time** (OOT) and **Majora's Mask** (MM) into a single cohesive game, similar to the OoTMM ROM hack, while maintaining the ability to independently track and merge upstream changes from both Ship of Harkinian and 2 Ship 2 Harkinian projects.

## Core Design Philosophy

### Primary Goals

1. **Unified Game Experience**: Both games run together as one cohesive experience with cross-game progression, shared inventory, and seamless transitions
2. **Independent Upstream Tracking**: Maintain ability to pull updates from upstream OOT and MM projects with minimal merge conflicts
3. **Minimal Game Code Modification**: Keep original game code pristine with only small hook insertions
4. **Separation of Concerns**: All unified game logic lives in a dedicated layer, separate from individual game implementations

### Key Principle: **Composition Over Modification**

Rather than merging or heavily modifying game code, we wrap and coordinate the games through an adapter layer.

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    ComboShip Executable                      │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │         Unified Game State Manager                     │ │
│  │  • Combined save system                                │ │
│  │  • Cross-game progression tracking                     │ │
│  │  • Shared inventory management                         │ │
│  │  • Scene/world transition coordination                 │ │
│  └────────────────────────────────────────────────────────┘ │
│                          ↓         ↓                         │
│  ┌──────────────────────────┬──────────────────────────┐    │
│  │    OOT Adapter           │     MM Adapter           │    │
│  │  • Wraps OOT systems     │  • Wraps MM systems      │    │
│  │  • Translates events     │  • Translates events     │    │
│  │  • Manages OOT state     │  • Manages MM state      │    │
│  └──────────────────────────┴──────────────────────────┘    │
│                          ↓         ↓                         │
│  ┌──────────────────────────┬──────────────────────────┐    │
│  │    soh.dll (OOT)         │     2ship.dll (MM)       │    │
│  │  • Original game logic   │  • Original game logic   │    │
│  │  • Minimal hooks         │  • Minimal hooks         │    │
│  │  • Independent updates   │  • Independent updates   │    │
│  └──────────────────────────┴──────────────────────────┘    │
│                          ↓         ↓                         │
│  ┌────────────────────────────────────────────────────────┐ │
│  │              libultraship (Shared Engine)              │ │
│  │  • Graphics, Audio, Input, Resource Management        │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## Directory Structure

```
ComboShip/
├── Combo/                          # Root build directory
│   ├── ARCHITECTURE.md             # This file
│   ├── README.md                   # Build instructions
│   ├── CMakeLists.txt              # Unified build system
│   │
│   ├── combo/                      # NEW: Unified game layer
│   │   ├── ComboShip.cpp           # Main entry point
│   │   ├── ComboShip.h
│   │   │
│   │   ├── core/                   # Core unified systems
│   │   │   ├── UnifiedGameState.h
│   │   │   ├── UnifiedGameState.cpp
│   │   │   ├── GameAdapter.h       # Base adapter interface
│   │   │   ├── OOTAdapter.h
│   │   │   ├── OOTAdapter.cpp
│   │   │   ├── MMAdapter.h
│   │   │   └── MMAdapter.cpp
│   │   │
│   │   ├── systems/                # Cross-game systems
│   │   │   ├── SharedInventory.h
│   │   │   ├── SharedInventory.cpp
│   │   │   ├── ProgressionSync.h
│   │   │   ├── ProgressionSync.cpp
│   │   │   ├── TransitionManager.h
│   │   │   ├── TransitionManager.cpp
│   │   │   ├── CombinedSaveSystem.h
│   │   │   └── CombinedSaveSystem.cpp
│   │   │
│   │   ├── hooks/                  # Hook registration system
│   │   │   ├── GameHooks.h
│   │   │   ├── GameHooks.cpp
│   │   │   ├── OOTHooks.h
│   │   │   └── MMHooks.cpp
│   │   │
│   │   └── data/                   # Cross-game data mappings
│   │       ├── ItemMapping.h       # OOT <-> MM item conversions
│   │       ├── SceneMapping.h      # Scene/world connections
│   │       └── ProgressionFlags.h  # Shared progression tracking
│   │
│   ├── soh/                        # OOT - Minimal modifications
│   │   ├── src/                    # Original decomp (+ hooks)
│   │   ├── soh/                    # OOT enhancements
│   │   └── CMakeLists.txt
│   │
│   ├── mm/                         # MM - Minimal modifications
│   │   ├── src/                    # Original decomp (+ hooks)
│   │   ├── 2s2h/                   # MM enhancements
│   │   └── CMakeLists.txt
│   │
│   ├── libultraship/               # Shared engine
│   ├── ZAPDTR/                     # Asset tools
│   └── OTRExporter/                # Asset exporters
│
├── OOT/                            # Reference: Original OOT project
└── MM/                             # Reference: Original MM project
```

## Core Components

### 1. Unified Game State Manager

**Location**: `combo/core/UnifiedGameState.h/cpp`

**Responsibilities**:
- Maintains combined save data for both games
- Tracks which game is currently active
- Manages cross-game progression and shared state
- Coordinates game transitions

**Key Interfaces**:

```cpp
class UnifiedGameState {
public:
    // Game state
    enum class ActiveGame { OOT, MM, TRANSITION };
    ActiveGame GetActiveGame() const;
    
    // Game switching
    void TransitionToOOT(TransitionPoint point);
    void TransitionToMM(TransitionPoint point);
    
    // Shared progression
    void SyncSharedProgression();
    bool HasSharedItem(ItemID item);
    void GiveSharedItem(ItemID item);
    
    // Save management
    void SaveCombinedState();
    void LoadCombinedState();
    
    // Event handling
    void OnItemObtained(ItemID item, ActiveGame source);
    void OnFlagSet(FlagID flag, ActiveGame source);
    void OnSceneChange(SceneID scene, ActiveGame source);
};
```

### 2. Game Adapters (Bridge Pattern)

**Location**: `combo/core/OOTAdapter.h/cpp`, `combo/core/MMAdapter.h/cpp`

**Purpose**: Provide a unified interface to interact with each game while keeping game-specific logic isolated.

**Key Interfaces**:

```cpp
class IGameAdapter {
public:
    virtual ~IGameAdapter() = default;
    
    // Lifecycle
    virtual void Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void Render() = 0;
    
    // State management
    virtual void Suspend() = 0;      // Pause when switching games
    virtual void Resume() = 0;       // Resume when switching back
    
    // Save data
    virtual void* GetSaveData() = 0;
    virtual void LoadSaveData(void* data) = 0;
    
    // Item/progression
    virtual void GiveItem(ItemID item) = 0;
    virtual bool HasItem(ItemID item) = 0;
    virtual void SetFlag(FlagID flag) = 0;
    virtual bool GetFlag(FlagID flag) = 0;
    
    // Scene management
    virtual void LoadScene(SceneID scene) = 0;
    virtual SceneID GetCurrentScene() = 0;
};

class OOTAdapter : public IGameAdapter {
    // Implements interface for OOT-specific systems
};

class MMAdapter : public IGameAdapter {
    // Implements interface for MM-specific systems
};
```

### 3. Hook System

**Location**: `combo/hooks/GameHooks.h/cpp`

**Purpose**: Provide a clean interface for game code to notify the unified layer of events without tight coupling.

**Design**:

```cpp
class GameHooks {
public:
    // Hook registration (called during adapter initialization)
    using ItemCallback = std::function<void(ItemID)>;
    using FlagCallback = std::function<void(FlagID)>;
    using SceneCallback = std::function<void(SceneID)>;
    
    static void RegisterItemObtainHook(ItemCallback callback);
    static void RegisterFlagSetHook(FlagCallback callback);
    static void RegisterSceneChangeHook(SceneCallback callback);
    
    // Hook invocation (called from game code)
    static void OnItemObtained(ItemID item);
    static void OnFlagSet(FlagID flag);
    static void OnSceneChange(SceneID scene);
};
```

**Game Code Integration** (minimal changes):

```cpp
// In soh/src/code/z_player.c
void Player_GiveItem(Player* player, ItemID item) {
    // Original OOT code
    Inventory_AddItem(item);
    
    #ifdef COMBO_BUILD
    // Single line addition - notify unified layer
    ComboHook_OnItemObtained(item);
    #endif
}
```

### 4. Cross-Game Systems

**Location**: `combo/systems/`

#### Shared Inventory System

**Purpose**: Manage items that transfer between games.

```cpp
class SharedInventory {
public:
    // Item transfer logic
    void OnItemObtained(ItemID item, ActiveGame source);
    bool IsSharedItem(ItemID item);
    
    // Item conversion
    ItemID ConvertOOTItemToMM(ItemID ootItem);
    ItemID ConvertMMItemToOOT(ItemID mmItem);
};
```

#### Progression Sync System

**Purpose**: Track and synchronize progression flags across games.

```cpp
class ProgressionSync {
public:
    // Flag synchronization
    void OnFlagSet(FlagID flag, ActiveGame source);
    bool ShouldSyncFlag(FlagID flag);
    
    // Progression tracking
    void UpdateProgressionState();
    bool MeetsRequirements(ProgressionRequirement req);
};
```

#### Transition Manager

**Purpose**: Handle seamless transitions between OOT and MM worlds.

```cpp
class TransitionManager {
public:
    // Transition points
    struct TransitionPoint {
        ActiveGame targetGame;
        SceneID targetScene;
        Vector3 spawnPosition;
        uint16_t spawnRotation;
    };
    
    // Transition execution
    void ExecuteTransition(TransitionPoint point);
    void RegisterTransitionPoint(SceneID scene, TransitionPoint point);
};
```

#### Combined Save System

**Purpose**: Manage unified save files that contain both games' data.

```cpp
class CombinedSaveSystem {
public:
    struct CombinedSave {
        uint32_t version;
        uint32_t checksum;
        
        // Game-specific save data
        OOTSaveData ootData;
        MMSaveData mmData;
        
        // Shared progression
        SharedProgressionData shared;
        
        // Metadata
        ActiveGame lastActiveGame;
        uint64_t playTime;
    };
    
    void Save(const std::string& filename);
    bool Load(const std::string& filename);
};
```

## Implementation Strategy

### Phase 1: Foundation (Current State)

**Status**: ✅ Complete

- [x] Unified build system
- [x] Both games build as separate DLLs
- [x] Shared libultraship infrastructure
- [x] Basic ComboShip executable

### Phase 2: Adapter Layer

**Goal**: Create the bridge between unified logic and game-specific implementations.

**Tasks**:
1. Define `IGameAdapter` interface
2. Implement `OOTAdapter` wrapping soh.dll
3. Implement `MMAdapter` wrapping 2ship.dll
4. Create basic game switching mechanism
5. Test independent game execution through adapters

**Files to Create**:
- `combo/core/GameAdapter.h`
- `combo/core/OOTAdapter.h/cpp`
- `combo/core/MMAdapter.h/cpp`

### Phase 3: Hook System

**Goal**: Enable game code to communicate with unified layer without tight coupling.

**Tasks**:
1. Design hook registration system
2. Implement hook callbacks in adapters
3. Add minimal `#ifdef COMBO_BUILD` hooks to game code
4. Test hook invocation and event propagation

**Files to Create**:
- `combo/hooks/GameHooks.h/cpp`
- `combo/hooks/OOTHooks.h/cpp`
- `combo/hooks/MMHooks.h/cpp`

**Game Code Changes** (examples):
- `soh/src/code/z_player.c`: Add item obtain hooks
- `soh/src/code/z_scene.c`: Add scene change hooks
- `mm/src/code/z_player.c`: Add item obtain hooks
- `mm/src/code/z_scene.c`: Add scene change hooks

### Phase 4: Unified Game State

**Goal**: Create central state management for combined game experience.

**Tasks**:
1. Design combined save data structure
2. Implement `UnifiedGameState` class
3. Create game switching logic
4. Implement state synchronization
5. Test state persistence across game switches

**Files to Create**:
- `combo/core/UnifiedGameState.h/cpp`
- `combo/systems/CombinedSaveSystem.h/cpp`

### Phase 5: Cross-Game Systems

**Goal**: Implement specific cross-game features.

**Tasks**:
1. **Shared Inventory**:
   - Define which items transfer between games
   - Implement item conversion mappings
   - Create item synchronization logic

2. **Progression Sync**:
   - Define shared progression flags
   - Implement flag synchronization
   - Create progression requirement system

3. **Transition System**:
   - Define transition points between games
   - Implement scene loading across games
   - Create seamless transition effects

**Files to Create**:
- `combo/systems/SharedInventory.h/cpp`
- `combo/systems/ProgressionSync.h/cpp`
- `combo/systems/TransitionManager.h/cpp`
- `combo/data/ItemMapping.h`
- `combo/data/SceneMapping.h`
- `combo/data/ProgressionFlags.h`

### Phase 6: Polish & Integration

**Goal**: Refine the unified experience.

**Tasks**:
1. Optimize game switching performance
2. Add transition animations/effects
3. Implement combined UI elements
4. Create unified settings/options
5. Extensive testing and bug fixing

## Design Patterns Used

### 1. Bridge Pattern (Adapters)

**Purpose**: Decouple abstraction (unified game logic) from implementation (specific game code).

**Benefits**:
- Games can be updated independently
- Unified logic doesn't depend on game internals
- Easy to add new games in the future

### 2. Observer Pattern (Hooks)

**Purpose**: Allow game code to notify unified layer of events without direct dependencies.

**Benefits**:
- Minimal coupling between layers
- Easy to add new event types
- Game code changes are minimal

### 3. Facade Pattern (Unified Game State)

**Purpose**: Provide simplified interface to complex subsystems.

**Benefits**:
- Single point of access for cross-game operations
- Hides complexity of dual-game coordination
- Easier to reason about system behavior

### 4. Strategy Pattern (Cross-Game Systems)

**Purpose**: Encapsulate different algorithms for cross-game features.

**Benefits**:
- Easy to modify behavior without changing core logic
- Can swap implementations for different game modes
- Testable in isolation

## Handling Upstream Updates

### Merge Strategy

When pulling updates from upstream Ship of Harkinian or 2 Ship 2 Harkinian:

1. **Game Code Updates** (`soh/src/` or `mm/src/`):
   - Most changes will merge cleanly
   - Only conflicts will be at hook insertion points
   - Hooks are small and clearly marked with `#ifdef COMBO_BUILD`

2. **Enhancement Updates** (`soh/soh/` or `mm/2s2h/`):
   - Should merge cleanly as they don't interact with combo layer
   - May need to update adapters if APIs change

3. **Build System Updates**:
   - May require adjustments to `Combo/CMakeLists.txt`
   - Keep game-specific CMakeLists mostly unchanged

### Conflict Resolution Guidelines

**For Hook Conflicts**:
```cpp
// If upstream changes code around a hook:
<<<<<<< HEAD (upstream)
void NewFunction() {
    DoSomething();
}
=======
void OldFunction() {
    DoSomething();
    #ifdef COMBO_BUILD
    ComboHook_OnEvent();
    #endif
}
>>>>>>> combo

// Resolution: Keep upstream code, re-add hook
void NewFunction() {
    DoSomething();
    #ifdef COMBO_BUILD
    ComboHook_OnEvent();
    #endif
}
```

**For API Changes**:
- Update adapters to match new game APIs
- Keep hook system stable
- Add compatibility layer if needed

## Build System Integration

### CMake Configuration

```cmake
# Combo/CMakeLists.txt

# Option to enable combo mode
option(BUILD_COMBO_MODE "Build unified OoTMM-style game" ON)

if(BUILD_COMBO_MODE)
    # Define combo build flag
    add_compile_definitions(COMBO_BUILD)
    
    # Build unified executable
    add_executable(ComboShip
        combo/ComboShip.cpp
        combo/core/UnifiedGameState.cpp
        combo/core/OOTAdapter.cpp
        combo/core/MMAdapter.cpp
        combo/hooks/GameHooks.cpp
        combo/systems/SharedInventory.cpp
        combo/systems/ProgressionSync.cpp
        combo/systems/TransitionManager.cpp
        combo/systems/CombinedSaveSystem.cpp
    )
    
    # Link both game DLLs
    target_link_libraries(ComboShip PRIVATE 
        soh 
        2ship 
        libultraship
    )
    
    # Include paths
    target_include_directories(ComboShip PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/combo
        ${CMAKE_CURRENT_SOURCE_DIR}/soh/include
        ${CMAKE_CURRENT_SOURCE_DIR}/mm/include
    )
else()
    # Build games independently (original behavior)
    add_executable(soh-standalone ...)
    add_executable(2ship-standalone ...)
endif()
```

### Conditional Compilation

**In Game Code**:
```cpp
// soh/src/code/z_player.c
#ifdef COMBO_BUILD
#include "combo/hooks/GameHooks.h"
#endif

void Player_GiveItem(Player* player, ItemID item) {
    // Original game logic
    Inventory_AddItem(item);
    
    #ifdef COMBO_BUILD
    // Notify combo layer
    ComboHook_OnItemObtained(item);
    #endif
}
```

**In Combo Code**:
```cpp
// combo/core/OOTAdapter.cpp
// Always compiled with COMBO_BUILD defined
#include "soh/include/z64.h"
#include "GameAdapter.h"

void OOTAdapter::Initialize() {
    // Register hooks
    GameHooks::RegisterItemObtainHook([this](ItemID item) {
        OnItemObtained(item);
    });
}
```

## Testing Strategy

### Unit Tests

**Adapter Tests**:
- Test adapter initialization
- Test state management (suspend/resume)
- Test item/flag operations
- Test scene loading

**Hook Tests**:
- Test hook registration
- Test hook invocation
- Test callback execution
- Test error handling

**State Management Tests**:
- Test save/load operations
- Test state synchronization
- Test game switching
- Test data integrity

### Integration Tests

**Cross-Game Tests**:
- Test item transfer between games
- Test progression synchronization
- Test scene transitions
- Test combined save system

**Performance Tests**:
- Test game switching latency
- Test memory usage
- Test save/load performance
- Test hook overhead

### Manual Testing Scenarios

1. **Basic Functionality**:
   - Start in OOT, obtain item, switch to MM, verify item transferred
   - Set flag in MM, switch to OOT, verify flag synchronized
   - Save in OOT, load in MM, verify state preserved

2. **Edge Cases**:
   - Switch games during cutscene
   - Switch games during item obtain animation
   - Switch games during scene transition
   - Multiple rapid game switches

3. **Progression Testing**:
   - Complete dungeon in OOT, verify MM progression updated
   - Obtain key item in MM, verify OOT access granted
   - Test all transition points between games

## Performance Considerations

### Memory Management

**Challenge**: Running two games simultaneously requires careful memory management.

**Solutions**:
1. **Lazy Loading**: Only load active game's assets
2. **Shared Resources**: Use libultraship's resource manager for common assets
3. **State Suspension**: Unload inactive game's dynamic data
4. **Memory Pools**: Pre-allocate memory for game switching

### Game Switching Optimization

**Target**: < 1 second transition time

**Techniques**:
1. **Async Loading**: Load target game assets while showing transition
2. **State Caching**: Keep minimal state of inactive game in memory
3. **Progressive Loading**: Load critical assets first, defer non-critical
4. **Transition Masking**: Use visual effects to hide loading

### Hook Overhead

**Goal**: Minimize performance impact of hook system

**Optimizations**:
1. **Inline Hooks**: Use inline functions for hot paths
2. **Conditional Compilation**: Hooks only active in combo build
3. **Batch Notifications**: Group multiple events when possible
4. **Lock-Free Design**: Avoid synchronization in hook callbacks

## Security & Stability

### Save Data Integrity

**Measures**:
1. **Checksums**: Validate save data on load
2. **Versioning**: Handle save format changes gracefully
3. **Backups**: Auto-backup before major operations
4. **Validation**: Verify data consistency before save

### Error Handling

**Strategy**:
1. **Graceful Degradation**: Fall back to single-game mode on errors
2. **Error Logging**: Comprehensive logging for debugging
3. **State Recovery**: Ability to recover from corrupted state
4. **User Notification**: Clear error messages for users

## Future Enhancements

### Potential Features

1. **Randomizer Integration**:
   - Cross-game randomizer logic
   - Unified seed generation
   - Combined logic rules

2. **Multiplayer Support**:
   - Synchronized cross-game co-op
   - Shared world state
   - Player-to-player item trading

3. **Additional Games**:
   - Framework designed to support more games
   - Adapter pattern makes this straightforward
   - Could add other N64 Zelda titles

4. **Enhanced Transitions**:
   - Custom transition animations
   - Lore-friendly connection points
   - Dynamic world state based on progression

5. **Unified Enhancements**:
   - Cross-game quality of life features
   - Combined achievement system
   - Unified mod support

## Conclusion

This architecture provides a robust foundation for creating a unified OoTMM-style experience while maintaining the ability to independently track upstream changes. The key principles are:

1. **Separation of Concerns**: Unified logic separate from game code
2. **Minimal Modification**: Small, well-defined hooks in game code
3. **Composition Over Inheritance**: Wrap games rather than modify them
4. **Stable Interfaces**: Hook system provides consistent API
5. **Independent Updates**: Games can be updated with minimal conflicts

By following this architecture, ComboShip can deliver a seamless combined game experience while preserving the ability to benefit from ongoing development of both Ship of Harkinian and 2 Ship 2 Harkinian projects.

---

**Document Version**: 1.0  
**Last Updated**: 2025-11-05  
**Status**: Design Phase