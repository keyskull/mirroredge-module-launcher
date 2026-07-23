#pragma once

#include <d3d9.h>
#include <d3dx9.h>

#pragma comment(lib, "d3d9.lib")

#include "me_sdk/me_sdk.h"

#ifdef MM_ENGINE_EXPORTS
#define MM_ENGINE_API __declspec(dllexport)
#elif defined(MM_CORE_EXPORTS)
#define MM_ENGINE_API __declspec(dllimport)
#else
#define MM_ENGINE_API
#endif

#define PLAYER_PAWN_BONE_COUNT (108)

typedef void (*RenderSceneCallback)(IDirect3DDevice9 *device);
typedef bool (*ProcessEventCallback)(Classes::UObject *, Classes::UFunction *,
                                     void *, void *);
typedef void (*LevelLoadCallback)(const wchar_t *levelName);
typedef void (*DeathCallback)();
typedef void (*ActorTickCallback)(Classes::AActor *actor);
typedef void (*BonesTickCallback)(Classes::TArray<Classes::FBoneAtom> *atoms);
typedef void (*TickCallback)(float delta);
typedef void (*InputCallback)(unsigned int &message, int keycode);

namespace Engine {

static const char *Characters[] = {"Faith",           "Kate",        "Celeste",
                                   "Assault Celeste", "Jacknife",    "Miller",
                                   "Kreeg",           "Pursuit Cop", "Ghost"};

enum class Character {
    Faith,
    Kate,
    Celeste,
    AssaultCeleste,
    Jacknife,
    Miller,
    Kreeg,
    PursuitCop,
    Ghost,
    Max
};

MM_ENGINE_API const char *const *GetCharacterNames();
MM_ENGINE_API int GetCharacterNameCount();

MM_ENGINE_API Classes::UTdGameEngine *GetEngine(bool update = false);
MM_ENGINE_API Classes::UTdGameViewportClient *GetViewportClient(bool update = false);
MM_ENGINE_API Classes::UTdConsole *GetConsole(bool update = false);
MM_ENGINE_API void ExecuteCommand(const wchar_t *command);
MM_ENGINE_API bool RunConsoleCommandNow(const wchar_t *command);
MM_ENGINE_API bool LoadLevel(const wchar_t *mapName);
MM_ENGINE_API bool StartGameFromMenu(const wchar_t *mapName);
// Story -> New Game path (tutorial). Prefer over StartGame(LevelName) from harness.
MM_ENGINE_API bool StartNewGameFromMenu(bool playCutScene = false);
MM_ENGINE_API Classes::AWorldInfo *GetWorld(const bool update = false);
MM_ENGINE_API Classes::ATdPlayerController *GetPlayerController(bool update = false);
// EndScene idle warm only: seed PC/world from TdEngine::GamePlayers once.
// Tick must not read GamePlayers — concurrent EndScene+Tick access hangs
// right after drain.warm.engine.idle.done (2026-07-21).
MM_ENGINE_API void CommitIdleWarmPlayerSeed();
MM_ENGINE_API bool TryGetSeedHostPose(float *x, float *y, float *z,
                                      unsigned short *yaw);
MM_ENGINE_API unsigned long long GetIdlePcSeedAgeMs();
MM_ENGINE_API Classes::ATdPlayerPawn *GetPlayerPawn(bool update = false);
MM_ENGINE_API bool CanSafelyUsePlayerPawn();
MM_ENGINE_API void SpawnCharacter(Character character,
                    Classes::ASkeletalMeshActorSpawnable *&spawned);
MM_ENGINE_API void SpawnCharacter(Character character,
                    Classes::ASkeletalMeshActorSpawnable **spawned);
MM_ENGINE_API void Despawn(Classes::ASkeletalMeshActorSpawnable *actor);
MM_ENGINE_API void TransformBones(Character character,
                    Classes::TArray<Classes::FBoneAtom> *dest,
                    Classes::FBoneAtom *src);

MM_ENGINE_API bool IsKeyDown(int);
MM_ENGINE_API bool WorldToScreen(IDirect3DDevice9 *device, Classes::FVector &inOutLocation);
MM_ENGINE_API HWND GetWindow();
MM_ENGINE_API void InjectKeyDown(UINT vk);
MM_ENGINE_API void InjectKeyUp(UINT vk);
MM_ENGINE_API void OnRenderScene(RenderSceneCallback callback);
MM_ENGINE_API void OnProcessEvent(ProcessEventCallback callback);
MM_ENGINE_API void OnPreLevelLoad(LevelLoadCallback callback);
MM_ENGINE_API void OnPostLevelLoad(LevelLoadCallback callback);
MM_ENGINE_API void OnPreDeath(DeathCallback callback);
MM_ENGINE_API void OnPostDeath(DeathCallback callback);
MM_ENGINE_API void OnActorTick(ActorTickCallback callback);
MM_ENGINE_API void OnBonesTick(BonesTickCallback callback);
MM_ENGINE_API void OnTick(TickCallback callback);

// Adds a standard input callback. Will not trigger if the menu is blocking
// input.
MM_ENGINE_API void OnInput(InputCallback callback);

// Adds a super input callback. A super input callback will trigger even if the
// menu is blocking input.
MM_ENGINE_API void OnSuperInput(InputCallback callback);

MM_ENGINE_API void BlockInput(bool block);
MM_ENGINE_API void BeginInitialization();
MM_ENGINE_API bool IsInitializing();
MM_ENGINE_API bool IsModReady();
MM_ENGINE_API void MarkReady();
MM_ENGINE_API bool IsGameReadyForModInit();
using MainThreadTask = void (*)();
MM_ENGINE_API void SetDeferredInitCallback(MainThreadTask initCallback);
MM_ENGINE_API void SetHostedMode(bool hosted);
MM_ENGINE_API bool IsHostedMode();
MM_ENGINE_API void SetHostedGameplayLive(bool live);
MM_ENGINE_API bool IsHostedGameplayLive();
MM_ENGINE_API void ClearFeaturePluginCallbacks();
MM_ENGINE_API bool InstallRendererCapture();
MM_ENGINE_API bool IsModD3D9ProxyActive();
MM_ENGINE_API bool HookDirect3D9Interface(IDirect3D9 *d3d);
MM_ENGINE_API void OnProxyDeviceCreated(IDirect3DDevice9 *device);
MM_ENGINE_API bool TryCaptureRenderer();
MM_ENGINE_API bool ArePresentationHooksInstalled();
MM_ENGINE_API bool InstallPeekMessageBootstrap();
MM_ENGINE_API void QueueMainThreadTask(MainThreadTask task);
MM_ENGINE_API bool Initialize();
MM_ENGINE_API bool InitializeSDK();
MM_ENGINE_API bool AreGameplayHooksInstalled();
MM_ENGINE_API bool InstallGameplayHooks();
MM_ENGINE_API bool EnsureGameplayHooks();
MM_ENGINE_API bool TryInstallGameplayHooksSync();
MM_ENGINE_API bool TryInstallGameplayHooksHosted();
MM_ENGINE_API bool InstallPresentationHooks(IDirect3DDevice9 *device = nullptr);
MM_ENGINE_API bool IsGameplayReadySafe();
MM_ENGINE_API bool TryGetGameplayMapName(char *out, size_t outSize);
MM_ENGINE_API void RequestGameplayActivation(MainThreadTask onActivated = nullptr);
MM_ENGINE_API void CancelGameplayActivation();

} // namespace Engine

