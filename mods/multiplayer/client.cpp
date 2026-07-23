#include "client_platform.h"
#include "client_internal.h"
#include "client_plugin.h"
#include "menu_shim.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

extern DWORD g_clientListenerThreadId;
DWORD g_clientListenerThreadId = 0;

namespace ClientInternal {
namespace {
void WriteClientLogFile(const char *message) {
    if (!message || !message[0]) {
        return;
    }

    char tempPath[MAX_PATH] = {};
    if (!GetTempPathA(static_cast<DWORD>(sizeof(tempPath)), tempPath)) {
        return;
    }

    char logPath[MAX_PATH] = {};
    snprintf(logPath, sizeof(logPath), "%smirroredge-multiplayer-client.log", tempPath);

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    FILE *file = fopen(logPath, "a");
    if (file) {
        fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\n",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                now.wSecond, now.wMilliseconds, message);
        fclose(file);
    }
}
} // namespace

void SetConnectionError(const std::string &error) {
    std::lock_guard<std::mutex> lock(g_lastConnectionErrorMutex);
    g_lastConnectionError = error;
}

void ClearConnectionError() {
    std::lock_guard<std::mutex> lock(g_lastConnectionErrorMutex);
    g_lastConnectionError.clear();
}

std::string GetConnectionError() {
    std::lock_guard<std::mutex> lock(g_lastConnectionErrorMutex);
    return g_lastConnectionError;
}

void ClientLog(const char *message) {
    if (!message || !message[0]) {
        return;
    }
    printf("%s\n", message);
    WriteClientLogFile(message);
    ModLog::Write(message);
}

void ClientLogf(const char *format, ...) {
    char buffer[512] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    ClientLog(buffer);
}

void HelpMarker(const char *desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(450.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void IgnorePlayerInput(bool ignoreInput) {
    static std::atomic<bool> pendingIgnoreInput{false};
    static std::atomic<bool> hasPendingIgnoreInput{false};

    pendingIgnoreInput.store(ignoreInput);
    hasPendingIgnoreInput.store(true);

    Engine::QueueMainThreadTask([]() {
        if (!hasPendingIgnoreInput.exchange(false)) {
            return;
        }

        const auto ignore = pendingIgnoreInput.load();
        const auto controller = Engine::GetPlayerController(false);
        if (!controller) {
            return;
        }

        MeSdk::Safe::Gameplay::TryWriteControllerIgnoreInput(controller, ignore);
    });
}

std::string WideToUtf8(const wchar_t *wide) {
    if (!wide || !*wide) {
        return {};
    }

    const auto size =
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string &text) {
    if (text.empty()) {
        return {};
    }

    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], needed);
    return result;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](char c) { return static_cast<char>(tolower(c)); });
    return value;
}

Client::Player *GetPlayerById(unsigned int id) {
    const auto it = players.ById.find(id);
    return it != players.ById.end() ? it->second : nullptr;
}

std::mutex g_clientEngineTaskMutex;
std::vector<std::function<void()>> g_clientEngineTasks;

// Forward declaration: defined below after FlushClientEngineTasks.
// Must be a separate no-unwind function so __try/__except doesn't
// conflict with C++ object destructors (C2712).
static void ExecuteClientEngineTaskSafe(const std::function<void()> &task);

void FlushClientEngineTasks() {
    if (g_shutdownRequested.load() || !g_pluginEnabled.load()) {
        std::lock_guard<std::mutex> lock(g_clientEngineTaskMutex);
        g_clientEngineTasks.clear();
        return;
    }

    std::vector<std::function<void()>> batch;
    {
        std::lock_guard<std::mutex> lock(g_clientEngineTaskMutex);
        batch.swap(g_clientEngineTasks);
    }

    if (!batch.empty()) {
        ClientLogf("client: flush_client_tasks count=%zu", batch.size());
    }

    // Process only the first task directly.  Re-queue the rest so
    // each gets its own Presentation::SafeExecuteTask wrapper on a
    // separate PumpTasks cycle.  This prevents a crash in one task
    // from killing all remaining tasks in the same batch.
    for (size_t i = 1; i < batch.size(); ++i) {
        QueueClientEngineTask(std::move(batch[i]));
    }
    if (!batch.empty()) {
        ExecuteClientEngineTaskSafe(batch[0]);
    }
}

