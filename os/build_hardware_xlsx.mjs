import fs from 'node:fs/promises';
import { Workbook, SpreadsheetFile } from '@oai/artifact-tool';

const outDir = new URL('./outputs/esp32s3-hardware/', import.meta.url).pathname;
await fs.mkdir(outDir, { recursive: true });
const wb = Workbook.create();
const summary = wb.worksheets.add('方案总览');
const bom = wb.worksheets.add('硬件BOM');
const wiring = wb.worksheets.add('接线关系');
const adapt = wb.worksheets.add('接口与待确认');

const navy = '#17365D', blue = '#D9EAF7', pale = '#F3F6F9', amber = '#FFF2CC', red = '#FCE4D6';
function setup(sheet, widths) {
  sheet.showGridlines = false;
  widths.forEach((w, i) => sheet.getRange(`${String.fromCharCode(65+i)}:${String.fromCharCode(65+i)}`).format.columnWidth = w);
}
function title(sheet, range, text) {
  const r = sheet.getRange(range); sheet.getRange(range.split(':')[0]).values = [[text]];
  r.format = { fill: navy, font: { name: 'Arial', size: 15, bold: true, color: '#FFFFFF' }, verticalAlignment: 'center' };
  r.format.rowHeight = 30;
}
function header(sheet, range) {
  sheet.getRange(range).format = { fill: '#1F4E78', font: { name: 'Arial', size: 10, bold: true, color: '#FFFFFF' }, wrapText: true, horizontalAlignment: 'center', verticalAlignment: 'center', rowHeight: 30 };
}
function body(sheet, range) {
  sheet.getRange(range).format.wrapText = true;
  sheet.getRange(range).format.verticalAlignment = 'center';
  sheet.getRange(range).format.borders = { preset: 'outside', style: 'thin', color: '#D7E0E8' };
}

// Overview
setup(summary, [22, 38, 68, 65]);
title(summary, 'A1:D1', 'ESP32-S3 智能面板｜硬件方案与连接总览');
summary.getRange('A3:D3').values = [['推荐方案', 'Waveshare ESP32-S3-Touch-LCD-4.3B（SKU 27848）', '一体板：ESP32-S3 + 4.3 英寸 RGB 电容触摸屏，800×480（5:3）。替代原 DSI 屏，并取代当前独立 DevKitC 主控板。', '官方资料：https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B']];
summary.getRange('A4:D4').values = [['显示适配', 'RGB LCD + 触摸 I²C', '屏幕及触摸已集成在一体板上，界面按 800×480 原生比例重排；不再采购外置 DSI 屏/转接板。现有 HTML 原型还需后续移植到 ESP-IDF LCD/LVGL 驱动。', 'ESP-IDF RGB LCD：https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html']];
summary.getRange('A5:D5').values = [['Matter / 无线', 'ESP32-S3 Wi-Fi 2.4GHz + BLE', 'Wi-Fi 用于网络服务和 Matter over Wi-Fi；BLE 用于配网。项目固件当前为 Matter 设备端。全屋其他 Matter 灯具聚合还需 Matter 控制器/家庭中枢。', '项目固件：os/main/matter_nodes.cpp']];
summary.getRange('A6:D6').values = [['三路实体开关', '3 个常开瞬时按钮 / 干接点', '推荐接到隔离数字输入模块。面板板载仅 2 路隔离数字输入，第三路要扩展。固件现有 GPIO4/5/6 默认值不适用于新板，需改为板载 IO 或确认后的空闲引脚。', '项目配置：os/main/Kconfig.projbuild；官方硬件接口见产品手册']];
summary.getRange('A7:D7').values = [['三路灯控', '3 路隔离继电器接口', '板载有 2 路隔离开漏输出，第三路需扩展。继电器触点按灯具电压、电流及 LED 浪涌选型；高压接线、熔断与外壳由合格电工实施。', '板载 DO 为开漏输出，额定能力需以官方规格书和具体批次为准']];
summary.getRange('A8:D8').values = [['供电', '稳定 5V DC 电源 + USB-C 数据线', '官方标注板卡典型 5V/450mA。继电器线圈若非板载供电需单独匹配电源；最终适配器电流由屏幕亮度和继电器型号核算。', '官方资料同上']];
summary.getRange('A10:D10').values = [['实施状态', '可以先采购主板和低压配件', '购买继电器/扩展板前，必须确认所控灯具额定电压/电流、开关接法、继电器触点规格和安装盒尺寸。未确认前不把任何 GPIO 当作最终接线定义。', 'GPIO 默认配置当前只适用于通用开发板设想']];
summary.getRange('A3:D10').format.wrapText = true;
summary.getRange('A3:A8').format.font = { bold: true, color: navy };
summary.getRange('A10:D10').format.fill = amber;
summary.getRange('A3:D10').format.rowHeight = 62;
summary.getRange('A10:D10').format.rowHeight = 54;
summary.getRange('A3:D8').format.borders = { preset: 'outside', style: 'thin', color: '#D7E0E8' };

