# 底层架构与核心算法

固件协议为 **Matter over Wi-Fi**（esp-matter / CHIP 数据模型）。本文说明模块分层、任务划分、数据流和每个核心算法的实现依据。UI 渲染（LVGL/LCD）不在本层范围内；显示驱动未来只消费 `app_state` 的快照与调色板。

## 1. 芯片事实依据（esp32-s3_datasheet_cn.pdf v2.2）

| 约束 | 规格书出处 | 对架构的影响 |
|---|---|---|
| 双核 Xtensa LX7 @ 240 MHz | 概述 | Core0 跑 Matter/Wi-Fi 协议栈，Core1 跑业务任务 |
| SRAM 512 KB（ROM 384 KB） | 特性 | 全部状态用静态结构 + 互斥锁，禁止按帧分配；天气 JSON 用 8 KB 静态接收缓冲 |
| 45 个可编程 GPIO | 引脚描述 | 3 输入 + 3 可选继电器输出余量充足 |
| Strapping 引脚 GPIO0/3/45/46 | 3 章 strapping 管脚 | Kconfig 默认开关用 GPIO4/5/6，menuconfig 帮助文本明确禁止绑定 strapping 脚 |
| USB 默认 GPIO19/20；Flash/PSRAM 占用相应管脚 | 同上 | 同上 |
| 2×12-bit SAR ADC、3×UART、2×I2C、LCD host、LED PWM、RMT、TWAI、USB OTG | 外设 | 本工程只用 GPIO 输入/输出，外设余量给未来屏幕/触摸 |
| Deep-sleep 7 µA，RTC 定时器/ULP 常驻 | 电源管理 | 本面板常供电，未启用低功耗路径 |

`io接口.png` 是经典 ESP32 DevKitC（非 S3）引脚图，不能用于 S3 接线；实际引出脚需对照模组原理图。

## 2. 分层架构

```
┌────────────────────────────────────────────────────────────┐
│ 接口层  panel_services（provider vtable + NVS 测试模式）      │
│         service_providers（把具体服务注册进 vtable）          │
│         未来: HTTP API 路由 / LVGL 显示驱动 只调这一层        │
├────────────────────────────────────────────────────────────┤
│ 服务层  time_service   weather_service   scene_engine       │
│         theme_service（纯函数）     channel_router（路由）    │
├────────────────────────────────────────────────────────────┤
│ 状态层  app_state（唯一状态源：互斥锁+快照+监听通知）          │
│         app_nvs（带版本号 blob 持久化）                       │
├────────────────────────────────────────────────────────────┤
│ 连接层  matter_nodes（esp-matter 端点/集群/事件桥接）          │
├────────────────────────────────────────────────────────────┤
│ 驱动层  switch_inputs（消抖状态机）   relay 输出（路由层内）   │
└────────────────────────────────────────────────────────────┘
```

依赖方向单向向下；上行只通过 `app_state` 的快照/监听。`panel_services` 是对外稳定 C 接口：测试模式替换灯光/场景为 fixture，天气因只读在两种模式下都取真实值。

## 3. 任务与核绑定

| 任务 | 核 | 优先级 | 职责 |
|---|---|---|---|
| CHIP/Matter、Wi-Fi | 0 | — | esp-matter `start()` 自建（协议栈内部任务） |
| `switch_scan` | 1 | 5 | 5 ms 采样 + 消抖状态机 + 长按 |
| `weather` | 1 | 4 | 等 IP → HTTPS 拉取/解析/缓存 → 周期刷新（默认 30 min） |
| `state_notify` | 1 | 4 | 串行分发快照给监听者（绝不在锁内回调） |
| `time_tick` | 1 | 3 | 1 s tick：时钟/问候/昼夜，仅变化时提交 |

启动顺序（`app_main.cpp`）：`app_nvs → panel_services(载入测试模式) → app_state → channel_router(继电器全灭) → time → weather → matter_nodes(建端点) → service_providers → switch_inputs → esp_matter::start`。输入任务在端点创建之后才启动，按键永远不会路由到不存在的端点。

## 4. 数据流（对齐 UI_DESIGN.md“事件与数据流”）

```
实体开关 GPIO ─→ 消抖状态机 ─→ 绑定路由 ─┬─ 绑定灯具: 切换绑定灯 (Matter OnOff + 继电器跟随)
                                         └─ 手动控制: 按键事件上报 / 场景键（触发绑定场景）
触摸 UI 卡片 ─→ 同一绑定路由（绑定卡=灯光开关，手动卡=事件合成）
Matter 客户端 OnOff 写入灯端点 (CHIP 任务, PRE_UPDATE) ─→ 路由层 ─→ 继电器跟随 + 状态模型
场景执行 (SC_*) ─→ set_light (src=SCENE) ─→ 本机 2 路灯
天气源 HTTPS ─→ weather_service(WMO 映射+采样) ─→ app_state ─→ NVS 缓存
系统时钟/SNTP ─→ time_service(问候/昼夜) ─→ app_state
app_state 变更 ─→ state_notify 任务 ─→ 监听者(UI) + 全屋灯具计数
```

