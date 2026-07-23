# Mirror's Edge SDK 技术文档 (`shared/me_sdk/`)

## 概览

`shared/me_sdk/` 是 Mirror's Edge (1.0) 的 Unreal Engine 3 逆向 SDK。提供以下核心能力：

- **UE3 类型映射** — 从游戏内存中安全读取 UE3 对象（UObject, AActor, APawn 等）的 C++ 结构体定义
- **全局变量初始化** — 通过 opcode pattern scan 定位 GNames / GObjects 内存地址
- **Safe 访问层** — SEH + 内存保护检查的安全封装，防止非法内存访问导致游戏崩溃
- **模式扫描** — 用于 hook 安装（ProcessEvent, LevelLoad, Tick 等）的 code pattern 定义
- **运行时验证** — 游戏二进制校验、类布局验证、代码探针门控
- **工具函数** — FVector/FRotator 数学运算、FName 字符串查询

## 目录结构

```
shared/me_sdk/
├── me_sdk.h                          # 总头文件 — include 所有 generated 包
├── sdk_verify.h                      # static_assert 宏定义
├── sdk_verify_generated.h            # 自动生成的类布局参考 (40 个类 + 3 个成员)
│
├── util/                             # 基础类型与工具
│   ├── ME_Basic.hpp                  # TArray<>, FName, FString, TEnumAsByte, FScriptInterface
│   ├── ME_Basic.cpp                  # UObject::GetFullName() 实现
│   ├── constants.h                   # 地图名/游戏模式常量 (Map_MainMenu, GameMode_Tag 等)
│   └── math.h                        # FVector/FRotator 数学运算 (Distance, VectorToRotator 等)
│
├── runtime/                          # 运行时 SDK 实现
│   ├── init.h / init.cpp             # 全局变量初始化 (ProbeGlobals, InitializeGlobals, ValidateRuntime)
│   ├── sdk_errors.h                  # 错误码枚举 (SdkError) + 运行时状态 (RuntimeStatus)
│   ├── game_signature.h              # 游戏二进制签名 (image size, code probe FNV gate)
│   ├── pattern.h / pattern.cpp       # 底层 pattern scan (FindPattern)
│   ├── patterns_globals.h            # GNames / GObjects opcode 签名
│   │
│   ├── safe_seh.h                    # SEH 过滤器宏 (ME_SDK_SAFE_EXCEPT_FILTER)
│   ├── safe_access.h / safe_access.cpp   # 安全内存访问层 (IsPlausibleUObject, TryMemcpy, TryProcessEvent...)
│   ├── safe_gui.h / safe_gui.cpp         # 安全 UI/世界对象查询 (TryFindTdPlayerController, TryReadWorldMenuState...)
│   ├── safe_gameplay.h / safe_gameplay.cpp # 安全玩法数据读写 (TryReadPawnPose, TryReadActorLocation...)
│   └── safe_gui_invoke.h / safe_gui_invoke.cpp # 安全 menu/render 回调执行
│
├── patterns/                         # Hook / 特征签名
│   ├── hooks.h                       # 玩法 hook 目标签名 (ProcessEvent, LevelLoad, Tick, ActorTick, BonesTick...)
│   └── tdgame.h                      # TdGame 特定签名 (StateHandler, ForceRoll)
│
└── generated/                        # UE3 头文件 dump
    ├── ME_Core_structs.hpp           # FVector, FRotator, FLinearColor, FBox... (值类型)
    ├── ME_Core_classes.hpp           # UObject, UField, UStruct, UFunction, UClass... (引用类型)
    │                                  #   (UStruct::Script at offset 0x50 — TArray<uint8_t>)
    ├── ME_Core_parameters.hpp        # Core 包中 UFunction 的 ProcessEvent 参数结构体
    ├── ME_Core_functions.cpp         # Core 包的 UFunction::ProcessEvent 链接
    ├── ME_Engine_*                    # Engine 包 (AActor, APawn, AWorldInfo, UGameViewportClient...)
    ├── ME_TdGame_*                    # TdGame 包 (ATdPlayerPawn, ATdPlayerController, UTdGameEngine...)
    ├── ME_Ts_*, ME_Tp_*, ME_Fp_*...  # 其他 UE3 包
    └── ... (共 16 个 UE3 包)

shared/ue_patcher/                     # 运行时 UE3 字节码/EXE 补丁框架
├── patcher_types.h                    # BinaryPatch, VersionedPatch, PatchSession 类型
├── exe_patcher.h / .cpp              # PE 二进制补丁（基于 Pattern::FindPattern）
├── ue3_opcodes.h / .cpp              # UE3 字节码操作码定义 (v536)
└── ue3_bytecode.h / .cpp             # 字节码解码器/迭代器/模式扫描/补丁器

shared/me_sdk/versions/               # 多版本游戏识别
├── me_version.h / .cpp               # PE 版本检测 (Retail/Steam/EA App/GOG)
└── me_versions.json                  # 已知偏移地址数据（仿 MET Offsets/*.json）
```

