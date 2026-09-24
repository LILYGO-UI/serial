# LILYGO Serial 架构总览

本应用是基于 C++17、LILYGO UI AppKit 和 LVGL 9.5 的串口调试工具。
设备构建面向 CM0 ARM64 Linux，桌面模拟器使用 SDL2；两者都通过 POSIX
串口接口访问运行应用的主机上的真实串口。

项目介绍和环境准备见 [English README](../README.md) / [中文 README](../README.zh-CN.md)，
操作、权限和运行限制见[串口使用说明](02-serial-usage.md)（英文），
布局、字体和生命周期要求见 [UI 开发规范](01-user-interface.md)。

## 数据流

```text
启动 / Scan ──> 扫描 /dev ──> ViewModel 端口列表 ──> LVGL 下拉框

用户输入 ──> View 事件 ──> ViewModel 命令 ──> Model / termios / ioctl
                                                       │
View 的 20 ms 定时器 ──> ViewModel::poll() ──> 非阻塞串口读写
                                                       │
LVGL 控件 <── View::refresh() <── revision Subject <── 状态与记录
```

应用没有串口工作线程。串口以 `O_NONBLOCK` 打开，`poll()` 使用零超时；
View 的 LVGL 定时器每 20 ms 请求一次轮询，处理接收、待发送数据和可读取的
RTS/DTR 状态。单次接收轮询最多读取 64 KiB。这个间隔是 UI 调度周期，
不是实时采样保证，也不是串口报文边界。

Model 在状态变化时递增 revision；ViewModel 将状态转换成文案、计数和日志，
通过一个整型 Subject 通知 View。空闲轮询不发布无变化的状态，View 也只在
日志文本改变时更新接收标签。

## 模块边界

| 模块 | 责任 |
| --- | --- |
| `src/main.cpp`、`src/app.*` | AppKit 入口、应用身份、ViewModel 和 View 的创建与销毁。 |
| `src/pages/serial/serial_model.*` | 端口发现、串口配置、非阻塞读写、控制线、收发记录、ASCII/HEX 转换和容量限制；不依赖 LVGL。 |
| `src/pages/serial/serial_view_model.*` | 所选端口、连接命令、显示偏好、错误状态、计数和接收日志格式化；通过 Subject 发布变化。 |
| `src/pages/serial/serial_view.*` | LVGL 控件、事件转发、20 ms 轮询定时器、状态绑定、响应式布局和软键盘。 |
| `src/components/components.*`、`lv_subject.hpp` | 公共语义颜色、基础控件及 Subject 的 RAII 生命周期。 |

依赖方向必须保持 `View -> ViewModel -> Model`。View 不得直接修改 Model，
ViewModel 不持有具体控件，Model 必须能在不初始化 LVGL 的情况下测试。
当前只有一个串口页面；只有出现真实的跨页面业务或导航需求时，才按需增加
`src/domain/` 或应用路由，不创建空目录或未使用的抽象。

构建中，纯 Model 位于 `lilygo_ui_serial_core`，UI 和 AppKit 集成位于
`lilygo_ui_serial_ui`；页面代码放在同一目录不改变这两个依赖层。

## 生命周期与连接状态

1. AppKit 调用 `app_open()`，先创建 ViewModel 并扫描端口，再创建 View；
   View 的根对象位于 AppKit 提供的 `context->root` 下。
2. 用户选择设备与参数后连接。Model 保存原始 termios 和能够读取到的 modem
   状态，设置 raw mode、波特率、帧格式和流控，并读回校验波特率与主要帧配置。
3. 连接期间，端口和串口参数不可修改。View 轮询接收数据并继续发送未完成的数据；
   用户可以调整接收/发送格式、时间戳、自动滚动和行结束符。
4. 断开时，Model 尝试释放本应用启用的 BREAK、恢复原始 termios 和 modem 状态，
   然后关闭文件描述符并丢弃尚未写出的队列。设备已经拔出时，恢复操作可能失败。
5. `app_close()` 先删除 View 的定时器和根 LVGL 对象，再销毁 ViewModel。
   对象绑定的 observer 随根对象删除；Model 析构负责关闭串口。

