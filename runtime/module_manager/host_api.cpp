#include "host_api.h"
#include "menu.h"
#include "mod_console.h"
#include "mod_log.h"
#include "mod_registry.h"
#include "plugin_ui_bridge.h"
#include "presentation.h"
#include "me_sdk/runtime/safe_gui_invoke.h"
#include "plugin_seh_guard.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

namespace {

ModHostApi g_api = {};
std::vector<MMOD_RenderSceneCallback> g_renderCallbacks;
std::vector<MMOD_PresentationTickFn> g_presentationTicks;
std::vector<MMOD_PresentationInputSyncFn> g_presentationInputSync;
std::mutex g_renderCallbacksMutex;
std::mutex g_presentationCallbacksMutex;

static void InvokeRenderCallbackSafely(MMOD_RenderSceneCallback callback,
                                       IDirect3DDevice9 *device) {
    MeSdk::Safe::Gui::InvokeRenderOverlay(callback, device);
}

struct PresentationTickContext {
    MMOD_PresentationTickFn callback = nullptr;
    IDirect3DDevice9 *device = nullptr;
};

struct PresentationInputSyncContext {
    MMOD_PresentationInputSyncFn callback = nullptr;
};

void InvokePresentationTick(void *data) {
    auto *ctx = static_cast<PresentationTickContext *>(data);
    ctx->callback(ctx->device);
}

void InvokePresentationInputSync(void *data) {
    auto *ctx = static_cast<PresentationInputSyncContext *>(data);
    ctx->callback();
}

void LogHostCallbackFault(const char *context, DWORD exceptionCode) {
    char message[160] = {};
    snprintf(message, sizeof(message),
             "module_manager: host callback crashed in %s (0x%08lX); callback removed",
             context ? context : "unknown",
             static_cast<unsigned long>(exceptionCode));
    ModLog::Write(message);
}

void HostAddTab(const char *name, MMOD_MenuTabCallback callback) {
    HostMenu::AddTab(name, callback);
}

void HostRemoveTab(const char *name) { HostMenu::RemoveTab(name); }

void HostInsertTab(int index, const char *name, MMOD_MenuTabCallback callback) {
    HostMenu::InsertTab(index, name, callback);
}

void HostOnRenderScene(MMOD_RenderSceneCallback callback) {
    if (callback) {
        std::lock_guard<std::mutex> lock(g_renderCallbacksMutex);
        g_renderCallbacks.push_back(callback);
    }
}

void HostOnPresentationTick(MMOD_PresentationTickFn callback) {
    if (callback) {
        std::lock_guard<std::mutex> lock(g_presentationCallbacksMutex);
        g_presentationTicks.push_back(callback);
    }
}

void HostOnPresentationInputSync(MMOD_PresentationInputSyncFn callback) {
    if (callback) {
        std::lock_guard<std::mutex> lock(g_presentationCallbacksMutex);
        g_presentationInputSync.push_back(callback);
    }
}

void HostInvalidateOverlayGraphics() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
}

void HostCreateOverlayGraphics() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
}

void HostSetOverlayDisplaySize(int width, int height) {
    if (!ImGui::GetCurrentContext() || width < 1 || height < 1) {
        return;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
}

void HostBlockInput(bool block) { HostMenu::SetBlockInput(block); }

bool HostArePresentationHooksInstalled() {
    return Presentation::AreHooksInstalled();
}

void HostQueueMainThreadTask(MMOD_MainThreadTask task) {
    Presentation::QueueMainThreadTask(task);
}

void HostShowMenu() { HostMenu::Show(); }

void HostHideMenu() { HostMenu::Hide(); }

bool HostIsMenuOpen() { return HostMenu::IsOpen(); }

void HostLogMessage(HMODULE module, const char *message) {
    if (!message || !message[0]) {
        return;
    }

    std::wstring moduleId;
    if (!ModRegistry::TryGetModuleId(module, moduleId)) {
        moduleId = L"plugin";
    }
    ModLog::WriteFromModule(moduleId.c_str(), message);
}

bool HostRequestLoadModule(const wchar_t *moduleId, const char *source) {
    if (!moduleId || !moduleId[0]) {
        return false;
    }
    return ModRegistry::RequestLoad(moduleId, source ? source : "host");
}

} // namespace

