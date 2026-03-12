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

## 21. 当前代码与本文档的不一致点

本节用于对照当前仓库中的实际实现，指出哪些地方已经向新架构靠拢，哪些地方仍然与本文档目标不一致。

结论先行：

- 当前仓库已经完成了目录层面的初步拆分，存在 `launcher / bootstrap / runtime / shared` 四层结构
- 注入路径已经从旧版的 APC 方案改成了 `CreateRemoteThread + LoadLibraryW`
- `bootstrap.dll` 的 `DllMain` 也已经缩减为极小实现

但这套代码目前仍然只是“新架构骨架”，还不能视为已经实现了本文档描述的完整系统。下面列出主要不一致点。

### 21.1 显式初始化链路没有真正打通

本文档要求的启动链路是：

1. `launcher.exe` 注入 `bootstrap.dll`
2. `launcher.exe` 显式触发 `BootstrapInitialize()`
3. `bootstrap.dll` 建立 IPC、加载 `runtime.dll`
4. `bootstrap.dll` 调用 `RuntimeInitialize()`
5. `launcher.exe` 在收到成功握手后再恢复游戏主线程

当前代码的问题是：

- `launcher` 只负责通过 `CreateRemoteThread + LoadLibraryW` 把 `bootstrap.dll` 映射进目标进程
- `launcher` 没有继续显式调用 `BootstrapInitialize()`
- `bootstrap.dll` 的 `DllMain` 只保存了模块句柄并返回，不会主动执行 `BootstrapInitialize()`

这意味着：

- `bootstrap` 实际上没有开始工作
- `runtime.dll` 实际上不会被 `bootstrap` 加载
- 后面的 IPC、配置下发、运行时初始化在正常情况下都不会发生

也就是说，当前“注入成功”只等于“模块被映射”，不等于“引导层已初始化”。

需要一致化的方向：

- 在 `launcher.exe` 中实现第二阶段远程调用，显式执行 `BootstrapInitialize()`
- 把“映射成功”和“初始化成功”分成两个不同状态
- 只有在 `BootstrapInitialize()` 返回成功后，才允许继续推进握手或恢复主线程

### 21.2 当前握手失败时仍然放行，与文档的 fail-closed 策略不一致

本文档要求：

- 在恢复游戏主线程前，必须确认 `bootstrap` 和 `runtime` 已初始化成功
- 如果握手失败、版本不匹配、Hook 未准备好，应拒绝继续启动，而不是放行半初始化状态

当前代码的问题是：

- `launcher` 在连接命名管道失败时，只记录一条 warning
- 随后仍然会直接 `ResumeThread()` 恢复游戏主线程

这意味着：

- “新架构的初始化握手”在失败时并不构成阻断条件
- 实际运行效果退化为“先注入一个 DLL，后续逻辑不保证成立”

需要一致化的方向：

- 将 `bootstrap` 握手失败视为启动失败
- 明确区分可容忍降级和不可容忍失败
- 在 `launcher` 中建立严格的启动状态机，只有到达 `BootstrapReady` 和 `RuntimeInitResult=Ok` 时才恢复主线程

### 21.3 IPC 协议定义与 IPC 实现不一致

本文档要求：

- IPC 使用清晰的请求/响应模型
- 消息应是结构化的，可扩展的
- `Hello`、`BootstrapReady`、`RuntimeInitResult` 等消息应完整传递会话信息和状态

当前代码的问题是：

- `shared/Protocol.hpp` 定义了结构化消息，包含 `std::string`、详细状态、扩展字段
- 实际的 `HandshakeClient` 和 `IpcServer` 并没有真正序列化这些结构
- 当前只发送了零碎字段，例如：
  - `HelloMessage` 只发送 `protocolVersion`
  - `BootstrapReadyMessage` 只发送 `status`
  - `RuntimeInitResultMessage` 只发送三个整数
- 消息头里的 `payloadSize` 和实际写入的数据也不总是严格对应消息定义

这意味着：

- 现在的协议只是“看起来有结构体定义”，实际上仍是手写的半定长二进制协议
- 协议定义和线上传输格式分离，后续很容易出现兼容性错误

需要一致化的方向：

- 选定单一协议策略
- 两种可行路线：
  - 路线 A：完全改成定长 POD 协议，不在消息里使用 `std::string`
  - 路线 B：保留结构化消息，但使用明确的序列化格式，例如长度前缀 + UTF-8 文本字段
- 让 `Protocol.hpp` 里的定义与读写实现一一对应

### 21.4 `bootstrap` 只创建了命名管道，但没有完成连接时序

本文档要求：

- `bootstrap` 建立 IPC
- `launcher` 和 `bootstrap` 完成握手
- 消息交换有明确的连接阶段