端口发现仅在 ViewModel 初始化和点击 **Scan** 时执行，不由定时器自动扫描。
重新扫描尽量保留所选路径；已经连接的可见端口即使暂时未被发现，也会留在列表中。
`/dev/ttyS0` 和 `/dev/serial0` 被端口发现逻辑隐藏，完整匹配规则见
[设备发现说明](02-serial-usage.md#device-discovery)。

## 数据与容量

| 层级 | 当前约束 |
| --- | --- |
| 输入控件 | 最多 16,384 个字符。 |
| 单条发送消息 | 解析后的字节加上所选行结束符合计最多 64 KiB。 |
| 待发送队列 | 最多 64 KiB；空间不足时整条新消息被拒绝。 |
| Model 历史 | RX/TX 合计最多 256 KiB 原始数据、512 条记录；任一超限就从最旧记录开始删除。 |
| 接收显示 | 仅包含 RX，格式化文本最多 32 KiB；必要时显示省略提示，每条显示记录最多保留末尾 4 KiB 原始数据。 |

记录对应一次成功的系统读写，不代表完整协议帧。RX/TX 计数统计实际读取或
成功写入系统的字节数；TX 增加不表示对端已经接收或响应。
接收格式切换会重新格式化保留的原始 RX 数据，不修改原始记录。

**Clear** 清除历史与 RX/TX 计数，保留连接和待发送队列；它不清空驱动中的
输入或输出缓冲区。断开和重连保留本次运行的历史与计数，关闭应用后这些数据、
所选端口和显示偏好不持久化。

## 失败策略

- 无设备时显示空列表并禁用连接；插入设备后需要点击 **Scan**。
- 参数无效、权限不足或驱动无法应用配置时，连接失败并显示具体错误。
- 发送格式错误或队列已满时拒绝该消息，不发送部分解析结果。
- 读写错误或检测到断开时关闭连接、清除未发送队列，并提示检查设备后重新连接；
  不自动重连。
- 控制线不受平台或驱动支持时显示错误，不伪造成功状态。RTS/CTS 模式下禁用
  手动 RTS 控制，DTR 和 BREAK 仍由驱动能力决定。

这些错误通过连接状态文案和页面顶部的错误条呈现。应用不使用网络服务，
不保存日志文件，也不提供协议解码或自动应答。

## 构建与配置边界

- CMake 最低版本为 3.21，应用源码和测试使用 C++17 或更新标准；AppKit
  集成边界使用其公开 C ABI。
- [`lpm.toml`](../lpm.toml) 是应用身份、版本、权限、资源和构建入口的唯一
  手工维护元数据源。当前包名/可执行文件是 `lilygo-ui-serial`，应用 ID 是
  `cc.lilygo.ui.Serial`；Launcher、Desktop 和 AppStream 元数据在配置阶段生成。
- 应用通过 `find_package(LilyGoUI CONFIG REQUIRED)` 使用锁定的 AppKit
  配置包。`third_party/cm0-appkit` 由 presets 指定；不得用应用侧
  `add_subdirectory()` 替代此接口，不得依赖 Launcher、其他应用仓库或 AppKit
  私有文件。LVGL 及补丁由 AppKit 提供，应用不引入第二份 LVGL。
- `host-simulator` 和 `cm0-cross` 是稳定构建入口。完整依赖安装、LPM 命令、
  BSP 下载和打包步骤见 README；构建产物不进入源码目录。
- `.cache/`、`.lpm/`、`.venv/`、`build/` 和 `dist/` 是本地状态或生成物，
  不得提交。`publish.json` 是发布流程临时数据，不得手工维护或作为源码依赖。
- 不得使用旧版 `cm0-app-*`、`lilygo-cm0-*`、`cc.lilygo.cm0.*` 或 `org.cm0.*`
  作为公开身份；仅迁移真实已发布包时才添加 Debian `Provides`、`Conflicts`
  或 `Replaces`。

## 验收

代码修改至少完成主机配置、构建和测试：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

当前六项测试覆盖 Model、ViewModel，以及 `568x1232`、`1232x568`、`390x844`、
`844x390` 四种视口下的渲染和交互。串口收发使用伪终端，覆盖格式校验、
部分写入、队列与历史限制、断开处理和终端配置恢复。

UI 修改还必须检查四种视口的渲染产物及软键盘、错误和尺寸切换状态，
要求见 [UI 验收规范](01-user-interface.md#验收)。修改安装、SDK、工具链、
元数据或打包逻辑时，还必须完成 `cm0-cross` 构建和 Debian 打包验证。
伪终端测试不能替代真实硬件上的波特率、帧格式、流控和 RTS/DTR/BREAK 电气验证。
