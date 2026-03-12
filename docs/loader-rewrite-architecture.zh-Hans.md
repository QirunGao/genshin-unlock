# Loader 重写架构设计

## 1. 目标与边界

本文档描述一个用于替换当前 `loader.exe -> plugin.dll` 方案的重写架构。目标不是改变功能，而是在保留以下能力的前提下，提供更易审计、更易排障、边界更清晰的实现：

- 启动原神游戏进程
- 在受控时机将自有运行时模块加载进游戏进程
- 在游戏进程内启用 FPS 解锁与 FOV 相关能力
- 支持版本检查、配置下发、日志收集和故障回退

必须明确的一点是：对第三方进程做 DLL 注入和内存修改，本身不属于 Windows 的官方扩展模型。因此，本设计追求的是：

- 更安全的工程实现
- 更明确的最小权限边界
- 更可预测的初始化流程
- 更易验证的故障处理路径

本文档不尝试把该行为包装为“官方支持的插件体系”。它只是对现有实现进行工程化重构。

## 2. 当前实现的问题

现有仓库的主链路是：

1. `loader.exe` 读取本地配置
2. `CreateProcessW(..., CREATE_SUSPENDED, ...)` 挂起启动游戏
3. 通过 `QueueUserAPC + LoadLibraryW` 注入 `plugin.dll`
4. 恢复主线程
5. `plugin.dll` 在 `DllMain` 中拉起线程并进入 60Hz 常驻循环

当前方案能工作，但存在以下结构性问题：

### 2.1 注入与业务逻辑耦合

注入器负责启动流程，插件负责配置解析、日志、文件监控、按键轮询和 Hook。模块职责不够清晰，且失败时上下文被分散在多个位置。

### 2.2 初始化时机过于隐式

`QueueUserAPC` 依赖 APC 调度时机；`DllMain` 中创建线程又引入 loader lock 相关风险。虽然这种做法常见，但不够稳健。

### 2.3 配置与文件监控位于被注入进程

当前 `plugin.dll` 自己读取 `plugin_config.json`，还监控目录变更并自动回写。这会把 JSON 解析、文件 I/O、目录监控和异常处理带入游戏进程。

### 2.4 故障边界不清晰

当前很难回答以下问题：

- 注入是否完成
- 运行时是否初始化成功
- 偏移解析是否成功
- Hook 是否安装成功
- 是否处于降级模式

### 2.5 通用 DLL 加载能力过大

`loader_config.json` 支持任意 `dllPaths`。如果目标只是加载自家模块，这种通用性会扩大风险面。

## 3. 重写后的总体架构

建议将系统拆为四层：

1. `launcher.exe`
2. `bootstrap.dll`
3. `runtime.dll`
4. `shared/` 协议与公共模型

### 3.1 设计原则

- 注入层最小化
- 业务层显式初始化
- 文件解析与用户交互尽量在外部进程完成
- 运行时对宿主版本做白名单验证
- 不保留“任意 DLL 注入器”能力
- 每个阶段都可观测、可失败、可回退

### 3.2 逻辑分层

#### `launcher.exe`

职责：

- 读取并校验本地配置
- 定位游戏安装路径
- 检查当前工具版本与游戏版本兼容性
- 生成启动参数
- 创建游戏进程
- 注入 `bootstrap.dll`
- 和进程内模块建立握手
- 下发配置快照
- 收集日志与状态

不负责：

- 直接做 FPS/FOV 业务逻辑
- 长期驻留在游戏进程内
- 维护 Hook 细节

#### `bootstrap.dll`

职责：

- 作为最小化进程内引导器
- 校验宿主模块、环境、版本和路径
- 建立与 `launcher.exe` 的 IPC
- 受控加载 `runtime.dll`
- 调用 `runtime.dll` 的显式初始化入口

不负责：

- 业务配置解析
- 文件监控
- 复杂 Hook 逻辑

#### `runtime.dll`

职责：

- 解析目标版本的地址映射或签名
- 安装 FPS/FOV 相关 Hook
- 按外部下发的配置快照更新行为
- 向 `launcher.exe` 上报状态与错误

不负责：

- 目录监控
- UI 交互
- 任意外部模块加载

#### `shared/`

职责：

- IPC 协议结构体
- 配置模型
- 状态码
- 日志事件
- 版本白名单与能力描述

## 4. 推荐目录结构

