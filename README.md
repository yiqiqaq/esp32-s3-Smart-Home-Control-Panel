# ESP32-S3 智能家居控制面板

基于 ESP32-S3、ESP-IDF 和 ESP-Matter 的智能家居触摸控制面板项目。项目包含 Matter 设备端固件、800×480 面板 UI、硬件方案与 PCB 布线资料，并提供浏览器交互预览和测试固件发布包。

![固件界面预览](os/ui/preview/firmware_home.png)

## 项目功能

- **Matter over Wi-Fi**：提供两路 On/Off Light 和三路 Generic Switch endpoint；实体开关、本机控制、场景与 Matter 写入通过通道路由汇总。
- **面板控制逻辑**：支持三路开关绑定灯具或空调，瞬动按钮/保持型翘板输入、消抖和长按识别。
- **显示与交互**：面向 800×480 触摸屏的 LVGL 面板界面，包含灯光、场景、天气、时间、空调、设备绑定和设置页面。
- **天气与时间**：通过 SNTP 获取时间；天气服务采用 Open-Meteo 兼容接口，支持预报、缓存和离线状态显示。
- **状态保存**：用户设置和天气快照保存在 NVS，断电后保留。
- **语音与音频**：包含板载音频编解码器、麦克风输入和语音助手功能骨架。
- **硬件资料**：提供 ESP32-S3 86 面板的 BOM、接线方案、PCB 资料和自动布线整改记录。

当前固件重点是面板本地控制和 Matter 设备端。全屋设备汇总需要 Matter Controller/家庭中枢集成；HTTP 服务路由和部分自定义动作仍在后续计划中。详细实现边界见 [`os/README.md`](os/README.md) 与 [`os/SERVICE_API.md`](os/SERVICE_API.md)。

## 硬件

当前板卡方案为 **Waveshare ESP32-S3-Touch-LCD-4.3C**，基于 ESP32-S3，配备 800×480 RGB 触摸屏及板载音频硬件。硬件清单、供电和连接说明见 [`os/HARDWARE.md`](os/HARDWARE.md)；PCB 设计资料位于 [`hardware/86panel/`](hardware/86panel/)。

> 固件测试模式默认开启，继电器输出默认关闭。ESP32 GPIO 只能连接符合规格的低压隔离接口，不能连接市电或灯具导线。市电部分须按当地规范设计并由具备资质的人员安装。

## 开发环境与构建

需要 ESP-IDF **5.5.5**、ESP-Matter 及其依赖。macOS 下从仓库根目录执行：

```bash
source os/export_idf.sh
idf.py --version
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

烧录和串口监视：

```bash
idf.py flash monitor
```

首次构建可能需要联网获取 ESP-Matter 组件。若 ESP-IDF 安装路径不是脚本默认路径，请先设置 `IDF_PATH`。硬件 GPIO、屏幕引脚和继电器配置必须根据所用开发板原理图核对；示例 GPIO 不是所有板卡的最终引脚分配。

## UI 预览

可在桌面浏览器打开以下页面查看交互原型，无需构建固件：

- [当前固件交互预览](os/ui/firmware-preview.html)
- [面板视觉稿](os/ui/panel-preview.html)
- [设置模式预览](os/ui/settings.html)

UI 设计说明与页面截图见 [`os/UI_DESIGN.md`](os/UI_DESIGN.md)。浏览器预览使用演示数据，不会连接真实设备或天气服务。

## 项目结构

```text
.
├── hardware/86panel/       PCB、BOM 与布线记录
├── drc_fix/                PCB 自动布线与 DRC 整改过程资料
├── os/
│   ├── main/               ESP-IDF 固件源码
│   ├── ui/                 HTML 预览及截图
│   ├── dist/               测试固件与发布压缩包
│   ├── HARDWARE.md         硬件、供电与安全说明
│   ├── ARCHITECTURE.md     固件架构与核心算法
│   └── SERVICE_API.md      面板服务接口约定
├── esp32-s3_datasheet_cn.pdf
└── README.md
```

## 测试固件

已保存的测试发布包位于 [`os/dist/`](os/dist/)，包含烧录镜像、分区表、校验和及烧录说明。请阅读对应版本的 `README.txt`，并确认目标硬件及安全配置后再使用。

## 许可证

目前仓库未声明开源许可证。若要复用或分发本项目，请先联系项目维护者确认授权。