// Separate no-unwind function so __try/__except doesn't conflict with
// C++ object destructors (C2712) in FlushClientEngineTasks.
static void ExecuteClientEngineTaskSafe(const std::function<void()> &task) {
    OutputDebugStringA("ExecuteClientEngineTaskSafe: ENTER\n");
    __try {
        task();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char buf[64] = {};
        snprintf(buf, sizeof(buf),
                 "client: flush_task_crash code=0x%08lx", GetExceptionCode());
        OutputDebugStringA(buf);
    }
    OutputDebugStringA("ExecuteClientEngineTaskSafe: EXIT\n");
}

void QueueClientEngineTask(std::function<void()> task) {
    if (g_shutdownRequested.load() || !g_pluginEnabled.load()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_clientEngineTaskMutex);
        g_clientEngineTasks.push_back(std::move(task));
    }
    ClientLog("client: queue_client_task -> Engine::QueueMainThreadTask");
    Engine::QueueMainThreadTask(FlushClientEngineTasks);
}

void MarkPluginThreadCrashed(const char *threadName, DWORD exceptionCode) {
    ClientLogf("client: %s thread crashed exception=0x%08lx",
               threadName ? threadName : "unknown",
               static_cast<unsigned long>(exceptionCode));
    MpDebugLog("client.cpp:ThreadGuard", "thread_crash", "H-CRASH",
               static_cast<uintptr_t>(exceptionCode), GetCurrentThreadId(), 0);

    disabled.store(true);
    connected.store(false);
    g_pluginEnabled.store(false);
    g_listenerStarted.store(false);
    ForceCloseClientSockets();
    if (g_listenerExitEvent) {
        SetEvent(g_listenerExitEvent);
    }
}

void StartClientListenerIfNeeded() {
    static std::atomic<bool> listenerSkipLogged{false};
    if (!g_listenerExitEvent) {
        g_listenerExitEvent =
            CreateEventW(nullptr, TRUE, FALSE, L"Local\\multiplayer_listener_exit");
    }

    std::lock_guard<std::mutex> lock(g_clientListenerThreadMutex);
    if (g_listenerStarted.load()) {
        if (!listenerSkipLogged.exchange(true)) {
            MpDebugLog("client.cpp:StartClientListenerIfNeeded", "listener_skip",
                       "H-CONN", GetCurrentThreadId(), 0, 0);
        }
        return;
    }

    if (g_listenerExitEvent) {
        ResetEvent(g_listenerExitEvent);
    }

    g_listenerStarted.store(true);
    listenerSkipLogged.store(false);

    // Join a previous thread that may have crashed/halted without full
    // Shutdown(). Without this, std::thread::operator= on a joinable
    // thread is UB and would call std::terminate().
    if (g_clientListenerThread.joinable()) {
        g_clientListenerThread.join();
    }

    MpDebugLog("client.cpp:StartClientListenerIfNeeded", "listener_spawn",
               "H-CONN", GetCurrentThreadId(), 0, 0);
    ClientLog("client: listener spawning");
    g_clientListenerThread = std::thread(ClientListener);
}

void UpdateHarnessSnapshot() {
    Client::HarnessStatus snap = {};
    snap.connected = connected.load() && !disabled.load();
    snap.currentMap = client.Level;
    players.Mutex.lock_shared();
    snap.remotePlayers = static_cast<int>(players.List.size());
    snap.spawnedPlayers = 0;
    snap.posedPlayers = 0;
    for (const auto &p : players.List) {
        if (!p) {
            continue;
        }
        // Count from stable spawn slots without assigning player->Actor on the
        // pipe thread — publishing Actor off-Tick made ApplyRemotePlayerWorldPoses
        // SEH-kill OnTick (host pose / bones never ran after SPAWN_OK).
        const bool hasActor =
            p->Actor != nullptr ||
            HasStableSpawnActor(p->Id);
        if (!hasActor) {
            continue;
        }
        ++snap.spawnedPlayers;
        if (p->ToTime) {
            ++snap.posedPlayers;
        }
    }
    players.Mutex.unlock_shared();

    snap.inGameplay =
        !client.Level.empty() && client.Level != "tdmainmenu";

    if (snap.spawnedPlayers > 0) {
        static int lastLoggedSpawned = -1;
        if (snap.spawnedPlayers != lastLoggedSpawned) {
            lastLoggedSpawned = snap.spawnedPlayers;
            ClientLogf("client: harness snapshot rem=%d sp=%d posed=%d",
                       snap.remotePlayers, snap.spawnedPlayers,
                       snap.posedPlayers);
        }
    }

    std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
    snap.posX = g_harnessSnapshot.posX;
    snap.posY = g_harnessSnapshot.posY;
    snap.posZ = g_harnessSnapshot.posZ;
    snap.yaw = g_harnessSnapshot.yaw;
    g_harnessSnapshot = snap;
}

} // namespace ClientInternal

