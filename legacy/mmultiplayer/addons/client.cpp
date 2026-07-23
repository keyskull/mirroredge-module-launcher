#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#pragma comment(lib, "ws2_32.lib")

#include "../agent_log.h"
#include "../engine.h"
#include "plugin_ui.h"
#include "ui_harness_plugin.h"
#include "../../shared/json.h"
#include "../menu.h"
#include "../modhost.h"
#include "../settings.h"
#include "../util.h"

extern DWORD g_clientListenerThreadId;

#include "client.h"

static char roomInput[0xFF] = {0};
static char nameInput[0xFF] = {0};
static char serverInput[0xFF] = {0};
static char chatInput[0x200] = {0};

static std::atomic<bool> connected{false};
static std::atomic<bool> loading{false};
static std::atomic<bool> disabled{false};
static std::string room, serverHost;
static bool hooksRegistered = false;
static bool networkTickHooksRegistered = false;
static bool remotePlayerHooksRegistered = false;
static std::atomic<bool> g_activateGameplayScheduled{false};
static std::atomic<int> g_hostedRemoteCount{0};
static std::atomic<uint64_t> g_lastPing{0};
static std::atomic<uint64_t> g_lastLatencyRequest{0};
static Client::HarnessStatus g_harnessSnapshot = {};
static std::mutex g_harnessSnapshotMutex;

static sockaddr_in server = {0};
static SOCKET tcpSocket = 0, udpSocket = 0;
static std::mutex g_tcpSocketMutex;
static thread_local bool g_isClientListenerThread = false;

static struct {
    bool Focused = false, ShowOverlay = true;
    int Keybind = 0;
    std::string Raw;
    unsigned long long LastTime;
    std::mutex Mutex;
} chat;

static Client::Player client = {0};

static struct {
    bool ShowNameTags = true;
    std::vector<Client::Player *> List;
    std::unordered_map<unsigned int, Client::Player *> ById;
    std::shared_mutex Mutex;
} players;

static auto interpolationEnabled = true;
static auto interpolationDelayMs = 100;
static auto showLatency = true;
static int latencyMs = -1;

static bool showTagDistanceOverlay = false;
static bool showTagCooldownOverlay = true;
static bool playerDiedAndSentJsonMessage = false;
static int tagCooldown = 5;
static ULONGLONG taggedTimed = 0;
static int previousTaggedId = 0;

static void HelpMarker(const char *desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(450.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void IgnorePlayerInput(bool ignoreInput) {
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

        controller->bIgnoreMoveInput = ignore ? 1 : 0;
        controller->bIgnoreButtonInput = ignore ? 1 : 0;
        controller->bIgnoreMovementFocus = ignore;
    });
}

static std::string WideToUtf8(const wchar_t *wide) {
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

static std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](char c) { return static_cast<char>(tolower(c)); });
    return value;
}

static Client::Player *GetPlayerById(unsigned int id) {
    const auto it = players.ById.find(id);
    return it != players.ById.end() ? it->second : nullptr;
}

static void AddChatMessage(std::string message);
static void Disconnect();
static void OnRenderGames(IDirect3DDevice9 *device);
static void UpdateHarnessSnapshot();
static void InstallClientRuntimeHooks();
static void StartClientListenerIfNeeded();
static void QueueClientEngineTask(std::function<void()> task);
static void TryActivateHostedGameplay();
static void QueueActivateHostedGameplay();
static void SyncCurrentLevelFromWorld();
static void NotifyServerLevel();

static std::mutex g_clientEngineTaskMutex;
static std::vector<std::function<void()>> g_clientEngineTasks;

static void FlushClientEngineTasks() {
    std::vector<std::function<void()>> batch;
    {
        std::lock_guard<std::mutex> lock(g_clientEngineTaskMutex);
        batch.swap(g_clientEngineTasks);
    }

    for (auto &task : batch) {
        task();
    }
}

static void QueueClientEngineTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(g_clientEngineTaskMutex);
        g_clientEngineTasks.push_back(std::move(task));
    }
    Engine::QueueMainThreadTask(FlushClientEngineTasks);
}

static void QueueSpawnPlayerIfReady(Client::Player *player) {
    if (!player) {
        return;
    }

    QueueClientEngineTask([player]() {
        if (disabled.load() || loading.load() || player->Level != client.Level || player->Actor) {
            return;
        }

        if (!Engine::IsHostedGameplayLive() ||
            !Engine::CanSafelyUsePlayerPawn()) {
            QueueSpawnPlayerIfReady(player);
            return;
        }

        Engine::SpawnCharacter(player->Character, player->Actor);
    });
}

static float LerpFloat(float from, float to, float alpha) {
    return from + (to - from) * alpha;
}

static Classes::FVector LerpVector(const Classes::FVector &from,
                                   const Classes::FVector &to, float alpha) {
    return {LerpFloat(from.X, to.X, alpha), LerpFloat(from.Y, to.Y, alpha),
            LerpFloat(from.Z, to.Z, alpha)};
}

static unsigned short LerpYaw(unsigned short from, unsigned short to,
                              float alpha) {
    auto delta = static_cast<int>(to) - static_cast<int>(from);
    if (delta > 0x8000) {
        delta -= 0x10000;
    } else if (delta < -0x8000) {
        delta += 0x10000;
    }

    return static_cast<unsigned short>(
        (static_cast<int>(from) + static_cast<int>(delta * alpha)) & 0xFFFF);
}

static void LerpBoneBuffer(Classes::FBoneAtom *dest,
                             const Classes::FBoneAtom *from,
                             const Classes::FBoneAtom *to, float alpha) {
    const auto destBase = reinterpret_cast<byte *>(dest);
    const auto fromBase = reinterpret_cast<const byte *>(from);
    const auto toBase = reinterpret_cast<const byte *>(to);

    for (auto i = 0; i < ARRAYSIZE(CompressedBoneOffsets); ++i) {
        const auto offset = CompressedBoneOffsets[i];
        *reinterpret_cast<float *>(destBase + offset) = LerpFloat(
            *reinterpret_cast<const float *>(fromBase + offset),
            *reinterpret_cast<const float *>(toBase + offset), alpha);
    }
}

static float GetInterpolationAlpha(unsigned long long fromTime,
                                   unsigned long long toTime,
                                   unsigned long long renderTime) {
    if (toTime <= fromTime) {
        return 1.0f;
    }

    if (renderTime <= fromTime) {
        return 0.0f;
    }

    if (renderTime >= toTime) {
        return 1.0f;
    }

    return static_cast<float>(renderTime - fromTime) /
           static_cast<float>(toTime - fromTime);
}

static void BuildRenderedPacket(const Client::Player *player,
                                Client::PACKET &packet) {
    packet = player->LastPacket;

    if (!interpolationEnabled || !player->ToTime) {
        return;
    }

    const auto renderTime =
        GetTickCount64() - static_cast<unsigned long long>(interpolationDelayMs);
    const auto alpha =
        GetInterpolationAlpha(player->FromTime, player->ToTime, renderTime);

    packet.Id = player->ToPacket.Id;
    packet.Position =
        LerpVector(player->FromPacket.Position, player->ToPacket.Position, alpha);
    packet.Yaw = LerpYaw(player->FromPacket.Yaw, player->ToPacket.Yaw, alpha);
    LerpBoneBuffer(packet.Bones, player->FromPacket.Bones, player->ToPacket.Bones,
                   alpha);
}

static void ApplyPacketSnapshot(Client::Player *player,
                                const Client::PACKET_COMPRESSED &packet) {
    const auto now = GetTickCount64();

    if (player->ToTime) {
        player->FromPacket = player->ToPacket;
        player->FromTime = player->ToTime;
    } else {
        player->FromPacket = {0};
        player->FromTime = now;
    }

    memcpy(&player->LastPacket, &packet,
           FIELD_OFFSET(Client::PACKET_COMPRESSED, CompressedBones));

    const auto bonesBase = reinterpret_cast<byte *>(player->LastPacket.Bones);
    for (auto i = 0; i < ARRAYSIZE(CompressedBoneOffsets); ++i) {
        *reinterpret_cast<float *>(bonesBase + CompressedBoneOffsets[i]) =
            static_cast<float>(packet.CompressedBones[i]) / 215.f;
    }

    player->ToPacket = player->LastPacket;
    player->ToTime = now;
}

static bool Setup() {
    static bool wsaStarted = false;
    if (!wsaStarted) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
            printf("client: WSAStartup failed\n");
            return false;
        }

        wsaStarted = true;
    }

    addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *result = nullptr;
    if (getaddrinfo(serverHost.c_str(), nullptr, &hints, &result)) {
        printf("client: getaddrinfo failed for \"%s\"\n", serverHost.c_str());
        return false;
    }

    IN_ADDR serverAddr = {0};
    for (auto *cur = result; cur; cur = cur->ai_next) {
        if (cur->ai_family == AF_INET) {
            serverAddr =
                reinterpret_cast<SOCKADDR_IN *>(cur->ai_addr)->sin_addr;
            break;
        }
    }

    freeaddrinfo(result);
    if (!serverAddr.S_un.S_addr) {
        printf("client: found no server address\n");
        return false;
    }

    server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(Client::Port);
    server.sin_addr = serverAddr;

    return true;
}

static char g_recvJsonBuffer[0x10000] = {0};
static size_t g_recvJsonPendingLen = 0;

static void ResetRecvJsonBuffer() {
    g_recvJsonPendingLen = 0;
    g_recvJsonBuffer[0] = '\0';
}