## 核心概念

### 1. UE3 类系统

UE3 使用反射系统，所有游戏对象的基类是 `UObject`。类层次结构：

```
UObject
├── UField
│   ├── UStruct
│   │   ├── UFunction     — 函数定义
│   │   └── UClass        — 类定义 (含 IsA 继承链)
│   └── UEnum
├── AActor
│   ├── AController
│   │   └── APlayerController
│   │       └── ATdPlayerController
│   ├── APawn
│   │   └── ATdPawn
│   │       └── ATdPlayerPawn
│   └── AWorldInfo
├── UEngine
│   └── UGameEngine
│       └── UTdGameEngine
└── UCanvas, UGameViewportClient, UPlayerInput...
```

### 2. 全局变量：GNames 和 GObjects

游戏运行时在内存中维护两个关键全局数组：

| 全局变量 | 存储内容 | 用途 |
|----------|---------|------|
| `GNames` | `TArray<FNameEntry*>` — 所有 FName 字符串的注册表 | 反查名字索引对应的字符串 |
| `GObjects` | `TArray<UObject*>` — 所有已分配的 UObject | 遍历寻找特定类型的对象 |

SDK 通过 opcode pattern scan 定位这两个数组的指针（详见 [初始化流程](#初始化流程)）。

### 3. pak 4-byte packing 问题

UE3 游戏二进制使用 `#pragma pack(push, 4)` 对齐。因此所有 generated 头文件和 ME_Basic.hpp 都用 `#pragma pack(push, 0x4)` 包裹。**编译时 static_assert (`sdk_verify_generated.h`) 不能在同一个 pack 上下文之外 include**，否则某些类的大小计算错误。

## 基础类型 (util/ME_Basic.hpp)

### `TArray<T>` — UE3 动态数组

```cpp
template<class T>
struct TArray {
    T* Data;      // 0x00 — 数据指针
    int32_t Count; // 0x04 — 当前元素数
    int32_t Max;   // 0x08 — 容量
};
```

`TArray` 总大小 = 0x0C (12 字节)。UE3 对象中所有数组字段（如 `UObject::GObjects`, `FName::GNames` 等）都使用 `TArray`。

### `FName` — UE3 名字表索引

```cpp
struct FName {
    int32_t Index;   // 在 GNames 数组中的索引
    int32_t Number;  // 去重编号 (相同路径的不同对象实例)
};
```

- 总大小 = 0x08 (8 字节)
- 通过 `FName::GNames[Index]->GetName()` 反向查找字符串
- `Index=0` 的 FName 总为 "None"
- 构造函数支持从字符串查找：`FName("SomeClassName")` — 需 GNames 已初始化

### `FString` — UE3 字符串

```cpp
struct FString : private TArray<wchar_t> {  // 继承自 TArray，存储宽字符
    const wchar_t* c_str() const;
    std::string ToString() const;
};
```

### `TEnumAsByte<T>` — UE3 字节枚举

UE3 将枚举值存储为 `uint8_t`（单字节）。`TEnumAsByte` 封装字节读写：
```cpp
TEnumAsByte<EMovementState> MovementState;
EMovementState state = MovementState.GetValue();  // 安全读取
```

### `FScriptInterface` / `TScriptInterface<T>` — UE3 接口指针

UE3 的接口引用是双指针结构（Object + Interface），用 `TScriptInterface<T>` 封装。

## 初始化流程 (runtime/init.cpp)

### GNames / GObjects 定位

SDK 通过 opcode pattern scan 在 `MirrorsEdge.exe` 二进制中定位 GNames 和 GObjects 指针：

```cpp
// 两个核心 opcode 模式 (patterns_globals.h)
GNames:   \x8B\x0D????\x8B\x84\x24????\x8B\x04\x81  → mov ecx, [GNames]
GObjects: \x8B\x15????\x8B\x0C\xB2\x8D\x44\x24\x30    → mov edx, [GObjects]
```

模式匹配后，从指令的操作数字节提取绝对地址（`patternPtr + 2`），得到 `TArray<>*` 指针。

### 三级初始化函数

| 函数 | 用途 | 何时调用 |
|------|------|----------|
| `ProbeGlobals()` | 只扫描不赋值 — 检查 GNames/GObjects 是否可用 | `core` 自旋等待 (`IsGameReadyForModInit`)（限流 ~100ms） |
| `InitializeGlobals()` | 扫描并赋值 `FName::GNames` 和 `UObject::GObjects` | `core` 模块初始化早期（`CompleteModInitialization`） |
| `AreGlobalsReady()` | 检查两个全局指针非空且数组合法 | 所有 Safe 访问函数的前置条件 |

关键路径：
```
core 初始化 → InitializeGlobals()
  → FindPattern(GNames) → 提取 TArray<FNameEntry*>* → 赋值 FName::GNames
  → FindPattern(GObjects) → 提取 TArray<UObject*>* → 赋值 UObject::GObjects
  → ValidateGlobalsTables() → 检查数组大小、非空、None 名字
```

### 二进制验证 (`ValidateRuntime`)

`ValidateRuntime()` 在 `core` 初始化时执行完整的验证链：

1. **ValidateGameBinary()** — 检查加载的 `MirrorsEdge.exe`：
   - MODULEINFO image size 落在 `[0x03000000, 0x04000000]` 范围内
   - 代码探针 FNV-1a 哈希（offset 0x1000, 4096 字节）与白名单匹配
2. **InitializeGlobals()** — 定位 + 验证 GNames/GObjects
3. **AreGlobalsReady()** — 最终表合法检查
4. **ValidateClassLayouts()** — 22 个关键类的 IsA 继承链验证

### 运行时状态与错误码

```cpp
enum class SdkError : uint32_t {
    Ok = 0,
    GameModuleMissing = 1,     // MirrorsEdge.exe 未加载
    GameImageSizeMismatch = 2, // image size 超出范围
    GameCodeProbeMismatch = 3, // 代码探针 FNV 未在白名单
    PatternGNamesNotFound = 4, // GNames opcode 模式未匹配
    PatternGObjectsNotFound = 5,
    GNamesPointerInvalid = 6,  // 指针地址不合法
    GObjectsPointerInvalid = 7,
    GNamesArrayInvalid = 8,    // 数组大小异常
    GObjectsArrayInvalid = 9,
    FNameSampleInvalid = 10,   // Index 0 不是 "None"
    ClassLayoutMismatch = 11,  // IsA 继承链验证失败
    ClassNotFound = 12,        // FindClass 未找到
};
```

可通过 `GetLastSdkError()` 和 `GetLastRuntimeStatus()` 获取当前状态。
Harness 可通过 `GET_STATUS` IPC 命令查询 `engine.sdkError` / `engine.sdkErrorName`。

## Safe 访问层

所有对 UE3 游戏对象的直接内存访问都可能因以下原因崩溃：
- 对象已被垃圾回收（GC）
- `ProcessEvent` 调用时的 vtable 损坏
- `TArray` 内部指针指向无效内存
- 跨线程访问 UE3 对象

Safe 访问层提供**双层防护**：外层检查（`IsPlausiblePointer`, `IsReadableMemory`）+ 内层 SEH 守卫。

### 内存安全检查 (safe_access)

| 函数 | 用途 |
|------|------|
| `IsPlausiblePointer(ptr)` | `ptr != nullptr && ptr >= 0x10000` — 排除 NULL/低地址 |
| `IsPlausibleUObject(obj)` | 检查对象不在本模块内 + Class 指针有效 |
| `IsReadableMemory(addr, size)` | `VirtualQuery` 逐区域检查目标内存为 COMMIT + 可读 |
| `IsWritableMemory(addr, size)` | 同上，检查可写 |
| `BoundedTArrayCount(array)` | 安全获取 TArray 元素数：检查指针、计数上限 (5M)、buffer 可读 |
| `TryMemcpy(src, dst, size)` | 带 SEH 守卫的 memcpy |
| `TryProcessEvent(obj, fn, params)` | 带 SEH 守卫的 ProcessEvent 调用 |
| `TryIsA(obj, class)` | 带 SEH 守卫的 IsA 类型检查 |

**关键设计原则：**
- `IsPlausibleUObject` 检查对象指针是否落在当前模块（`MirrorsEdge.exe` / `multiplayer.dll`）的地址空间内。UE3 堆对象一定在外部（由游戏分配），如果指针指向我们自己的 DLL 就说明指针已损坏。
- 所有 `Try*` 函数先做内存可读检查，再包裹 SEH 守卫 — 双重防御。

### Safe Gui (safe_gui)

安全读取 UE3 UI/世界状态的高级封装：

```cpp
// 对象查找 (通过遍历 GObjects)
Classes::ATdPlayerController* TryFindTdPlayerController(bool refresh = false);
Classes::AWorldInfo*         TryFindActiveWorldInfo(bool refresh = false);
Classes::UTdGameEngine*      TryFindTdGameEngine(bool refresh = false);

// 菜单/引擎状态
bool TryReadEngineMenuState(engine, out);     // bSmoothFrameRate, frame limits, DisplayGamma
bool TryReadWorldMenuState(controller, world, out);  // inMainMenu, timeDilation, streaming levels
bool TryReadPlayerOverlay(pawn, controller, out);   // pos, velocity, rotation, movementState
bool TryGetWorldMapName(world, out);         // SEH 守卫虚函数调用 world->GetMapName()

// 写操作
bool TryWriteEngineSmoothFrameRate(engine, enabled);
bool TryWriteEngineFrameRateLimits(engine, minFps, maxFps);
bool TryWriteWorldScalars(world, timeDilation, gravityZ);
bool TrySetStreamingLevelLoaded(level, loaded);
```

**缓存机制：** 所有 `TryFind*` 函数使用 static 缓存，`refresh=false` 时返回上次结果，避免重复遍历 GObjects。此处有一个已知约束：在垃圾回收后缓存可能失效，调用方应在合适的时机（关卡加载切换后）使用 `refresh=true`。

### Safe Gameplay (safe_gameplay)

安全读取/写入玩家和 Actor 数据的封装：

```cpp
// Pawn 状态
bool TryReadPawnPose(pawn, out);          // location, velocity, rotation, health, physics, movementState
bool TryReadPawnHealth(pawn, health);
bool TryApplyGodModeScalars(pawn);        // health = maxHealth, EnterFallingHeight = -inf
bool TryApplyFlyState(pawn, location);    // 设置飞行状态 (physics=PHYS_None, bCollideWorld=false)

// Actor 变换 (用于远程玩家)
bool TryReadActorLocation(actor, out);
bool TryWriteActorLocation(actor, location);
bool TryReadActorRotation(actor, out);
bool TryWriteActorRotation(actor, rotation);

// 骨骼网格
bool TryReadSkeletalMeshComponent(actor, comp);
bool TryReadMeshLocalAtomsBuffer(comp, buffer, count);
bool TryGetMesh3pBoneBuffer(pawn, buffer);

// 飞行 tick 上下文
bool TryReadFlyTickContext(controller, out);  // rawJoyUp, rawJoyRight, controllerYaw
```

**设计模式：** 每个 public `Try*` 函数三步：
1. `IsPlausibleUObject()` + `TryIsA()` 验证对象类型
2. 调用内部 `Raw*` 函数（包裹 SEH `__try/__except`）
3. `Raw*` 函数内直接访问 UE3 对象成员 — SEH 守卫捕获任何 ACCESS_VIOLATION

### Safe State (safe_state)

安全读取 UE3 复制状态（网络同步数据）的封装 — 常用于多人游戏信息获取：

```cpp
#include "me_sdk/runtime/safe_state.h"

// 玩家复制信息 (PRI)
PlayerReplicationInfoSnapshot pri;
if (MeSdk::Safe::State::TryReadPlayerReplicationInfo(pri, out)) {...}
// → out.score, out.deaths, out.ping, out.playerId, out.teamId, out.bBot, out.bReadyToPlay...

// TdGame 扩展 PRI
TdPlayerReplicationInfoSnapshot tdPri;
if (MeSdk::Safe::State::TryReadTdPlayerReplicationInfo(pri, out)) {...}

// 队伍信息
TeamInfoSnapshot team;
if (MeSdk::Safe::State::TryReadTeamInfo(team, out)) {...}
// → out.teamIndex, out.score, out.size

// 游戏复制信息 (GRI)
GameReplicationInfoSnapshot gri;
if (MeSdk::Safe::State::TryReadGameReplicationInfo(gri, out)) {...}
// → out.bMatchHasBegun, out.remainingTime, out.goalScore, out.playerCount...

// 查找辅助
auto *pri = MeSdk::Safe::State::TryFindLocalPlayerReplicationInfo(controller);
auto *gri = MeSdk::Safe::State::TryFindGameReplicationInfo(world);
```

### Safe Gameplay 扩展 — 关卡/导航

```cpp
// 检查点 / 复活点
CheckpointSnapshot cp;
if (MeSdk::Safe::Gameplay::TryReadTdCheckpoint(checkpoint, out)) {...}
// → out.location, out.rotation, out.checkpointId

// Stashpoint (多人模式互动点)
StashpointSnapshot sp;
if (MeSdk::Safe::Gameplay::TryReadTdStashpoint(stash, out)) {...}
// → out.location, out.stashPointId, out.territoryOfTeam

// 导航点 (AI bot 路径)
NavigationPointSnapshot nav;
if (MeSdk::Safe::Gameplay::TryReadNavigationPoint(nav, out)) {...}
// → out.location, out.bBlocked, out.bEndPoint

// 小地图元数据
TdMapInfoSnapshot map;
if (MeSdk::Safe::Gameplay::TryReadTdMapInfo(mapInfo, out)) {...}
// → out.worldToMiniMapOrigo, out.mapSpecificWidgetScale
```

## Pattern 扫描 (patterns/)

### 底层引擎 (pattern.h)

```cpp
namespace Pattern {
    void* FindPattern(const char* pattern, const char* mask);               // 在 MirrorsEdge.exe 中搜索
    void* FindPattern(const char* module, const char* pattern, const char* mask); // 在指定模块中搜索
    void* FindPattern(void* base, int size, const char* pattern, const char* mask); // 在自定义范围内搜索
}
```

模式格式：`pattern` = 十六进制字节序列，`mask` = `x` (匹配) / `?` (跳过)。
跨页对齐处理：pattern 不在页边界内返回 nullptr。

### Hook 目标签名 (patterns/hooks.h)

包含所有需要通过 TrampolineHook 安装的 UE3 函数签名：

| 模式 | 用途 |
|------|------|
| `ProcessEvent` | UE3 的 `UObject::ProcessEvent` — 所有脚本调用的入口 |
| `LevelLoad` | UE3 的 `LoadMap` — 关卡加载/切换 hook |
| `Tick` | 主引擎 Tick — 每帧驱动游戏逻辑 |
| `ActorTick` | AActor::Tick — 每个 Actor 的帧更新 |
| `BonesTick` | 骨骼动画 Tick — bot 动画更新 |
| `PreDeath` / `PostDeath` | 死亡处理 (teleport) |
| `ProjectionTick` | 视野投影更新 |

### 游戏特定签名 (patterns/tdgame.h)

| 模式 | 用途 |
|------|------|
| `StateHandler` | TdGame 状态处理 — 用于游戏模式检测 |
| `ForceRoll` | ForceRoll 地址 — 玩家强制翻滚 |

## 类布局验证

### 编译时 (sdk_verify.h + sdk_verify_generated.h)

```cpp
MMOD_SDK_VERIFY_CLASS_SIZE(UObject, 0x003C);
MMOD_SDK_VERIFY_CLASS_SIZE(AActor, 0x01C0);
MMOD_SDK_VERIFY_MEMBER_OFFSET(ClassName, MemberName, 0x00XX);
```

40 个关键类大小和 3 个成员偏移的 `static_assert` 检查。

**注意事项：**
- generated 头文件使用 `#pragma pack(4)`，不能从 `me_sdk.h` 末尾全局 include
- `sdk_verify_generated.h` 由 `generate-static-asserts.ps1` 自动生成
- CI 检查该文件是否过期

### 运行时 (init.cpp)

`ValidateClassLayouts()` 在游戏内验证 22 个关键类的 IsA 继承链：

```cpp
{ "Class Core.Object",                nullptr },
{ "Class Core.Field",                 "Class Core.Object" },
...
{ "Class TdGame.TdPlayerController",  "Class Engine.PlayerController" },
{ "Class TdGame.TdPawn",              "Class Engine.Pawn" },
{ "Class TdGame.TdPlayerPawn",        "Class TdGame.TdPawn" },
```

通过 `UObject::FindClass` + `cls->IsA(parent)` 验证。

## 使用示例

### 初始化 SDK

```cpp
#include "me_sdk/me_sdk.h"
#include "me_sdk/runtime/init.h"

// 完整初始化 (验证二进制 + 定位全局变量 + 验证类布局)
if (!MeSdk::ValidateRuntime(true)) {
    // 检查 MeSdk::GetLastSdkError() / MeSdk::GetLastRuntimeStatus()
    return false;
}
```

### 安全遍历 GObjects 查找对象

```cpp
#include "me_sdk/runtime/safe_access.h"

Classes::UObject* FindObjectByName(const std::string& name) {
    Classes::UObject* result = nullptr;
    MeSdk::Safe::ForEachGlobalObject(
        [](Classes::UObject* obj, int idx, void* ctx) -> bool {
            if (MeSdk::Safe::TryIsA(obj, Classes::ATdPlayerPawn::StaticClass())) {
                auto** out = static_cast<Classes::UObject**>(ctx);
                *out = obj;
                return false; // 停止遍历
            }
            return true;
        },
        &result);
    return result;
}
```

### 安全读取玩家状态

```cpp
#include "me_sdk/runtime/safe_gui.h"
#include "me_sdk/runtime/safe_gameplay.h"

void ReadPlayerState() {
    auto* controller = MeSdk::Safe::Gui::TryFindTdPlayerController(false);
    if (!controller) return;

    auto* pawn = controller->Pawn;  // 从这里开始不安全 — 需要使用 Safe 访问
    PawnPoseSnapshot pose;
    if (MeSdk::Safe::Gameplay::TryReadPawnPose(
            static_cast<Classes::ATdPlayerPawn*>(pawn), pose)) {
        // pose.location, pose.velocity, pose.rotation 等已安全填充
    }
}
```

### UE3 字节码补丁 (`shared/ue_patcher/`)

SDK 现在支持运行时字节码解码和补丁。操作码表参考 MirrorsEdgeTweaks 的 `BytecodeBuilder.cs` (v536)。

#### UStruct::Script 成员

`UStruct` 类（`ME_Core_classes.hpp`）已添加 `Script` 成员：

```cpp
TArray<unsigned char>  Script;       // 偏移 0x50 — UE3 字节码缓冲区
const unsigned char *ScriptData() const;
size_t               ScriptSize() const;
bool                 HasScript()   const;
```

偏移验证：`UStruct` 总大小保持 `0x90` — 成员布局为 `SuperField(0x44) + Children(0x48) + PropertySize(0x4C) + Script.TArray(0x50) + padding(0x34) = 0x90`。

#### 操作码表 (`ue3_opcodes.h`)

完整操作码定义（按 MET v536 验证）：

| 操作码 | 值 | 操作数 | 说明 |
|--------|-----|---------|------|
| `OP_LocalVariable` | `0x00` | int32 | 局部变量索引 |
| `OP_InstanceVariable` | `0x01` | int32 | import/export 索引 |
| `OP_Return` | `0x04` | (none) | 函数返回 |
| `OP_Jump` | `0x06` | uint16 | 无条件跳转（绝对偏移） |
| `OP_JumpIfNot` | `0x07` | uint16 | 条件跳转 |
| `OP_IntConst` | `0x0A` | int32 | 整数常量 |
| `OP_Nothing` | `0x0B` | (none) | NOP |
| `OP_Let` | `0x0F` | (none) | 赋值 = dest expr + value expr |
| `OP_EndFunctionParms` | `0x16` | (none) | 终止原生函数参数 |
| `OP_Skip` | `0x18` | uint16 | 跳过 N 字节 |
| `OP_Context` | `0x19` | variable | 对象上下文 + u16 skipSize + u16 fieldType + inner |
| `OP_VirtualFunction` | `0x1B` | FName(8) | 虚函数调用 |
| `OP_FinalFunction` | `0x1C` | FName(8) | 最终函数调用 |
| `OP_FloatConst` | `0x1E` | float32 | 浮点常量 |
| `OP_StringConst` | `0x1F` | nul-str | 空终止 ANSI 字符串 |
| `OP_ByteConst` | `0x24` | uint8 | 字节常量 |
| `OP_DynamicCast` | `0x2E` | variable | 动态类型转换 + inner |
| `OP_StructMember` | `0x35` | variable | 结构体成员访问 |
| `OP_PrimitiveCast` | `0x38` | uint8 + inner | 基元类型转换 |
| `OP_DelegateFunction` | `0x42` | variable | 委托调用 |
| `OP_EndOfScript` | `0x53` | (none) | 字节码结束标记 |
| `OP_And_BoolBool` | `0x82` | variable | 布尔 AND 短路求值 |
| Math ops | `0xAB–0xF5` | (none,栈操作) | Add, Subtract, Multiply, Divide, FMin, FMax, Tan, Atan |

#### 字节码解码器 (`ue3_bytecode.h/.cpp`)

```cpp
#include "ue_patcher/ue3_bytecode.h"

// 解码单条指令
Classes::UFunction *func = ...;
const Instruction insn = Bytecode::DecodeInstruction(
    func->ScriptData(), func->ScriptSize(), func->ScriptData());

// 遍历所有指令
int count = Bytecode::WalkBytecode(script, len,
    [](const Instruction &insn, void *ctx) -> bool {
        // insn.opcode, insn.size, insn.name, insn.start
        return true; // continue
    }, nullptr);

// 查找字节序列
ptrdiff_t offset = Bytecode::FindBytecodePattern(
    script, len, pattern_bytes, "xxxx????");
```

可变长度操作码（Context, DynamicCast 等）通过递归解码内部表达式计算大小。

#### 字节码补丁器

```cpp
// 替换字节序列（同长度 或 缩小到更短用 NOP 填充）
PatchResult result = Bytecode::PatchBytes(
    uStruct,
    offset,
    replace_bytes, replace_len,
    expect_bytes, expect_len);  // 安全检查（可选）

// 使 Script 可写 / 恢复保护
DWORD oldProt = Bytecode::MakeScriptWritable(uStruct);
// ... 直接修改 ...
Bytecode::RestoreScriptProtection(uStruct, oldProt);
```

**限制**：`InsertBytes()` 不支持（cooked package 中 Script 指向序列化缓冲区，无法扩展）。补丁必须是同长度替换。如需插入新字节码，必须先在 package 文件中预留空间。

#### 多版本地址表 (`me_sdk/versions/`)

```cpp
#include "me_sdk/versions/me_version.h"

MeVersion::VersionInfo info;
if (MeVersion::Detect(info)) {
    // info.build = Build::ME_1_0_Retail / Steam / EA_App / GOG
    // info.exeFileSize, info.exeTimestamp, info.fileVersion[]
}
```

版本指纹数据在 `me_versions.json` 中定义，支持按版本选择不同地址和补丁方案。

## 约束与注意事项

1. **x86 only** — Mirror's Edge 是 32 位游戏。所有指针都是 4 字节，结构体使用 `#pragma pack(4)`。
2. **GNames/GObjects 初始化顺序** — 在全局变量就绪之前，`FName("str")` 构造函数、`FindObject<T>()` 和所有 Safe 访问函数会立即失败（`AreGlobalsReady()` 返回 false）。
3. **线程安全** — GObjects 遍历 (`ForEachGlobalObject`, `TryFind*`) **不是线程安全的**。UE3 可能在游戏线程外修改对象表。Safe 层提供 SEH 保护但**不提供并发控制**。调用方应只在游戏主线程或持有适当锁时访问 GObjects。
4. **GC 后的缓存失效** — `TryFind*` 使用 static 缓存。垃圾回收后对象可能被移动/释放。在关卡切换或明确的 GC 事件后需使用 `refresh=true`。
5. **`sdk_verify_generated.h` 的 pack 上下文** — 不能从 `me_sdk.h` 末尾全局 include，会造成 packing 错误。只应在正确 `#pragma pack` 上下文的 .cpp 文件中 include。
6. **不要从 inject worker 线程或 D3D9 hook 线程调用 UE3 API** — 渲染线程上的 UObject 访问会造成致命崩溃（R6025 / ACCESS_VIOLATION）。所有 UE3 操作必须在游戏主线程上执行。
7. **修复策略** — 如果测试发现 SDK 数据错误（错误的成员偏移、无效的 ProcessEvent 参数结构体、模式匹配失败），应**直接修复 `shared/me_sdk/`** 中的 generated 头文件或 pattern，而不是在 `runtime/` 或 mod 代码中添加 workaround。

## 相关文档

- [architecture.md](architecture.md) — 整体架构与文件映射
- [sdk-verification.md](sdk-verification.md) — SDK 验证工具 (纯指令扫描、Ghidra、CI)
- [troubleshooting.md](troubleshooting.md) — 快速症状排查
- [known-issues/README.md](known-issues/README.md) — 已知问题登记册