using namespace ClientInternal;

bool ClientPlugin::Initialize() {
    client.Name =
        Settings::GetSetting("client", "name", "anonymous").get<std::string>();
    const size_t n = sizeof(nameInput) - 1;
    strncpy(nameInput, client.Name.c_str(), n);
    nameInput[n] = '\0';

    room = Settings::GetSetting("client", "room", "lobby").get<std::string>();
    const size_t n2 = sizeof(roomInput) - 1;
    strncpy(roomInput, room.c_str(), n2);
    roomInput[n2] = '\0';

    SetConfiguredServerHost(
        Settings::GetSetting("client", "server", "176.58.101.83")
            .get<std::string>());
    const auto configuredServer = GetConfiguredServerHost();
    const size_t n3 = sizeof(serverInput) - 1;
    strncpy(serverInput, configuredServer.c_str(), n3);
    serverInput[n3] = '\0';

    client.Character =
        Settings::GetSetting("client", "character", Engine::Character::Faith)
            .get<Engine::Character>();

    chat.Keybind = Settings::GetSetting("client", "chatKeybind", 0x54);

    players.ShowNameTags = Settings::GetSetting("client", "showNameTags", true);
    chat.ShowOverlay = Settings::GetSetting("client", "showChatOverlay", true);
    interpolationEnabled =
        Settings::GetSetting("client", "interpolation", true).get<bool>();
    interpolationDelayBaseMs =
        Settings::GetSetting("client", "interpolationDelay", 100).get<int>();
    if (interpolationDelayBaseMs < 0) {
        interpolationDelayBaseMs = 0;
    }
    if (interpolationDelayBaseMs > 250) {
        interpolationDelayBaseMs = 250;
    }
    interpolationDelayMs = interpolationDelayBaseMs;
    interpolationDelayAuto =
        Settings::GetSetting("client", "interpolationDelayAuto", true)
            .get<bool>();
    poseSmoothEnabled =
        Settings::GetSetting("client", "poseSmooth", true).get<bool>();
    poseSmoothAlpha =
        Settings::GetSetting("client", "poseSmoothAlpha", 0.45f).get<float>();
    poseSnapUu =
        Settings::GetSetting("client", "poseSnapUu", 350.0f).get<float>();
    boneSmoothEnabled =
        Settings::GetSetting("client", "boneSmooth", true).get<bool>();
    boneSmoothAlpha =
        Settings::GetSetting("client", "boneSmoothAlpha", 0.55f).get<float>();
    boneSmoothIdleAlpha =
        Settings::GetSetting("client", "boneSmoothIdleAlpha", 0.70f)
            .get<float>();
    boneSmoothWalkAlpha =
        Settings::GetSetting("client", "boneSmoothWalkAlpha", 0.80f)
            .get<float>();
    showRemoteStanceOnNametag =
        Settings::GetSetting("client", "showRemoteStanceOnNametag", false)
            .get<bool>();
    showLatency = Settings::GetSetting("client", "showLatency", true).get<bool>();
    showTagDistanceOverlay =
        Settings::GetSetting("games", "tagShowDistanceOverlay", false)
            .get<bool>();
    softCollisionEnabled =
        Settings::GetSetting("client", "softCollision", true).get<bool>();
    softCollisionRadius =
        Settings::GetSetting("client", "softCollisionRadius", 88.0f)
            .get<float>();
    softCollisionStrength =
        Settings::GetSetting("client", "softCollisionStrength", 0.55f)
            .get<float>();
    worldClampEnabled =
        Settings::GetSetting("client", "worldClamp", true).get<bool>();
    worldClampUp =
        Settings::GetSetting("client", "worldClampUp", 80.0f).get<float>();
    worldClampDown =
        Settings::GetSetting("client", "worldClampDown", 400.0f).get<float>();
    worldClampMaxLateral =
        Settings::GetSetting("client", "worldClampMaxLateral", 50.0f)
            .get<float>();
    interactKeybind =
        Settings::GetSetting("client", "interactKeybind", 0x45).get<int>();
    interactMaxMeters =
        Settings::GetSetting("client", "interactMaxMeters", 2.5f).get<float>();
    tagCooldown = client.CoolDownTag;

    disabled.store(false);
    g_pluginEnabled.store(true);
    // Do NOT call Gameplay::PrewarmClasses() here — StaticClass during inject
    // has left multiplayer with no client.log (2026-07-20). TryReadPawnPose
    // skips TryIsA when this DLL's class cache is cold (RawRead only).
    ClientLogf("client: initialized server=%s:%u room=%s name=%s",
               configuredServer.c_str(), static_cast<unsigned>(Client::Port),
               room.c_str(), client.Name.c_str());
    Menu::AddTab("Multiplayer", MultiplayerTab);
    EnsureClientRuntimeHooks();
    StartClientListenerIfNeeded();

    return true;
}

