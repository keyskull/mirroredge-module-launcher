#include "client_platform.h"
#include "client_internal.h"

#include "menu_shim.h"
#include "timing_constants.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace ClientInternal {
std::string NormalizeServerHost(std::string host) {
    host.erase(host.begin(),
               std::find_if(host.begin(), host.end(),
                            [](unsigned char ch) { return !std::isspace(ch); }));
    host.erase(std::find_if(host.rbegin(), host.rend(),
                            [](unsigned char ch) { return !std::isspace(ch); })
                   .base(),
               host.end());
    return host;
}

bool IsLoopbackHost(const std::string &host) {
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

std::string GetConfiguredServerHost() {
    std::lock_guard<std::mutex> lock(g_serverHostMutex);
    return serverHost;
}

void SetConfiguredServerHost(const std::string &host) {
    const auto normalized = NormalizeServerHost(host);
    std::lock_guard<std::mutex> lock(g_serverHostMutex);
    serverHost = normalized;
}

ConnectionTarget BuildConnectionTarget() {
    auto host = NormalizeServerHost(GetConfiguredServerHost());
    if (host.empty()) {
        host = "176.58.101.83";
    }

    return {host, IsLoopbackHost(host)};
}

bool Setup(const std::string &host) {
    static bool wsaStarted = false;
    if (!wsaStarted) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
            ClientLog("client: WSAStartup failed");
            return false;
        }

        wsaStarted = true;
    }

    addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result)) {
        ClientLogf("client: getaddrinfo failed for \"%s\"", host.c_str());
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
        ClientLog("client: found no server address");
        return false;
    }

    server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(Client::Port);
    server.sin_addr = serverAddr;

    return true;
}

void ResetRecvJsonBuffer() {
    g_recvJsonPendingLen = 0;
    g_recvJsonBuffer[0] = '\0';
}

