# Seamless Transition System - Technical Design

## The Challenge

How do we create seamless transitions between OOT and MM when they are two separate game DLLs with independent game loops, memory spaces, and rendering systems?

## The Solution: Coordinated State Transfer

The key insight is that we don't need both games running simultaneously during gameplay - we only need to make the **switch appear seamless** to the player. This is achieved through careful state management and transition masking.

## High-Level Flow

```
Player triggers transition (e.g., enters special door in OOT)
    ↓
1. Capture current game state
    ↓
2. Show transition effect (fade, warp, etc.)
    ↓
3. Suspend current game
    ↓
4. Load target game with prepared state
    ↓
5. Resume with transition effect
    ↓
Player continues in new game
```

## Detailed Technical Implementation

### Phase 1: Transition Trigger

**In Game Code** (minimal hook):

```cpp
// soh/src/code/z_player.c
void Player_EnterDoor(Player* player, DoorActor* door) {
    #ifdef COMBO_BUILD
    // Check if this is a cross-game transition door
    if (ComboHook_IsTransitionDoor(door->doorId)) {
        ComboHook_RequestTransition(door->doorId);
        return; // Don't execute normal door logic
    }
    #endif
    
    // Normal OOT door logic
    Door_Enter(player, door);
}
```

**In Combo Layer**:

```cpp
// combo/systems/TransitionManager.cpp
void TransitionManager::OnTransitionRequested(DoorID doorId) {
    // Look up transition configuration
    TransitionConfig config = GetTransitionConfig(doorId);
    
    // Start transition sequence
    BeginTransition(config);
}
```

### Phase 2: State Capture

**Capture Current Game State**:

```cpp
struct CapturedGameState {
    // Player state
    Vector3 position;
    Vector3 velocity;
    uint16_t rotation;
    int16_t health;
    int16_t magic;
    
    // Inventory (already synced via SharedInventory)
    // Flags (already synced via ProgressionSync)
    
    // Temporary state
    uint32_t rupees;
    uint8_t hearts;
    uint8_t magicLevel;
    
    // Camera state
    Vector3 cameraPosition;
    Vector3 cameraTarget;
    
    // Time/environment
    uint16_t timeOfDay;
    uint8_t weatherState;
};

CapturedGameState TransitionManager::CaptureState(IGameAdapter* adapter) {
    CapturedGameState state;
    
    // Get player data from active game
    state.position = adapter->GetPlayerPosition();
    state.rotation = adapter->GetPlayerRotation();
    state.health = adapter->GetPlayerHealth();
    // ... capture all relevant state
    
    return state;
}
```

### Phase 3: Transition Effect (Masking)

**Purpose**: Hide the game switch from the player

**Techniques**:

1. **Fade to Black**:
```cpp
void TransitionManager::ShowTransitionEffect(TransitionType type) {
    switch(type) {
        case TransitionType::FADE:
            // Render full-screen fade overlay
            StartFadeOut(0.5f); // 500ms fade
            break;
            
        case TransitionType::WARP:
            // Show warp effect (like OOT's blue warp)
            StartWarpEffect();
            break;
            
        case TransitionType::DOOR:
            // Show door opening animation
            StartDoorTransition();
            break;
    }
}
```

2. **Loading Screen** (if needed):
```cpp
void TransitionManager::ShowLoadingScreen() {
    // Render loading screen with game-appropriate art
    // Can show tips, lore, or progress indicator
    RenderLoadingScreen(targetGame);
}
```

### Phase 4: Game Suspension

**Suspend Current Game**:

```cpp
void TransitionManager::SuspendCurrentGame() {
    IGameAdapter* currentAdapter = GetActiveAdapter();
    
    // 1. Stop game loop
    currentAdapter->Suspend();
    
    // 2. Save current state to unified save
    UnifiedGameState::Instance->SaveCurrentGameState();
    
    // 3. Unload non-essential resources
    currentAdapter->UnloadDynamicAssets();
    
    // 4. Keep minimal state in memory for quick resume
    currentAdapter->CacheMinimalState();
}
```

**What Suspend Does**:

```cpp
// combo/core/OOTAdapter.cpp
void OOTAdapter::Suspend() {
    // 1. Pause game logic
    gPauseGameLoop = true;
    
    // 2. Stop audio
    Audio_StopAllSounds();
    Audio_PauseMusic();
    
    // 3. Clear frame buffers
    Gfx_ClearBuffers();
    
    // 4. Unload scene-specific data
    Scene_UnloadDynamic();
    
    // 5. Keep player/inventory data in memory
    // (already managed by UnifiedGameState)
}
```

