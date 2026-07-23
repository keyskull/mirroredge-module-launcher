#pragma once
// timing_constants.h — Named timeout/sleep constants shared across modules.
//
// These values are tuned for Mirror's Edge 1.0 startup and IPC timing.
// Keep them centralized to avoid magic-number duplication.

#include <windows.h>

namespace Timing {

// ---- IPC ----
constexpr DWORD kPipeRetryBackoffMs    = 200;   // Retry wait between pipe connect attempts
constexpr DWORD kPumpThreadIntervalMs  = 25;    // Pump thread sleep per tick
constexpr DWORD kIpcCallTimeoutMs      = 10000; // Max wait for IPC response

// ---- Core init ----
constexpr DWORD kStandaloneInitWaitMs   = 8000;  // Wait for game bootstrap
constexpr DWORD kD3D9PollIntervalMs     = 100;   // d3d9.dll load polling
constexpr DWORD kPostBootstrapSettleMs  = 3000;  // Settle after PeekMessage bootstrap
constexpr int   kGameReadyMaxPolls      = 300;   // Max IsGameReadyForModInit polls
constexpr int   kInitCompleteMaxPolls   = 900;   // Max init-complete polls
constexpr DWORD kInitDeadlineMs         = 240000; // 4-min init deadline

// ---- Engine hooks ----
constexpr DWORD kGameplayHooksTimeoutMs = 30000; // Max wait for gameplay hooks
constexpr DWORD kMaxSpawnQueueAgeMs     = 10000; // Max age for spawn queue entries
constexpr DWORD kLevelLoadExpireMs      = 10000; // Auto-expire stuck level load flag
constexpr DWORD kHookPollMs             = 1;     // Poll yield for hook function pointer resolution
constexpr DWORD kLazyInstallPollMs      = 5;     // Poll interval for lazy hook install wait

// ---- D3D9 / module_manager ----
constexpr DWORD kD3D9PollMs            = 50;    // Device-ready polling interval
constexpr DWORD kThreadStartSettleMs   = 32;    // Post-CreateThread settle (~2 frames)
constexpr DWORD kAutoloadYieldMs       = 4;     // Autoload loop yield

// ---- Frame-rate related ----
constexpr int kTargetFps               = 60;    // Nominal game frame rate
constexpr DWORD kOneFrameMs            = 16;    // ~1 frame at 60 FPS (best-effort pump sleep)
constexpr DWORD kSpawnDrainIntervalMs  = 200;   // Drain spawn queue on EndScene

// ---- Multiplayer network ----
constexpr DWORD kUdpPollMs             = 10;    // Poll interval when UDP socket unavailable
constexpr DWORD kStatusPollMs          = 500;   // Status thread update interval
constexpr DWORD kReconnectPollMs       = 100;   // Retry poll when client disabled
constexpr DWORD kReconnectBackoffBaseMs = 250;  // Base delay for exponential backoff
constexpr DWORD kShutdownPollMs        = 50;    // Per-iteration wait during graceful shutdown

} // namespace Timing