enum {
    D3D9_EXPORT_QUERYINTERFACE = 0,
    D3D9_EXPORT_ADDREF,
    D3D9_EXPORT_RELEASE,
    D3D9_EXPORT_TESTCOOPERATIVELEVEL,
    D3D9_EXPORT_GETAVAILABLETEXTUREMEM,
    D3D9_EXPORT_EVICTMANAGEDRESOURCES,
    D3D9_EXPORT_GETDIRECTD,
    D3D9_EXPORT_GETDEVICECAPS,
    D3D9_EXPORT_GETDISPLAYMODE,
    D3D9_EXPORT_GETCREATIONPARAMETERS,
    D3D9_EXPORT_SETCURSORPROPERTIES,
    D3D9_EXPORT_SETCURSORPOSITION,
    D3D9_EXPORT_SHOWCURSOR,
    D3D9_EXPORT_CREATEADDITIONALSWAPCHAIN,
    D3D9_EXPORT_GETSWAPCHAIN,
    D3D9_EXPORT_GETNUMBEROFSWAPCHAINS,
    D3D9_EXPORT_RESET,
    D3D9_EXPORT_PRESENT,
    D3D9_EXPORT_GETBACKBUFFER,
    D3D9_EXPORT_GETRASTERSTATUS,
    D3D9_EXPORT_SETDIALOGBOXMODE,
    D3D9_EXPORT_SETGAMMARAMP,
    D3D9_EXPORT_GETGAMMARAMP,
    D3D9_EXPORT_CREATETEXTURE,
    D3D9_EXPORT_CREATEVOLUMETEXTURE,
    D3D9_EXPORT_CREATECUBETEXTURE,
    D3D9_EXPORT_CREATEVERTEXBUFFER,
    D3D9_EXPORT_CREATEINDEXBUFFER,
    D3D9_EXPORT_CREATERENDERTARGET,
    D3D9_EXPORT_CREATEDEPTHSTENCILSURFACE,
    D3D9_EXPORT_UPDATESURFACE,
    D3D9_EXPORT_UPDATETEXTURE,
    D3D9_EXPORT_GETRENDERTARGETDATA,
    D3D9_EXPORT_GETFRONTBUFFERDATA,
    D3D9_EXPORT_STRETCHRECT,
    D3D9_EXPORT_COLORFILL,
    D3D9_EXPORT_CREATEOFFSCREENPLAINSURFACE,
    D3D9_EXPORT_SETRENDERTARGET,
    D3D9_EXPORT_GETRENDERTARGET,
    D3D9_EXPORT_SETDEPTHSTENCILSURFACE,
    D3D9_EXPORT_GETDEPTHSTENCILSURFACE,
    D3D9_EXPORT_BEGINSCENE,
    D3D9_EXPORT_ENDSCENE,
    D3D9_EXPORT_CLEAR,
    D3D9_EXPORT_SETTRANSFORM,
    D3D9_EXPORT_GETTRANSFORM,
    D3D9_EXPORT_MULTIPLYTRANSFORM,
    D3D9_EXPORT_SETVIEWPORT,
    D3D9_EXPORT_GETVIEWPORT,
    D3D9_EXPORT_SETMATERIAL,
    D3D9_EXPORT_GETMATERIAL,
    D3D9_EXPORT_SETLIGHT,
    D3D9_EXPORT_GETLIGHT,
    D3D9_EXPORT_LIGHTENABLE,
    D3D9_EXPORT_GETLIGHTENABLE,
    D3D9_EXPORT_SETCLIPPLANE,
    D3D9_EXPORT_GETCLIPPLANE,
    D3D9_EXPORT_SETRENDERSTATE,
    D3D9_EXPORT_GETRENDERSTATE,
    D3D9_EXPORT_CREATESTATEBLOCK,
    D3D9_EXPORT_BEGINSTATEBLOCK,
    D3D9_EXPORT_ENDSTATEBLOCK,
    D3D9_EXPORT_SETCLIPSTATUS,
    D3D9_EXPORT_GETCLIPSTATUS,
    D3D9_EXPORT_GETTEXTURE,
    D3D9_EXPORT_SETTEXTURE,
    D3D9_EXPORT_GETTEXTURESTAGESTATE,
    D3D9_EXPORT_SETTEXTURESTAGESTATE,
    D3D9_EXPORT_GETSAMPLERSTATE,
    D3D9_EXPORT_SETSAMPLERSTATE,
    D3D9_EXPORT_VALIDATEDEVICE,
    D3D9_EXPORT_SETPALETTEENTRIES,
    D3D9_EXPORT_GETPALETTEENTRIES,
    D3D9_EXPORT_SETCURRENTTEXTUREPALETTE,
    D3D9_EXPORT_GETCURRENTTEXTUREPALETTE,
    D3D9_EXPORT_SETSCISSORRECT,
    D3D9_EXPORT_GETSCISSORRECT,
    D3D9_EXPORT_SETSOFTWAREVERTEXPROCESSING,
    D3D9_EXPORT_GETSOFTWAREVERTEXPROCESSING,
    D3D9_EXPORT_SETNPATCHMODE,
    D3D9_EXPORT_GETNPATCHMODE,
    D3D9_EXPORT_DRAWPRIMITIVE,
    D3D9_EXPORT_DRAWINDEXEDPRIMITIVE,
    D3D9_EXPORT_DRAWPRIMITIVEUP,
    D3D9_EXPORT_DRAWINDEXEDPRIMITIVEUP,
    D3D9_EXPORT_PROCESSVERTICES,
    D3D9_EXPORT_CREATEVERTEXDECLARATION,
    D3D9_EXPORT_SETVERTEXDECLARATION,
    D3D9_EXPORT_GETVERTEXDECLARATION,
    D3D9_EXPORT_SETFVF,
    D3D9_EXPORT_GETFVF,
    D3D9_EXPORT_CREATEVERTEXSHADER,
    D3D9_EXPORT_SETVERTEXSHADER,
    D3D9_EXPORT_GETVERTEXSHADER,
    D3D9_EXPORT_SETVERTEXSHADERCONSTANTF,
    D3D9_EXPORT_GETVERTEXSHADERCONSTANTF,
    D3D9_EXPORT_SETVERTEXSHADERCONSTANTI,
    D3D9_EXPORT_GETVERTEXSHADERCONSTANTI,
    D3D9_EXPORT_SETVERTEXSHADERCONSTANTB,
    D3D9_EXPORT_GETVERTEXSHADERCONSTANTB,
    D3D9_EXPORT_SETSTREAMSOURCE,
    D3D9_EXPORT_GETSTREAMSOURCE,
    D3D9_EXPORT_SETSTREAMSOURCEFREQ,
    D3D9_EXPORT_GETSTREAMSOURCEFREQ,
    D3D9_EXPORT_SETINDICES,
    D3D9_EXPORT_GETINDICES,
    D3D9_EXPORT_CREATEPIXELSHADER,
    D3D9_EXPORT_SETPIXELSHADER,
    D3D9_EXPORT_GETPIXELSHADER,
    D3D9_EXPORT_SETPIXELSHADERCONSTANTF,
    D3D9_EXPORT_GETPIXELSHADERCONSTANTF,
    D3D9_EXPORT_SETPIXELSHADERCONSTANTI,
    D3D9_EXPORT_GETPIXELSHADERCONSTANTI,
    D3D9_EXPORT_SETPIXELSHADERCONSTANTB,
    D3D9_EXPORT_GETPIXELSHADERCONSTANTB,
    D3D9_EXPORT_DRAWRECTPATCH,
    D3D9_EXPORT_DRAWTRIPATCH,
    D3D9_EXPORT_DELETEPATCH,
    D3D9_EXPORT_CREATEQUERY
};
