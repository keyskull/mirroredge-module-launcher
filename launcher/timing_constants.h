#pragma once

// Launcher-internal timing / polling constants.
// Configurable timeouts live in config.h (LauncherConfig).

namespace LauncherTiming {

constexpr DWORD kSleepInterruptChunkMs      = 200;  // chunk size in SleepInterruptible
constexpr DWORD kReadyEventPollStepMs       = 200;  // WaitForSingleObject poll step
constexpr DWORD kGameProcessPollMs          = 200;  // poll while waiting for game .exe
constexpr DWORD kManagerReadyPollMs         = 100;  // poll while waiting for module_manager
constexpr DWORD kPipeCreateRetryMs          = 500;  // CreateNamedPipeW failure delay
constexpr DWORD kPipeConnectRetryMs         = 200;  // ConnectNamedPipe failure delay
constexpr int   kConfigBypassMaxRetries     = 25;   // config-integrity bypass attempt limit
constexpr DWORD kConfigBypassRetryMs        = 40;   // delay between bypass attempts
constexpr int   kWatchProcessMaxRetries     = 40;   // OpenProcess retry limit in watcher
constexpr DWORD kWatchProcessRetryMs        = 50;   // delay between OpenProcess retries
constexpr DWORD kInputRestoreFirstDelayMs   = 150;  // delay after game exits before 1st restore
constexpr DWORD kInputRestoreSecondDelayMs  = 350;  // delay between 1st and 2nd restore
constexpr int   kInputRestoreBurstCount     = 4;    // restore-attempt count per burst
constexpr UINT  kInputRestoreBurstIntervalMs = 250; // SetTimer interval between burst calls
constexpr DWORD kConfigBypassMonitorPollMs  = 50;   // background monitor poll interval

} // namespace LauncherTiming
