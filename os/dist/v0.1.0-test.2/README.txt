ESP32-S3 三路智能面板 固件 v0.1.0-test.2（测试 2 版）
=====================================================

适用硬件 : Waveshare ESP32-S3-Touch-LCD-4.3C 一体板
           ESP32-S3-WROOM-1-N16R8，16MB Flash / 8MB PSRAM，RGB 800×480 + GT911
构建环境 : ESP-IDF 5.5.5 · esp-matter 1.6.0 · LVGL 9.6.0
构建日期 : 2026-10-07

本版本内容（测试 2）
--------------------
- 设置页支持通道绑定：按房间选择灯具或空调，也可选手动开关事件或场景键。
- 主界面控制卡按实际绑定的设备显示名称；空调卡进入独立控制面板。
- 新增语音助手界面、板载麦克风录音、API 配置 HTTP 接口、语音动作路由和 WAV TTS 回放。
- 新增音乐播放控制栏与 API 控制接口（上一首、播放/暂停、下一首、音量）。
- 内置主题、天气/时间联动、六个场景及原有 Matter 2 灯 + 3 Generic Switch 端点。
- 固件版本戳：v0.1.0-test.2；编译后的应用分区剩余约 18%。

安全与验证状态
--------------
- 测试模式默认开启；继电器 GPIO 默认禁用，避免驱动外部负载。
- 实体开关 GPIO 输入默认关闭；触摸 UI 和 Matter 逻辑可独立构建。
- 本版已通过 ESP-IDF 固件构建，但尚未在实体板卡上联调。语音需要配置兼容的语音 API 地址和密钥；TTS 仅支持 WAV，不支持 MP3/AAC 解码或流式音乐播放。
- Matter 使用测试 DAC/PAI 证书。量产前必须换用正式认证材料。

烧录方式 A：合并镜像（推荐）
----------------------------
将 `flash_all_v0.1.0-test.2.bin` 写入 0x0：

  python -m esptool --chip esp32s3 -p <串口号> -b 460800 \
    --before default_reset --after hard_reset write_flash 0x0 \
    flash_all_v0.1.0-test.2.bin

也可用乐鑫 Flash Download Tool / esptool GUI，地址设为 0x0。

烧录方式 B：分文件烧录
----------------------
  地址 0x00000  bootloader.bin
  地址 0x08000  partition-table.bin
  地址 0x0F000  ota_data_initial.bin
  地址 0x20000  matter_home_panel.bin

  python -m esptool --chip esp32s3 -p <串口号> -b 460800 write_flash \
    --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0 bootloader.bin 0x8000 partition-table.bin \
    0xf000 ota_data_initial.bin 0x20000 matter_home_panel.bin

文件校验
--------
校验本目录中的 SHA256SUMS.txt。合并镜像与分文件烧录二选一即可。

已知限制
--------
- 未进行实体屏幕、触摸、音频、Matter 配网或继电器硬件联调。
- 语音 API 服务需由使用者提供并配置；本包不含服务端、API 密钥或语音资源。
- TTS 当前仅支持 WAV；没有 MP3/AAC 解码和流式音乐播放实现。
- 全屋灯具统计目前只包含面板本机的两路灯；远端设备同步待接入。