```text
src/
  launcher/
    Main.cpp
    AppConfig.cpp
    GameLocator.cpp
    VersionPolicy.cpp
    ProcessStarter.cpp
    Injector.cpp
    HandshakeClient.cpp
    Logger.cpp
  bootstrap/
    DllMain.cpp
    BootstrapEntry.cpp
    HostValidator.cpp
    RuntimeLoader.cpp
    IpcServer.cpp
  runtime/
    RuntimeMain.cpp
    RuntimeState.cpp
    MemoryResolver.cpp
    HookManager.cpp
    FpsService.cpp
    FovService.cpp
    InputSampler.cpp
    LoggerProxy.cpp
  shared/
    Protocol.hpp
    StatusCode.hpp
    ConfigModel.hpp
    VersionTable.hpp
    EventLog.hpp
include/
  ...
docs/
  loader-rewrite-architecture.zh-Hans.md
```

## 5. 进程模型与信任边界

### 5.1 进程

- `launcher.exe`：控制平面
- `GenshinImpact.exe` 或 `YuanShen.exe`：被控平面

### 5.2 模块

游戏进程内只允许加载两类自有模块：

- `bootstrap.dll`
- `runtime.dll`

默认禁止：

- 通过配置指定任意 DLL
- 从可写临时目录加载模块
- 从相对路径或网络路径加载模块

### 5.3 信任边界

可信输入：

- 本地签名过的 `launcher.exe`
- 本地签名过的 `bootstrap.dll`
- 本地签名过的 `runtime.dll`
- 白名单版本表

不可信输入：

- 外部可编辑配置文件内容
- 未知游戏版本
- 运行时环境异常
- 来自磁盘的非签名模块

## 6. 启动与初始化时序

推荐的启动顺序如下：

```text
launcher.exe
  -> 读取 launcher 配置
  -> 校验自有模块签名/哈希
  -> 定位游戏路径
  -> 解析游戏版本
  -> 生成启动参数
  -> CreateProcessW(CREATE_SUSPENDED)
  -> 注入 bootstrap.dll
  -> 等待 bootstrap 就绪事件
  -> 下发 runtime 配置快照
  -> 接收 runtime 初始化结果
  -> ResumeThread(主线程)
  -> 进入状态监控循环
```

游戏进程内：

```text
bootstrap.dll
  -> 被 LoadLibraryW 映射
  -> DllMain 仅保存句柄并返回
  -> 由显式入口 BootstrapInitialize() 执行真正初始化
  -> 校验宿主模块名、版本、路径
  -> 建立 IPC
  -> 加载 runtime.dll
  -> 调用 RuntimeInitialize()
  -> 返回初始化状态
```

运行时：

```text
runtime.dll
  -> RuntimeInitialize()
  -> 解析版本表
  -> 解析目标地址
  -> 安装 Hook
  -> 进入运行状态
  -> 接受配置更新
  -> 上报心跳和错误
```

## 7. 注入策略

### 7.1 不推荐继续使用 APC 作为主路径

对于重写版，建议使用更显式的加载流程：

1. `VirtualAllocEx`
2. `WriteProcessMemory`
3. `CreateRemoteThread` 调用 `LoadLibraryW`
4. `WaitForSingleObject`
5. `GetExitCodeThread` 校验返回值

这样做的优点：

- 成功/失败更容易判断
- 不依赖 APC 的执行时机
- 更适合做初始化握手
- 便于实现超时与日志

### 7.2 为什么不是直接在注入后恢复主线程

注入成功不等于运行时可用。建议在恢复游戏主线程之前，至少确认：

- `bootstrap.dll` 已映射
- `bootstrap` 已完成宿主校验
- `runtime.dll` 已加载
- `runtime` 已完成版本表匹配
- 第一批关键 Hook 已创建成功

如果上述任何一步失败，应明确终止流程并提示用户，而不是恢复一个半初始化状态的游戏进程。

## 8. DllMain 约束

`DllMain` 必须保持最小化。

### 8.1 `bootstrap.dll` 的 `DllMain`

允许做的事：

- 保存 `HMODULE`
- `DisableThreadLibraryCalls`
- 初始化极少量静态变量

禁止做的事：

- 文件 I/O
- JSON 解析
- 复杂日志初始化
- 创建工作线程并立刻做重活
- 安装 Hook
- 调用可能导致 loader lock 问题的 API 组合

### 8.2 `runtime.dll`

同样不应在 `DllMain` 中做业务初始化。业务入口应是显式导出函数，例如：

```cpp
extern "C" __declspec(dllexport)
RuntimeInitResult RuntimeInitialize(const RuntimeInitParams* params);
```

