#pragma once

#include "client_platform.h"

#include "client.h"

#include "agent_log.h"
#include "json.h"
#include "mod_log.h"
#include "plugin_ui.h"
#include "plugin_seh_guard.h"
#include "settings.h"
#include "me_sdk/util/constants.h"
#include "me_sdk/util/math.h"
#include "me_sdk/runtime/safe_gameplay.h"
#include "me_sdk/runtime/safe_access.h"
#include "me_sdk/runtime/safe_gui.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <d3d9.h>

extern DWORD g_clientListenerThreadId;

namespace ClientInternal {

struct ChatState {
    bool Focused = false;
    bool ShowOverlay = true;
    int Keybind = 0;
    std::string Raw;
    unsigned long long LastTime = 0;
    std::mutex Mutex;
};

struct PlayersState {
    bool ShowNameTags = true;
    std::vector<Client::Player *> List;
    std::unordered_map<unsigned int, Client::Player *> ById;
    std::shared_mutex Mutex;
};

extern char roomInput[0xFF];
extern char nameInput[0xFF];
extern char serverInput[0xFF];
extern char chatInput[0x200];

extern std::atomic<bool> connected;
extern std::atomic<bool> loading;
extern std::atomic<bool> disabled;
extern std::atomic<bool> g_shutdownRequested;
extern std::atomic<bool> g_listenerStarted;
extern std::thread g_clientListenerThread;
extern std::mutex g_clientListenerThreadMutex;
extern HANDLE g_listenerExitEvent;
extern std::string room;
extern std::string serverHost;
extern std::mutex g_serverHostMutex;

extern bool hooksRegistered;
extern bool networkTickHooksRegistered;
extern bool remotePlayerHooksRegistered;
extern bool renderHookRegistered;

extern std::atomic<int> g_hostedRemoteCount;
extern std::atomic<uint64_t> g_lastPing;
extern std::atomic<uint64_t> g_lastLatencyRequest;
extern std::atomic<int> g_connectionAttempt;
extern std::atomic<unsigned> g_levelProbeFaults;
extern std::atomic<uint64_t> g_lastLevelProbeDiagnostic;

extern std::string g_lastConnectionError;
extern std::mutex g_lastConnectionErrorMutex;
extern Client::HarnessStatus g_harnessSnapshot;
extern std::mutex g_harnessSnapshotMutex;

extern sockaddr_in server;
extern SOCKET tcpSocket;
extern SOCKET udpSocket;
extern std::mutex g_tcpSocketMutex;
extern char g_recvJsonBuffer[0x10000];
extern size_t g_recvJsonPendingLen;

extern thread_local bool g_isClientListenerThread;
extern thread_local bool g_isClientBackgroundThread;

extern ChatState chat;
extern Client::Player client;
extern std::atomic<bool> g_pluginEnabled;
extern PlayersState players;

extern bool interpolationEnabled;
extern int interpolationDelayMs;
// V18: when true, interpolationDelayMs tracks UDP interval + TCP latency.
extern bool interpolationDelayAuto;
extern int interpolationDelayBaseMs;
// Per-tick EMA toward network target (reduces soft-coll / packet jitter).
extern bool poseSmoothEnabled;
extern float poseSmoothAlpha; // 0..1 per apply tick (~60Hz)
extern bool boneSmoothEnabled;
extern float boneSmoothAlpha; // 0..1 per BonesTick (~60Hz); walk/run
extern float boneSmoothIdleAlpha; // stronger EMA when remote nearly idle
extern float boneSmoothWalkAlpha; // follow walk cycle faster than idle
extern bool showRemoteStanceOnNametag;
extern float poseSnapUu;      // snap instead of lerp/EMA beyond this distance
// Host pose TX rate cap (Hz). 0 = unlimited. Parkour/state-change bypass.
extern int hostPoseTxMaxHz;
extern bool showLatency;
extern int latencyMs;

extern bool showTagDistanceOverlay;
extern bool showTagCooldownOverlay;
extern bool playerDiedAndSentJsonMessage;
extern int tagCooldown;
extern ULONGLONG taggedTimed;
extern int previousTaggedId;

// Fake soft collision: XY separate remotes from local pawn (live pose path only).
// Does not mutate remotes on disconnect (KI-2026-012).
extern bool softCollisionEnabled;
extern float softCollisionRadius;
extern float softCollisionStrength;

// FastTrace/Trace clamp of remote UDP targets (floor + wall). Live pose only.
// Does NOT enable remote PHYS / bCollideWorld (KI-012).
extern bool worldClampEnabled;
extern float worldClampUp;
extern float worldClampDown;
extern float worldClampMaxLateral;

// B1 near-distance interact (TCP only; no remote Actor mutate — KI-2026-012).
extern int interactKeybind;
extern float interactMaxMeters;
extern ULONGLONG lastInteractSentMs;

void ClientLog(const char *message);
void ClientLogf(const char *format, ...);

void SetConnectionError(const std::string &error);
void ClearConnectionError();
std::string GetConnectionError();

void QueueClientEngineTask(std::function<void()> task);
void MarkPluginThreadCrashed(const char *threadName, DWORD exceptionCode);

std::string WideToUtf8(const wchar_t *wide);
std::wstring Utf8ToWide(const std::string &text);
std::string ToLower(std::string value);

Client::Player *GetPlayerById(unsigned int id);

void IgnorePlayerInput(bool ignoreInput);
void HelpMarker(const char *desc);

void UpdateHarnessSnapshot();

// Pull EndScene drain results into player->Actor (stable slots).
// ONLY call from game Tick (TrackRemotePlayerSpawnResults) — not GET_STATUS.
void SyncActorFromStableSlot(Client::Player *player);
bool HasStableSpawnActor(unsigned int playerId);
// Pull actor out of the stable slot (and clear it) for disconnect/despawn.
Classes::ASkeletalMeshActorSpawnable *TakeStableSpawnActor(unsigned int playerId);

inline bool PlayerHasRemoteVisual(const Client::Player *p) {
    return p && p->Actor != nullptr;
}
inline Classes::AActor *PlayerRemoteActor(Client::Player *p) {
    return p ? p->Actor : nullptr;
}
inline const Classes::AActor *PlayerRemoteActor(const Client::Player *p) {
    return p ? p->Actor : nullptr;
}
void ClearPlayerRemoteVisual(Client::Player *p);
bool TryReadPlayerRemoteSkel(Client::Player *p,
                             Classes::USkeletalMeshComponent *&out);

// Disconnect UX (KI-2026-012): ref-drop only — never ShutDown / Location park
// after TransformBones. Orphan meshes remain until level unload.
void QueueParkRemoteActor(Classes::ASkeletalMeshActorSpawnable *actor);
void DrainParkedRemoteActors();

// Tick-safe local PC/pawn resolve via Engine::GetWorld ControllerList/PawnList.
// allowExtraResolve: ONLY true from OnLocalPoseNetworkTick (game Tick).
// Never enable from GET_STATUS / pipe / EndScene.
Classes::ATdPlayerController *ResolveLocalPlayerController(
    bool allowExtraResolve = false);
Classes::ATdPlayerPawn *ResolveLocalPlayerPawn(
    bool allowExtraResolve = false);

// Host↔remote distance in meters (MeSdk::Distance). Returns false if either
// side has no usable pose. B0/B1 Tag + interact.
bool TryGetLocalHostLocation(Classes::FVector &out);
bool TryGetRemoteLocation(const Client::Player *remote, Classes::FVector &out);
float HostRemoteDistanceMeters(const Client::Player *remote);
// Nearest same-level remote within maxMeters; nullptr if none. outDist optional.
Client::Player *FindNearestRemote(float maxMeters, float *outDist = nullptr);
void TrySendNearestInteract(const char *kind);

// Network
std::string NormalizeServerHost(std::string host);
bool IsLoopbackHost(const std::string &host);
std::string GetConfiguredServerHost();
void SetConfiguredServerHost(const std::string &host);

struct ConnectionTarget {
    std::string host;
    bool local = false;
};

ConnectionTarget BuildConnectionTarget();
bool Setup(const std::string &host);
void ResetRecvJsonBuffer();
bool RecvJsonMessage(json &msg);
bool SendJsonMessage(json msg);
void ForceCloseClientSockets();
bool ConnectTcpInterruptible(SOCKET sock);
void Disconnect();
void AddChatMessage(std::string message);
void SendChatInput();
void StartClientListenerIfNeeded();

void ClientListener();

// Remote players / presentation
void QueueSpawnPlayerIfReady(Client::Player *player);
void QueueSpawnEligibleRemotePlayers();
void TrySpawnPendingRemotePlayers();
bool TrySpawnPlayerDirect(Client::Player *player);
void TrackRemotePlayerSpawnResults();
void ApplyInitialRemotePlayerPose(Client::Player *player);
void ApplyPacketSnapshot(Client::Player *player,
                         const Client::PACKET_COMPRESSED &packet,
                         bool hasVelocityTrailer = false,
                         bool hasMoveTrailer = false,
                         bool hasSeqTrailer = false);
void BuildRenderedPacket(Client::Player *player, Client::PACKET &packet);

void InstallClientNetworkTickHooks();
void InstallClientRemotePlayerHooks();
void InstallClientSimulationHooks();
void EnsureClientGameplayCallbacks();
void EnsureClientRenderHook();
void EnsureClientRemotePlayerPresentation();

void CompleteMultiplayerHostedActivation();
void CompleteMultiplayerRemoteLevelActivation();
void QueueActivateHostedGameplay();
void HandlePostLevelGameplayEntry();

// Level / hosted gameplay
void NotifyServerLevel();
void QueueLevelProbe();
void SyncCurrentLevelFromWorld();
void ApplyManualClientLevel(const std::string &level);
void ApplyManualClientLevelOnMainThread(const std::string &level);
void SyncCurrentLevelIfInGameplay();
// When late-injected after bots already joined a real map, client.Level can
// stay on tdmainmenu (OnPostLevelLoad missed). Adopt a remote non-menu level
// so spawn eligibility can match without UE3 world probing.
bool TryAdoptRemoteGameplayLevel();
bool IsMenuLevelName(const std::string &level);
// Synthetic Set Gameplay "gameplay" matches any concrete non-menu map (and
// equal strings). Used for spawn / UDP-compat while host upgrades map name.
bool LevelsCompatible(const std::string &a, const std::string &b);
// When client.Level is still the synthetic "gameplay", probe StreamingLevels /
// GetMapName (cached world only) and upgrade to e.g. tutorial_p.
bool TryUpgradeGameplayLevelName();

bool TryEnsureGameplayHooksForSetGameplay();

void InstallClientRuntimeHooks();
void EnsureClientRuntimeHooks();

// UI callbacks registered with engine (split so SEH removes only one path)
void OnTick(float delta);
void OnTickMaintenance(float delta);
void OnTickPoseNetwork(float delta);
void OnTickApplyRemotePoses(float delta);
void OnTickGames(float delta);
void OnRender(IDirect3DDevice9 *device);
void OnRenderGames(IDirect3DDevice9 *device);
void MultiplayerTab();

} // namespace ClientInternal