bool Client::GetHarnessStatus(HarnessStatus &out) {
    // Do NOT call RefreshHarnessPose here — it may touch GamePlayers via
    // GetPlayerController(false) when the PC cache is cold, and GET_STATUS
    // is often polled from a pipe/EndScene context (hang). Host pose is
    // written by OnLocalPoseNetworkTick on the game Tick path instead.
    UpdateHarnessSnapshot();
    std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
    out = g_harnessSnapshot;
    return true;
}

void Client::RefreshHarnessPose() {
    // Always refresh non-pose fields so GET_STATUS reports accurate
    // inGameplay / spawnedPlayers even when the player pawn hasn't
    // been created yet (e.g. during an intro cinematic).
    UpdateHarnessSnapshot();

    if (disabled.load() || loading.load() || !connected.load()) {
        return;
    }

    if (client.Level.empty() || client.Level == "tdmainmenu") {
        return;
    }

    // Pipe/GET_STATUS path: never GamePlayers-seed (hang). Engine cache only.
    auto pawn = ResolveLocalPlayerPawn(false);
    if (!pawn) {
        // Tutorial / cinematic: PC+camera often exist before AcknowledgedPawn
        // is filled. Use camera location so Follow bots and harness pos work.
        if (auto *pc = ResolveLocalPlayerController(false)) {
            Classes::ACamera *cam = nullptr;
            if (MeSdk::Safe::TryReadField(&pc->PlayerCamera, cam) && cam &&
                MeSdk::Safe::IsPlausibleUObject(cam)) {
                Classes::FVector loc = {};
                Classes::FRotator rot = {};
                if (MeSdk::Safe::TryReadField(&cam->Location, loc)) {
                    std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
                    g_harnessSnapshot.posX = loc.X;
                    g_harnessSnapshot.posY = loc.Y;
                    g_harnessSnapshot.posZ = loc.Z;
                    if (MeSdk::Safe::TryReadField(&cam->Rotation, rot)) {
                        g_harnessSnapshot.yaw = static_cast<unsigned short>(
                            rot.Yaw % 0x10000);
                    }
                }
            }
        }
        UpdateHarnessSnapshot();
        return;
    }

    MeSdk::Safe::Gameplay::PawnPoseSnapshot pose = {};
    if (!MeSdk::Safe::Gameplay::TryReadPawnPose(pawn, pose)) {
        return;
    }

    const float posX = pose.location.X;
    const float posY = pose.location.Y;
    const float posZ = pose.location.Z + pose.targetMeshTranslationZ;
    const auto yaw =
        static_cast<unsigned short>(pose.rotation.Yaw % 0x10000);

    {
        std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
        g_harnessSnapshot.posX = posX;
        g_harnessSnapshot.posY = posY;
        g_harnessSnapshot.posZ = posZ;
        g_harnessSnapshot.yaw = yaw;
    }
    UpdateHarnessSnapshot();
}

extern "C" __declspec(dllexport) int __stdcall MmMultiplayer_EnsureRuntimeHooks() {
    EnsureClientRuntimeHooks();
    return hooksRegistered ? 1 : 0;
}