static bool RecvJsonMessage(json &msg) {
    for (;;) {
        if (g_recvJsonPendingLen > 0) {
            const auto *end = static_cast<const char *>(
                memchr(g_recvJsonBuffer, '\0', g_recvJsonPendingLen));
            if (end) {
                const size_t messageLen = static_cast<size_t>(end - g_recvJsonBuffer);
                try {
                    msg = json::parse(g_recvJsonBuffer, g_recvJsonBuffer + messageLen);
                    const size_t consumed = messageLen + 1;
                    if (consumed < g_recvJsonPendingLen) {
                        memmove(g_recvJsonBuffer, g_recvJsonBuffer + consumed,
                                g_recvJsonPendingLen - consumed);
                    }
                    g_recvJsonPendingLen -= consumed;
                    g_recvJsonBuffer[g_recvJsonPendingLen] = '\0';
                    return true;
                } catch (...) {
                    printf("client: failed parse -> %.*s\n",
                           static_cast<int>(messageLen), g_recvJsonBuffer);
                    const size_t consumed = messageLen + 1;
                    if (consumed < g_recvJsonPendingLen) {
                        memmove(g_recvJsonBuffer, g_recvJsonBuffer + consumed,
                                g_recvJsonPendingLen - consumed);
                    }
                    g_recvJsonPendingLen -= consumed;
                    g_recvJsonBuffer[g_recvJsonPendingLen] = '\0';
                    continue;
                }
            }
        }

        if (g_recvJsonPendingLen >= sizeof(g_recvJsonBuffer) - 1) {
            printf("client: tcp json buffer overflow, resetting\n");
            ResetRecvJsonBuffer();
        }

        int received = 0;
        {
            std::lock_guard<std::mutex> lock(g_tcpSocketMutex);
            received = recv(
                tcpSocket, g_recvJsonBuffer + g_recvJsonPendingLen,
                static_cast<int>(sizeof(g_recvJsonBuffer) - g_recvJsonPendingLen - 1),
                0);
        }
        if (received <= 0) {
            MpDebugLog("client.cpp:RecvJsonMessage", "recv_fail", "H-CONN",
                       received < 0 ? static_cast<uintptr_t>(WSAGetLastError())
                                    : 0,
                       g_recvJsonPendingLen, GetCurrentThreadId());
            return false;
        }

        g_recvJsonPendingLen += static_cast<size_t>(received);
        g_recvJsonBuffer[g_recvJsonPendingLen] = '\0';
    }
}

static bool SendJsonMessage(json msg) {
    const auto data = msg.dump() + '\0';

    std::lock_guard<std::mutex> lock(g_tcpSocketMutex);
    if (!tcpSocket) {
        return false;
    }

    if (send(tcpSocket, data.c_str(), static_cast<int>(data.size()), 0) !=
        static_cast<int>(data.size())) {
        return false;
    }

    return true;
}

static void AddChatMessage(std::string message) {
    static const auto maxMessages = 100;
    static auto messageCount = 0;

    SYSTEMTIME time;
    GetLocalTime(&time);

    char formattedTime[0xFF];
    sprintf_s(formattedTime, sizeof(formattedTime), "%d:%02d: ", time.wHour,
              time.wMinute);

    const auto formattedMsg = formattedTime + message + "\n";

    chat.Mutex.lock();

    chat.Raw += formattedMsg;
    chat.LastTime = GetTickCount64();
    ++messageCount;

    while (messageCount > maxMessages) {
        const auto pos = chat.Raw.find('\n');
        if (pos == std::string::npos) {
            break;
        }

        chat.Raw.erase(0, pos + 1);
        --messageCount;
    }

    chat.Mutex.unlock();
}

static void SendChatInput() {
    if (connected.load()) {
        for (auto c = &chatInput[0]; *c; ++c) {
            if (!isblank(*c)) {
                SendJsonMessage({
                    {"type", "chat"},
                    {"id", client.Id},
                    {"body", chatInput},
                });

                break;
            }
        }
    }

    chatInput[0] = 0;
}

static void Disconnect() {
    const bool fromListener = g_isClientListenerThread;

    MpDebugLog("client.cpp:Disconnect", "disconnect", "H-CONN",
               GetCurrentThreadId(), g_clientListenerThreadId,
               fromListener ? 1u : 0u);

    if (!fromListener) {
        std::lock_guard<std::mutex> lock(g_tcpSocketMutex);
        if (tcpSocket) {
            shutdown(tcpSocket, SD_BOTH);
        }
        return;
    }

    if (connected.load()) {
        SendJsonMessage({
            {"type", "disconnect"},
            {"id", client.Id},
        });

        AddChatMessage("Disconnected");
    }

    if (tcpSocket) {
        std::lock_guard<std::mutex> lock(g_tcpSocketMutex);
        shutdown(tcpSocket, SD_BOTH);
        closesocket(tcpSocket);
        tcpSocket = 0;
    }

    if (udpSocket) {
        shutdown(udpSocket, SD_BOTH);
        closesocket(udpSocket);
        udpSocket = 0;
    }

    players.Mutex.lock();
    std::vector<Classes::ASkeletalMeshActorSpawnable *> actorsToDespawn;
    for (const auto &p : players.List) {
        if (p->Actor) {
            actorsToDespawn.push_back(p->Actor);
        }

        delete p;
    }

    players.List.clear();
    players.ById.clear();
    g_hostedRemoteCount = 0;
    players.Mutex.unlock();

    if (!actorsToDespawn.empty()) {
        QueueClientEngineTask([actorsToDespawn]() {
            for (auto *actor : actorsToDespawn) {
                Engine::Despawn(actor);
            }
        });
    }

    connected.store(false);
    latencyMs = -1;
    ResetRecvJsonBuffer();
}

static void PlayerHandler() {
    while (connected.load()) {
        static Client::PACKET_COMPRESSED packet;

        int serverSize = sizeof(server);
        if (recvfrom(udpSocket, reinterpret_cast<char *>(&packet),
                     sizeof(packet), 0, reinterpret_cast<sockaddr *>(&server),
                     &serverSize) < 0) {

            continue;
        }

        players.Mutex.lock_shared();

        const auto player = GetPlayerById(packet.Id);
        if (player) {
            ApplyPacketSnapshot(player, packet);
        }

        players.Mutex.unlock_shared();
    }
}

static bool Join() {
    if (client.Level.empty()) {
        client.Level = "tdmainmenu";
        loading.store(true);
    }

    if (!SendJsonMessage({
            {"type", "connect"},
            {"room", room},
            {"name", client.Name},
            {"level", client.Level},
            {"character", client.Character},
        })) {

        printf("client: failed to send connect msg\n");
        return false;
    }

    json msg;
    if (!RecvJsonMessage(msg)) {
        printf("client: failed to receive connect\n");
        return false;
    }

    const auto msgType = msg["type"];
    const auto msgId = msg["id"];
    const auto msgGameMode = msg["gameMode"];
    const auto msgTaggedPlayerId = msg["taggedPlayerId"];
    const auto msgCanTag = msg["canTag"];

    if (!msgType.is_string() || msgType != "id" || !msgId.is_number_integer() ||
        !msgGameMode.is_string() || !msgTaggedPlayerId.is_number_integer() ||
        !msgCanTag.is_boolean()) {
        printf("client: malformed connect response\n");
        return false;
    }

    client.Id = msgId;
    client.GameMode = msgGameMode.get<std::string>();
    client.TaggedPlayerId = previousTaggedId = msgTaggedPlayerId;
    client.CanTag = msgCanTag.get<bool>();

    printf("client: joined with id %x\n", client.Id);
    return true;
}