### Phase 5: Target Game Loading

**Load Target Game**:

```cpp
void TransitionManager::LoadTargetGame(TransitionConfig config) {
    IGameAdapter* targetAdapter = GetTargetAdapter(config.targetGame);
    
    // 1. Initialize if not already loaded
    if (!targetAdapter->IsInitialized()) {
        targetAdapter->Initialize();
    }
    
    // 2. Load target scene
    targetAdapter->LoadScene(config.targetScene);
    
    // 3. Apply captured state
    ApplyStateToTarget(targetAdapter, config);
    
    // 4. Position player at spawn point
    targetAdapter->SetPlayerPosition(config.spawnPosition);
    targetAdapter->SetPlayerRotation(config.spawnRotation);
    
    // 5. Resume game loop
    targetAdapter->Resume();
}
```

**State Application**:

```cpp
void TransitionManager::ApplyStateToTarget(
    IGameAdapter* target, 
    TransitionConfig config
) {
    // Transfer captured state to target game
    CapturedGameState state = capturedState;
    
    // Apply player state
    target->SetPlayerHealth(state.health);
    target->SetPlayerMagic(state.magic);
    
    // Inventory/flags already synced via SharedInventory/ProgressionSync
    
    // Apply environment state (if applicable)
    if (config.transferTimeOfDay) {
        target->SetTimeOfDay(ConvertTimeOfDay(state.timeOfDay));
    }
    
    // Apply camera state
    if (config.transferCamera) {
        target->SetCameraPosition(state.cameraPosition);
        target->SetCameraTarget(state.cameraTarget);
    }
}
```

### Phase 6: Resume with Effect

**Complete Transition**:

```cpp
void TransitionManager::CompleteTransition() {
    // 1. Target game is now running
    SetActiveGame(targetGame);
    
    // 2. Fade in from transition effect
    StartFadeIn(0.5f);
    
    // 3. Resume audio
    Audio_ResumeMusic();
    
    // 4. Transition complete
    isTransitioning = false;
    
    // 5. Notify systems
    UnifiedGameState::Instance->OnTransitionComplete();
}
```

## Memory Management During Transitions

### Strategy: Hot-Swap with Minimal Overlap

```cpp
class TransitionMemoryManager {
public:
    void PrepareTransition() {
        // 1. Allocate transition buffer (for captured state)
        transitionBuffer = AllocateTransitionBuffer();
        
        // 2. Pre-load critical target assets (async)
        PreloadCriticalAssets(targetGame, targetScene);
    }
    
    void ExecuteSwap() {
        // 3. Unload source game dynamic data
        sourceAdapter->UnloadDynamicAssets();
        
        // 4. Load target game data
        targetAdapter->LoadSceneAssets(targetScene);
        
        // 5. Free transition buffer
        FreeTransitionBuffer();
    }
};
```

**Memory Timeline**:

```
Before Transition:
├─ OOT Game Data: 200MB
├─ Shared Resources: 50MB
└─ Available: 750MB

During Transition:
├─ OOT Minimal State: 50MB (suspended)
├─ Transition Buffer: 10MB
├─ MM Loading: 150MB (growing)
├─ Shared Resources: 50MB
└─ Available: 740MB

After Transition:
├─ MM Game Data: 200MB
├─ Shared Resources: 50MB
└─ Available: 750MB
```

## Rendering During Transitions

### Double-Buffered Approach

```cpp
void TransitionManager::RenderTransition() {
    // Frame N: Last frame of source game
    sourceAdapter->Render();
    ApplyTransitionEffect(fadeAlpha);
    
    // Frame N+1 to N+X: Transition effect only
    while (isLoading) {
        RenderTransitionEffect();
        UpdateLoadingProgress();
    }
    
    // Frame N+X+1: First frame of target game
    targetAdapter->Render();
    ApplyTransitionEffect(1.0f - fadeAlpha);
}
```

### Seamless Visual Continuity

**Technique 1: Matching Transition Points**

```cpp
struct TransitionPoint {
    // Source game exit
    SceneID sourceScene;
    Vector3 sourcePosition;
    
    // Target game entrance
    SceneID targetScene;
    Vector3 targetPosition;
    
    // Visual continuity
    TransitionType visualType;  // DOOR, WARP, FADE, etc.
    Color transitionColor;      // Match game aesthetics
    float duration;             // Transition length
};

// Example: OOT Temple of Time <-> MM Clock Tower
TransitionPoint templeToClockTower = {
    .sourceScene = OOT_TEMPLE_OF_TIME,
    .sourcePosition = {0, 0, 100},  // In front of Door of Time
    
    .targetScene = MM_CLOCK_TOWER,
    .targetPosition = {0, 0, -100}, // Inside Clock Tower
    
    .visualType = TransitionType::WARP,
    .transitionColor = {0, 100, 255, 255}, // Blue warp
    .duration = 2.0f
};
```

