#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Sixth pass: retry remaining nets with relaxed safety (2mil) and pour margin (+3)."""
import json, time

import router2 as R

# monkey-patch relaxed margins for this pass
_orig_safety = R.safety_of
R.safety_of = lambda net: 2
_orig_design = R.design_clr
R.design_clr = lambda c1, c2: _orig_design(c1, c2)
_pour_extra = {'val': 3}
_wm = R.Board.working_maps


def working_maps(self, net):
    c = R.cls_of(net)
    h = R.width_of(net) / 2
    sf = R.safety_of(net)
    rs = R.design_clr(c, 1) + h + sf
    rm = R.design_clr(c, 0) + h + sf
    rv_s = R.VIA_R + R.design_clr(c, 1) + sf
    rv_m = R.VIA_R + R.design_clr(c, 0) + sf
    selv = [R.np.zeros((R.N, R.N), R.np.uint8), R.np.zeros((R.N, R.N), R.np.uint8)]
    mains = [R.np.zeros((R.N, R.N), R.np.uint8), R.np.zeros((R.N, R.N), R.np.uint8)]
    vselv = [R.np.zeros((R.N, R.N), R.np.uint8), R.np.zeros((R.N, R.N), R.np.uint8)]
    vmains = [R.np.zeros((R.N, R.N), R.np.uint8), R.np.zeros((R.N, R.N), R.np.uint8)]
    for p in self.pads:
        if p.net == net: continue
        sa, va, r, rv = (selv, vselv, rs, rv_s) if p.cls == 1 else (mains, vmains, rm, rv_m)
        for l in p.layers():
            self._stamp_pad_r(p, sa[l], l, r)
            self._stamp_pad_r(p, va[l], l, rv)
    for rt in self.routes:
        if rt['net'] == net: continue
        cc = R.cls_of(rt['net'])
        hh = rt['w'] / 2
        sa, va, r, rv = (selv, vselv, rs, rv_s) if cc == 1 else (mains, vmains, rm, rv_m)
        for (ax, ay), (bx, by) in zip(rt['pts'], rt['pts'][1:]):
            self.stamp_capsule(sa[rt['layer']], ax, ay, bx, by, hh + r)
            self.stamp_capsule(va[rt['layer']], ax, ay, bx, by, hh + rv)
    for v in self.vias:
        if v['net'] == net: continue
        cc = R.cls_of(v['net'])
        sa, va, r, rv = (selv, vselv, rs, rv_s) if cc == 1 else (mains, vmains, rm, rv_m)
        for l in (0, 1):
            self.stamp_disc(sa[l], v['x'], v['y'], R.VIA_R + r)
            self.stamp_disc(va[l], v['x'], v['y'], R.VIA_R + rv)
    base, vbase = [], []
    for l in (0, 1):
        bb = selv[l] | mains[l] | self.margin
        if c == 0: bb = bb | self.margin_main
        if net != 'GND' and l == 1:
            bb = bb | self.pour_map(rs + _pour_extra['val'])
        base.append(bb)
        vb = vselv[l] | vmains[l] | self.margin
        if net != 'GND':
            vb = vb | self.pour_map(rv_s + _pour_extra['val'])
        vbase.append(vb)
    for p in self.pads:
        if p.net != net: continue
        for l in p.layers():
            c0, r0, c1, r1 = self._bbox_cells((p.x - 10, p.y - 10, p.x + 10, p.y + 10))
            if c1 < c0 or r1 < r0: continue
            X = R.np.arange(c0, c1 + 1)[None, :] * R.GS - R.HALF
            Y = R.np.arange(r0, r1 + 1)[:, None] * R.GS - R.HALF
            disc = ((X - p.x) ** 2 + (Y - p.y) ** 2) <= 64
            base[l][r0:r1 + 1, c0:c1 + 1][disc] = 0
            vbase[l][r0:r1 + 1, c0:c1 + 1][disc] = 0
    blocked_b = [base[l].T.tobytes() for l in (0, 1)]
    via_b = [vbase[l].T.tobytes() for l in (0, 1)]
    return blocked_b, via_b


R.Board.working_maps = working_maps


def main():
    bd = R.Board()
    rt = json.load(open('routes6.json'))
    bd.routes = [dict(net=r['net'], layer=r['layer'], w=r['w'], pts=[tuple(p) for p in r['pts']])
                 for r in rt['routes']]
    bd.vias = [dict(net=v['net'], x=v['x'], y=v['y']) for v in rt['vias']]
    print(f'state: routes={len(bd.routes)} vias={len(bd.vias)}', flush=True)
    nets = {}
    for p in bd.pads:
        if p.net: nets.setdefault(p.net, []).append(p)
    order = ['GPA3', 'GPA4', 'GPA5', 'GPA6', 'GPA7',
             'RLY1', 'RLY2', 'RLY3', 'DI3', 'DIN3', 'SDA', 'SCL', '$1N9']
    for net in order:
        t1 = time.time()
        ok = R.route_net(bd, net, nets[net])
        nseg = sum(1 for r in bd.routes if r['net'] == net)
        nv = sum(1 for v in bd.vias if v['net'] == net)
        print(f'{net:8s} ok={ok} segs={nseg} vias={nv} {time.time()-t1:.1f}s', flush=True)
    out = dict(routes=[dict(net=r['net'], layer=r['layer'], w=r['w'], pts=r['pts']) for r in bd.routes],
               vias=[dict(net=v['net'], x=v['x'], y=v['y']) for v in bd.vias])
    json.dump(out, open('routes7.json', 'w'))
    print('written routes7.json:', len(out['routes']), 'routes,', len(out['vias']), 'vias')


if __name__ == '__main__':
    main()