## 5. 核心算法

### 5.1 积分式消抖状态机（switch_inputs.cpp）

5 ms 采样，每通道维护 `raw → candidate → stable` 三级：

```
sample = (GPIO == 0)                     // 按下接地，内部上拉
if sample != candidate: candidate = sample; run = 1
elif candidate != stable:
    if ++run >= DEBOUNCE_MS/5: settle()  // 连续 N 次一致才翻转 stable
```

单次毛刺最多推进 `run` 一次，永远无法跨越 stable 电平；与原时间戳法相比状态迁移路径显式、参数化（`PANEL_DEBOUNCE_MS`，默认 25 ms）。

- **瞬动模式**（menuconfig choice）：stable 下降沿发 `SW_PRESSED`，上升沿发 `SW_RELEASED`；按住 ≥ `PANEL_LONGPRESS_MS`（默认 550 ms，与原型长按一致）补发一次 `SW_LONG_PRESS`。
- **翘板模式**：每次 stable 翻转发 `SW_LEVEL(level)`，灯具同步到触点位置（双稳态同步，不按 toggle 处理）。
- 上电时把当前触点状态收养为 stable，避免开机误发事件。

### 5.2 绑定路由（channel_router.cpp）

三路通道全部是自定义开关，绑定关系存于用户配置（`channel_binding[]`/`channel_bind_light[]`，随 PNL1 v2 持久化）：

- **绑定设备**（`CH_BIND_LIGHT` + `channel_dev_kind`）：绑灯时开/关切换灯（`set_light`），绑空调时按键切换空调电源（`app_state_set_ac`，状态随 PNL1 v4 持久化）；
- **手动控制**（`CH_BIND_MANUAL`）：未配置动作时按下→`InitialPress`、松开→`ShortRelease`、翘板→`SwitchLatched(position)`、长按→本地动作钩子（预留）；配置了场景动作（`channel_action[]`）则为场景键——按下触发绑定场景，不再上报按键事件；
- 灯命令源（`CH_SRC_LOCAL/SCENE/MATTER`）汇聚到 `set_light()`：非 MATTER 源先 `attribute::update()` 通知 fabric；继电器"跟随灯"——所有绑定到该灯的通道继电器同步动作（默认绑定 1:1）；
- MATTER 源（CHIP 任务 `PRE_UPDATE` 回调）直接写继电器 + 状态模型（GPIO 写幂等，状态模型有锁）。

**安全**：继电器上电默认全灭；`panel_services_test_mode()` 为真时 `relay_write()` 直接返回——逻辑全跑、输出全断。

### 5.3 场景引擎（scene_engine.cpp）

场景表精确复制原型 `runScene()` 的状态矩阵，`targets[5]` 按原型灯具顺序 `[客厅,餐厅,卧室,走廊,阳台]`：

```
home:[1,1,0,1,0]  rest:[0,0,1,0,0]  away:[0,0,0,0,0]
movie:[1,0,0,0,0] read:[0,0,1,0,0]
```

固件只执行本机 2 路（下标 0/1），下标 2–4 留给家庭控制器同步（当前边界，README 已注明）。场景集合固定六个（UI 直出全部，无场景库浮层）；未来由控制器导入的场景同样以 6 为上限。

### 5.4 天气适配器（weather_service.cpp）

- **WMO 4677 天气码 → 四桶**（与原型的 sun/cloud/rain/snow 对应）：
  `0→晴`；`51–67, 80–82, 95–99→雨`；`71–77, 85, 86→雪`；其余（1–3 云、45/48 雾）→多云。
- **HTTP**：流式 `open → fetch_headers → read → close`（`perform()` 会丢弃响应体），mbedtls 证书 bundle 校验 TLS，10 s 超时，8 KB 静态缓冲。
- **逐小时采样**：`forecast_days=1` 返回 24 条本地小时数据，下标即当前小时；取 `[now, +2, +4, +6 h]` 四点，温度四舍五入、天气码同表映射。
- **缓存与回退**：成功后写 NVS（`WETH` blob，带 magic+version）；启动时先读缓存，无缓存显示与原型一致的演示值（24°/42%/晴/杭州）；拉取失败保留最后快照并把 `stale` 置位，1 分钟后重试，成功后回到周期刷新。
- 天气在测试模式下仍取真实值（只读、无安全面），`panel_services.weather_get` 从 `app_state` 映射。

