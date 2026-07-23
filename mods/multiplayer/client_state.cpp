#include "client_platform.h"
#include "client_internal.h"

namespace ClientInternal {

char roomInput[0xFF] = {0};
char nameInput[0xFF] = {0};
char serverInput[0xFF] = {0};
char chatInput[0x200] = {0};

std::atomic<bool> connected{false};
std::atomic<bool> loading{false};
std::atomic<bool> disabled{false};
std::atomic<bool> g_shutdownRequested{false};
std::atomic<bool> g_listenerStarted{false};
std::thread g_clientListenerThread;
std::mutex g_clientListenerThreadMutex;
HANDLE g_listenerExitEvent = nullptr;
std::string room;
std::string serverHost;
std::mutex g_serverHostMutex;

bool hooksRegistered = false;
bool networkTickHooksRegistered = false;
bool remotePlayerHooksRegistered = false;
bool renderHookRegistered = false;

std::atomic<int> g_hostedRemoteCount{0};
std::atomic<uint64_t> g_lastPing{0};
std::atomic<uint64_t> g_lastLatencyRequest{0};
std::atomic<int> g_connectionAttempt{0};
std::atomic<unsigned> g_levelProbeFaults{0};
std::atomic<uint64_t> g_lastLevelProbeDiagnostic{0};

std::string g_lastConnectionError;
std::mutex g_lastConnectionErrorMutex;
Client::HarnessStatus g_harnessSnapshot = {};
std::mutex g_harnessSnapshotMutex;

sockaddr_in server = {0};
SOCKET tcpSocket = 0;
SOCKET udpSocket = 0;
std::mutex g_tcpSocketMutex;
char g_recvJsonBuffer[0x10000] = {0};
size_t g_recvJsonPendingLen = 0;

thread_local bool g_isClientListenerThread = false;
thread_local bool g_isClientBackgroundThread = false;

ChatState chat;
Client::Player client = {0};
std::atomic<bool> g_pluginEnabled{false};
PlayersState players;

bool interpolationEnabled = true;
int interpolationDelayMs = 100;
bool interpolationDelayAuto = true;
int interpolationDelayBaseMs = 100;
bool poseSmoothEnabled = true;
float poseSmoothAlpha = 0.45f;
float poseSnapUu = 350.0f;
int hostPoseTxMaxHz = 60;
bool boneSmoothEnabled = true;
float boneSmoothAlpha = 0.55f;
float boneSmoothIdleAlpha = 0.70f;
// Walking remotes: follow UDP bone cycle faster (less smear on limbs).
float boneSmoothWalkAlpha = 0.80f;
bool showRemoteStanceOnNametag = false;
bool showLatency = true;
int latencyMs = -1;

bool showTagDistanceOverlay = false;
bool showTagCooldownOverlay = true;
bool playerDiedAndSentJsonMessage = false;
int tagCooldown = 5;
ULONGLONG taggedTimed = 0;
int previousTaggedId = 0;

bool softCollisionEnabled = true;
float softCollisionRadius = 88.0f;
float softCollisionStrength = 0.55f;

bool worldClampEnabled = true;
float worldClampUp = 80.0f;
float worldClampDown = 400.0f;
// Max |lateral| UU from host body corridor — keeps Kate on narrow tutorial walks
// without Actor::Trace (panels/fence clip). 50 matches harness Cam ±30 slots.
float worldClampMaxLateral = 50.0f;

int interactKeybind = 0x45; // E
float interactMaxMeters = 2.5f;
ULONGLONG lastInteractSentMs = 0;

} // namespace ClientInternal