当前代码的问题是：

- `bootstrap::IpcServer::Create()` 只调用了 `CreateNamedPipeA`
- `BootstrapInitialize()` 没有调用 `WaitForConnection()`
- 却在后面直接尝试 `SendBootstrapReady()`

这会导致：

- 命名管道端点被创建了，但连接是否建立并不明确
- 即使未来 `BootstrapInitialize()` 被远程调用，也可能在客户端尚未连接时直接发送消息失败

需要一致化的方向：

- 在 `BootstrapInitialize()` 中明确加入 `WaitForConnection()` 阶段
- 先接收 `Hello`，再发送 `BootstrapReady`
- 把 `IpcReady` 状态真正纳入初始化链路

### 21.5 `runtime.dll` 仍在进程内读取和回写配置文件

本文档明确要求：

- 只有 `launcher.exe` 负责读取 `launcher_config.json` 和 `runtime_config.json`
- 进程内模块只接收经过校验后的配置快照
- 不建议 `runtime.dll` 直接 watch JSON 文件，也不建议它自行回写配置

当前代码的问题是：

- `launcher` 确实读取了 `runtime_config.json`
- 但 `runtime.dll` 启动后又重新读取同一个 `runtime_config.json`
- `runtime` 在热键触发后还会自行修改内存态配置并回写文件

这意味着：

- 配置控制权仍然分散在 `launcher` 和 `runtime` 两边
- 外部下发的 `ConfigSnapshotMessage` 无法成为唯一配置来源
- 被注入进程仍承担文件 I/O、JSON 解析和配置持久化责任

需要一致化的方向：

- 删除 `runtime.dll` 内的文件读取和文件回写逻辑
- 将热键产生的配置变化改成“事件上报给 `launcher`”，由 `launcher` 决定是否持久化
- 让 `runtime` 只维护内存中的原子配置快照

### 21.6 `runtime` 的启动参数没有通过引导链传递进去

本文档要求：

- `launcher` 发送配置快照
- `bootstrap` 调用 `RuntimeInitialize(params)`
- `runtime` 使用这份显式参数完成初始化

当前代码的问题是：

- `launcher` 虽然会发送 `ConfigSnapshotMessage`
- 但 `bootstrap` 当前调用 `RuntimeInitialize(nullptr)`
- `runtime` 的初始化逻辑只有在 `params != nullptr` 时才应用初始快照

这意味着：

- 即便未来握手链路打通，目前这份配置也不会被真正带进 `RuntimeInitialize()`
- `runtime` 仍主要依赖文件态配置，而不是外部控制面传入的快照

需要一致化的方向：

- 在 `bootstrap` 中接收配置消息后构造 `RuntimeInitParams`
- 将配置快照作为显式参数传入 `RuntimeInitialize()`
- 把“IPC 下发配置”和“runtime 初始化参数”统一成一条链

### 21.7 运行时状态机只实现了一部分，没有接入真实观测链路

本文档要求：

- `runtime.dll` 维护明确状态机
- 状态转移需要上报
- `launcher.exe` 能观测关键状态变化

当前代码的问题是：

- `RuntimeState` 已存在
- `RuntimeMain` 里也在调用 `TransitionTo(...)`
- 但这些状态只存在于本地内存
- 当前没有真正的 heartbeat 或状态事件上报逻辑

这意味着：

- 状态机目前只是运行时内部的局部对象
- `launcher` 仍无法知道当前 runtime 是 `Running`、`Degraded` 还是 `Fatal`

需要一致化的方向：

- 将 `RuntimeState` 和 IPC 打通
- 在关键状态切换时发送 `StatusHeartbeat` 或专门的状态变更事件
- 让 `launcher` 按状态驱动 UI、日志和失败处理

### 21.8 `HookManager` 已存在，但实际业务未通过它管理

本文档要求：

- 使用统一的 `HookManager`
- 由它负责 Hook 注册、安装、启停、异常回滚和状态上报

当前代码的问题是：

- `HookManager` 类已经实现
- 但 `RuntimeMain` 并未通过 `HookManager` 安装或管理 FOV Hook
- `FovService` 仍然直接创建并操控 `mmh::Hook`

这意味着：

- Hook 生命周期仍然分散
- `HookManager` 目前更像预留骨架，而不是系统的真实调度中心

需要一致化的方向：

- 把 FOV Hook 的注册和启停迁移到 `HookManager`
- `FovService` 只保留策略与状态，不直接持有底层 Hook 生命周期
- 让 Hook 事件能走统一上报路径

### 21.9 FOV 运行状态仍依赖全局变量，未完全收敛到对象模型

本文档建议：

- 将 FOV 相关运行状态归入明确的 `FovRuntimeState` 对象
- 避免散落的全局共享状态

