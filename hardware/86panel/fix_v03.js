// ESP32-S3 三路智能面板 86盒载板 — v0.3 修复脚本
// 用法（在嘉立创EDA中打开本工程后执行其一）：
//   1) LCEDA-Pro invoke --session <会话> --ext-uuid eda --code "$(cat fix_v03.js)"
//   2) 或在 AI 会话中让 AI 执行（编辑器空闲时）
// 内容：A. SWB 网络命名  B. J2/J5 重定义（MCP23017 架构）  C. MCP 改线
//       D. PCB U1/J1/U10 板框间距内移 + 5V 区重排  E. PCB 焊盘网络同步
// 执行前：点击原理图画布使其获得焦点；执行后：保存 → 导入更新到 PCB → 复核差异。

const out = { sch: {}, pcb: {} };

// ============ A/B/C. 原理图 ============
try {
  // --- A. SWB 命名（基极结点）---
  for (const [x1, y1, x2, y2, net] of [
    [320, 425, 298, 425, 'SWB1'],
    [540, 425, 518, 425, 'SWB2'],
    [760, 425, 738, 425, 'SWB3'],
  ]) { try { await eda.sch_PrimitiveWire.create([x1, y1, x2, y2], net); out.sch['swb_' + net] = 1; } catch (e) { out.sch['swb_' + net] = String(e).slice(0, 40); } }

  // --- B1. J2 旧桩线删除（RLY1/2/3、SDA、SCL、INT）---
  const schWires = await eda.sch_PrimitiveWire.getAll();
  const j2del = [[138,505,160,505],[138,495,160,495],[138,485,160,485],[138,475,160,475],[138,465,160,465],[138,455,160,455]];
  for (const w of schWires) {
    const l = JSON.stringify(w.line || []);
    for (const d of j2del) {
      const dd = JSON.stringify(d);
      if (l === dd || l === JSON.stringify([d[2], d[3], d[0], d[1]])) { try { await eda.sch_PrimitiveWire.delete(w.primitiveId); out.sch['j2del_' + d[1]] = 1; } catch (e) { out.fail = out.fail || []; } break; }
    }
  }
  // --- B2. J2 新定义：3=SDA 4=SCL 5-8=GND ---
  const j2pins = { 3: [160, 505], 4: [160, 495], 5: [160, 485], 6: [160, 475], 7: [160, 465], 8: [160, 455] };
  await eda.sch_PrimitiveWire.create([160, 505, 138, 505], 'SDA');
  await eda.sch_PrimitiveWire.create([160, 495, 138, 495], 'SCL');
  for (const n of [5, 6, 7, 8]) { try { await eda.sch_PrimitiveComponent.createNetFlag('Ground', 'GND', j2pins[n][0], j2pins[n][1], 0, false); out.sch['j2gnd_' + n] = 1; } catch (e) { out.sch['j2gnd_' + n] = String(e).slice(0, 40); } }

  // --- B3. J5 旧 DI 桩线删除（3/4/5）---
  const j5del = [[1100,280,1120,280],[1100,270,1120,270],[1100,260,1120,260]];
  for (const w of schWires) {
    const l = JSON.stringify(w.line || []);
    for (const d of j5del) {
      if (l === JSON.stringify(d) || l === JSON.stringify([d[2], d[3], d[0], d[1]])) { try { await eda.sch_PrimitiveWire.delete(w.primitiveId); out.sch['j5del_' + d[1]] = 1; } catch (e) {} break; }
    }
  }

  // --- C1. U9 GPA0-2 桩线改名为 RLY1-3（删除重建）---
  const gpaDel = [[920,335,942,335],[920,325,942,325],[920,315,942,315]];
  for (const w of schWires) {
    const l = JSON.stringify(w.line || []);
    for (const d of gpaDel) { if (l === JSON.stringify(d) || l === JSON.stringify([d[2], d[3], d[0], d[1]])) { try { await eda.sch_PrimitiveWire.delete(w.primitiveId); } catch (e) {} break; } }
  }
  await eda.sch_PrimitiveWire.create([920, 335, 942, 335], 'RLY1');
  await eda.sch_PrimitiveWire.create([920, 325, 942, 325], 'RLY2');
  await eda.sch_PrimitiveWire.create([920, 315, 942, 315], 'RLY3');
  // J6.1-3 同步改名
  const j6Del = [[1000,300,1020,300],[1000,290,1020,290],[1000,280,1020,280]];
  for (const w of schWires) {
    const l = JSON.stringify(w.line || []);
    for (const d of j6Del) { if (l === JSON.stringify(d) || l === JSON.stringify([d[2], d[3], d[0], d[1]])) { try { await eda.sch_PrimitiveWire.delete(w.primitiveId); } catch (e) {} break; } }
  }
  await eda.sch_PrimitiveWire.create([1020, 300, 1000, 300], 'RLY1');
  await eda.sch_PrimitiveWire.create([1020, 290, 1000, 290], 'RLY2');
  await eda.sch_PrimitiveWire.create([1020, 280, 1000, 280], 'RLY3');

  // --- C2. U9 GPB0-2 接 DI1-3 ---
  await eda.sch_PrimitiveWire.create([820, 335, 798, 335], 'DI1');
  await eda.sch_PrimitiveWire.create([820, 325, 798, 325], 'DI2');
  await eda.sch_PrimitiveWire.create([820, 315, 798, 315], 'DI3');

  // --- C3. U9.18 INT 桩线删除（改轮询，INT 悬空）---
  for (const w of schWires) {
    const l = JSON.stringify(w.line || []);
    if (l === JSON.stringify([920,235,942,235]) || l === JSON.stringify([942,235,920,235])) { try { await eda.sch_PrimitiveWire.delete(w.primitiveId); out.sch['intDel'] = 1; } catch (e) {} break; }
  }
} catch (e) { out.sch.error = String(e).slice(0, 80); }