// BOM
setup(bom, [8, 17, 43, 12, 17, 54, 54, 72]);
title(bom, 'A1:H1', '物料清单（BOM）');
bom.getRange('A3:H3').values = [['编号','类别','硬件 / 建议规格','数量','需求级别','功能','连接 / 安装关系','型号依据与来源']]; header(bom, 'A3:H3');
const bomRows = [
['H01','主控与显示','Waveshare ESP32-S3-Touch-LCD-4.3B，SKU 27848；4.3″ RGB 800×480，S3-WROOM-1-N16R8（16MB Flash/8MB PSRAM）',1,'推荐必需','运行 ESP-IDF/Matter、按 800×480 原生比例显示并接收触摸输入。','USB-C/5V 供电；板载屏、触摸与主控内部连接。此方案替代独立 DevKitC 和旧 DSI 屏。','https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B'],
['H02','电源','稳压 5V DC 电源，建议预留屏幕和外设余量；最终电流按实测与继电器线圈计算',1,'必需','给一体板供电。板卡官方典型值 5V/450mA，外接继电器负载另行核算。','接一体板 USB-C 或 DC 输入（二选一，按板卡说明）；不要将市电接入低压端。','板卡电源规格：https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B'],
['H03','数据线','USB-C 数据线（支持数据与供电）',1,'必需','烧录、串口调试和初期供电。','开发阶段连接主板 USB-C 与电脑。','板卡接口见官方文档：https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B'],
['H04','实体输入','常开瞬时按钮/墙壁干接点开关，额定按低压控制回路选择',3,'必需','提供三路本地物理按键输入，对应三路灯/自定义 Matter 开关逻辑。','每个开关接一路隔离 DI 的输入端；按键走低压控制线，不直接连 ESP32 裸 GPIO。','项目现有按钮扫描是 GPIO 到 GND、内部上拉；迁移到隔离 DI 需同步适配驱动。'],
['H05','输入隔离','4 通道光耦隔离数字输入模块，支持干接点，输出逻辑兼容 3.3V',1,'必需（建议）','为三路实体开关提供隔离，保留一路扩展。','3 个开关分别接 DI1–DI3；模块逻辑侧接 I/O 扩展或经核实的板载输入通道。具体电平/共地依模块手册。','板载仅 2 路隔离 DI；第三路需要扩展。购买时确认输出电气规格与接线图。'],
['H06','继电器输出','3 通道隔离继电器模块；输入兼容逻辑侧；触点按灯具电压、电流和 LED 浪涌选型',1,'必需（真实控灯）','以继电器触点实现三路灯具的物理开/关。','三路逻辑控制分别到 IN1–IN3；继电器触点侧接入灯具开关回路。线圈供电与控制电源按模块手册。','板载 2 路隔离开漏 DO 不能直接驱动灯具/市电；确认触点额定值和安全认证。'],
['H07','I/O 扩展','3.3V 兼容 I²C GPIO 扩展模块（建议 MCP23017 类，需确认与板载 I²C/驱动兼容）',1,'必需（扩展方案）','补足剩余输入/输出控制逻辑，避免占用屏幕 RGB、触摸或启动配置引脚。','连接板卡可用 I²C 总线 SDA/SCL、3.3V、GND；地址脚按模块设定。GPIO 侧驱动 H05/H06 低压逻辑端。','正式采购前核实 4.3B 可用 I²C 引脚、地址冲突、上拉和固件驱动；不是隔离器或功率驱动器。'],
['H08','低压互连','带护套端子、低压导线、连接器/线束及固定件',1,'必需','可靠连接按钮、I/O 扩展、继电器控制侧和直流电源。','低压控制线与市电灯线分槽/分束，并按设备端子规格选线径和端子。','端子间距、电流和线径按具体模块/机壳确定。'],
['H09','安全安装','绝缘阻燃外壳/面板底盒、隔板、应力释放件；必要的支路保护器件',1,'真实控灯必需','固定面板和继电器，降低触电、短路、拉脱和过热风险。','高低压分区、端子防触碰；保护器件由电工按当地规范和回路设计确定。','不可按裸板/杜邦线长期控制市电灯具。'],
['H10','Matter 网络','2.4GHz Wi-Fi 路由器/接入点',1,'已有则无需另购','让面板通过 Wi-Fi 入网，承载 Matter over Wi-Fi 和天气服务访问。','接入家庭局域网；配网使用 BLE。','设备支持 Wi-Fi 2.4GHz 与 BLE：https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B'],
['H11','Matter 控制器','兼容 Matter 的家庭中枢/控制器（如现有平台支持的 Matter Controller）',1,'条件必需','管理其他 Matter 灯具/设备并汇总全屋照明状态；作为面板服务端的数据来源。','与面板加入同一 Matter 家庭/控制域；需确认当前固件是否实现 Matter Controller/订阅或由 Home Assistant 等上层服务提供聚合。','当前项目代码定义的主要是设备端节点；不能仅凭 Matter 设备端自动读取全屋其他节点。'],
['H12','网络服务','天气 API 服务账号/接口凭据（可用服务按地区选择）',1,'功能必需（天气）','提供最近小时天气预报、温度和天气状态。','通过 Wi-Fi HTTPS 请求；凭据保存方式与服务条款/配额需在软件阶段确定。','属于软件服务，不是板上硬件；网络服务由项目 weather_service 管理。'],
['H13','调试工具','电脑 + USB 口；必要时 USB-UART/逻辑分析工具',1,'开发必需','烧录、日志、检查 I²C/扩展模块。','USB-C 数据线直连优先；只有主板调试接口需要时才加 USB-UART。','不需要常驻安装在成品内。'],
['A01','备选屏幕','Waveshare 4.3inch Capacitive Touch LCD，SKU 16249，800×480 RGB、40-pin FFC、I²C 触摸',1,'备选（二选一）','在保留另一块 ESP32-S3 主控板时使用的独立屏幕。','需要自行制作/购买兼容 FFC 转接和信号线，RGB 24-bit/并口时序与触摸接线按两板原理图核对。','仅适用于实际主控为 ESP32-S3 且有足够可用 RGB 引脚；不与 H01 同时采购。https://www.waveshare.com/4.3inch-Capacitive-Touch-LCD.htm'],
];
bom.getRange(`A4:H${3+bomRows.length}`).values = bomRows;
body(bom, `A4:H${3+bomRows.length}`);
bom.getRange(`A4:H${3+bomRows.length}`).format.rowHeight = 58;
bom.getRange(`A4:H${3+bomRows.length}`).format.verticalAlignment = 'top';
bom.getRange(`E4:E${3+bomRows.length}`).format.fill = '#EAF2F8';
bom.getRange(`A${bomRows.length+3}:H${bomRows.length+3}`).format.fill = pale;
bom.freezePanes.freezeRows(3);