static void ClientListener() {
    g_isClientListenerThread = true;
    g_clientListenerThreadId = GetCurrentThreadId();
    MpDebugLog("client.cpp:ClientListener", "listener_enter", "H-CONN",
               reinterpret_cast<uintptr_t>(GetModuleHandleW(L"mmultiplayer.dll")),
               GetCurrentThreadId(), 0);

    for (;; Disconnect(), Sleep(500)) {
        if (disabled.load()) {
            continue;
        }

        if (Engine::IsHostedMode()) {
            serverHost = "127.0.0.1";
        }

        printf("client: connecting\n");
        MpDebugLog("client.cpp:ClientListener", "connecting", "H-CONN",
                   static_cast<uintptr_t>(serverHost.size()),
                   serverHost == "127.0.0.1" ? 1u : 0u, 0);

        if (!Setup()) {
            MpDebugLog("client.cpp:ClientListener", "setup_fail", "H-CONN",
                       static_cast<uintptr_t>(serverHost.size()), 0, 0);
            continue;
        }

        ResetRecvJsonBuffer();

        tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (tcpSocket < 0) {
            tcpSocket = 0;

            printf("client: failed to create tcp socket\n");
            continue;
        }

        udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udpSocket < 0) {
            udpSocket = 0;

            printf("client: failed to create udp socket\n");
            continue;
        }

        if (connect(tcpSocket, reinterpret_cast<const sockaddr *>(&server),
                    sizeof(server))) {
            printf("client: failed to connect\n");
            MpDebugLog("client.cpp:ClientListener", "tcp_fail", "H-CONN", 0, 0,
                       WSAGetLastError());
            continue;
        }

        int tcpNoDelay = 1;
        setsockopt(tcpSocket, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char *>(&tcpNoDelay),
                   sizeof(tcpNoDelay));

        if (!Join()) {
            MpDebugLog("client.cpp:ClientListener", "join_fail", "H-CONN", 0, 0,
                       0);
            continue;
        }

        connected.store(true);
        MpDebugLog("client.cpp:ClientListener", "connected", "H-CONN",
                   static_cast<uintptr_t>(client.Id),
                   ModHost::IsAttached() ? 1u : 0u,
                   Engine::IsHostedMode() ? 1u : 0u);
        AddChatMessage("Connected");
        UpdateHarnessSnapshot();

        std::thread playerHandlerThread(PlayerHandler);
        std::thread statusThread;

        const auto pingNow = GetTickCount64();
        g_lastPing.store(pingNow, std::memory_order_relaxed);
        g_lastLatencyRequest.store(pingNow, std::memory_order_relaxed);
        if (!Engine::IsHostedMode()) {
            statusThread = std::thread([]() {
            while (connected.load()) {
                Sleep(500);
                UpdateHarnessSnapshot();

                if (!loading.load() && !ModHost::IsAttached() &&
                    GetTickCount64() - g_lastPing.load(std::memory_order_relaxed) >
                        15000) {
                    printf("client: timed out\n");
                    connected.store(false);
                    if (tcpSocket) {
                        std::lock_guard<std::mutex> lock(g_tcpSocketMutex);
                        shutdown(tcpSocket, SD_BOTH);
                    }
                    return;
                }

                if (GetTickCount64() -
                        g_lastLatencyRequest.load(std::memory_order_relaxed) >
                    2000) {
                    if (SendJsonMessage({
                            {"type", "client_ping"},
                            {"id", client.Id},
                            {"ts", static_cast<double>(GetTickCount64())},
                        })) {
                        g_lastPing.store(GetTickCount64(),
                                         std::memory_order_relaxed);
                    }
                    g_lastLatencyRequest.store(GetTickCount64(),
                                               std::memory_order_relaxed);
                }
            }
            });
        }

        json msg;
        MpDebugLog("client.cpp:ClientListener", "listen_start", "H-CONN",
                   static_cast<uintptr_t>(tcpSocket), g_recvJsonPendingLen,
                   GetCurrentThreadId());
        while (RecvJsonMessage(msg)) {
            g_lastPing.store(GetTickCount64(), std::memory_order_relaxed);

            auto msgType = msg["type"];
            if (!msgType.is_string()) {
                continue;
            }

            if (msgType == "connect") {
                const auto msgId = msg["id"];
                const auto msgName = msg["name"];
                const auto msgCharacter = msg["character"];
                const auto msgLevel = msg["level"];

                MpDebugLog("client.cpp:ClientListener", "connect_rx", "H-CONN",
                           ModHost::IsAttached() ? 1u : 0u,
                           msgId.is_number() ? 1u : 0u, 0);

                if (!msgId.is_number() || !msgName.is_string() ||
                    !msgCharacter.is_number() ||
                    !msgLevel.is_string()) {
                    continue;
                }

                players.Mutex.lock();

                const auto player = new Client::Player();
                player->Id = msgId.get<uint32_t>();
                player->Name = msgName.get<std::string>();
                player->Character =
                    static_cast<Engine::Character>(msgCharacter.get<int>());
                player->Level = msgLevel.get<std::string>();
                player->LastPacket = {0};
                player->Actor = nullptr;

                // Default bones
                static const unsigned long defaultBones[] = {
                    0x0,        0x0,        0x0,        0x3f800000, 0x0,
                    0x80000000, 0x0,        0x3f800000, 0xbe8605c3, 0x3e813707,
                    0xbf24045a, 0x3f2d1e27, 0x3d6d4fd8, 0xc2d86b2f, 0x3f7b81d2,
                    0x3f7fffff, 0x3d4f3d5f, 0x3d4b4f09, 0xbd6731d4, 0x3f7ef26f,
                    0x40ad8500, 0xbe38fe00, 0xbd30e280, 0x3f7fffff, 0x3d6d25b8,
                    0x3c50a631, 0xbbcb5123, 0x3f7f8b7b, 0x4176d5a8, 0xbcc36c00,
                    0x3bc3b800, 0x3f7fffff, 0x3b56e31b, 0x3c1d44cc, 0x3c9b6f10,
                    0x3f7ff0d5, 0x416df1c0, 0x3a218000, 0xbc4c6c00, 0x3f7fffff,
                    0x3c344e05, 0xbeaa3619, 0x3d038638, 0x3f71487c, 0x413aca60,
                    0x0,        0x0,        0x3f800000, 0x3e4ff5a3, 0x3dd3f4fe,
                    0xbe113006, 0x3f769aab, 0x40ee5180, 0x0,        0x0,
                    0x3f7fffff, 0xbef44c3d, 0x3f059855, 0xbf059855, 0xbef44c44,
                    0x3fdec8e6, 0x38cfd167, 0x4053a616, 0x3f800000, 0x0,
                    0x80000000, 0x3d1ee107, 0xbf7fcead, 0x40183689, 0x3f932ccd,
                    0xa73c3627, 0x3f800000, 0x0,        0x80000000, 0x0,
                    0xbf7fffff, 0x4019fca4, 0xbf065204, 0xa6d0b2f0, 0x3f800000,
                    0xbdca5ca3, 0xbf2b69a6, 0x3da5375f, 0xbf3b50ed, 0x40ebb7d3,
                    0x3f2c634a, 0x3da798ec, 0x3f800000, 0xbd3c39e0, 0xbf346eaf,
                    0x3d7908a2, 0xbf348daa, 0x40e06ab0, 0x3f451e0d, 0xbfaf8ce4,
                    0x3f800000, 0xbd627d92, 0xbf34770a, 0x3d627d92, 0xbf34770a,
                    0x40dbf9e7, 0x40429764, 0x2824eaa5, 0x3f800000, 0x3f346eaf,
                    0xbd3c39e0, 0xbf348da9, 0xbd79096b, 0x40e06a5d, 0x3f4504ff,
                    0x3faf8d1c, 0x3f800000, 0xbcedb9ef, 0xbcedb9ef, 0xbf34dde7,
                    0xbf34dde9, 0x40f9968e, 0xa96d0eaf, 0x40f84d50, 0x3f800000,
                    0xba53b730, 0xbc798018, 0x0,        0xbf7ff860, 0x40491fe2,
                    0x3d2e3000, 0x3e51f880, 0x3f800000, 0x39a8334d, 0xbb468806,
                    0x3ab56d4b, 0xbf7fffa1, 0x4045f6c0, 0xbe705ebb, 0x3fc2c718,
                    0x3f800000, 0xb7faebf0, 0xbb944618, 0xbb1f532a, 0xbf7fff22,
                    0x403f32ed, 0xb9d4fc15, 0xbb162336, 0x3f800000, 0xbaa24902,
                    0xbac6c3e9, 0x376865f3, 0xbf7fffdf, 0xb1f42200, 0x34a2c140,
                    0xb322c140, 0x3f800000, 0x3f7ffa3c, 0x0,        0x3c476a39,
                    0xbbac98de, 0xc0491fde, 0x3d2e4106, 0x3e51f94f, 0x3f800000,
                    0xbf7ffe13, 0x3ae63dcc, 0xb7faebf4, 0xbbf4385f, 0xc03ed4ca,
                    0xb9e01b3a, 0xb69dcd8f, 0x3f800000, 0xbf7ffe15, 0xba93e861,
                    0x0,        0xbbf82c92, 0xc045d174, 0xbe6e1474, 0x3fc2cac1,
                    0x3f800000, 0xbcedb9ef, 0xbcedb9ef, 0xbf34dde7, 0xbf34dde9,
                    0x411a1f57, 0x4084ad09, 0x412109a8, 0x3f800000, 0xbd0cd319,
                    0xbd0cd319, 0xbf34cd79, 0xbf34cecf, 0x410d1ea7, 0x3f1c3052,
                    0x412a1350, 0x3f800000, 0xbcedb9ef, 0xbcedb9ef, 0xbf34dde7,
                    0xbf34dde9, 0x3fd93a61, 0x3d04bdba, 0x412e9e84, 0x3f800000,
                    0xbce68df9, 0xbd079441, 0xbf3464f3, 0xbf354d25, 0x3f50f1e8,
                    0x400bd200, 0x41191f24, 0x3f800000, 0xbcedb9ef, 0xbcedb9ef,
                    0xbf34dde7, 0xbf34dde9, 0x40ab40f2, 0x408b19e5, 0x4120911b,
                    0x3f800000, 0xbccb5e78, 0xbd02db18, 0xbf34ec4a, 0xbf34d1b9,
                    0x4031478e, 0x40050827, 0x41252b37, 0x3f800000, 0xbced58cd,
                    0xbceeef46, 0xbf348168, 0xbf3539f1, 0x3fbaec25, 0x3fc0e126,
                    0x412648e9, 0x3f800000, 0xbcedb9ef, 0xbcedb9ef, 0xbf34dde7,
                    0xbf34dde9, 0x407b6e40, 0xbd02f406, 0x4143ae0a, 0x3f800000,
                    0xbced8cde, 0xbcedb13b, 0xbf34deb4, 0xbf34dd2e, 0x4003b5d5,
                    0x4093c217, 0x41097a2f, 0x3f800000, 0xbdb90a4b, 0xbf34d3de,
                    0xbf338915, 0xbd054524, 0x41813d01, 0x40da93b4, 0x402d2cd5,
                    0x3f800000, 0x0,        0xbe503700, 0x0,        0xbf7aa6e7,
                    0x41429ba7, 0xa8d00000, 0x3904d5cd, 0x3f800000, 0x0,
                    0x3f2836bb, 0x0,        0xbf40fa1c, 0x408a6fd6, 0x28500000,
                    0xbdfd83ab, 0x3f800000, 0x3f338915, 0x3d05413b, 0xbdb90a4b,
                    0xbf34d3e1, 0x41813d01, 0xc0da93b4, 0x402d2cd5, 0x3f800000,
                    0x0,        0xbe503700, 0x0,        0xbf7aa6e7, 0xc1429ba7,
                    0x286f5d3f, 0xb904d5cd, 0x3f800000, 0x0,        0x3f2836bb,
                    0x0,        0xbf40fa1c, 0xc08a6fd6, 0xa67ae9f6, 0x3dfd83ab,
                    0x3f800000, 0x3f34deb5, 0x3f34dd2c, 0xbced8cdf, 0xbcedb15c,
                    0x4003b0f0, 0xc093c13f, 0x41097a45, 0x3f800000, 0x3f34ed30,
                    0x3f34d0d2, 0xbccb5f87, 0xbd02db33, 0x40314ef4, 0xc005079a,
                    0x41252af7, 0x3f800000, 0x3f34caae, 0x3f34caae, 0xbd0fa646,
                    0xbd12bc15, 0x410d077a, 0xbf1ba6c1, 0x412a29ba, 0x3f800000,
                    0x3f3464f3, 0x3f354d23, 0xbce68df9, 0xbd07964e, 0x3f50eef6,
                    0xc00bd0b0, 0x41191f02, 0x3f800000, 0x3f34dde7, 0x3f34dde7,
                    0xbcedb9ef, 0xbcedbf6c, 0x407b7519, 0x3d02f406, 0x4143add4,
                    0x3f800000, 0x3f34dde7, 0x3f34dde7, 0xbcedb9ef, 0xbcedbf6c,
                    0x411a6ffe, 0xc0849306, 0x4120d2a1, 0x3f800000, 0x3f34dde7,
                    0x3f34dde7, 0xbcedb9ef, 0xbcedbf6c, 0x40ab40ee, 0xc08b19e3,
                    0x412090f6, 0x3f800000, 0x3f34816a, 0x3f3539f0, 0xbced58d0,
                    0xbceeef96, 0x3fbae0ba, 0xbfc0e143, 0x41264926, 0x3f800000,
                    0xbe1938d4, 0xbd07bf75, 0x3f39aa5e, 0x3f2bd417, 0x40a17f68,
                    0x404a9f91, 0x3f29f5cc, 0x3f7fffff, 0xbd2c8e15, 0x3d0b98ae,
                    0x3f22505c, 0x3f457a80, 0x4153afac, 0x3f517700, 0xc00b3680,
                    0x3f7fffff, 0x3cefb5d3, 0x3e60295c, 0xbbd71cc6, 0xbf79ac40,
                    0x41c49739, 0xa91668f7, 0xa6b99ea0, 0x3f7fffff, 0x3eb77684,
                    0x3ab8a283, 0x3d64c7af, 0xbf6e9283, 0x41ce7127, 0xb1cea788,
                    0x34f4e59b, 0x3f7fffff, 0xbcd48a5b, 0x3a2c8fcc, 0x3c1a1235,
                    0xbf7fe706, 0x2960c564, 0xa5777e2b, 0x28c7ca73, 0x3f7fffff,
                    0x3ca2de97, 0x3d837a86, 0x3b92c526, 0xbf7f6b2c, 0x3f4cc3af,
                    0xbe15e7bc, 0x3e1cd9b4, 0x3f7fffff, 0xbd87334a, 0xbd95525e,
                    0xbe9101a3, 0xbf74394d, 0x4103e95b, 0x279b7e36, 0xa6abc62e,
                    0x3f7fffff, 0x0,        0xbcd097ca, 0xbe9b4d2f, 0xbf73da27,
                    0x40860f23, 0x27dffcfd, 0x28577026, 0x3f7fffff, 0x0,
                    0xbcd64b7d, 0xbe9f8bf5, 0xbf732943, 0x40276d9c, 0xa98f2007,
                    0x2a257039, 0x3f7fffff, 0x3c3c011a, 0xbca70299, 0xbbf1c595,
                    0xbf7fec48, 0x3f519d23, 0xbdfb9e8a, 0xbe6f1b9f, 0x3f7fffff,
                    0xbd2ca327, 0x3d567487, 0xbed0636a, 0xbf6933e6, 0x40fe0834,
                    0xa65a5ec5, 0x25fe205e, 0x3f7fffff, 0x0,        0xbd09fe13,
                    0xbecbaaee, 0xbf6ab73c, 0x4086c8f3, 0xa78983de, 0x2843cf75,
                    0x3f7fffff, 0x0,        0xbcb51efb, 0xbe85a947, 0xbf770ed5,
                    0x3ff3aab2, 0xa90cf2b4, 0x29c3e539, 0x3f7fffff, 0xbc31aad3,
                    0xbda1afc3, 0xbd55a785, 0xbf7ed611, 0x3f5c6a28, 0xbead3b06,
                    0xbf4a6ecb, 0x3f7fffff, 0xbc5cd8a4, 0x3d7c5dfb, 0xbf05997b,
                    0xbf59c6b5, 0x40fbdd91, 0xa88e8676, 0xa86b14e1, 0x3f7fffff,
                    0x0,        0xbcfd5a0e, 0xbeb9e44b, 0xbf6e664e, 0x405667c5,
                    0xa7d099d0, 0x281578c5, 0x3f7fffff, 0x0,        0xbcbcb0ff,
                    0xbe8a72d1, 0xbf766476, 0x3fb51c88, 0xa789f7f7, 0x29ac9b0e,
                    0x3f7fffff, 0x3d4d6f76, 0x3df509a7, 0x3d358a95, 0xbf7d9530,
                    0x3f527f05, 0x3d330f25, 0x3f3a15d9, 0x3f7fffff, 0xbda5ab59,
                    0xbdda1918, 0xbe87cac5, 0xbf747248, 0x41032140, 0x27a61d5c,
                    0x282b64c1, 0x3f7fffff, 0x0,        0xbc24341e, 0xbdf53051,
                    0xbf7e2553, 0x4082d2ca, 0xa84b044d, 0xa80d6134, 0x3f7fffff,
                    0x0,        0xbcb0d45c, 0xbe84055a, 0xbf774809, 0x401734de,
                    0x29e6fed8, 0xaa01d153, 0x3f7fffff, 0x3f190049, 0x3e860e26,
                    0xbbc627d7, 0xbf41fd13, 0x3f7c2fda, 0x3f6495ec, 0x40004305,
                    0x3f7fffff, 0x3d5d49d2, 0x3db285c9, 0xbd035d35, 0xbf7e8492,
                    0x408d7e6b, 0x28c8a59d, 0xa9552f76, 0x3f7fffff, 0x0,
                    0x80000000, 0xbd8a96fd, 0xbf7f69c6, 0x407c81e7, 0x29984d45,
                    0xa9bf88c8, 0x3f7fffff, 0x3da34e58, 0x0,        0x0,
                    0xbf7f2f51, 0x414e7127, 0xa69b22f2, 0x28bbecf1, 0x3f7fffff,
                    0x3d2350a1, 0xbb6762cf, 0x3b469adb, 0x3f7fcb2d, 0x0,
                    0x0,        0xb7000000, 0x3f7fffff, 0x3f393865, 0x3f2cd425,
                    0x3e02fe7b, 0x3d8794a6, 0x40a18142, 0xc04a9f94, 0x3f29f9a7,
                    0x3f7fffff, 0xbe596946, 0x3a2e15b4, 0x3f0b7695, 0x3f4fae8f,
                    0xc153afc0, 0xbf516800, 0x400b3670, 0x3f7fffff, 0xbd1dc0a9,
                    0xbeae7ab7, 0x3d3d253d, 0x3f702f11, 0xc1c49838, 0xb9f20000,
                    0x37000000, 0x3f7fffff, 0xbe2ef847, 0xbcae27a9, 0xbd857e01,
                    0x3f7b9fb0, 0xc1ce7060, 0x39ce0000, 0xb9bca000, 0x3f7fffff,
                    0x3cdf815d, 0xbc547158, 0xbc38585f, 0x3f7fddf1, 0xbcc6a000,
                    0xbd2dec00, 0x3e6326c0, 0x3f7fffff, 0x3c85cd1c, 0x3d057f0e,
                    0x3d82cd80, 0xbf7f4e8c, 0xbf4ce3ac, 0x3e15466a, 0xbe1c6b4b,
                    0x3f7fffff, 0xbce04975, 0x3afb0dff, 0xbe72b68c, 0xbf789b0e,
                    0xc103e7be, 0x3a122d9e, 0xb9aa2e1f, 0x3f7fffff, 0x0,
                    0x80000000, 0xbefac829, 0xbf5f3043, 0xc0861178, 0xb98eb65f,
                    0x39121d63, 0x3f7fffff, 0x0,        0x80000000, 0xbea7055b,
                    0xbf71fef2, 0xc0276fa7, 0xb8ee403f, 0x390f8902, 0x3f7fffff,
                    0x3d61ed0a, 0xbd27425a, 0x3d32060c, 0xbf7f2769, 0xbf51a871,
                    0x3dfb643e, 0x3e6f4d8c, 0x3f7fffff, 0xbdef358b, 0x3d98e303,
                    0xbed912fa, 0xbf651ef5, 0xc0fe09a8, 0xb9a151b0, 0x38a26694,
                    0x3f7fffff, 0x0,        0x80000000, 0xbebb2bbe, 0xbf6e47e5,
                    0xc086c73c, 0x39994d35, 0xb93ef555, 0x3f7fffff, 0x0,
                    0x80000000, 0xbe9b0dc6, 0xbf73fa88, 0xbff3ad14, 0xb903b537,
                    0x37e5cfda, 0x3f7fffff, 0x3d033d9a, 0xbdba0bdb, 0xbd4b678d,
                    0xbf7e7dff, 0xbf5c848d, 0x3eacfbe3, 0x3f4a8acf, 0x3f7fffff,
                    0xbe02c377, 0x3ddde49a, 0xbed0e1db, 0xbf65c2c7, 0xc0fbdcd3,
                    0x38360c40, 0xb7fe7ab0, 0x3f7fffff, 0x0,        0x80000000,
                    0xbef3b87a, 0xbf6122ba, 0xc056653b, 0x398c0975, 0xb92284a0,
                    0x3f7fffff, 0x0,        0x80000000, 0xbed0e10e, 0xbf69ba27,
                    0xbfb51fad, 0xb915c318, 0x3895aa60, 0x3f7fffff, 0x3daf56f4,
                    0x3dc6f260, 0x3dc38476, 0xbf7caa63, 0xbf528d9c, 0xbd3404e4,
                    0xbf3a09aa, 0x3f7fffff, 0xbe26b78d, 0xbde3a8e7, 0xbe140887,
                    0xbf783b87, 0xc10321d8, 0xb95bb3d6, 0x39257720, 0x3f7fffff,
                    0x0,        0x80000000, 0xbe44c718, 0xbf7b3a93, 0xc082d30f,
                    0xb8cc24f4, 0x38bfe843, 0x3f7fffff, 0x0,        0x80000000,
                    0xbe732d15, 0xbf78ad39, 0xc01732a7, 0x38b5d673, 0xb8e125c8,
                    0x3f7fffff, 0x3f1c3b12, 0x3e8fde0a, 0xbd9777c0, 0xbf3caa9d,
                    0xbf7c47ca, 0xbf64b275, 0xc0003d5f, 0x3f7fffff, 0x3ec3e948,
                    0xbd9143ea, 0xbdf215c5, 0xbf69dec4, 0xc08d7ccc, 0x39c2dd0a,
                    0x38f3d3f2, 0x3f7fffff, 0x0,        0x80000000, 0xbddec879,
                    0xbf7e7b17, 0xc07c824d, 0xb8841d0e, 0x389bc125, 0x3f7fffff,
                    0xbe4db1c3, 0x3f37eb89, 0x3f29c65a, 0xbd78a9d8, 0x4163bf40,
                    0xc1c152e0, 0xc1dd425f, 0x3f7fffff, 0xbea8c024, 0x398a6773,
                    0x3b3ce4c5, 0x3f71b1ce, 0xc14e7198, 0xb8f40000, 0x38b38000,
                    0x3f7fffff, 0x3e0c83fc, 0xbb1e2be6, 0x3bf1ada7, 0x3f7d920d,
                    0x0,        0xb6800000, 0x0,        0x3f7fffff, 0x3f3a7c56,
                    0x3f28e7cd, 0xbe367eb7, 0xbd42f9d8, 0xc0e19d70, 0x411bde30,
                    0xc015ae9b, 0x3f7fffff, 0xbe3d544f, 0xbac32fe1, 0x3c8d4eb9,
                    0x3f7b8c16, 0x3cbdaa00, 0xc236a106, 0xbc863000, 0x3f7fffff,
                    0x3eb0e7a6, 0xbd8465a3, 0xbc8dbbc1, 0x3f6f9f35, 0x3c971c00,
                    0xc238d924, 0xbd8cec00, 0x3f7fffff, 0xbed6efb5, 0x0,
                    0x0,        0xbf6859a4, 0x29043834, 0xc1780629, 0x2791c62d,
                    0x3f7fffff, 0xbb5706c0, 0x3d91659d, 0x3b082757, 0x3f7f5a24,
                    0xb5800000, 0x0,        0x0,        0x3f7fffff, 0x3daeebee,
                    0xbc484791, 0x3f35be95, 0x3f32eee1, 0xc0e19e60, 0xc11bde34,
                    0xc015ae6d, 0x3f7fffff, 0xbe02ab3d, 0x3a009807, 0x3bf1076c,
                    0x3f7de670, 0xb5800000, 0x42361576, 0x36000000, 0x3f7fffff,
                    0x3eef9c11, 0xbd752f9a, 0xbc76aa3d, 0x3f61af06, 0x36000000,
                    0x42385c1a, 0x35000000, 0x3f7fffff, 0xbebfa9db, 0x0,
                    0x0,        0xbf6d62e7, 0x2755baea, 0x41780617, 0x37bb54b8,
                    0x3f7fffff, 0x3a049678, 0x3d3ec44a, 0x3bb2a169, 0x3f7fb7e6,
                    0x0,        0x37800000, 0xb5800000, 0x3f7fffff};

                memcpy(player->LastPacket.Bones, defaultBones,
                       sizeof(defaultBones));

                if (player->Level == client.Level && !loading.load()) {
                    QueueSpawnPlayerIfReady(player);
                } else {
                    player->Actor = nullptr;
                }

                AddChatMessage(player->Name + " joined the room");

                players.List.push_back(player);
                players.ById[player->Id] = player;
                const auto remoteCount = players.List.size();
                players.Mutex.unlock();
                UpdateHarnessSnapshot();
                MpDebugLog("client.cpp:ClientListener", "remote_join", "H-CONN",
                           static_cast<uintptr_t>(player->Id),
                           static_cast<uintptr_t>(remoteCount), 0);
            } else if (msgType == "name") {
                const auto msgId = msg["id"];
                const auto msgName = msg["name"];

                if (!msgId.is_number_integer() || !msgName.is_string()) {
                    continue;
                }

                players.Mutex.lock_shared();

                const auto player = GetPlayerById(msgId);
                if (player) {
                    auto newName = msgName.get<std::string>();
                    AddChatMessage(player->Name + " renamed to " + newName);
                    player->Name = newName;
                }

                players.Mutex.unlock_shared();
            } else if (msgType == "chat") {
                const auto msgBody = msg["body"];
                if (!msgBody.is_string()) {
                    continue;
                }

                players.Mutex.lock_shared();
                AddChatMessage(msgBody.get<std::string>());
                players.Mutex.unlock_shared();
            } else if (msgType == "level") {
                const auto msgId = msg["id"];
                const auto msgLevel = msg["level"];

                if (!msgId.is_number_integer() || !msgLevel.is_string()) {
                    continue;
                }

                players.Mutex.lock();

                const auto player = GetPlayerById(msgId);
                if (player) {
                    player->Level = msgLevel.get<std::string>();

                    if (player->Level == client.Level) {
                        if (!player->Actor && !loading.load()) {
                            QueueSpawnPlayerIfReady(player);
                        }
                    } else if (player->Actor) {
                        auto *actor = player->Actor;
                        player->Actor = nullptr;
                        QueueClientEngineTask([actor]() {
                            Engine::Despawn(actor);
                        });
                    }
                }

                players.Mutex.unlock();
            } else if (msgType == "character") {
                const auto msgId = msg["id"];
                const auto msgCharacter = msg["character"];

                if (!msgId.is_number_integer() ||
                    !msgCharacter.is_number_integer() || msgCharacter < 0 ||
                    msgCharacter >= Engine::Character::Max) {
                    continue;
                }

                players.Mutex.lock();

                const auto player = GetPlayerById(msgId);
                if (player) {
                    player->Character = msgCharacter;

                    if (!loading.load()) {
                        if (player->Actor) {
                            auto *actor = player->Actor;
                            player->Actor = nullptr;
                            QueueClientEngineTask([actor]() {
                                Engine::Despawn(actor);
                            });
                        }

                        QueueSpawnPlayerIfReady(player);
                    }
                }

                players.Mutex.unlock();
            } else if (msgType == "gameMode") {
                const auto msgGameMode = msg["gameMode"];
                if (!msgGameMode.is_string()) {
                    continue;
                }

                taggedTimed = 0;
                previousTaggedId = 0;
                IgnorePlayerInput(false);

                client.CanTag = false;
                client.TaggedPlayerId = 0;
                client.GameMode = msgGameMode.get<std::string>();
            } else if (msgType == "canTag") {
                taggedTimed = 0;
                client.CanTag = true;

                if (client.Id == client.TaggedPlayerId) {
                    IgnorePlayerInput(false);
                }
            } else if (msgType == "tagged") {
                const auto msgTaggedPlayerId = msg["taggedPlayerId"];
                const auto msgTagCooldown = msg["coolDown"];

                if (!msgTaggedPlayerId.is_number_integer() ||
                    !msgTagCooldown.is_number_integer()) {
                    continue;
                }

                if (players.List.empty()) {
                    SendJsonMessage({
                        {"type", "endGameMode"},
                    });
                    AddChatMessage(
                        "[Tag] Tag has ended since you're the only one in this room");
                    continue;
                }

                client.TaggedPlayerId = msgTaggedPlayerId;
                client.CanTag = false;
                client.CoolDownTag = msgTagCooldown;

                if (client.Id == client.TaggedPlayerId &&
                    !playerDiedAndSentJsonMessage) {
                    char buffer[0xFF];

                    if (previousTaggedId == 0) {
                        sprintf_s(buffer, sizeof(buffer),
                                  "[Tag] %s was randomly choosen to be tagged",
                                  client.Name.c_str());
                    } else {
                        const auto previousTaggedPlayer =
                            GetPlayerById(previousTaggedId);
                        if (previousTaggedPlayer) {
                            sprintf_s(buffer, sizeof(buffer),
                                      "[Tag] %s tagged %s",
                                      previousTaggedPlayer->Name.c_str(),
                                      client.Name.c_str());
                        }
                    }

                    SendJsonMessage({
                        {"type", "announce"},
                        {"body", buffer},
                    });
                }

                taggedTimed = GetTickCount64();
                previousTaggedId = msgTaggedPlayerId;
                IgnorePlayerInput(client.Id == msgTaggedPlayerId);
            } else if (msgType == "ping") {
                if (SendJsonMessage({
                        {"type", "pong"},
                        {"id", client.Id},
                    })) {
                    g_lastPing.store(GetTickCount64(), std::memory_order_relaxed);
                }
            } else if (msgType == "client_pong") {
                g_lastPing.store(GetTickCount64(), std::memory_order_relaxed);
                const auto msgTs = msg["ts"];
                if (msgTs.is_number()) {
                    latencyMs = static_cast<int>(
                        GetTickCount64() -
                        static_cast<unsigned long long>(msgTs.get<double>()));
                }
            } else if (msgType == "disconnect") {
                const auto msgId = msg["id"];
                if (!msgId.is_number_integer()) {
                    continue;
                }

                players.Mutex.lock();
                players.List.erase(std::remove_if(
                    players.List.begin(), players.List.end(),
                    [&msgId](Client::Player *p) {
                        if (p->Id != msgId) {
                            return false;
                        }

                        if (p->Actor) {
                            auto *actor = p->Actor;
                            p->Actor = nullptr;
                            QueueClientEngineTask([actor]() {
                                Engine::Despawn(actor);
                            });
                        }

                        AddChatMessage(p->Name + " left the room");

                        players.ById.erase(p->Id);
                        delete p;
                        return true;
                    }));
                players.Mutex.unlock();
            }
        }

        MpDebugLog("client.cpp:ClientListener", "listen_end", "H-CONN",
                   GetCurrentThreadId(), 0, 0);

        if (statusThread.joinable()) {
            statusThread.join();
        }

        if (playerHandlerThread.joinable()) {
            playerHandlerThread.join();
        }

        printf("client: shutdown\n");
    }
}

