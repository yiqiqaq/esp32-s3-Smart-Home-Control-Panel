ESP32-S3 三路智能面板 固件 v0.1.0-test.1（测试 1 版）
=====================================================

适用硬件 : Waveshare ESP32-S3-Touch-LCD-4.3B（ESP32-S3-WROOM-1-N16R8，
           16MB Flash / 8MB PSRAM，RGB 800×480 + GT911 触摸）
构建环境 : ESP-IDF v5.5.5 · esp-matter 1.4 · LVGL 9.6
发布日期 : 2026-10-07

本版本内容（测试 1）
--------------------
- 触摸 UI 首版：首页（Matter 状态 / 时钟 / 天气 / 灯具统计 / 逐小时预报 /
  三通道卡片 / 快捷场景条）、场景库浮层、设置页（主题库四选一、天气与昼夜
  联动开关、测试模式开关）。
- Matter over Wi-Fi：2 路 On/Off Light + 1 路 Generic Switch 端点。
- 实体开关输入默认关闭（纯触摸 UI 形态）；继电器输出默认禁用，
  测试模式默认开启——所有输出保持断开，仅验证界面与协议逻辑。
- 默认状态：清透蓝主题 · 演示天气（24°/杭州）· SNTP/天气联网后自动刷新。

烧录方式 A：合并镜像（推荐，一条命令写入全部）
------------------------------------------------
  python -m esptool --chip esp32s3 -p <串口号> -b 460800 \
    --before default_reset --after hard_reset write_flash 0x0 flash_all_v0.1.0-test.1.bin

也可用乐鑫 Flash Download Tool / esptool GUI 选择该文件、地址填 0x0。

烧录方式 B：分文件烧录
----------------------
  地址 0x00000  bootloader.bin
  地址 0x08000  partition-table.bin
  地址 0x0F000  ota_data_initial.bin
  地址 0x20000  matter_home_panel.bin

  python -m esptool --chip esp32s3 -p <串口号> -b 460800 write_flash \
    --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0 bootloader.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin 0x20000 matter_home_panel.bin

macOS 串口形如 /dev/cu.usbmodem*，Windows 为 COMx。

首次上电
--------
1. USB-C 供电（5V/450mA），屏幕点亮进入首页，测试模式芯片可见属正常。
2. Wi-Fi 配网 / Matter 配网使用任意 Matter 控制器（Apple Home、Google Home、
   Home Assistant 等）扫描配网码或手动 pairing；联网后时钟与天气自动刷新。
3. 固件版本可在后续"系统状态"页查看，当前也可用
   `python -m esptool --chip esp32s3 image_info matter_home_panel.bin` 或串口日志
   （TAG: home_panel）确认。

文件校验
--------
见 SHA256SUMS.txt。合并镜像与分文件二选一即可，两者内容一致。

已知边界（与仓库 ARCHITECTURE.md 一致）
----------------------------------------
- 通道设置页、自定义开关详情页、全屋灯具列表、面板名称编辑、恢复出厂未实现。
- HTTP API 路由（SERVICE_API.md 契约）未实现。
- 量产前需替换 Matter 测试 DAC/PAI 证书为正式认证材料。