const ModHostApi *HostApi_Get() {
    static bool initialized = false;
    if (!initialized) {
        g_api.version = MMOD_HOST_API_VERSION;
        g_api.AddTab = HostAddTab;
        g_api.RemoveTab = HostRemoveTab;
        g_api.InsertTab = HostInsertTab;
        g_api.OnRenderScene = HostOnRenderScene;
        g_api.OnPresentationTick = HostOnPresentationTick;
        g_api.OnPresentationInputSync = HostOnPresentationInputSync;
        g_api.InvalidateOverlayGraphics = HostInvalidateOverlayGraphics;
        g_api.CreateOverlayGraphics = HostCreateOverlayGraphics;
        g_api.SetOverlayDisplaySize = HostSetOverlayDisplaySize;
        g_api.BlockInput = HostBlockInput;
        g_api.ArePresentationHooksInstalled = HostArePresentationHooksInstalled;
        g_api.QueueMainThreadTask = HostQueueMainThreadTask;
        g_api.ShowMenu = HostShowMenu;
        g_api.HideMenu = HostHideMenu;
        g_api.IsMenuOpen = HostIsMenuOpen;
        g_api.LogMessage = HostLogMessage;
        g_api.ui = PluginUiBridge_GetApi();
        g_api.RequestLoadModule = HostRequestLoadModule;
        g_api.hostModule = GetModuleHandleW(L"module_manager.dll");
        initialized = true;
    }

    return &g_api;
}

void HostApi_RenderScene(IDirect3DDevice9 *device) {
    std::vector<MMOD_RenderSceneCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(g_renderCallbacksMutex);
        callbacks = g_renderCallbacks;
    }

    for (const auto callback : callbacks) {
        InvokeRenderCallbackSafely(callback, device);
    }
}

void HostApi_PresentationTick(IDirect3DDevice9 *device) {
    std::vector<MMOD_PresentationTickFn> callbacks;
    {
        std::lock_guard<std::mutex> lock(g_presentationCallbacksMutex);
        callbacks = g_presentationTicks;
    }

    for (const auto callback : callbacks) {
        if (callback) {
            PresentationTickContext ctx = {callback, device};
            DWORD exceptionCode = 0;
            if (!PluginSehGuard::InvokeVoid(
                    "host_presentation_tick",
                    "host_api.cpp:HostApi_PresentationTick",
                    InvokePresentationTick, &ctx, &exceptionCode)) {
                std::lock_guard<std::mutex> lock(g_presentationCallbacksMutex);
                g_presentationTicks.erase(
                    std::remove(g_presentationTicks.begin(),
                                g_presentationTicks.end(), callback),
                    g_presentationTicks.end());
                LogHostCallbackFault("presentation_tick", exceptionCode);
            }
        }
    }
}

void HostApi_PresentationInputSync() {
    std::vector<MMOD_PresentationInputSyncFn> callbacks;
    {
        std::lock_guard<std::mutex> lock(g_presentationCallbacksMutex);
        callbacks = g_presentationInputSync;
    }

    for (const auto callback : callbacks) {
        if (callback) {
            PresentationInputSyncContext ctx = {callback};
            DWORD exceptionCode = 0;
            if (!PluginSehGuard::InvokeVoid(
                    "host_presentation_input_sync",
                    "host_api.cpp:HostApi_PresentationInputSync",
                    InvokePresentationInputSync, &ctx, &exceptionCode)) {
                std::lock_guard<std::mutex> lock(g_presentationCallbacksMutex);
                g_presentationInputSync.erase(
                    std::remove(g_presentationInputSync.begin(),
                                g_presentationInputSync.end(), callback),
                    g_presentationInputSync.end());
                LogHostCallbackFault("presentation_input_sync", exceptionCode);
            }
        }
    }
}

bool HostApi_WantsOverlay() {
    bool hasRenderCallbacks = false;
    {
        std::lock_guard<std::mutex> lock(g_renderCallbacksMutex);
        hasRenderCallbacks = !g_renderCallbacks.empty();
    }
    return HostMenu::IsOpen() || ModConsole::IsOpen() || hasRenderCallbacks;
}