static void OnTick(float delta) {
    if (disabled.load()) {
        return;
    }

    static float sum = 0;
    sum += delta;

    if (!loading.load() && connected.load() && sum > 0.016f) {
        const bool inLevel =
            !client.Level.empty() && client.Level != "tdmainmenu";
        auto pawn = inLevel ? Engine::GetPlayerPawn(true) : nullptr;

        static Client::PACKET_COMPRESSED packet;

        if (pawn) {
            packet.Id = client.Id;
            packet.Position = pawn->Location;
            packet.Position.Z += pawn->TargetMeshTranslationZ;
            packet.Yaw = pawn->Rotation.Yaw % 0x10000;

            {
                std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
                g_harnessSnapshot.posX = packet.Position.X;
                g_harnessSnapshot.posY = packet.Position.Y;
                g_harnessSnapshot.posZ = packet.Position.Z;
                g_harnessSnapshot.yaw =
                    static_cast<unsigned short>(packet.Yaw % 0x10000);
            }
            UpdateHarnessSnapshot();
        }

        if (!pawn || !Engine::CanSafelyUsePlayerPawn()) {
            return;
        }

        packet.Id = client.Id;
        packet.Position = pawn->Location;
        packet.Position.Z += pawn->TargetMeshTranslationZ;
        packet.Yaw = pawn->Rotation.Yaw % 0x10000;

        const auto bonesBuffer =
            pawn->Mesh3p ? pawn->Mesh3p->LocalAtoms.Buffer() : nullptr;
        if (!bonesBuffer) {
            sum = 0;
            return;
        }

        const auto bonesBase = reinterpret_cast<byte *>(bonesBuffer);

        for (auto i = 0; i < ARRAYSIZE(CompressedBoneOffsets); ++i) {
            packet.CompressedBones[i] = static_cast<short>(
                roundf(*reinterpret_cast<float *>(
                           bonesBase + CompressedBoneOffsets[i]) *
                       215.0f));
        }

        sendto(udpSocket, reinterpret_cast<const char *>(&packet),
               sizeof(packet), 0,
               reinterpret_cast<const sockaddr *>(&server), sizeof(server));

        sum = 0;
    }
}

