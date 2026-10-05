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
实体开关 GPIO ─→ 消抖状态机 ─→ 通道用途路由 ─┬─ 灯具:   Matter OnOff 属性 + 隔离继电器
                                            └─ 自定义: Generic Switch 事件 + 本地动作钩子
Matter 客户端 OnOff 写入 (CHIP 任务, PRE_UPDATE) ─→ 同一路由层 ─→ 继电器 + 状态模型
场景执行 (SC_*) ─→ 通道路由 (src=SCENE) ─→ 本机 2 路灯具
天气源 HTTPS ─→ weather_service(WMO 映射+采样) ─→ app_state ─→ NVS 缓存
系统时钟/SNTP ─→ time_service(问候/昼夜) ─→ app_state
app_state 变更 ─→ state_notify 任务 ─→ 监听者(未来 UI) + 全屋灯具计数
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

### 5.2 通道用途路由（channel_router.cpp）

三个命令源（`CH_SRC_LOCAL/SCENE/MATTER`）汇聚到 `channel_router_set_channel()`：

- 非 MATTER 源先 `attribute::update()` 通知 Matter fabric，再写继电器、再更新状态模型；
- MATTER 源（CHIP 任务 `PRE_UPDATE` 回调）直接写继电器 + 状态模型（GPIO 写幂等，状态模型有锁）；
- `CH_KIND_CUSTOM` 通道不进灯具统计：按下→`InitialPress`、松开→`ShortRelease`、翘板→`SwitchLatched(position)`、长按→本地动作钩子（预留）。

**安全**：继电器上电默认全灭；`panel_services_test_mode()` 为真时 `relay_write()` 直接返回——逻辑全跑、输出全断。

### 5.3 场景引擎（scene_engine.cpp）

场景表精确复制原型 `runScene()` 的状态矩阵，`targets[5]` 按原型灯具顺序 `[客厅,餐厅,卧室,走廊,阳台]`：

```
home:[1,1,0,1,0]  rest:[0,0,1,0,0]  away:[0,0,0,0,0]
movie:[1,0,0,0,0] read:[0,0,1,0,0]
```

固件只执行本机 2 路（下标 0/1），下标 2–4 留给家庭控制器同步（当前边界，README 已注明）。固定场景在 `app_state_update_config` 中原子地做 read-modify-write，空槽 `0xFF`、上限 3、溢出返回 `ESP_ERR_INVALID_STATE`，并持久化。

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

- 唯一状态源 `app_snapshot_t`：3 通道（名称/用途/状态/GPIO）+ 用户配置 + 天气 + 时间 + 灯具计数 + 活动场景。互斥锁保护，`app_state_get_snapshot()` 取副本。
- 变更走 `commit → 置脏 → 通知队列`；`state_notify` 任务在锁外串行调用监听者，慢消费者不会阻塞生产者。
- **灯具统计**：只数 `CH_KIND_LIGHT` 通道，Generic Switch 不进分子也不进分母（UI_DESIGN.md 规则）。远端灯具待家庭控制器同步后并入。
- NVS blob 统一 `magic + version` 头（配置 `PNL1`、天气 `WETH`），版本不符自动回退默认值并重写；只在用户显式改动或天气成功拉取时写盘，寿命友好。

## 6. Matter 数据模型（matter_nodes.cpp）

| Endpoint | Device type | 集群/事件 |
|---|---|---|
| 1 | On/Off Light | OnOff 属性；本地/场景改动经 `attribute::update()` 上报 |
| 2 | On/Off Light | 同上 |
| 3 | Generic Switch | Switch 集群（2 位），显式添加 `momentary_switch` + `action_switch` feature；`InitialPress/ShortRelease/SwitchLatched` 事件 |

远端 OnOff 写入由 `attribute_update` 回调（`PRE_UPDATE`，CHIP 任务）转给路由层——这是 Matter 与本机输出之间唯一的桥。AP 侧使用测试 DAC/PAI，量产前必须替换正式认证材料（README 安全边界）。

## 7. 配置项（menuconfig → Home panel hardware）

`PANEL_TEST_MODE_DEFAULT`（默认开）、开关/继电器 GPIO×3、开关触点类型（瞬动/翘板）、消抖毫秒、长按毫秒、POSIX 时区、NTP 服务器、天气 URL/城市标签/刷新周期。全部有默认值，零配置即可编译烧录（测试模式下继电器始终断开）。

## 8. 已知边界

- HTTP API 路由未实现（`SERVICE_API.md` 已定义契约）；`service_providers` 已就绪等接入。
- 全屋灯具统计目前只含本机 2 路；远端灯具需 Matter 控制器订阅同步。
- 每路“自定义动作”只有长按钩子，动作执行器待定义。
- 4.3" DSI 屏无法直连 S3（无 MIPI DSI host），显示方案见 SERVICE_API.md“性能约束”。