### 5.5 主题/昼夜解析（theme_service.cpp，纯函数）

复刻原型 CSS 变量级联：

```
palette = kBase[theme]                                  // 清透蓝/暖阳/薄荷/浅色
if time_auto && night && theme==清透蓝: palette = kCoolNight   // data-time="night"
if weather_auto: palette.glow = kWeatherGlow[weather]         // data-weather 氛围光
```

调色板数值直接取自 `panel-preview.html` 的 CSS（bg/card/card2/ink/muted/accent/edge/glow），未来显示驱动把它映射到 LVGL 主题即可；本模块不做任何渲染。浅色主题显式深色文字/强调色，符合 UI_DESIGN.md。

### 5.6 时间服务（time_service.cpp）

STA `GOT_IP` 启动 SNTP（`CONFIG_PANEL_NTP_SERVER` + `pool.ntp.org` 兜底，`IMMED` 同步模式），`LOST_IP` 停止但保留已同步纪元。问候语四段与原型一致：`<6 夜深 / <11 早上 / <18 下午 / 其余 晚上`；昼夜判定 `06:00–17:59`。1 s tick 里组合 `clock/date/greeting/is_day/synced`，**仅在字符串或相位变化时提交**状态（app_state 内部 memcmp 去重），分钟不变不产生通知流量。

### 5.7 状态模型与持久化（app_state.cpp / app_nvs.cpp）

- 唯一状态源 `app_snapshot_t`：2 路灯（名称/状态）+ 3 通道开关（名称/绑定/触发态/GPIO）+ 用户配置（含绑定）+ 天气 + 时间 + 灯具计数 + 活动场景。互斥锁保护，`app_state_get_snapshot()` 取副本。
- 变更走 `commit → 置脏 → 通知队列`；`state_notify` 任务在锁外串行调用监听者，慢消费者不会阻塞生产者。
- **灯具统计**：数 `lights[2]`（本机两路灯实体），开关不进分子也不进分母（UI_DESIGN.md 规则）。远端灯具待家庭控制器同步后并入。
- NVS blob 统一 `magic + version` 头（配置 `PNL1` v2——含通道绑定、天气 `WETH`），版本不符自动回退默认值并重写；只在用户显式改动或天气成功拉取时写盘，寿命友好。

## 6. Matter 数据模型（matter_nodes.cpp）

| Endpoint | Device type | 集群/事件 |
|---|---|---|
| 1 | On/Off Light | 灯 1（客厅主灯）OnOff 属性；本地/场景改动经 `attribute::update()` 上报 |
| 2 | On/Off Light | 灯 2（餐厅吊灯）同上 |
| 3–5 | Generic Switch | 每通道一个开关端点；Switch 集群（2 位），`momentary_switch` + `action_switch` feature；手动绑定通道上报 `InitialPress/ShortRelease/SwitchLatched` |

灯端点可被三路开关任意绑定（固件内路由，改变绑定无需重新配网）；手动通道的事件经 Switch 集群上报，供中枢做自动化。远端 OnOff 写入由 `attribute_update` 回调（`PRE_UPDATE`，CHIP 任务）转给路由层——这是 Matter 与本机输出之间唯一的桥。AP 侧使用测试 DAC/PAI，量产前必须替换正式认证材料（README 安全边界）。

## 7. 配置项（menuconfig → Home panel hardware）

`PANEL_TEST_MODE_DEFAULT`（默认开）、开关/继电器 GPIO×3、开关触点类型（瞬动/翘板）、消抖毫秒、长按毫秒、POSIX 时区、NTP 服务器、天气 URL/城市标签/刷新周期。全部有默认值，零配置即可编译烧录（测试模式下继电器始终断开）。

## 8. 已知边界

- HTTP API 路由未实现（`SERVICE_API.md` 已定义契约）；`service_providers` 已就绪等接入。
- 全屋灯具统计目前只含本机 2 路；远端灯具需 Matter 控制器订阅同步。
- 每路“自定义动作”只有长按钩子，动作执行器待定义。
- 显示栈已落地（`bsp_display` + `ui_app`/`ui_home`/`ui_settings`，LVGL 9 + esp_lvgl_port，
  Waveshare 4.3C RGB 屏 + GT911 触摸 + 板载 ES8311/ES7210 双麦音频）。
- 语音助手已搭骨架：`bsp_audio`（I2S1 全双工 16k，PA 经 CH422G IO3）→ `voice_assistant`
  （录音→POST 用户 API→JSON 动作分发到路由/场景/绑定/媒体，TTS WAV 回放）→ `ui_voice`
  （小爱风格全屏层）→ `service_http`（手机端写入 API 地址/密钥）。真机联调与流式播放待做。