bool RecvJsonMessage(json &msg) {
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
                    ClientLogf("client: failed parse -> %.*s",
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
            ClientLog("client: tcp json buffer overflow, resetting");
            ResetRecvJsonBuffer();
        }

        SOCKET sock = 0;
        {
            std::lock_guard<std::mutex> lock(g_tcpSocketMutex);
            sock = tcpSocket;
        }
        if (!sock) {
            MpDebugLog("client.cpp:RecvJsonMessage", "recv_no_socket", "H-CONN",
                       0, g_recvJsonPendingLen, GetCurrentThreadId());
            return false;
        }

        const int received = recv(
            sock, g_recvJsonBuffer + g_recvJsonPendingLen,
            static_cast<int>(sizeof(g_recvJsonBuffer) - g_recvJsonPendingLen - 1),
            0);
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

bool SendJsonMessage(json msg) {
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

void AddChatMessage(std::string message) {
    static const auto maxMessages = 100;
    static auto messageCount = 0;

    SYSTEMTIME time;
    GetLocalTime(&time);

    char formattedTime[0xFF];
    snprintf(formattedTime, sizeof(formattedTime), "%d:%02d: ", time.wHour,
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

void SendChatInput() {
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

void ForceCloseClientSockets() {
    {
        std::lock_guard<std::mutex> lock(g_tcpSocketMutex);
        if (tcpSocket) {
            shutdown(tcpSocket, SD_BOTH);
            closesocket(tcpSocket);
            tcpSocket = 0;
        }
    }

    if (udpSocket) {
        shutdown(udpSocket, SD_BOTH);
        closesocket(udpSocket);
        udpSocket = 0;
    }
}

void ConfigureClientSocketTimeouts(SOCKET sock) {
    if (!sock) {
        return;
    }

    DWORD timeoutMs = Timing::kIpcCallTimeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
}

bool ConnectTcpInterruptible(SOCKET sock) {
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
        return false;
    }

    const int rc =
        connect(sock, reinterpret_cast<const sockaddr *>(&server), sizeof(server));
    if (rc == 0) {
        mode = 0;
        ioctlsocket(sock, FIONBIO, &mode);
        return true;
    }

    if (WSAGetLastError() != WSAEWOULDBLOCK) {
        return false;
    }

    while (!g_shutdownRequested.load() && !disabled.load()) {
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(sock, &writefds);
        timeval timeout = {0, static_cast<long>(Timing::kPipeRetryBackoffMs * 1000)};

        const int sel = select(0, nullptr, &writefds, nullptr, &timeout);
        if (g_shutdownRequested.load()) {
            return false;
        }
        if (sel < 0) {
            return false;
        }
        if (sel == 0) {
            continue;
        }

        int soError = 0;
        int soLen = sizeof(soError);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char *>(&soError), &soLen) != 0) {
            return false;
        }
        if (soError != 0) {
            WSASetLastError(soError);
            return false;
        }

        mode = 0;
        ioctlsocket(sock, FIONBIO, &mode);
        return true;
    }

    return false;
}

void Disconnect() {
    const bool fromListener = g_isClientListenerThread;

    MpDebugLog("client.cpp:Disconnect", "disconnect", "H-CONN",
               GetCurrentThreadId(), g_clientListenerThreadId,
               fromListener ? 1u : 0u);

    // Non-listener (UI thread): signal connected=false first to stop UDP
    // threads, then close sockets. The listener detects recv failure and
    // performs full player/actor cleanup from its own context.
    if (!fromListener) {
        connected.store(false);
        ForceCloseClientSockets();
        return;
    }

    // Listener thread: full disconnect sequence.
    // Store connected=false BEFORE closing sockets to prevent UDP threads
    // from racing on stale socket handles.
    connected.store(false);

    if (SendJsonMessage({
            {"type", "disconnect"},
            {"id", client.Id},
        })) {
        AddChatMessage("Disconnected");
    }

    ForceCloseClientSockets();

    players.Mutex.lock();
    std::vector<Classes::ASkeletalMeshActorSpawnable *> actorsToDespawn;
    for (const auto &p : players.List) {
        if (p) {
            p->HasRemoteBoneMotion = false;
            p->ToTime = 0;
            if (p->Actor) {
                actorsToDespawn.push_back(p->Actor);
            }
            ClearPlayerRemoteVisual(p);
        }

        delete p;
    }

    players.List.clear();
    players.ById.clear();
    g_hostedRemoteCount = 0;
    players.Mutex.unlock();

    if (!actorsToDespawn.empty() && !g_shutdownRequested.load()) {
        // Level unload / full disconnect: null-only elsewhere; skip park writes.
        actorsToDespawn.clear();
    }

    latencyMs = -1;
    ResetRecvJsonBuffer();
}

void PlayerHandlerBody() {
    while (connected.load()) {
        static Client::PACKET_COMPRESSED packet;

        if (!udpSocket) {
            Sleep(Timing::kUdpPollMs);
            continue;
        }

        int serverSize = sizeof(server);
        packet = {};
        const int n =
            recvfrom(udpSocket, reinterpret_cast<char *>(&packet),
                     sizeof(packet), 0, reinterpret_cast<sockaddr *>(&server),
                     &serverSize);
        if (n < 18) {
            continue;
        }

        // 676 = legacy; 688 = +Velocity; 690 = +Move/Phys; 692 = +Seq.
        const int kLegacyPacketBytes = Client::kPacketBytesLegacy;
        const int kVelocityPacketBytes = Client::kPacketBytesVelocity;
        const int kMovePacketBytes = Client::kPacketBytesMove;
        const int kSeqPacketBytes = Client::kPacketBytesSeq;
        if (n < kLegacyPacketBytes || n > kSeqPacketBytes) {
            continue;
        }
        const bool hasVelocityTrailer = (n >= kVelocityPacketBytes);
        const bool hasMoveTrailer = (n >= kMovePacketBytes);
        const bool hasSeqTrailer = (n >= kSeqPacketBytes);

        players.Mutex.lock();

        const auto player = GetPlayerById(packet.Id);
        if (player) {
            ApplyPacketSnapshot(player, packet, hasVelocityTrailer,
                                hasMoveTrailer, hasSeqTrailer);
        }

        players.Mutex.unlock();
    }
}

void PlayerHandlerThunk(void *) { PlayerHandlerBody(); }

void PlayerHandler() {
    DWORD exceptionCode = 0;
    if (!PluginSehGuard::InvokeVoid(
            "multiplayer_player_handler", "client.cpp:PlayerHandler",
            PlayerHandlerThunk, nullptr, &exceptionCode)) {
        MarkPluginThreadCrashed("player_handler", exceptionCode);
    }
}

bool Join() {
    if (client.Level.empty()) {
        client.Level = "tdmainmenu";
        loading.store(true);
    }

    if (!SendJsonMessage({
            {"type", "connect"},
            {"room", room},
            {"name", client.Name},
            {"level", client.Level},
            {"character", static_cast<int>(client.Character)},
        })) {

        ClientLog("client: failed to send connect msg");
        return false;
    }

    json msg;
    if (!RecvJsonMessage(msg)) {
        ClientLog("client: failed to receive connect");
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
        ClientLog("client: malformed connect response");
        return false;
    }

    client.Id = msgId;
    client.GameMode = msgGameMode.get<std::string>();
    client.TaggedPlayerId = previousTaggedId = msgTaggedPlayerId;
    client.CanTag = msgCanTag.get<bool>();

    ClientLogf("client: joined with id %x", client.Id);
    return true;
}

void RemoteStatusThreadBody() {
    while (connected.load()) {
        Sleep(Timing::kStatusPollMs);
        QueueLevelProbe();
        UpdateHarnessSnapshot();

        if (!loading.load() && !ModHost::IsAttached() &&
            GetTickCount64() - g_lastPing.load(std::memory_order_relaxed) >
                15000) {
            ClientLog("client: timed out");
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
                g_lastPing.store(GetTickCount64(), std::memory_order_relaxed);
            }
            g_lastLatencyRequest.store(GetTickCount64(), std::memory_order_relaxed);
        }
    }
}

void HostedStatusThreadBody() {
    while (connected.load()) {
        Sleep(Timing::kStatusPollMs);
        QueueLevelProbe();
        UpdateHarnessSnapshot();

        if (GetTickCount64() -
                g_lastLatencyRequest.load(std::memory_order_relaxed) >
            2000) {
            if (SendJsonMessage({
                    {"type", "client_ping"},
                    {"id", client.Id},
                    {"ts", static_cast<double>(GetTickCount64())},
                })) {
                g_lastPing.store(GetTickCount64(), std::memory_order_relaxed);
            }
            g_lastLatencyRequest.store(GetTickCount64(), std::memory_order_relaxed);
        }
    }
}

void RemoteStatusThreadThunk(void *) { RemoteStatusThreadBody(); }

void HostedStatusThreadThunk(void *) { HostedStatusThreadBody(); }

void RunGuardedClientThread(const char *threadName, const char *context,
                                   const char *location,
                                   PluginSehGuard::VoidThunk thunk) {
    g_isClientBackgroundThread = true;
    DWORD exceptionCode = 0;
    const bool ok = PluginSehGuard::InvokeVoid(context, location, thunk, nullptr,
                                               &exceptionCode);
    g_isClientBackgroundThread = false;
    if (!ok) {
        MarkPluginThreadCrashed(threadName, exceptionCode);
    }
}

void RemoteStatusThread() {
    RunGuardedClientThread("remote_status", "multiplayer_remote_status",
                           "client.cpp:RemoteStatusThread",
                           RemoteStatusThreadThunk);
}

void HostedStatusThread() {
    RunGuardedClientThread("hosted_status", "multiplayer_hosted_status",
                           "client.cpp:HostedStatusThread",
                           HostedStatusThreadThunk);
}

void ClientListenerBody() {
    g_isClientBackgroundThread = true;
    g_isClientListenerThread = true;
    g_clientListenerThreadId = GetCurrentThreadId();
    MpDebugLog("client.cpp:ClientListener", "listener_enter", "H-CONN",
               reinterpret_cast<uintptr_t>(GetModuleHandleW(L"multiplayer.dll")),
               GetCurrentThreadId(), 0);
    ClientLogf("client: listener started thread=%lu", GetCurrentThreadId());

    for (;;) {
        if (disabled.load()) {
            if (g_shutdownRequested.load()) {
                Disconnect();
                break;
            }
            Sleep(Timing::kReconnectPollMs);
            continue;
        }

        const auto target = BuildConnectionTarget();
        const std::string &connectionHost = target.host;
        const bool localServer = target.local;

        ClientLogf("client: connecting to %s:%u room=%s mode=%s",
                   connectionHost.c_str(), static_cast<unsigned>(Client::Port),
                   room.c_str(), localServer ? "local" : "remote");
        MpDebugLog("client.cpp:ClientListener", "connecting", "H-CONN",
                   static_cast<uintptr_t>(connectionHost.size()),
                   localServer ? 1u : 0u, 0);

        if (!Setup(connectionHost)) {
            MpDebugLog("client.cpp:ClientListener", "setup_fail", "H-CONN",
                       static_cast<uintptr_t>(connectionHost.size()), 0, 0);
            SetConnectionError("DNS resolution failed for " + connectionHost);
            Sleep(Timing::kReconnectBackoffBaseMs);
            continue;
        }

        ClearConnectionError();
        ResetRecvJsonBuffer();

        tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (tcpSocket == INVALID_SOCKET) {
            tcpSocket = 0;

            ClientLogf("client: failed to create tcp socket err=%lu", WSAGetLastError());
            SetConnectionError("Failed to create TCP socket");
            Sleep(Timing::kReconnectBackoffBaseMs);
            continue;
        }

    udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket == INVALID_SOCKET) {
        udpSocket = 0;

        ClientLogf("client: failed to create udp socket err=%lu", WSAGetLastError());
        closesocket(tcpSocket);
        tcpSocket = 0;
        SetConnectionError("Failed to create UDP socket");
        Sleep(Timing::kReconnectBackoffBaseMs);
        continue;
    }

        if (!ConnectTcpInterruptible(tcpSocket)) {
            ClientLogf("client: failed to connect err=%lu", WSAGetLastError());
            MpDebugLog("client.cpp:ClientListener", "tcp_fail", "H-CONN", 0, 0,
                       WSAGetLastError());
            ForceCloseClientSockets();
            SetConnectionError("TCP connection refused / timed out");
            const int attempt = g_connectionAttempt.fetch_add(1);
            const DWORD delayMs = Timing::kReconnectBackoffBaseMs *
                static_cast<DWORD>(1 << (attempt > 4 ? 4 : attempt));
            Sleep(delayMs);
            continue;
        }

        g_connectionAttempt.store(0);

        ConfigureClientSocketTimeouts(tcpSocket);

        int tcpNoDelay = 1;
        setsockopt(tcpSocket, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char *>(&tcpNoDelay),
                   sizeof(tcpNoDelay));

        if (!Join()) {
            MpDebugLog("client.cpp:ClientListener", "join_fail", "H-CONN", 0, 0,
                       0);
            ForceCloseClientSockets();
            SetConnectionError("Server rejected join / malformed response");
            const int attempt = g_connectionAttempt.fetch_add(1);
            const DWORD delayMs = Timing::kReconnectBackoffBaseMs *
                static_cast<DWORD>(1 << (attempt > 4 ? 4 : attempt));
            Sleep(delayMs);
            continue;
        }

        g_connectionAttempt.store(0);
        ClearConnectionError();

        connected.store(true);
        g_levelProbeFaults.store(0);
        MpDebugLog("client.cpp:ClientListener", "connected", "H-CONN",
                   static_cast<uintptr_t>(client.Id),
                   ModHost::IsAttached() ? 1u : 0u,
                   localServer ? 1u : 0u);
        ClientLogf("client: connected id=%x mode=%s", client.Id,
                   localServer ? "local" : "remote");
        AddChatMessage("Connected to " + connectionHost);
        UpdateHarnessSnapshot();

        std::thread playerHandlerThread(PlayerHandler);
        std::thread statusThread;

        const auto pingNow = GetTickCount64();
        g_lastPing.store(pingNow, std::memory_order_relaxed);
        g_lastLatencyRequest.store(pingNow, std::memory_order_relaxed);
        if (localServer) {
            statusThread = std::thread(HostedStatusThread);
        } else {
            statusThread = std::thread(RemoteStatusThread);
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

                const auto joinId = msgId.get<uint32_t>();
                const auto joinCharacter = msgCharacter.get<int>();
                if (joinCharacter < 0 ||
                    joinCharacter >= static_cast<int>(Engine::Character::Max)) {
                    ClientLogf("client: ignored connect id=%x invalid character %d",
                                 joinId, joinCharacter);
                    continue;
                }

                players.Mutex.lock();

                if (GetPlayerById(joinId)) {
                    players.Mutex.unlock();
                    ClientLogf("client: ignored duplicate connect id=%x", joinId);
                    continue;
                }

                const auto player = new Client::Player();
                player->Id = joinId;
                player->Name = msgName.get<std::string>();
                player->Character =
                    static_cast<Engine::Character>(joinCharacter);
                player->Level = msgLevel.get<std::string>();
                player->LastPacket = {0};
                player->LastPacket.Id = player->Id;
                player->Actor = nullptr;

                // Faith-layout rest pose (108 FBoneAtom = 864 floats). Kate
                // TransformBones reads src[45..107]; a shorter array used to
                // overrun into heap (KI-2026-005). Keep this at full 108.
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
                static_assert(
                    sizeof(defaultBones) ==
                        PLAYER_PAWN_BONE_COUNT * sizeof(Classes::FBoneAtom),
                    "defaultBones must be full Faith 108-atom layout");

                memcpy(player->LastPacket.Bones, defaultBones,
                       sizeof(defaultBones));
                // Enable TransformBones with Faith rest layout immediately so
                // Kate/Miller remotes are not T-pose until first UDP bone pkt.
                player->HasRemoteBoneMotion = true;
                memcpy(player->LastGoodBonePose.Bones, defaultBones,
                       sizeof(defaultBones));
                player->HasLastGoodBones = true;

                if (player->Level == client.Level && !loading.load()) {
                    QueueSpawnPlayerIfReady(player);
                } else {
                    ClearPlayerRemoteVisual(player);
                }

                AddChatMessage(player->Name + " joined the room");
                ClientLogf("client: peer joined id=%x name=%s defaultBones=1",
                           player->Id, player->Name.c_str());

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

                players.Mutex.lock();

                const auto player = GetPlayerById(msgId);
                if (player) {
                    auto newName = msgName.get<std::string>();
                    AddChatMessage(player->Name + " renamed to " + newName);
                    player->Name = newName;
                }

                players.Mutex.unlock();
            } else if (msgType == "chat") {
                const auto msgBody = msg["body"];
                if (!msgBody.is_string()) {
                    continue;
                }

                players.Mutex.lock_shared();
                AddChatMessage(msgBody.get<std::string>());
                players.Mutex.unlock_shared();
            } else if (msgType == "announce") {
                const auto msgBody = msg["body"];
                if (!msgBody.is_string()) {
                    continue;
                }
                AddChatMessage(msgBody.get<std::string>());
            } else if (msgType == "interact") {
                const auto msgFrom = msg["from"];
                const auto msgTo = msg["to"];
                const auto msgKind = msg["kind"];
                if (!msgFrom.is_number_integer() || !msgTo.is_number_integer() ||
                    !msgKind.is_string()) {
                    continue;
                }
                const unsigned fromId = msgFrom.get<unsigned>();
                const unsigned toId = msgTo.get<unsigned>();
                // Sender already showed local echo.
                if (fromId == client.Id) {
                    continue;
                }
                players.Mutex.lock_shared();
                const auto *fromP = GetPlayerById(fromId);
                const auto *toP =
                    (toId == client.Id) ? nullptr : GetPlayerById(toId);
                const std::string fromName =
                    fromP ? fromP->Name : ("#" + std::to_string(fromId));
                const std::string toName =
                    (toId == client.Id)
                        ? "you"
                        : (toP ? toP->Name : ("#" + std::to_string(toId)));
                players.Mutex.unlock_shared();
                char buffer[0x140];
                snprintf(buffer, sizeof(buffer), "[Interact] %s %s %s",
                         fromName.c_str(), msgKind.get<std::string>().c_str(),
                         toName.c_str());
                AddChatMessage(buffer);
                ClientLogf("client: interact recv kind=%s from=%x to=%x",
                           msgKind.get<std::string>().c_str(), fromId, toId);
                // Wave: face-host yaw window only (Mesh V13) — no bone nudges.
                if (_stricmp(msgKind.get<std::string>().c_str(), "wave") == 0) {
                    players.Mutex.lock();
                    if (auto *fp = GetPlayerById(fromId)) {
                        fp->WaveGestureFrames = 120;
                        ClientLogf("client: remote wave gesture id=%x", fromId);
                    }
                    players.Mutex.unlock();
                }
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
                    ClientLogf("client: remote level id=%x -> %s (local=%s)",
                               player->Id, player->Level.c_str(),
                               client.Level.c_str());

                    if (LevelsCompatible(player->Level, client.Level)) {
                        ClientLogf(
                            "client: remote spawn deferred id=%x live=%d loading=%d actor=%p",
                            player->Id,
                            Engine::IsHostedGameplayLive() ? 1 : 0,
                            loading.load() ? 1 : 0, player->Actor);
                    } else if (PlayerHasRemoteVisual(player)) {
                        player->HasRemoteBoneMotion = false;
                        player->ToTime = 0;
                        ClearPlayerRemoteVisual(player);
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
                    const auto newCharacter =
                        static_cast<Engine::Character>(msgCharacter.get<int>());
                    const bool characterChanged =
                        player->Character != newCharacter;
                    player->Character = newCharacter;

                    // Bots often send "character" after the first spawn already
                    // succeeded with the same Character. Despawning then leaves
                    // Actor=null and spawnedPlayers=0 until a later respawn.
                    if (!loading.load() && characterChanged) {
                        if (PlayerHasRemoteVisual(player)) {
                            player->HasRemoteBoneMotion = false;
                            player->ToTime = 0;
                            ClearPlayerRemoteVisual(player);
                        }

                        ClientLogf("client: character changed id=%x -> %d; respawn deferred",
                                   player->Id, static_cast<int>(newCharacter));
                        if (player->Level == client.Level) {
                            QueueSpawnPlayerIfReady(player);
                        }
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
                ClientLogf("client: tag mode live gameMode=%s",
                           client.GameMode.empty() ? "(none)" : client.GameMode.c_str());
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
                        snprintf(buffer, sizeof(buffer),
                                  "[Tag] %s was randomly choosen to be tagged",
                                  client.Name.c_str());
                    } else {
                        const auto previousTaggedPlayer =
                            GetPlayerById(previousTaggedId);
                        if (previousTaggedPlayer) {
                            snprintf(buffer, sizeof(buffer),
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
                ClientLogf("client: tagged id=%x coolDown=%d",
                           static_cast<unsigned>(msgTaggedPlayerId.get<unsigned>()),
                           msgTagCooldown.get<int>());
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

                        // Null visual only — do not queue park/Despawn after
                        // TransformBones (KI-2026-012: any post-bone mutate hangs,
                        // including drop-drain during Tick). Orphans until level load.
                        SyncActorFromStableSlot(p);
                        p->HasRemoteBoneMotion = false;
                        p->ToTime = 0;
                        ClearPlayerRemoteVisual(p);
                        (void)TakeStableSpawnActor(p->Id);

                        AddChatMessage(p->Name +
                                       " left the room (mesh may remain until "
                                       "map change)");
                        ClientLogf("client: peer left id=%x name=%s",
                                   p->Id, p->Name.c_str());

                        players.ById.erase(p->Id);
                        delete p;
                        return true;
                    }));
                players.Mutex.unlock();
            }
        }

        MpDebugLog("client.cpp:ClientListener", "listen_end", "H-CONN",
                   GetCurrentThreadId(), 0, 0);

        // Detect unexpected disconnection (not user-initiated)
        const bool wasDisconnectedByUser = disabled.load();
        const bool wasConnected = connected.load();

        if (statusThread.joinable()) {
            statusThread.join();
        }

        if (playerHandlerThread.joinable()) {
            playerHandlerThread.join();
        }

        ClientLog("client: shutdown");
        Disconnect();

        // Notify user if connection was lost unexpectedly
        if (wasConnected && !wasDisconnectedByUser) {
            AddChatMessage("Connection lost - reconnecting...");
        }

        // Poll with short sleep to avoid delaying shutdown by a full
        // 500ms when g_shutdownRequested is true.
        for (int i = 0; i < 10; ++i) {
            if (g_shutdownRequested.load()) {
                break;
            }
            Sleep(Timing::kShutdownPollMs);
        }
    }

    g_isClientListenerThread = false;
    g_isClientBackgroundThread = false;
    g_clientListenerThreadId = 0;
    if (g_listenerExitEvent) {
        SetEvent(g_listenerExitEvent);
    }
}

void ClientListenerThunk(void *) { ClientListenerBody(); }

void ClientListener() {
    DWORD exceptionCode = 0;
    if (!PluginSehGuard::InvokeVoid(
            "multiplayer_client_listener", "client.cpp:ClientListener",
            ClientListenerThunk, nullptr, &exceptionCode)) {
        g_isClientBackgroundThread = false;
        g_isClientListenerThread = false;
        g_clientListenerThreadId = 0;
        MarkPluginThreadCrashed("client_listener", exceptionCode);
    }
}
} // namespace ClientInternal