**Technique 2: Matching Visual Styles**

```cpp
void TransitionManager::ConfigureTransitionEffect(
    ActiveGame source, 
    ActiveGame target
) {
    // Match transition to source game's visual style
    if (source == ActiveGame::OOT) {
        // Use OOT-style warp effect
        effect = CreateOOTWarpEffect();
    } else {
        // Use MM-style warp effect
        effect = CreateMMWarpEffect();
    }
    
    // Blend to target game's style during transition
    effect.SetBlendTarget(target);
}
```

## Audio Continuity

### Cross-Fade Music

```cpp
void TransitionManager::HandleAudioTransition() {
    // 1. Fade out source game music
    Audio_FadeOutMusic(0.5f);
    
    // 2. Play transition sound effect
    Audio_PlayTransitionSFX(transitionType);
    
    // 3. Fade in target game music
    Audio_FadeInMusic(targetScene.musicId, 0.5f);
}
```

### Ambient Sound Continuity

```cpp
void TransitionManager::TransferAmbientState() {
    // If transitioning between similar environments
    if (IsEnvironmentSimilar(sourceScene, targetScene)) {
        // Keep ambient sounds playing
        Audio_TransferAmbientSounds();
    } else {
        // Cross-fade to new ambient sounds
        Audio_CrossFadeAmbient(0.5f);
    }
}
```

## Performance Optimization

### Async Loading

```cpp
class AsyncTransitionLoader {
public:
    void StartAsyncLoad(TransitionConfig config) {
        // Start loading target scene on background thread
        loadThread = std::thread([this, config]() {
            targetAdapter->PreloadScene(config.targetScene);
            targetAdapter->PreloadAssets(config.requiredAssets);
            loadComplete = true;
        });
    }
    
    bool IsLoadComplete() {
        return loadComplete;
    }
    
    void WaitForLoad() {
        if (loadThread.joinable()) {
            loadThread.join();
        }
    }
};
```

### Progressive Loading

```cpp
void TransitionManager::ProgressiveLoad() {
    // Load in priority order
    
    // Priority 1: Player and immediate surroundings
    targetAdapter->LoadPlayerAssets();
    targetAdapter->LoadNearbyGeometry();
    
    // Can start rendering here with low-detail
    
    // Priority 2: Scene geometry and textures
    targetAdapter->LoadSceneGeometry();
    targetAdapter->LoadSceneTextures();
    
    // Priority 3: Actors and effects
    targetAdapter->LoadSceneActors();
    targetAdapter->LoadEffects();
    
    // Priority 4: Background elements
    targetAdapter->LoadBackgroundElements();
}
```

### Caching Strategy

```cpp
class TransitionCache {
private:
    // Keep recently used scenes in memory
    std::map<SceneID, CachedSceneData> sceneCache;
    
public:
    void CacheScene(SceneID scene) {
        if (sceneCache.size() >= MAX_CACHED_SCENES) {
            EvictLeastRecentlyUsed();
        }
        sceneCache[scene] = LoadSceneData(scene);
    }
    
    bool IsSceneCached(SceneID scene) {
        return sceneCache.contains(scene);
    }
    
    void LoadFromCache(SceneID scene) {
        // Much faster than loading from disk
        targetAdapter->LoadCachedScene(sceneCache[scene]);
    }
};
```

## Transition Types

### 1. Door Transition

```cpp
void TransitionManager::ExecuteDoorTransition(TransitionConfig config) {
    // 1. Play door opening animation (source game)
    sourceAdapter->PlayDoorAnimation(config.doorId);
    
    // 2. Fade to white/black as player enters
    StartFadeOut(0.3f);
    
    // 3. Switch games during fade
    SuspendCurrentGame();
    LoadTargetGame(config);
    
    // 4. Fade in from white/black (target game)
    StartFadeIn(0.3f);
    
    // 5. Play door closing animation (target game)
    targetAdapter->PlayDoorAnimation(config.targetDoorId);
}
```

### 2. Warp Transition

```cpp
void TransitionManager::ExecuteWarpTransition(TransitionConfig config) {
    // 1. Play warp effect (source game)
    sourceAdapter->PlayWarpEffect(config.warpType);
    
    // 2. Warp animation (1-2 seconds)
    RenderWarpAnimation(config.warpColor);
    
    // 3. Switch games during warp
    SuspendCurrentGame();
    LoadTargetGame(config);
    
    // 4. Complete warp in target game
    targetAdapter->CompleteWarpEffect();
}
```

