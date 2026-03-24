# Implementation Roadmap - In-Game Switching

## Goal

Enable players to run OOT and MM separately but switch between them in-game whenever needed, with minimal modifications to the original game code.

## Current State ✅

You already have:
- ✅ Unified build system (both games compile together)
- ✅ Both games as separate DLLs (soh.dll and 2ship.dll)
- ✅ Shared libultraship infrastructure
- ✅ ComboShip executable that can load both games

## What You Need to Add

### Phase 1: Basic Game Switching (Core Functionality)

**Goal**: Be able to switch between OOT and MM at runtime

#### Step 1.1: Create Game Manager

**File**: `Combo/combo/core/GameManager.h/cpp`

```cpp
class GameManager {
public:
    enum class Game { OOT, MM };
    
    void Initialize();
    void SwitchToGame(Game targetGame);
    Game GetCurrentGame() const { return currentGame; }
    
    void Update(float deltaTime);
    void Render();
    
private:
    Game currentGame = Game::OOT;
    bool isTransitioning = false;
};
```

**Implementation**:
```cpp
void GameManager::SwitchToGame(Game targetGame) {
    if (targetGame == currentGame) return;
    
    // 1. Fade out current game
    StartFadeOut();
    
    // 2. Save current game state
    if (currentGame == Game::OOT) {
        SaveOOTState();
    } else {
        SaveMMState();
    }
    
    // 3. Load target game
    if (targetGame == Game::OOT) {
        LoadOOTState();
    } else {
        LoadMMState();
    }
    
    // 4. Fade in new game
    StartFadeIn();
    
    currentGame = targetGame;
}
```

#### Step 1.2: Add Switch Trigger

**Option A: Menu-Based** (Easiest to implement first)

Add to existing pause menu or create new menu:

```cpp
// In ComboShip.cpp or GUI code
void ShowGameSwitchMenu() {
    if (ImGui::BeginMenu("Switch Game")) {
        if (ImGui::MenuItem("Switch to OOT", "F1")) {
            gameManager->SwitchToGame(GameManager::Game::OOT);
        }
        if (ImGui::MenuItem("Switch to MM", "F2")) {
            gameManager->SwitchToGame(GameManager::Game::MM);
        }
        ImGui::EndMenu();
    }
}
```

**Option B: Hotkey** (Add later)

```cpp
// In input handling
if (IsKeyPressed(KEY_F1)) {
    gameManager->SwitchToGame(GameManager::Game::OOT);
}
if (IsKeyPressed(KEY_F2)) {
    gameManager->SwitchToGame(GameManager::Game::MM);
}
```

**Option C: In-Game Trigger** (Add much later)

```cpp
// Minimal hook in game code
#ifdef COMBO_BUILD
if (player->position == SPECIAL_WARP_POINT) {
    ComboHook_RequestGameSwitch(targetGame);
}
#endif
```

#### Step 1.3: Update ComboShip Main Loop

**File**: `Combo/combo/ComboShip.cpp`

```cpp
// Current main loop (simplified)
void MainLoop() {
    while (running) {
        // Currently runs one game
        UpdateGame();
        RenderGame();
    }
}

// New main loop with game manager
void MainLoop() {
    GameManager gameManager;
    gameManager.Initialize();
    
    while (running) {
        // Game manager handles which game is active
        gameManager.Update(deltaTime);
        gameManager.Render();
    }
}
```

### Phase 2: State Management

**Goal**: Preserve game state when switching

#### Step 2.1: Save/Load System

**File**: `Combo/combo/core/StateManager.h/cpp`

```cpp
class StateManager {
public:
    struct GameState {
        // Minimal state needed to resume
        uint8_t* saveData;      // Game's save data
        uint32_t saveDataSize;
        SceneID currentScene;
        Vector3 playerPosition;
        // Add more as needed
    };
    
    void SaveOOTState(GameState& state);
    void LoadOOTState(const GameState& state);
    
    void SaveMMState(GameState& state);
    void LoadMMState(const GameState& state);
};
```

#### Step 2.2: Integrate with Existing Save Systems

```cpp
void StateManager::SaveOOTState(GameState& state) {
    // Use existing OOT save system
    // Just capture current state
    state.saveData = GetOOTSaveData();
    state.saveDataSize = GetOOTSaveDataSize();
    state.currentScene = GetOOTCurrentScene();
    state.playerPosition = GetOOTPlayerPosition();
}
```

### Phase 3: Transition Effects (Polish)

**Goal**: Make switching feel smooth

#### Step 3.1: Simple Fade