## 9. IPC 设计

建议使用命名管道或本地命名共享内存加事件对象。优先级建议如下：

1. 命名管道
2. 共享内存 + 事件
3. 仅用于极简单场景的命名事件

推荐选择命名管道，原因是：

- 请求/响应模型清晰
- 易于传输结构化日志和状态
- 更容易扩展
- 调试成本较低

### 9.1 IPC 会话模型

会话至少包含：

- `session_id`
- `launcher_pid`
- `game_pid`
- `tool_version`
- `game_version`
- `protocol_version`

### 9.2 消息类型

至少定义以下消息：

- `Hello`
- `BootstrapReady`
- `RuntimeLoadRequest`
- `RuntimeInitResult`
- `ConfigSnapshot`
- `ConfigApplyResult`
- `StatusHeartbeat`
- `HookStateChanged`
- `ErrorEvent`
- `ShutdownRequest`

### 9.3 配置下发策略

不建议在 `runtime.dll` 内直接 watch JSON 文件。

推荐流程：

1. `launcher.exe` 读取配置文件
2. 做字段校验和默认值补齐
3. 生成一份只包含运行所需字段的快照
4. 通过 IPC 发给 `runtime.dll`
5. `runtime.dll` 原子替换配置快照

## 10. 配置模型拆分

建议把当前配置拆成两份：

### 10.1 `launcher_config.json`

外部启动配置：

- `gamePath`
- `overrideArgs`
- `monitorIndex`
- `displayMode`
- `screenWidth`
- `screenHeight`
- `mobilePlatform`
- `additionalArgs`
- `closeLauncherOnSuccess`
- `logLevel`

### 10.2 `runtime_config.json`

运行时行为配置：

- `unlockFps`
- `targetFps`
- `autoThrottle`
- `unlockFov`
- `targetFov`
- `fovPresets`
- `fovSmoothing`
- `unlockFovKey`
- `nextFovPresetKey`
- `prevFovPresetKey`

只有 `launcher.exe` 负责读取这两份 JSON。进程内模块只看到经校验后的快照。

## 11. 版本兼容策略

当前实现主要用文件版本和固定偏移工作。重写版建议改为“双层校验”：

1. 文件版本白名单
2. 每个版本对应的偏移或签名表

### 11.1 白名单表

例：

```text
game version -> runtime support entry
5.4.0 -> resolver profile A
5.4.1 -> resolver profile A
5.5.0 -> resolver profile B
```

### 11.2 地址解析策略优先级

建议顺序：

1. 版本绑定偏移表
2. 签名扫描
3. 明确失败并停用功能

不建议：

- 在未知版本上继续盲写固定偏移
- 解析失败后仍进入运行态

### 11.3 功能级降级

如果部分能力解析失败，应支持分项降级：

- FPS 可用，FOV 不可用
- FOV 可用，FPS 不可用
- 全部不可用，仍保留日志上报

## 12. runtime 内部状态机

建议为 `runtime.dll` 建立明确状态机：

```text
Created
  -> HostValidated
  -> IpcReady
  -> ConfigReady
  -> SymbolsResolved
  -> HooksInstalled
  -> Running
  -> Degraded
  -> Shutdown
  -> Fatal
```

### 12.1 状态转移原则

- 每个状态只能向少数预定义状态转移
- 每次转移必须上报事件
- 进入 `Fatal` 后禁止继续尝试写游戏内存

## 13. Hook 管理策略

建议引入统一的 `HookManager`，而不是让各功能组件各自分散控制生命周期。

`HookManager` 负责：

- 注册 Hook 定义
- 安装顺序
- 启用/禁用
- 异常回滚
- 状态上报

### 13.1 FPS 功能

FPS 功能仍然可以保留“直接写目标变量”的模式，但应做到：

- 地址解析成功后再启用
- 写入前做空指针和范围校验
- 失焦降帧与优先级调整解耦

### 13.2 FOV 功能

FOV 功能建议保留 detour 模式，但要将以下逻辑从全局静态变量中拆出：

- 当前目标值
- 平滑滤波器状态
- Hook 是否启用
- 上一次观测到的实例
- 恢复默认 FOV 的状态

这些状态建议归入一个明确的 `FovRuntimeState` 对象，避免散落的全局共享状态。

## 14. 输入采样与更新循环

不建议所有功能都绑在一个固定 60Hz 大循环里。

推荐拆成：

- 输入采样循环
- 配置应用路径
- Hook 回调路径
- 心跳上报路径

### 14.1 运行模型建议