// Wiring
setup(wiring, [10, 33, 36, 46, 50, 68]);
title(wiring, 'A1:F1', '连接关系表（低压逻辑与灯具回路分开说明）');
wiring.getRange('A3:F3').values = [['序号','起点 / 设备','终点 / 设备','信号/端子关系','作用与注意事项','状态/依据']]; header(wiring, 'A3:F3');
const wireRows = [
['W01','5V 稳压电源','4.3B 一体板 USB-C 或 DC 输入','按板卡指定输入端接入 5V；选一种供电路径','板卡典型功耗约 450mA；屏幕背光和继电器扩展应留有余量。禁止把外部电压接到 3.3V GPIO。','需按实际电源和附件负载复核；https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B'],
['W02','电脑 USB','4.3B USB-C 数据口','USB 数据/供电连接','烧录和日志调试。最终供电可改为合规 5V 电源。','开发连接'],
['W03','板载 RGB 屏与电容触摸','板载 ESP32-S3 / 板载触摸接口','一体板内部连线，无需外接 FFC','800×480 RGB LCD；触摸经板载 I²C 接口。需在固件集成 esp_lcd RGB 与触摸驱动/LVGL。','硬件集成；软件适配尚需完成'],
['W04','按钮 1–3（干接点）','4 路光耦隔离 DI 模块 DI1–DI3','每路按钮闭合时改变对应 DI 状态；接线依输入模块 dry-contact 图示','按钮只接隔离模块的现场输入端。不可把未知墙盒线直接接 GPIO。','低压输入设计；现场确认开关类型'],
['W05','4 路隔离 DI 模块逻辑输出','4.3B 可用隔离 DI / GPIO 扩展输入','模块输出电平、上拉及极性按数据手册匹配 3.3V I/O；三路分别映射 Matter 通道','官方板载只有 2 DI；第三路要外扩。若 DI 模块本身需 5/12/24V 输入电源，单独按规格供电。','精确引脚待选型；不可假设 GPIO4/5/6 可用'],
['W06','3.3V I²C GPIO 扩展模块','4.3B 板卡空闲 I²C SDA/SCL','SDA↔SDA、SCL↔SCL；3.3V 和 GND；地址脚依模块设定','扩展模块只增加逻辑 GPIO，不具备隔离或继电器触点能力。核实板载 I²C 引脚/总线地址并适配驱动。','MCP23017 类方案，待按板卡针脚表确认'],
['W07','GPIO 扩展逻辑输出 / 板载 DO0–DO1','隔离继电器模块 IN1–IN3','每路控制一只继电器；板载 DO 两路，第三路由扩展输出提供','板载 DO 为开漏类型，最大下拉能力及电平依官方资料；不得直接带灯具负载。逻辑侧供电按继电器模块规格。','三路输出；具体模块输入兼容性待确认'],
['W08','继电器触点 COM/NO/NC','灯具被控回路','每路使用一组触点按电工设计串接被控导线；NO/NC 依据默认安全状态选择','市电灯线安装有触电/火灾危险；由合格电工依据额定电压、电流、LED 浪涌、断路器及当地规范完成。不可照本行文字自行带电接线。','概念连接，不是施工接线图'],
['W09','路由器 2.4GHz Wi-Fi','4.3B ESP32-S3','无线配网；BLE 用于初始配网','Matter 网络通信、天气 API 访问。','官方无线能力：https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B'],
['W10','家庭 Matter 控制器/家庭中枢','面板设备端及全屋 Matter 灯具','局域网 Matter 控制/状态同步，具体 Fabric、角色、订阅架构需软件确认','要让“家中照明”聚合全家设备状态，单有这块 Matter 设备端不足；需控制器服务或实现 Matter Controller。','项目当前节点角色参考 os/main/matter_nodes.cpp'],
];
wiring.getRange(`A4:F${3+wireRows.length}`).values = wireRows;
body(wiring, `A4:F${3+wireRows.length}`);
wiring.getRange(`A4:F${3+wireRows.length}`).format.rowHeight = 58;
wiring.getRange(`A4:F${3+wireRows.length}`).format.verticalAlignment = 'top';
wiring.getRange('A11:F11').format.fill = red;
wiring.freezePanes.freezeRows(3);