```cpp
class TransitionEffect {
public:
    void StartFadeOut(float duration = 0.5f);
    void StartFadeIn(float duration = 0.5f);
    void Update(float deltaTime);
    void Render();
    
private:
    float fadeAlpha = 0.0f;
    bool isFading = false;
};
```

#### Step 3.2: Loading Screen (Optional)

```cpp
void ShowLoadingScreen(Game targetGame) {
    // Show simple loading screen during switch
    RenderLoadingScreen(GetGameName(targetGame));
}
```

## Minimal Implementation (MVP)

To get basic switching working quickly:

### Files to Create:

1. **`Combo/combo/core/GameManager.h`** (~50 lines)
   - Game enum
   - SwitchToGame() function
   - Update/Render functions

2. **`Combo/combo/core/GameManager.cpp`** (~200 lines)
   - Basic switching logic
   - Fade effects
   - State save/load calls

3. **`Combo/combo/core/StateManager.h`** (~30 lines)
   - GameState struct
   - Save/Load function declarations

4. **`Combo/combo/core/StateManager.cpp`** (~100 lines)
   - Save/Load implementations
   - Interface with existing save systems

### Files to Modify:

1. **`Combo/combo/ComboShip.cpp`** (~10 lines changed)
   - Create GameManager instance
   - Update main loop to use GameManager

2. **`Combo/CMakeLists.txt`** (~5 lines added)
   - Add new source files to build

### Total New Code: ~400 lines

## Testing Plan

### Test 1: Basic Switch
1. Start ComboShip
2. Play OOT for a bit
3. Press F1 to switch to MM
4. Verify MM loads
5. Press F2 to switch back to OOT
6. Verify OOT resumes where you left off

### Test 2: State Preservation
1. Start in OOT
2. Collect some items
3. Switch to MM
4. Switch back to OOT
5. Verify items are still there

### Test 3: Multiple Switches
1. Switch between games multiple times
2. Verify no crashes
3. Verify no memory leaks
4. Verify performance is acceptable

## Future Enhancements (After MVP)

### Phase 4: Cross-World Randomizer
- Add randomizer integration (see RANDOMIZER_ARCHITECTURE.md)
- Implement item placement across games
- Add logic checking

### Phase 5: Enhanced Transitions
- Custom transition animations
- In-game warp points
- Seamless scene transitions

### Phase 6: UI Improvements
- Better game switch menu
- Location tracker
- Item tracker
- Progress indicators

## Development Timeline Estimate

### Week 1: Core Switching
- Day 1-2: Create GameManager
- Day 3-4: Create StateManager
- Day 5: Integrate with ComboShip
- Day 6-7: Testing and bug fixes

### Week 2: Polish
- Day 1-2: Add fade effects
- Day 3-4: Improve state management
- Day 5: Add hotkeys
- Day 6-7: Testing and refinement

### Week 3+: Randomizer (Optional)
- Implement cross-world randomizer
- Add item placement logic
- Create UI for randomizer

## Key Principles

1. **Keep It Simple**: Start with basic menu-based switching
2. **Minimal Modifications**: Don't modify game code unless absolutely necessary
3. **Iterate**: Get basic switching working first, add features later
4. **Test Often**: Test after each major change
5. **Preserve Independence**: Games should still work separately

## Success Criteria

✅ Can switch from OOT to MM at any time  
✅ Can switch from MM to OOT at any time  
✅ Game state is preserved when switching  
✅ No crashes or memory leaks  
✅ Performance is acceptable (< 2 second switch time)  
✅ Both games remain independently updatable from upstream  

## Getting Started

1. Create the directory structure:
   ```
   Combo/combo/core/
   ```

2. Create GameManager.h with basic structure

3. Implement simple SwitchToGame() function

4. Add menu item to trigger switch

5. Test basic switching (even if state isn't preserved yet)

6. Iterate and improve

## Questions to Answer During Implementation

- **Q**: How much state needs to be preserved?
  - **A**: Start minimal (just save data), add more as needed

- **Q**: Should both games stay loaded in memory?
  - **A**: Start with unloading inactive game, optimize later if needed

- **Q**: How to handle audio during switch?
  - **A**: Simple fade out/in, improve later

- **Q**: What if player is in a cutscene?
  - **A**: Initially, disable switching during cutscenes

## Summary

The architecture is designed to let you:
- ✅ Run both games independently
- ✅ Switch between them in-game
- ✅ Preserve state when switching
- ✅ Keep games separate for upstream updates
- ✅ Add randomizer features later

Start with the MVP (basic switching), then iterate and add features as needed. The architecture documents provide the blueprint, this roadmap provides the step-by-step implementation path.