- 输入采样：固定频率，例如 60Hz
- 配置应用：仅在配置版本号变化时执行
- FPS 写入：固定频率或事件驱动
- FOV 调整：以 Hook 回调为主
- 心跳：1 秒一次

这样比“所有东西在一个大循环里每次都跑一遍”更容易定位时延和错误。

## 15. 日志与可观测性

日志必须分层。

### 15.1 `launcher.exe` 日志

记录：

- 配置加载
- 版本检查
- 进程创建
- 注入步骤
- IPC 建连
- 运行时状态变化

### 15.2 `bootstrap.dll` 日志

只记录：

- 宿主校验
- runtime 加载
- IPC 建立
- 初始化失败原因

### 15.3 `runtime.dll` 日志

记录：

- 地址解析结果
- Hook 安装结果
- 配置应用结果
- 降级事件
- 致命错误

### 15.4 日志输出方向

首选通过 IPC 回传到 `launcher.exe`，由外部进程落盘。被注入进程内只保留最少量应急日志。

## 16. 错误处理策略

每个阶段都应有明确返回码，而不只依赖异常文本。

建议定义统一状态码，例如：

- `Ok`
- `ConfigInvalid`
- `GameNotFound`
- `GameVersionUnsupported`
- `ModuleSignatureInvalid`
- `InjectFailed`
- `BootstrapInitFailed`
- `RuntimeLoadFailed`
- `RuntimeInitFailed`
- `SymbolResolveFailed`
- `HookInstallFailed`
- `IpcDisconnected`

所有错误都应带上：

- 模块名
- 阶段名
- 错误码
- 原始系统错误
- 可读文本

## 17. 安全控制建议

### 17.1 模块加载限制

- 固定模块名，不允许配置任意 DLL 路径
- 只从 `launcher.exe` 所在目录加载
- 拒绝相对路径
- 拒绝网络路径
- 校验签名或哈希

### 17.2 最小权限

- 仅请求必要的进程访问权限
- 句柄尽量缩短生命周期
- 避免在运行期保留不必要的远程内存

### 17.3 输入校验

对所有配置字段做：

- 类型校验
- 范围校验
- 白名单校验
- 版本兼容校验

### 17.4 未知版本默认拒绝

任何不在白名单中的游戏版本，默认不进入 Hook 阶段。

## 18. 迁移策略

建议分三阶段迁移。

### 阶段一：外部控制面重构

目标：

- 保留现有 `plugin.dll`
- 重写 `loader.exe` 为 `launcher.exe`
- 去掉通用 `dllPaths`
- 引入更清晰的注入、日志和错误处理

收益：

- 用户侧可见行为变化最小
- 能先稳定控制面

### 阶段二：最小 bootstrap 层引入

目标：

- 将原 `plugin.dll` 拆出最小 `bootstrap.dll`
- 把复杂初始化移出 `DllMain`
- 建立 IPC 握手

收益：

- 显著改善初始化时序和排障能力

### 阶段三：runtime 业务层重写

目标：

- 将配置读取、循环控制、Hook 管理模块化
- 引入状态机、版本白名单、功能级降级

收益：

- 进程内逻辑更稳，版本适配成本更低

## 19. 建议复用与建议重写的部分

### 19.1 可以复用

- 游戏路径发现逻辑
- 版本读取逻辑
- 启动参数生成逻辑
- 一部分 Win32 文件、字符串、版本工具封装

### 19.2 建议重写

- 远程加载实现
- `DllMain` 初始化路径
- 运行时配置模型流转
- 组件主循环
- Hook 生命周期管理
- 日志落盘方式

## 20. 最终建议

如果目标是“功能不变但架构更安全”，最关键的不是改一个注入 API，而是做下面五件事：

1. 去掉任意 DLL 加载能力，收缩为专用启动器
2. 把注入层、引导层、业务层拆开
3. 让初始化变成显式握手流程，而不是隐式副作用
4. 把配置解析和日志外移到 `launcher.exe`
5. 用版本白名单和状态机管理运行时行为

重写后，系统应当满足以下验收标准：

- 注入成功与否可明确判断
- runtime 初始化成功与否可明确判断
- 未知版本默认拒绝
- Hook 部分失败可降级，不应直接失控
- 所有关键状态都能被 `launcher.exe` 观测
- 游戏进程内不再承担不必要的文件监控和配置解析职责

这套设计仍然不是 Windows 的“官方插件模型”，但它会比当前实现更接近一个可维护、可审计、可回退的工程系统。