### 3. Cutscene Transition

```cpp
void TransitionManager::ExecuteCutsceneTransition(TransitionConfig config) {
    // 1. Play transition cutscene (source game)
    sourceAdapter->PlayCutscene(config.transitionCutscene);
    
    // 2. At cutscene midpoint, switch games
    // (during a camera pan or fade)
    SuspendCurrentGame();
    LoadTargetGame(config);
    
    // 3. Continue cutscene in target game
    targetAdapter->PlayCutscene(config.arrivalCutscene);
}
```

### 4. Instant Transition (for testing/debugging)

```cpp
void TransitionManager::ExecuteInstantTransition(TransitionConfig config) {
    // No visual effect - immediate switch
    SuspendCurrentGame();
    LoadTargetGame(config);
    // Useful for debugging transition logic
}
```

## Error Handling

### Graceful Degradation

```cpp
void TransitionManager::HandleTransitionError(TransitionError error) {
    switch(error) {
        case TransitionError::LOAD_FAILED:
            // Revert to source game
            CancelTransition();
            ShowErrorMessage("Failed to load target scene");
            break;
            
        case TransitionError::STATE_CORRUPT:
            // Try to recover state
            if (RecoverState()) {
                RetryTransition();
            } else {
                CancelTransition();
            }
            break;
            
        case TransitionError::MEMORY_INSUFFICIENT:
            // Free more memory and retry
            FreeNonEssentialAssets();
            RetryTransition();
            break;
    }
}
```

### State Validation

```cpp
bool TransitionManager::ValidateTransition(TransitionConfig config) {
    // Check if transition is valid
    if (!IsTransitionPointValid(config.sourcePoint)) {
        return false;
    }
    
    if (!IsTransitionPointValid(config.targetPoint)) {
        return false;
    }
    
    // Check if player meets requirements
    if (!MeetsTransitionRequirements(config.requirements)) {
        return false;
    }
    
    // Check if target scene is available
    if (!IsSceneAvailable(config.targetScene)) {
        return false;
    }
    
    return true;
}
```

## Example: Complete Transition Flow

```cpp
// Player enters special door in OOT Temple of Time
void Example_TempleToClockTower() {
    // 1. Trigger detected
    TransitionConfig config = {
        .sourceGame = ActiveGame::OOT,
        .sourceScene = OOT_TEMPLE_OF_TIME,
        .sourcePosition = {0, 0, 100},
        
        .targetGame = ActiveGame::MM,
        .targetScene = MM_CLOCK_TOWER,
        .targetPosition = {0, 0, -100},
        .spawnRotation = 0x8000, // Face opposite direction
        
        .transitionType = TransitionType::WARP,
        .transitionColor = {0, 100, 255, 255},
        .duration = 2.0f
    };
    
    // 2. Validate transition
    if (!ValidateTransition(config)) {
        return;
    }
    
    // 3. Capture current state
    CapturedGameState state = CaptureState(ootAdapter);
    
    // 4. Start transition effect
    ShowTransitionEffect(TransitionType::WARP);
    
    // 5. Async load target scene
    asyncLoader.StartAsyncLoad(config);
    
    // 6. Suspend OOT
    ootAdapter->Suspend();
    
    // 7. Wait for load (hidden by transition effect)
    asyncLoader.WaitForLoad();
    
    // 8. Load MM with prepared state
    mmAdapter->LoadScene(config.targetScene);
    ApplyStateToTarget(mmAdapter, config);
    
    // 9. Resume MM
    mmAdapter->Resume();
    SetActiveGame(ActiveGame::MM);
    
    // 10. Complete transition effect
    CompleteTransition();
    
    // Player is now in MM Clock Tower!
}
```

## Performance Targets

- **Transition Duration**: 1-3 seconds (depending on type)
- **Loading Time**: < 2 seconds (with async loading)
- **Memory Overhead**: < 50MB during transition
- **Frame Rate**: Maintain 60 FPS during transition effects
- **Audio Latency**: < 100ms for music cross-fade

## Summary

Seamless transitions between separate game DLLs are achieved through:

1. **State Capture**: Save current game state before switching
2. **Visual Masking**: Use transition effects to hide the switch
3. **Coordinated Loading**: Suspend source, load target efficiently
4. **State Transfer**: Apply captured state to target game
5. **Smooth Resume**: Complete transition with matching effects

The key is that players never see both games simultaneously - they only see carefully orchestrated transitions that make the switch feel seamless. The separate DLLs are an implementation detail hidden by the transition system.