extern "C" __declspec(dllexport) int __stdcall MmMultiplayer_GetHarnessStatus(
    int *connected, int *remotePlayers, float *posX, float *posY, float *posZ,
    unsigned short *yaw, int *inGameplay, wchar_t *mapOut, int mapOutChars) {
    Client::HarnessStatus status = {};
    if (!Client::GetHarnessStatus(status)) {
        return 0;
    }

    if (connected) {
        *connected = status.connected ? 1 : 0;
    }
    if (remotePlayers) {
        *remotePlayers = status.remotePlayers;
    }
    if (posX) {
        *posX = status.posX;
    }
    if (posY) {
        *posY = status.posY;
    }
    if (posZ) {
        *posZ = status.posZ;
    }
    if (yaw) {
        *yaw = status.yaw;
    }
    if (inGameplay) {
        *inGameplay = status.inGameplay ? 1 : 0;
    }
    if (mapOut && mapOutChars > 0) {
        mapOut[0] = L'\0';
        if (!status.currentMap.empty()) {
            const std::wstring wide = Utf8ToWide(status.currentMap);
            const size_t wcsn = static_cast<size_t>(mapOutChars) - 1;
            wcsncpy(mapOut, wide.c_str(), wcsn);
            mapOut[wcsn] = L'\0';
        }
    }

    return 1;
}

extern "C" __declspec(dllexport) int __stdcall MmMultiplayer_GetSpawnedPlayers() {
    Client::HarnessStatus status = {};
    if (!Client::GetHarnessStatus(status)) {
        return 0;
    }
    return status.spawnedPlayers;
}

extern "C" __declspec(dllexport) int __stdcall MmMultiplayer_GetPosedPlayers() {
    Client::HarnessStatus status = {};
    if (!Client::GetHarnessStatus(status)) {
        return 0;
    }
    return status.posedPlayers;
}

// Workaround for OnPostLevelLoad never firing (LoadMap throws C++ exception).
// Called from core's FORCE_HOSTED_LIVE pipe command to directly trigger
// the hosted activation chain: QueueActivateHostedGameplay ->
// RequestGameplayActivation -> CompleteMultiplayerHostedActivation (fast path).
// In hosted mode the fast path immediately fires: EnsureClientGameplayCallbacks,
// SetHostedGameplayLive(true), QueueSpawnEligibleRemotePlayers.
extern "C" __declspec(dllexport) void __cdecl MmMultiplayer_ForcePostLevelInit() {
    if (!ModHost::IsAttached()) {
        ClientLog("client: ForcePostLevelInit skip not attached");
        return;
    }
    if (disabled.load()) {
        ClientLog("client: ForcePostLevelInit skip disabled");
        return;
    }
    ClientLog("client: ForcePostLevelInit starting");
    // Harness / FORCE_HOSTED_LIVE often runs while client.Level is still menu.
    // Mirror the Multiplayer "Set Gameplay" button so activation can proceed
    // and TryAdoptRemoteGameplayLevel can pick up bot maps (tutorial_p).
    const bool menuLevel =
        client.Level.empty() || client.Level == "tdmainmenu";
    if (menuLevel) {
        ClientLog("client: ForcePostLevelInit applying Set Gameplay from menu");
        ClientInternal::ApplyManualClientLevel("gameplay");
        ClientLog("client: ForcePostLevelInit done (queued Set Gameplay)");
        return;
    }
    if (ClientInternal::TryAdoptRemoteGameplayLevel()) {
        ClientInternal::EnsureClientRemotePlayerPresentation();
    }
    ClientInternal::QueueActivateHostedGameplay();
    ClientLog("client: ForcePostLevelInit done");
}

void ClientPlugin::Shutdown() {
    if (!g_pluginEnabled.load() && !g_listenerStarted.load()) {
        return;
    }

    g_shutdownRequested.store(true);
    disabled.store(true);
    g_pluginEnabled.store(false);
    connected.store(false);

    {
        std::lock_guard<std::mutex> lock(g_clientEngineTaskMutex);
        g_clientEngineTasks.clear();
    }

    Engine::SetHostedGameplayLive(false);
    Engine::ClearFeaturePluginCallbacks();

    ForceCloseClientSockets();

    latencyMs = -1;
    ResetRecvJsonBuffer();

    Menu::RemoveTab("Multiplayer");

    if (g_listenerExitEvent) {
        WaitForSingleObject(g_listenerExitEvent, 20000);
    }

    {
        std::lock_guard<std::mutex> lock(g_clientListenerThreadMutex);
        if (g_clientListenerThread.joinable()) {
            g_clientListenerThread.join();
        }
    }

    players.Mutex.lock();
    for (const auto &p : players.List) {
        delete p;
    }
    players.List.clear();
    players.ById.clear();
    g_hostedRemoteCount = 0;
    players.Mutex.unlock();

    g_listenerStarted.store(false);
    g_shutdownRequested.store(false);
    hooksRegistered = false;
    networkTickHooksRegistered = false;
    remotePlayerHooksRegistered = false;
    renderHookRegistered = false;
    Engine::CancelGameplayActivation();
}
