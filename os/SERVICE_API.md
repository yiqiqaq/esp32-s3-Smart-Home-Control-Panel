# 服务 API 与设置模式

## 当前实现边界

- `main/panel_services.h` 定义固件侧 C 接口和 weather / home-lights / scenes provider vtable。
- `panel_services.cpp` 实现可持久化的 NVS 测试模式和五灯具/五场景/天气 fixture。测试模式下灯具、场景操作只返回演示成功，`app_main.cpp` 会跳过继电器输出。
- `ui/settings.html` 是设置模式 HTML 页面，支持服务地址设置、测试模式开关、API 连通性按钮及服务配置存储。本地原型设置保存在浏览器 localStorage。
- 真实天气和全屋 Matter Controller provider 尚未接入；HTTP server/router 也尚未在固件中注册。HTML 页面列出的 REST 路由是设备端待实现 API 契约，不代表服务已上线。

## 本地 HTTP API v1 契约

面板服务仅应在受信任的家庭局域网内启用。不要将这些管理路由直接暴露到公网。

| Method | Path | 用途 |
|---|---|---|
| GET | `/api/v1/status` | 固件版本、Wi-Fi、Matter commissioning、测试模式状态 |
| GET | `/api/v1/settings` | 读取主题、天气位置、刷新周期、测试模式 |
| PUT | `/api/v1/settings` | 更新非秘密 UI 和服务配置 |
| PUT | `/api/v1/settings/test-mode` | 保存 `{ "enabled": true }`；`true` 使用 fixture 且禁止继电器驱动 |
| GET | `/api/v1/weather/current` | 当前温度、湿度、天气代码、更新时间、缓存过期状态 |
| GET | `/api/v1/weather/hourly?hours=6` | 未来小时预报 |
| GET | `/api/v1/home/lights` | 返回已发现灯具状态；过滤非灯具 Generic Switch |
| PUT | `/api/v1/home/lights/{device_id}` | 控制灯具 `{ "on": true }` |
| GET | `/api/v1/scenes` | 获取手机/家庭控制器同步场景及 pinned 状态 |
| POST | `/api/v1/scenes/{scene_id}/activate` | 调用指定场景 |
| PUT | `/api/v1/scenes/{scene_id}/pin` | 固定状态 `{ "pinned": true }`；最多 3 项 |

建议使用的响应形状：

```json
{
  "ok": true,
  "test_mode": true,
  "data": {},
  "updated_at": "2026-10-05T12:00:00Z",
  "stale": false
}
```

错误响应统一为 `{"ok":false,"error":{"code":"SERVICE_UNAVAILABLE","message":"..."}}`。灯具设备状态必须来自 Matter attribute subscription/家庭控制器缓存；不要在 HTTP 请求处理线程内执行阻塞 Matter 操作或网络天气请求。

## ESP32-S3 性能约束

- 目标 UI 画布为 800×480（5:3）。已确认的 4.3inch DSI LCD 使用 MIPI DSI，而 ESP32-S3 仅提供 RGB/I80/SPI 等 LCD host；没有 MIPI DSI PHY/host。HTML 适配完成，但当前 MCU 与屏幕不能直连，需确定 DSI bridge 或改用支持 DSI 的显示主控后才能落地 LCD driver。
- UI 采用小型状态快照，不在渲染回调里做 HTTP/Matter I/O。Weather fetch 和 Matter controller 查询应在独立低优先级任务执行，通过队列投递轻量数据到 UI task。
- 天气建议 15–60 分钟拉取并缓存；首页只维护 6 小时短预报。离线时继续显示最后一次快照并标记 stale。
- 固定数组上限由 `PANEL_MAX_LIGHTS` / `PANEL_MAX_SCENES` 控制；不为天气记录或每帧界面更新动态分配大块内存。
- 不默认开启 PSRAM 专用内存、RGB framebuffer 或高刷新率动画，因为规格书不代表具体 S3 模组/开发板有 PSRAM 和指定屏幕接口。
- 默认测试模式在 `menuconfig → Home panel hardware` 开启，并持久化到 NVS。进入真实运行模式前，须先连接并验证硬件 provider 和继电器安全逻辑。

## 语音助手（v0.2 契约，固件已实现）

语音 API 的地址与密钥由手机端写入面板，面板把录音交给该 API 并执行返回的动作。

### 配置（面板作为 HTTP 服务端，端口 80，CORS 开放）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/v1/voice/config` | 返回 `{"api_url": "...", "api_key_set": true}`（密钥只写不读） |
| POST | `/api/v1/voice/config` | body `{"api_url": "...", "api_key": "..."}`，持久化到 NVS |

### 查询（面板 → 语音 API，POST api_url）

- 请求体：WAV（16 kHz / 16 bit / 单声道），头 `X-Api-Key: <key>`，`Content-Type: audio/wav`
- 音乐控制改为 JSON body：`{"cmd": "play|pause|next|prev"}` 或 `{"volume": 40}`
- 响应体：

```json
{
  "reply": "好的，已为你打开客厅主灯",
  "tts_url": "https://.../tts.wav",
  "action": {
    "type": "light",  "light": 1, "on": true,
    "type": "scene",  "scene": 0,
    "type": "bind",   "channel": 3, "mode": "light", "light": 1,
                      "mode": "manual", "action_scene": 0,
    "type": "music",  "playing": true, "title": "歌名", "volume": 40,
    "type": "ac",     "ac": 1, "on": true, "mode": "cool|heat",
                     "fan": "auto|low|mid|high", "temp": 26, "temp_now": 27
  }
}
```

- `reply`：面板全屏语音层显示的应答文本；`tts_url` 可选，指向 16k/16-bit/单声道 WAV，面板拉流播放（PA 自动使能）。
- `action` 可选；`light`/`ac` 的索引从 1 开始；`ac.mode` 取 cool/heat、`ac.fan` 取 auto/low/mid/high、`temp` 为设定温度（16–30）、`temp_now` 为上报室温（仅更新显示）；`bind.mode` 取 light（配 `light` 索引）/manual（可用 `action_scene` 配成场景键）/ac（配 `ac` 索引）。
- 未实现：MP3/AAC 解码（TTS 暂只支持 WAV）、流式音乐播放管线。