// ============ D/E. PCB ============
try {
  await eda.dmt_EditorControl.openDocument(pcbUuid = '409bac3a59142ccd');
  await new Promise(r => setTimeout(r, 4000));
  const comps = await eda.pcb_PrimitiveComponent.getAll();
  const m = {}; for (const c of comps) m[c.designator] = c.primitiveId;

  // --- D1. U1 内移 250mil（板框间距 3mil → 253mil）---
  for (const [des, x, y] of [['U1', -400, 1000], ['J1', -1330, 1000], ['U10', 0, -1440],
    ['C1', 700, 1050], ['C2', 860, 1050], ['U2', 650, 1350], ['C3', 830, 1350], ['C4', 830, 1150],
    ['R7', 960, 1350], ['LED1', 960, 1150], ['Q4', 700, 450]]) {
    try { const p = await eda.pcb_PrimitiveComponent.get(m[des]); const ap = p.toAsync(); ap.setState_X(x); ap.setState_Y(y); ap.done(); out.pcb['mv_' + des] = 1; } catch (e) { out.pcb['mv_' + des] = String(e).slice(0, 40); }
  }

  // --- E. 焊盘网络同步 ---
  const netMap = {
    U1: { '1': 'AC_L_M', '2': 'AC_N_M' },
    J2: { '3': 'SDA', '4': 'SCL', '5': 'GND', '6': 'GND', '7': 'GND', '8': 'GND' },
    U9: { '1': 'DI1', '2': 'DI2', '3': 'DI3', '28': 'RLY1', '27': 'RLY2', '26': 'RLY3', '18': '' },
    J6: { '1': 'RLY1', '2': 'RLY2', '3': 'RLY3' },
    J5: { '3': '', '4': '', '5': '' },
  };
  for (const [des, map] of Object.entries(netMap)) {
    const id = m[des]; if (!id) { out.pcb['net_' + des] = 'NOID'; continue; }
    const pads = await eda.pcb_PrimitiveComponent.getAllPinsByPrimitiveId(id) || [];
    for (const p of pads) {
      const num = String(p.padNumber ?? p.pinNumber);
      const net = map[num];
      if (net === undefined) continue;
      try { await eda.pcb_PrimitivePad.modify(p.primitiveId, { net: net || null }); out.pcb['net_' + des + '-' + num] = net || 'cleared'; } catch (e) { out.pcb['net_' + des + '-' + num] = String(e).slice(0, 30); }
    }
  }
  await eda.pcb_Document.save();
} catch (e) { out.pcb.error = String(e).slice(0, 80); }

return out;