static void OnTickGames(float delta) {
    if (client.GameMode != GameMode_Tag) {
        return;
    }

    static float sum = 0;
    sum += delta;

    if (!loading.load() && connected.load() && sum > 0.16f) {
        auto pawn = Engine::GetPlayerPawn();
        if (!pawn) {
            return;
        }

        sum = 0;

        if (client.Id != client.TaggedPlayerId) {
            if (pawn->Health <= 0 && !playerDiedAndSentJsonMessage) {
                SendJsonMessage({
                    {"type", "dead"},
                });

                char buffer[0xFF];
                sprintf_s(buffer, sizeof(buffer),
                          "[Tag] %s died and they will chase instead",
                          client.Name.c_str());

                SendJsonMessage({
                    {"type", "announce"},
                    {"body", buffer},
                });

                playerDiedAndSentJsonMessage = true;
                client.CanTag = false;
            }
        } else if (!client.CanTag) {
            IgnorePlayerInput(true);
        }

        if (playerDiedAndSentJsonMessage && pawn->Health == 100) {
            playerDiedAndSentJsonMessage = false;
        }
    }
}

static void OnRender(IDirect3DDevice9 *device) {
    if (disabled.load()) {
        return;
    }

    static const auto inputHeightOffset = 50.0f;
    static const auto inputWidthOffset = 50.0f;
    static const auto unfocusedChatMessages = 5;

    if (players.ShowNameTags) {
        auto window = ImGui::BeginRawScene("##client-backbuffer-nametags");
        if (window && window->DrawList) {
        players.Mutex.lock_shared();

        for (auto p : players.List) {
            if (p->Level == client.Level && p->Actor &&
                p->Actor->SkeletalMeshComponent) {
                auto pos = p->Actor->Location;
                pos.Z = p->MaxZ + 27.5f;

                if (Engine::WorldToScreen(device, pos)) {
                    auto size = ImGui::CalcTextSize(p->Name.c_str());
                    auto topLeft =
                        ImVec2(pos.X - size.x / 2.0f, pos.Y - size.y);

                    window->DrawList->AddRectFilled(
                        topLeft - ImVec2(3.0f, 1.0f),
                        ImVec2(pos.X + size.x / 2.0f, pos.Y) +
                            ImVec2(3.0f, 1.0f),
                        ImColor(ImVec4(0, 0, 0, 0.4f)));

                    const auto tagged =
                        client.GameMode == GameMode_Tag &&
                        p->Id == client.TaggedPlayerId;
                    window->DrawList->AddText(
                        topLeft,
                        ImColor(tagged ? ImVec4(1, 0, 0, 1) : ImVec4(1, 1, 1, 1)),
                        p->Name.c_str());
                }
            }
        }

        players.Mutex.unlock_shared();
        ImGui::EndRawScene();
        }
    }

    const auto window = ImGui::BeginRawScene("##client-backbuffer-chat");
    if (window && window->DrawList) {
    const auto io = ImGui::GetIO();

    const auto width = io.DisplaySize.x / 3.0f;

    auto opacity = 1.0f;
    if (!chat.Focused) {
        if (chat.ShowOverlay) {
            auto diff =
                static_cast<float>(GetTickCount64() - chat.LastTime) / 1000.0f;
            if (diff > 5.0f) {
                opacity = max(0, 1.0f - (diff - 5.0f));
            }
        } else {
            opacity = 0.0f;
        }
    }

    if (opacity > 0.0f) {
        chat.Mutex.lock();

        auto body = chat.Raw;
        if (!chat.Focused) {
            auto messages = 0;
            for (auto i = static_cast<int>(body.size()) - 1; i >= 0; --i) {
                if (body[i] == '\n') {
                    ++messages;
                }

                if (messages > unfocusedChatMessages) {
                    body = body.substr(i + 1);
                    break;
                }
            }
        }

        const auto height =
            ImGui::CalcTextSize(body.c_str(), nullptr, false, width).y +
            (ImGui::GetTextLineHeight() / 6.0f);

        const auto pos = ImVec2(inputWidthOffset,
                                io.DisplaySize.y - inputHeightOffset - height);

        ImGui::SetWindowPos(pos, ImGuiCond_Always);
        ImGui::SetWindowSize(
            ImVec2(window->Size.x, max(window->Size.y, height)));

        window->DrawList->AddRectFilled(
            pos, ImVec2(pos.x + width, pos.y + height),
            ImColor(ImVec4(0, 0, 0, 0.4f * opacity)));

        ImGui::PushTextWrapPos(width);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, opacity));
        ImGui::TextWrapped("%s", body.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();

        chat.Mutex.unlock();
    }

    ImGui::EndRawScene();
    }

    if (showLatency && connected.load() && latencyMs >= 0) {
        const auto latencyWindow = ImGui::BeginRawScene("##client-backbuffer-latency");
        if (latencyWindow && latencyWindow->DrawList) {
        const auto io = ImGui::GetIO();
        char latencyText[0x40];
        sprintf_s(latencyText, sizeof(latencyText), "Ping: %d ms", latencyMs);

        const auto size = ImGui::CalcTextSize(latencyText);
        const auto pos =
            ImVec2(io.DisplaySize.x - size.x - 12.0f, 12.0f);

        latencyWindow->DrawList->AddRectFilled(
            pos - ImVec2(4.0f, 2.0f),
            ImVec2(pos.x + size.x, pos.y + size.y) + ImVec2(4.0f, 2.0f),
            ImColor(ImVec4(0, 0, 0, 0.45f)));
        latencyWindow->DrawList->AddText(
            pos, ImColor(ImVec4(0.85f, 1.0f, 0.85f, 1.0f)), latencyText);
        ImGui::EndRawScene();
        }
    }

    if (chat.Focused) {
        if (ImGui::BeginRawScene("##client-backbuffer-chatinput")) {
        const auto io = ImGui::GetIO();

        ImGui::SetWindowPos(
            ImVec2(inputWidthOffset, io.DisplaySize.y - inputHeightOffset),
            ImGuiCond_Always);
        ImGui::SetKeyboardFocusHere(0);

        ImGui::PushItemWidth(io.DisplaySize.x - inputWidthOffset * 2);
        ImGui::InputText("##client-chat-overlay-input", chatInput,
                         sizeof(chatInput));
        ImGui::PopItemWidth();

        ImGui::EndRawScene();
        }
    }

    OnRenderGames(device);
}