// Adaptation and open questions
setup(adapt, [22, 42, 72, 68]);
title(adapt, 'A1:D1', '接口适配、限制与采购前确认');
adapt.getRange('A3:D3').values = [['检查项','当前已知','对硬件/固件的影响','下一步确认']]; header(adapt, 'A3:D3');
const adaptRows = [
['现有目标芯片','工程 target 为 ESP32-S3；ESP-IDF / ESP-Matter 工程。','一体板仍是 ESP32-S3，但板级引脚映射、LCD、触摸、扩展 IO 都要重新适配。','拿到具体 SKU 实物/原理图后，建立 board pin map。'],
['现有实体输入默认值','os/sdkconfig 里 GPIO4、GPIO5、GPIO6；按钮逻辑为 GPIO 输入+内部上拉，闭合接 GND。','不能照搬到 4.3B：板载显示/触摸使用了部分 GPIO；例如官方板级接口将 GPIO4 用于触摸 IRQ、GPIO5 用于 LCD DE。','改为板载隔离 DI/外置隔离 DI；修改 Kconfig 与 switch_inputs 驱动。'],
['现有真实继电器输出','GPIO relay defaults 为 -1，即当前三路真实继电器未启用。','必须确定隔离继电器模块、驱动接口、触点能力与供电后，才可启用真实灯控。','提供灯具电压/电流、LED 灯数量/浪涌、墙盒线路和回路保护信息。'],
['板载隔离 IO 数量','4.3B 板载 2 路隔离数字输入、2 路隔离开漏数字输出；扩展芯片资源见官方接口表。','三路按钮 + 三路灯控至少各需一条扩展逻辑通道；外扩 I²C GPIO 仍需隔离/驱动器件。','选一套明确的 4DI/4DO 隔离接口模块并核对逻辑电平和接口。'],
['GPIO/电气约束','RGB 屏占用大量并行 GPIO；触摸走 I²C；板载 IO 经 IO 扩展器。','不能按通用 ESP32-S3 DevKit pinout 推断空闲脚；避免启动绑带脚、USB/JTAG 和板上外设冲突。','以所选板完整原理图与 ESP-IDF 驱动配置为准。'],
['屏幕软件栈','当前 UI 是 HTML 预览；固件里还需要实际 LCD/触摸 GUI 栈。','HTML 文件不会直接在 MCU 上运行；需要将界面重做/移植为 LVGL 或原生绘制。','集成 RGB panel、触摸驱动、LVGL，按 800×480 适配内存与刷新。'],
['Matter 全屋状态','当前代码构建 Matter 设备端节点；不代表已实现其它节点的发现/控制/状态订阅。','“家中照明”真实聚合需要家庭控制器服务或在固件侧实现 Matter Controller。','确定 Home Assistant / Matter Hub 作为状态数据源及 API/订阅方案。'],
['市电施工','继电器触点切换灯具回路属于市电施工。','ESP32 GPIO/板载 450mA 开漏 DO 都不能直接控制交流灯具；必须经过适当额定、隔离的继电器。','由合格电工按当地电气规范选器件、保险保护、线径、隔板和外壳。'],
['备选独立屏','SKU 16249 是独立 800×480 RGB 电容触屏，40-pin FFC，触摸 I²C。','只有在保留已确认具备足够 RGB 引脚的 ESP32-S3 主板时考虑；不能与一体板 H01 同时购买。','先提供现有开发板确切型号/原理图，再核对 FFC 转接、RGB 时序、引脚和信号电平。https://www.waveshare.com/4.3inch-Capacitive-Touch-LCD.htm'],
];
adapt.getRange(`A4:D${3+adaptRows.length}`).values = adaptRows;
body(adapt, `A4:D${3+adaptRows.length}`);
adapt.getRange(`A4:D${3+adaptRows.length}`).format.rowHeight = 64;
adapt.getRange(`A4:D${3+adaptRows.length}`).format.verticalAlignment = 'top';
adapt.getRange('A5:D5').format.fill = amber;
adapt.getRange('A6:D8').format.fill = amber;
adapt.getRange('A11:D11').format.fill = red;
adapt.freezePanes.freezeRows(3);

await wb.recalculate();
for (const sh of [summary,bom,wiring,adapt]) {
  const png = await wb.render({ sheetName: sh.name, autoCrop: 'all', scale: 1, format: 'png' });
  await fs.writeFile(`${outDir}/${sh.name}.png`, new Uint8Array(await png.arrayBuffer()));
}
const xlsx = await SpreadsheetFile.exportXlsx(wb);
await xlsx.save(`${outDir}/ESP32-S3智能面板硬件清单与接线.xlsx`);
console.log((await wb.inspect({kind:'workbook,sheet,table',maxChars:3000,tableMaxRows:5,tableMaxCols:8})).ndjson);
console.log(`Saved ${outDir}/ESP32-S3智能面板硬件清单与接线.xlsx`);
