# Character Spawn & Bot Visibility Testing Guide

This guide describes the step-by-step procedure for verifying multiplayer character spawning and remote bot visibility in Mirror's Edge. Use this process to verify that each character model loads, attaches correctly, receives bone updates, and does not trigger crashes.

**AI agents:** Before diagnosing "Set Gameplay stuck" or empty spawn drains, read **[mp-set-gameplay-runbook.md](mp-set-gameplay-runbook.md)** (verified 2026-07-18: activation works; `listSize=0` means no remotes).

## Character IDs Reference

The `Engine::Character` enum (defined in [`runtime/engine/engine.h`](../runtime/engine/engine.h)) maps to the following numeric IDs and visual models:

| ID | Name | Skeletal Mesh Path | Remapping Notes |
|----|------|--------------------|-----------------|
| **0** | **Faith** | `CH_TKY_Crim_Fixer.SK_TKY_Crim_Fixer` | **Remapped to Kate (1)** on spawn to avoid transient material crashes. |
| **1** | **Kate** | `CH_TKY_Cop_Patrol_Female.SK_TKY_Cop_Patrol_Female` | Baseline multiplayer model. Z-pivot offset applied (+94). |
| **2** | **Celeste** | `CH_Celeste.SK_Celeste` | Standard Celeste. |
| **3** | **Assault Celeste** | `CH_TKY_Cop_Pursuit_Female.SK_TKY_Cop_Pursuit_Female` | Pursuit Celeste. |
| **4** | **Jacknife** | `CH_TKY_Crim_Jacknife.SK_TKY_Crim_Jacknife` | Jacknife. |
| **5** | **Miller** | `CH_Miller.SK_Miller` | Miller. Z-pivot offset applied (+94). |
| **6** | **Kreeg** | `CH_Kreeg.SK_Kreeg` | Kreeg. Z-pivot offset applied (+94). |
| **7** | **Pursuit Cop** | `CH_TKY_Cop_Pursuit.SK_TKY_Cop_Pursuit` | Pursuit Cop. |
| **8** | **Ghost** | `TT_Ghost.GhostCharacter_01` | Time Trial Ghost. |

---

## Step-by-Step Testing Procedure

### 1. Build and Deploy
Ensure you are using the latest compiled binaries. Close the game if it is running, then run:
```powershell
.\build.ps1 -DeployProxy
```
This builds all modules (including `engine.dll` and `multiplayer.dll`) and deploys the D3D9 proxy to the game directory.

### 2. Start the Game
Start the game through the launcher (this triggers config integrity bypass):
```powershell
.\ModuleLauncher.bat
```
Click **Launch Game** in the launcher UI. Wait for the game to reach the main menu.

### 3. Load Into a Level
Start a new game/campaign or load a save to enter gameplay (e.g. `tutorial_p`).

### 4. Enable Modules and Connect
1. Press `Insert` or `F10` to open the Module Manager overlay.
2. Go to the **Modules** tab.
3. Inject the `core` module (which loads `engine.dll`).
4. Inject the `multiplayer` module.
5. In the new **Multiplayer** tab:
   - Configure a Name (e.g., `HarnessPlayer`).
   - Configure Server IP: `127.0.0.1`.
   - Configure Room: `playthrough-lobby`.
   - Click **Set Gameplay**. This puts your client in gameplay mode on the server.
6. Close the overlay by pressing `Insert` again.

### 5. Launch a Character Bot
Open a PowerShell terminal and use the helper script to launch a bot with the desired character ID:
```powershell
# Spawn Celeste (ID 2)
.\temp-test-character.ps1 -CharacterId 2
```
This script will automatically start `multiplayer-server.exe` if it's not running, terminate any stale bots with the same name, and launch a new powershell process running the bot simulator.

### 6. Verify in Game
- Confirm that the bot window successfully connects (shows `Assigned ID` and `Starting position broadcast`).
- Turn your camera in-game to locate the bot actor.
- Confirm that the character model renders correctly (e.g., is visually Celeste, Miller, etc., depending on the ID).
- Confirm that the game does not crash when the bot connects, moves, or disconnects.

---

## Quick triage (do this first)

```powershell
# Primary multiplayer client log (NOT launcher [core] drain lines alone)
Get-Content $env:TEMP\mirroredge-multiplayer-client.log -Tail 80

# Look for:
#   "activation set live"          → Set Gameplay succeeded
#   "listSize=0"                   → no remotes; start server+bots, do not "fix" activation
#   "spawn ok" / non-null actor    → spawn path OK
```

If you only see `[core] engine: MmodDrainSpawnQueue queue=0 spawned=0` in the launcher, that only means the **engine drain is empty** — open `mirroredge-multiplayer-client.log` before changing code.

## Log Verification

You can verify character spawning details by inspecting the following logs in your `%TEMP%` directory:

1. **`%TEMP%\mirroredge-engine-spawn.log`**:
   Logs engine-level spawn stages for each remote character:
   ```
   engine: spawn stage begin character=2
   engine: spawn stage before actor spawn character=2
   engine: spawn stage actor spawned actor=09E9F000
   engine: spawn stage load mesh character=2
   engine: spawn stage set skeletal mesh mesh=07A1D800
   engine: spawn stage load material index=0
   engine: spawn stage set material index=0 material=082E4D00
   ...
   engine: spawn stage ok actor=09E9F000
   ```

2. **`%TEMP%\mirroredge-multiplayer-client.log`**:
   Logs multiplayer plugin logic:
   ```
   client: spawn queued id=e09c85cf level=tutorial_p
   client: spawn request submitted id=e09c85cf actorSlot=0B6F0764
   client: spawn ok id=e09c85cf actor=09E9F000
   ```