static void OnRenderGames(IDirect3DDevice9 *device) {
    if (!showTagDistanceOverlay && !showTagCooldownOverlay) {
        return;
    }

    const auto pawn = Engine::GetPlayerPawn();
    const auto controller = Engine::GetPlayerController();
    if (!pawn || !controller) {
        return;
    }

    if (client.Level.empty() || client.Level == Map_MainMenu) {
        return;
    }

    int playersInTheSameLevel = 0;
    float longestNameWidth = 0.0f;

    for (const auto &p : players.List) {
        if (p->Level == client.Level && p->Actor) {
            ++playersInTheSameLevel;
            const float nameWidth =
                ImGui::CalcTextSize(p->Name.c_str(), nullptr, false).x;
            if (nameWidth > longestNameWidth) {
                longestNameWidth = nameWidth;
            }
        }
    }

    if (playersInTheSameLevel == 0) {
        return;
    }

    char buffer[0x200] = {0};
    const auto io = ImGui::GetIO();
    const float textHeight = ImGui::GetTextLineHeight();

    static const float padding = 5.0f;
    static const float maxNameWidth = 192.0f;

    if (showTagDistanceOverlay) {
        auto window = ImGui::BeginRawScene("##tag-info");
        if (window && window->DrawList) {

        static const float rightPadding = 80.0f;
        float y = (playersInTheSameLevel * textHeight) - textHeight + (padding / 2);
        const float x = min(maxNameWidth, longestNameWidth);

        window->DrawList->AddRectFilled(
            ImVec2(),
            ImVec2(rightPadding + x + padding, y + padding + textHeight),
            ImColor(ImVec4(0, 0, 0, 0.4f)));

        for (const auto &p : players.List) {
            if (!p->Actor || p->Level != client.Level) {
                continue;
            }

            const float dist = Distance(p->Actor->Location, pawn->Location);
            if (dist >= 10.0f) {
                sprintf_s(buffer, "%.0f m", dist);
            } else {
                sprintf_s(buffer, "%.1f m", dist);
            }

            auto name = p->Name;
            auto color = ImColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

            if (client.GameMode == GameMode_Tag &&
                p->Id == client.TaggedPlayerId) {
                color = ImColor(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            }

            if (ImGui::CalcTextSize(name.c_str(), nullptr, false).x >
                maxNameWidth) {
                int charactersToRemove = 3;
                do {
                    name = p->Name;
                    name = name.substr(0, name.size() - charactersToRemove++) +
                           "...";
                } while (ImGui::CalcTextSize(name.c_str(), nullptr, false).x >
                         maxNameWidth);
            }

            window->DrawList->AddText(ImVec2(padding, y), color, buffer);
            window->DrawList->AddText(ImVec2(rightPadding, y), color,
                                      name.c_str());

            y -= textHeight;
        }

        ImGui::EndRawScene();
        }
    }

    if (showTagCooldownOverlay) {
        if (taggedTimed == 0) {
            return;
        }

        const auto timeLeftTick =
            taggedTimed +
            (static_cast<unsigned long long>(client.CoolDownTag) * 1000) -
            GetTickCount64();
        const auto timeLeft = static_cast<float>(timeLeftTick) / 1000.0f;

        if (timeLeft < 0.0f || timeLeft > UINT_MAX) {
            return;
        }

        auto playerName = client.Name;
        if (client.Id != client.TaggedPlayerId) {
            const auto player = GetPlayerById(client.TaggedPlayerId);
            if (!player) {
                return;
            }
            playerName = player->Name;
        }

        auto name = playerName;
        if (ImGui::CalcTextSize(name.c_str(), nullptr, false).x > maxNameWidth) {
            int charactersToRemove = 3;
            do {
                name = playerName;
                name = name.substr(0, name.size() - charactersToRemove++) +
                       "...";
            } while (ImGui::CalcTextSize(name.c_str(), nullptr, false).x >
                     maxNameWidth);
        }

        sprintf_s(buffer, "%s can move in %.1f seconds", name.c_str(), timeLeft);

        auto window = ImGui::BeginRawScene("##tag-timeleft");
        if (window && window->DrawList) {

        const float textSize = ImGui::CalcTextSize(buffer, nullptr, false).x;
        const float topMiddleX = io.DisplaySize.x / 2.0f - textSize / 2.0f;

        window->DrawList->AddRectFilled(
            ImVec2(topMiddleX - padding, 0),
            ImVec2(topMiddleX + textSize + padding, textHeight + padding),
            ImColor(ImVec4(0, 0, 0, 0.4f)));
        window->DrawList->AddText(
            ImVec2(topMiddleX, padding / 2.0f),
            ImColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), buffer);

        ImGui::EndRawScene();
        }
    }
}