当前代码的问题是：

- 头文件里已经引入了 `FovRuntimeState`
- 但 `FovService.cpp` 仍然使用文件级全局：
  - `g_fovMutex`
  - `g_fovHook`
  - `g_fovState`
- `FovService` 类内自己的 `mutex` 和 `state` 成员没有真正承载核心状态

这意味着：

- 表面结构已经向文档靠拢
- 实际运行状态仍没有真正收口到对象边界内

需要一致化的方向：

- 让 `FovService` 自己拥有运行状态
- 将 Hook 回调与实例状态做可控绑定
- 至少实现单例对象到回调的明确桥接，而不是裸全局变量

### 21.10 运行模型仍然是单一 60Hz 大循环

本文档建议：

- 拆分输入采样、配置应用、Hook 回调、心跳上报
- 避免所有事情都在一个固定频率大循环中执行

当前代码的问题是：

- `RuntimeLoop()` 中每次循环都在做：
  - 键盘采样
  - 配置应用
  - FOV 预设切换
  - FPS/FOV 更新
  - 配置回写
  - 固定 60Hz 休眠

这意味着：

- 运行时职责仍然过度集中
- 定位时序问题和性能问题会比较困难

需要一致化的方向：

- 保留第一阶段实现也可以，但要明确这是过渡方案
- 后续至少应拆成：
  - 输入采样路径
  - 配置更新路径
  - Hook 回调路径
  - 心跳上报路径

### 21.11 完整性校验目前只有“文件存在性检查”，没有真正的信任校验

本文档要求：

- 校验签名或哈希
- 固定模块名
- 固定路径
- 不信任未验证模块

当前代码的问题是：

- `launcher` 会检查 `bootstrap.dll` 和 `runtime.dll` 是否存在
- `InjectBootstrap()` 与 `LoadRuntime()` 也会拒绝相对路径和网络路径
- 但并没有真正实现签名或哈希校验

这意味着：

- 当前“模块完整性校验”仍然停留在路径和存在性级别
- 还没有形成真正的可信来源模型

需要一致化的方向：

- 开发阶段先引入哈希白名单
- 发布阶段再补 Authenticode 签名校验
- 将“存在性检查”改名为“路径检查”，避免与真正的完整性验证混淆

### 21.12 版本白名单的匹配粒度比文档更宽

本文档要求：

- 未知版本默认拒绝
- 使用明确白名单和版本表

当前代码的问题是：

- `VersionTable` 的 `IsSupported()` 和 `GetProfile()` 只比对 `major/minor`
- 没有严格比对完整版本号

这意味着：

- 任何同主次版本但不同 patch/build 的版本，都可能被当成“已支持”
- 这与“未知版本默认拒绝”的目标不一致

需要一致化的方向：

- 至少在版本表中支持完整版本匹配
- 如果要做“同系列复用同一 profile”，也应该是显式声明，而不是默认用 `major/minor` 模糊匹配

### 21.13 `launcher` 的监控面还未建立

本文档要求：

- `launcher.exe` 作为控制平面，需要收集日志、状态与错误
- 应在恢复主线程后进入状态监控阶段

当前代码的问题是：

- `launcher` 在恢复主线程后只等待 3 秒，然后结束
- 没有持续监听 heartbeat、Hook 状态变化和错误事件

这意味着：

- 当前 `launcher` 仍更接近一次性启动器
- 还不是本文档定义的“控制平面”

需要一致化的方向：

- 至少实现最小监控循环
- 接收并记录 heartbeat、错误与功能状态变化
- `closeLauncherOnSuccess` 再决定是否立即退出

## 22. 一致化优先级建议

如果要把当前代码推进到“与本文档一致”的第一版，建议按下面顺序修正：

### 第一优先级

- 打通 `BootstrapInitialize()` 的显式远程调用
- 修正 IPC 建连时序
- 将握手失败改为阻断启动

这是因为这些问题决定了新架构是否真正成立。

### 第二优先级

- 删除 `runtime.dll` 内的文件配置读写
- 把配置快照显式传给 `RuntimeInitialize()`
- 让 `launcher` 成为唯一配置入口

这是因为这些问题决定了控制平面和运行面的边界是否清晰。

### 第三优先级

- 统一 IPC 协议定义与序列化实现
- 接入状态机、heartbeat、错误上报
- 将 `HookManager` 变成真实的 Hook 调度中心

这是因为这些问题决定了系统是否真正可观测、可维护。

### 第四优先级

- 引入哈希或签名校验
- 收紧版本白名单匹配粒度
- 将 FOV 全局状态进一步收口

这是因为这些问题决定了系统是否真正满足本文档提出的安全和长期演进目标。