static void RenderTagMinigamesSection() {
    if (!ImGui::CollapsingHeader("Tag / Minigames##client-tag-section",
                                 ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Text("Tag");

    if (ImGui::Checkbox("Distance Overlay##Tag-DistanceOverlay",
                        &showTagDistanceOverlay)) {
        Settings::SetSetting("games", "tagShowDistanceOverlay",
                             showTagDistanceOverlay);
    }
    HelpMarker(
        "Shows the distance to other players in meters. Tag doesn't need to be "
        "enabled for this");

    if (client.GameMode == GameMode_Tag) {
        ImGui::Checkbox("Cooldown Overlay##Tag-CooldownOverlay",
                        &showTagCooldownOverlay);
        HelpMarker("When someone gets tagged, shows the cooldown at the top "
                   "middle of the screen until they can move again");
    }

    if (client.Level == Map_MainMenu || players.List.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped(
            "You can't start tag when you're in the main menu or when there are "
            "no other players in the room!");
    } else {
        char buffer[0xFF];

        if (client.GameMode == GameMode_None) {
            ImGui::SliderInt("Cooldown Timer##Tag-CooldownSlider", &tagCooldown,
                             1, 60);
            HelpMarker(
                "Cooldown in seconds (1–60). Default is 5.");

            ImGui::Separator();

            if (ImGui::Button("Update Cooldown Timer##Tag-UpdateCooldownTime")) {
                client.CoolDownTag = tagCooldown;
                SendJsonMessage({
                    {"type", "cooldown"},
                    {"cooldown", tagCooldown},
                });

                sprintf_s(buffer, sizeof(buffer),
                          "[Tag] %s changed the cooldown to be %d second%s",
                          client.Name.c_str(), tagCooldown,
                          tagCooldown != 1 ? "s" : "");

                SendJsonMessage({
                    {"type", "announce"},
                    {"body", buffer},
                });
            }

            ImGui::SameLine();
            if (ImGui::Button("Start Tag##client-start-tag")) {
                SendJsonMessage({
                    {"type", "startTagGameMode"},
                });

                sprintf_s(buffer, sizeof(buffer), "[Tag] %s started tag",
                          client.Name.c_str());

                SendJsonMessage({
                    {"type", "announce"},
                    {"body", buffer},
                });
            }
            HarnessUi::Record("mm/multiplayer/client-start-tag", Engine::GetWindow());
        }

        if (client.GameMode == GameMode_Tag) {
            if (ImGui::Button("End Tag##Tag-EndTag")) {
                SendJsonMessage({
                    {"type", "endGameMode"},
                });

                sprintf_s(buffer, sizeof(buffer), "[Tag] %s ended tag",
                          client.Name.c_str());

                SendJsonMessage({
                    {"type", "announce"},
                    {"body", buffer},
                });
            }
        }
    }
}

static void MultiplayerTab() {
    // #region agent log
    MpDebugLog("client.cpp:MultiplayerTab", "tab_enter", "H-T");
    // #endregion

    HarnessUi::BeginFrame();

    if (connected.load() && latencyMs >= 0) {
        ImGui::Text("Status: Connected  |  Ping: %d ms", latencyMs);
    } else if (connected.load()) {
        ImGui::Text("Status: Connected  |  Ping: measuring...");
    } else {
        ImGui::Text("Status: %s",
                    disabled.load() ? "Disabled" : "Connecting");
    }

    ImGui::Separator();

    const auto nameInputCallback = []() {
        if (client.Name != nameInput) {
            auto empty = true;
            for (auto c : std::string(nameInput)) {
                if (!isblank(c)) {
                    empty = false;
                    break;
                }
            }

            if (!empty) {
                AddChatMessage(client.Name + " renamed to " + nameInput);
                client.Name = nameInput;
                Settings::SetSetting("client", "name", client.Name);

                if (connected.load()) {
                    SendJsonMessage({
                        {"type", "name"},
                        {"id", client.Id},
                        {"name", client.Name},
                    });
                }
            }
        }
    };

    ImGui::Text("Name");
    ImGui::SameLine();
    if (ImGui::InputText("##client-name-input", nameInput, sizeof(nameInput),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        nameInputCallback();
    }

    ImGui::SameLine();
    if (ImGui::Button("Change##client-name-button")) {
        nameInputCallback();
    }

    ImGui::Text("Character");
    ImGui::SameLine();

    const auto selectedCharacter =
        Engine::Characters[static_cast<size_t>(client.Character)];

    if (ImGui::BeginCombo("##client-character", selectedCharacter)) {
        for (auto i = 0; i < IM_ARRAYSIZE(Engine::Characters); ++i) {
            const auto c = Engine::Characters[i];
            const auto s = (c == selectedCharacter);

            if (ImGui::Selectable(c, s)) {
                client.Character = static_cast<Engine::Character>(i);
                Settings::SetSetting("client", "character", client.Character);

                if (connected.load()) {
                    SendJsonMessage({
                        {"type", "character"},
                        {"id", client.Id},
                        {"character", client.Character},
                    });
                }
            }

            if (s) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    const auto serverInputCallback = []() {
        if (serverHost != serverInput) {
            auto empty = true;
            for (auto c : std::string(serverInput)) {
                if (!isblank(c)) {
                    empty = false;
                    break;
                }
            }

            if (!empty) {
                serverHost = serverInput;
                Settings::SetSetting("client", "server", serverHost);

                if (connected.load()) {
                    Disconnect();
                }
            }
        }
    };

    ImGui::Text("Server");
    ImGui::SameLine();
    if (ImGui::InputText("##client-server-input", serverInput,
                         sizeof(serverInput),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        serverInputCallback();
    }
    HarnessUi::Record("mm/multiplayer/server-input", Engine::GetWindow());

    ImGui::SameLine();
    if (ImGui::Button("Apply##client-server-button")) {
        serverInputCallback();
    }
    HarnessUi::Record("mm/multiplayer/server-apply", Engine::GetWindow());

    if (ImGui::Checkbox("Interpolation##client-interpolation",
                        &interpolationEnabled)) {
        Settings::SetSetting("client", "interpolation", interpolationEnabled);
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Show Latency##client-show-latency", &showLatency)) {
        Settings::SetSetting("client", "showLatency", showLatency);
    }

    if (interpolationEnabled) {
        if (ImGui::SliderInt("Interp Delay (ms)##client-interp-delay",
                             &interpolationDelayMs, 0, 250)) {
            Settings::SetSetting("client", "interpolationDelay",
                                 interpolationDelayMs);
        }
    }

    const auto roomInputCallback = []() {
        if (room != roomInput) {
            auto empty = true;
            for (auto c : std::string(roomInput)) {
                if (!isblank(c)) {
                    empty = false;
                    break;
                }
            }

            if (!empty) {
                room = roomInput;
                Settings::SetSetting("client", "room", room);

                if (connected.load()) {
                    Disconnect();
                }
            }
        }
    };

    ImGui::Text("Room");
    ImGui::SameLine();
    if (ImGui::InputText("##client-room-input", roomInput, sizeof(roomInput),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        roomInputCallback();
    }

    ImGui::SameLine();
    if (ImGui::Button("Join##client-room-button")) {
        roomInputCallback();
    }
    HarnessUi::Record("mm/multiplayer/client-join", Engine::GetWindow());

    if (ImGui::Hotkey("Chat Keybind##client-chat-keybind", &chat.Keybind)) {
        Settings::SetSetting("client", "chatKeybind", chat.Keybind);
    }

    if (ImGui::Checkbox("Show Nametags##client-show-nametags",
                        &players.ShowNameTags)) {

        Settings::SetSetting("client", "showNameTags", players.ShowNameTags);
    }

    ImGui::SameLine();

    if (ImGui::Checkbox("Show Chat Overlay##client-show-chat",
                        &chat.ShowOverlay)) {

        Settings::SetSetting("client", "showChatOverlay", chat.ShowOverlay);
    }

    ImGui::SameLine();

    bool pauseConnection = disabled.load();
    if (ImGui::Checkbox("Pause Connection##client-pause", &pauseConnection)) {
        disabled.store(pauseConnection);
        if (disabled.load()) {
            Disconnect();
        }
    }

    ImGui::Text("Chat");

    chat.Mutex.lock();
    ImGui::InputTextMultiline(
        "##client-chat", const_cast<char *>(chat.Raw.c_str()), chat.Raw.size(),
        {0, 0}, ImGuiInputTextFlags_ReadOnly);
    chat.Mutex.unlock();

    if (ImGui::InputText("##client-chat-input", chatInput, sizeof(chatInput),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {

        SendChatInput();
    }

    ImGui::SameLine();
    if (ImGui::Button("Send##client-chat-send")) {
        SendChatInput();
    }
    HarnessUi::Record("mm/multiplayer/client-send", Engine::GetWindow());

    players.Mutex.lock_shared();
    if (ImGui::TreeNode("##client-players", "Players (%d)",
                        players.List.size())) {
        for (auto p : players.List) {
            ImGui::Text("%s - %s", p->Name.c_str(), p->Level.c_str());
            ImGui::SameLine();

            if (ImGui::Button(
                    ("Goto##client-goto-" + std::to_string(p->Id)).c_str()) &&
                p->Level == client.Level && p->Actor) {

                auto pawn = Engine::GetPlayerPawn();
                if (pawn) {
                    pawn->Location = p->Actor->Location;
                }
            }
        }

        ImGui::TreePop();
    }
    players.Mutex.unlock_shared();

    ImGui::Separator();
    RenderTagMinigamesSection();
}

static void InstallClientNetworkTickHooks() {
    if (networkTickHooksRegistered) {
        return;
    }
    networkTickHooksRegistered = true;

    Engine::OnTick(OnTick);
    Engine::OnTick(OnTickGames);
}

static void InstallClientRemotePlayerHooks() {
    if (remotePlayerHooksRegistered) {
        return;
    }
    remotePlayerHooksRegistered = true;

    Engine::OnActorTick([](Classes::AActor *actor) {
        if (!actor || disabled.load()) {
            return;
        }

        players.Mutex.lock_shared();

        for (const auto &p : players.List) {
            if (p->Actor == actor && p->Actor->SkeletalMeshComponent &&
                p->Id == p->LastPacket.Id) {
                static Client::PACKET rendered;
                BuildRenderedPacket(p, rendered);

                p->Actor->Location = rendered.Position;
                p->Actor->Rotation = {0, rendered.Yaw, 0};
                p->MaxZ = rendered.Position.Z;
            }
        }

        players.Mutex.unlock_shared();
    });

    Engine::OnBonesTick([](Classes::TArray<Classes::FBoneAtom> *bones) {
        if (disabled.load()) {
            return;
        }

        players.Mutex.lock_shared();

        for (const auto &p : players.List) {
            if (p->Actor && p->Actor->SkeletalMeshComponent &&
                p->Actor->SkeletalMeshComponent->LocalAtoms.Buffer() ==
                    bones->Buffer() &&
                p->Id == p->LastPacket.Id) {

                static Client::PACKET rendered;
                BuildRenderedPacket(p, rendered);
                Engine::TransformBones(p->Character, bones, rendered.Bones);
            }
        }

        players.Mutex.unlock_shared();
    });
}

static void InstallClientSimulationHooks() {
    InstallClientNetworkTickHooks();
    InstallClientRemotePlayerHooks();
}

static void TryActivateHostedGameplay() {
    g_activateGameplayScheduled = false;

    if (!ModHost::IsAttached() || disabled.load()) {
        return;
    }

    SyncCurrentLevelFromWorld();

    if (client.Level.empty()) {
        QueueActivateHostedGameplay();
        return;
    }

    if (client.Level == "tdmainmenu") {
        return;
    }

    if (connected.load()) {
        InstallClientNetworkTickHooks();
    }

    if (!Engine::CanSafelyUsePlayerPawn()) {
        QueueActivateHostedGameplay();
        return;
    }

    InstallClientRemotePlayerHooks();
    Engine::OnRenderScene(OnRender);
    Engine::SetHostedGameplayLive(true);
    MpDebugLog("client.cpp:TryActivateHostedGameplay", "live", "H-LEVEL", 0, 0,
               0);
}

static void QueueActivateHostedGameplay() {
    if (!ModHost::IsAttached()) {
        return;
    }

    Engine::SetHostedGameplayLive(false);
    if (g_activateGameplayScheduled.exchange(true)) {
        return;
    }

    Engine::QueueMainThreadTask(TryActivateHostedGameplay);
}

static void NotifyServerLevel() {
    if (!connected.load() || disabled.load()) {
        return;
    }

    const auto notifyId = client.Id;
    const auto notifyLevel = client.Level;
    QueueClientEngineTask([notifyId, notifyLevel]() {
        if (!connected.load() || disabled.load()) {
            return;
        }

        SendJsonMessage({
            {"type", "level"},
            {"id", notifyId},
            {"level", notifyLevel},
        });
    });
}

static void SyncCurrentLevelFromWorld() {
    if (!client.Level.empty() && client.Level != "tdmainmenu") {
        return;
    }

    if (auto *world = Engine::GetWorld(false)) {
        const auto level =
            ToLower(WideToUtf8(world->GetMapName(false).c_str()));
        if (!level.empty() && level != "tdmainmenu") {
            client.Level = level;
            loading.store(false);
            UpdateHarnessSnapshot();
            NotifyServerLevel();
            MpDebugLog("client.cpp:SyncCurrentLevelFromWorld", "synced", "H-LEVEL",
                       static_cast<uintptr_t>(level.size()), 0, 0);
        }
    }
}

static void SyncCurrentLevelIfInGameplay() {
    SyncCurrentLevelFromWorld();
    UpdateHarnessSnapshot();

    if (ModHost::IsAttached() && !client.Level.empty() &&
        client.Level != "tdmainmenu") {
        QueueActivateHostedGameplay();
    }
}

static void InstallClientRuntimeHooks() {
    MpDebugLog("client.cpp:InstallClientRuntimeHooks", "enter", "H-HOOKS");
    if (!ModHost::IsAttached()) {
        InstallClientSimulationHooks();
        Engine::OnRenderScene(OnRender);
    }

    if (!ModHost::IsAttached()) {
        Engine::OnInput([](unsigned int &msg, int keycode) {
            if (!chat.Focused && msg == WM_KEYDOWN && keycode == chat.Keybind) {
                chat.Focused = true;
                Engine::BlockInput(true);
            }
        });

        Engine::OnSuperInput([](unsigned int &msg, int keycode) {
            if (chat.Focused) {
                if (msg == WM_KEYUP && keycode == VK_RETURN) {
                    SendChatInput();
                    chat.Focused = false;
                    Engine::BlockInput(false);
                } else if (msg == WM_KEYUP && keycode == VK_ESCAPE) {
                    chat.Focused = false;
                    Engine::BlockInput(false);
                }
            }
        });
    }

    Engine::OnPreLevelLoad([](const wchar_t *levelNameW) {
        if (ModHost::IsAttached()) {
            Engine::SetHostedGameplayLive(false);
        }

        const auto level = ToLower(WideToUtf8(levelNameW));
        MpDebugLog("client.cpp:OnPreLevelLoad", "enter", "H-LEVEL",
                   static_cast<uintptr_t>(level.size()), 0, 0);

        players.Mutex.lock();
        loading.store(true);
        client.Level = level;

        for (const auto &p : players.List) {
            if (p->Actor) {
                Engine::Despawn(p->Actor);
                p->Actor = nullptr;
            }
        }

        players.Mutex.unlock();
        UpdateHarnessSnapshot();
    });

    Engine::OnPostLevelLoad([](const wchar_t *levelNameW) {
        const auto level = ToLower(WideToUtf8(levelNameW));
        MpDebugLog("client.cpp:OnPostLevelLoad", "enter", "H-LEVEL",
                   static_cast<uintptr_t>(level.size()), 0, 0);

        if (client.Level != "tdmainmenu") {
            players.Mutex.lock_shared();

            for (const auto &p : players.List) {
                if (!p->Actor && p->Level == client.Level) {
                    QueueSpawnPlayerIfReady(p);
                }
            }

            loading.store(false);
            players.Mutex.unlock_shared();
            if (connected.load()) {
                InstallClientNetworkTickHooks();
            }
            QueueActivateHostedGameplay();
        }

        if (connected.load()) {
            NotifyServerLevel();
        }
    });

    Engine::OnPreDeath([]() {
        players.Mutex.lock_shared();
        loading.store(true);
        players.Mutex.unlock_shared();
    });

    Engine::OnPostDeath([]() {
        players.Mutex.lock_shared();
        loading.store(false);

        for (const auto &p : players.List) {
            if (!p->Actor && p->Level == client.Level) {
                QueueSpawnPlayerIfReady(p);
            }
        }

        players.Mutex.unlock_shared();
    });

    MpDebugLog("client.cpp:InstallClientRuntimeHooks", "hooks_done", "H-HOOKS");
    StartClientListenerIfNeeded();
}

static void StartClientListenerIfNeeded() {
    static std::atomic<bool> listenerStarted{false};
    static HANDLE listenerStartMutex =
        CreateMutexW(nullptr, FALSE, L"Local\\mmultiplayer_client_listener");

    if (!listenerStartMutex) {
        return;
    }

    WaitForSingleObject(listenerStartMutex, INFINITE);
    const bool alreadyStarted = listenerStarted.exchange(true);
    ReleaseMutex(listenerStartMutex);

    if (alreadyStarted) {
        MpDebugLog("client.cpp:StartClientListenerIfNeeded", "listener_skip",
                   "H-CONN", GetCurrentThreadId(), 0, 0);
        return;
    }

    MpDebugLog("client.cpp:StartClientListenerIfNeeded", "listener_spawn",
               "H-CONN", GetCurrentThreadId(), 0, 0);
    std::thread(ClientListener).detach();
}

bool Client::Enable() {
    client.Name =
        Settings::GetSetting("client", "name", "anonymous").get<std::string>();
    strncpy_s(nameInput, sizeof(nameInput) - 1, client.Name.c_str(),
              sizeof(nameInput) - 1);

    room = Settings::GetSetting("client", "room", "lobby").get<std::string>();
    strncpy_s(roomInput, sizeof(roomInput) - 1, room.c_str(),
              sizeof(roomInput) - 1);

    serverHost = Settings::GetSetting("client", "server", "176.58.101.83")
                     .get<std::string>();
    strncpy_s(serverInput, sizeof(serverInput) - 1, serverHost.c_str(),
              sizeof(serverInput) - 1);

    client.Character =
        Settings::GetSetting("client", "character", Engine::Character::Faith)
            .get<Engine::Character>();

    chat.Keybind = Settings::GetSetting("client", "chatKeybind", 0x54);

    players.ShowNameTags = Settings::GetSetting("client", "showNameTags", true);
    chat.ShowOverlay = Settings::GetSetting("client", "showChatOverlay", true);
    interpolationEnabled =
        Settings::GetSetting("client", "interpolation", true).get<bool>();
    interpolationDelayMs =
        Settings::GetSetting("client", "interpolationDelay", 100).get<int>();
    showLatency = Settings::GetSetting("client", "showLatency", true).get<bool>();
    showTagDistanceOverlay =
        Settings::GetSetting("games", "tagShowDistanceOverlay", false)
            .get<bool>();
    tagCooldown = client.CoolDownTag;

    disabled.store(false);
    enabled_ = true;
    Menu::AddTab("Multiplayer", MultiplayerTab);

    if (!hooksRegistered) {
        hooksRegistered = true;
        Engine::QueueMainThreadTask(InstallClientRuntimeHooks);
    }

    return true;
}

static void UpdateHarnessSnapshot() {
    Client::HarnessStatus snap = {};
    snap.connected = connected.load() && !disabled.load();
    snap.currentMap = client.Level;
    players.Mutex.lock_shared();
    snap.remotePlayers = static_cast<int>(players.List.size());
    players.Mutex.unlock_shared();

    snap.inGameplay =
        !client.Level.empty() && client.Level != "tdmainmenu";

    std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
    snap.posX = g_harnessSnapshot.posX;
    snap.posY = g_harnessSnapshot.posY;
    snap.posZ = g_harnessSnapshot.posZ;
    snap.yaw = g_harnessSnapshot.yaw;
    g_harnessSnapshot = snap;
}

bool Client::GetHarnessStatus(HarnessStatus &out) {
    std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
    out = g_harnessSnapshot;
    return true;
}

void Client::RefreshHarnessPose() {
    if (disabled.load() || loading.load() || !connected.load()) {
        return;
    }

    if (client.Level.empty() || client.Level == "tdmainmenu") {
        return;
    }

    auto pawn = Engine::GetPlayerPawn(true);
    if (!pawn) {
        return;
    }

    const float posX = pawn->Location.X;
    const float posY = pawn->Location.Y;
    const float posZ = pawn->Location.Z + pawn->TargetMeshTranslationZ;
    const auto yaw =
        static_cast<unsigned short>(pawn->Rotation.Yaw % 0x10000);

    {
        std::lock_guard<std::mutex> lock(g_harnessSnapshotMutex);
        g_harnessSnapshot.posX = posX;
        g_harnessSnapshot.posY = posY;
        g_harnessSnapshot.posZ = posZ;
        g_harnessSnapshot.yaw = yaw;
    }
    UpdateHarnessSnapshot();
}

void Client::Disable() {
    if (!enabled_) {
        return;
    }

    disabled.store(true);
    enabled_ = false;
    connected.store(false);

    {
        std::lock_guard<std::mutex> lock(g_tcpSocketMutex);
        if (tcpSocket) {
            shutdown(tcpSocket, SD_BOTH);
        }
    }

    if (udpSocket) {
        shutdown(udpSocket, SD_BOTH);
    }

    IgnorePlayerInput(false);
    latencyMs = -1;
    ResetRecvJsonBuffer();
    UpdateHarnessSnapshot();

    Engine::QueueMainThreadTask([]() {
        Menu::RemoveTab("Multiplayer");
    });
}

std::string Client::GetId() const { return "multiplayer"; }

std::string Client::GetName() const { return "Multiplayer"; }

std::string Client::GetDescription() const {
    return "Online multiplayer with rooms, chat, player sync, and Tag mode.";
}