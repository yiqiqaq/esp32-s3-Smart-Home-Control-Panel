#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Seventh pass: GPA/RLY nets FIRST on an empty (power-free) midsection."""
import json, time

import router2 as R
import sixth_pass as S  # relaxed working_maps patch


def main():
    bd = R.Board()
    rt = json.load(open('routes9.json'))
    routes = [dict(net=r['net'], layer=r['layer'], w=r['w'], pts=[tuple(p) for p in r['pts']])
              for r in rt['routes'] if r['net'] not in ('+5V', '+3V3')]
    vias = [dict(net=v['net'], x=v['x'], y=v['y']) for v in rt['vias'] if v['net'] not in ('+5V', '+3V3')]
    # also drop partial signal routes that failed before (keep DI1 which succeeded)
    bd.routes = routes
    bd.vias = vias
    print(f'state: routes={len(bd.routes)} vias={len(bd.vias)}', flush=True)
    nets = {}
    for p in bd.pads:
        if p.net: nets.setdefault(p.net, []).append(p)
    order = ['RLY1', 'RLY2', 'RLY3', 'GPA7', 'DI3', 'DI_COM', 'DIN3', 'SDA', 'SCL']
    for net in order:
        t1 = time.time()
        ok = R.route_net(bd, net, nets[net])
        nseg = sum(1 for r in bd.routes if r['net'] == net)
        nv = sum(1 for v in bd.vias if v['net'] == net)
        print(f'{net:8s} ok={ok} segs={nseg} vias={nv} {time.time()-t1:.1f}s', flush=True)
    out = dict(routes=[dict(net=r['net'], layer=r['layer'], w=r['w'], pts=r['pts']) for r in bd.routes],
               vias=[dict(net=v['net'], x=v['x'], y=v['y']) for v in bd.vias])
    json.dump(out, open('routes10.json', 'w'))
    print('written routes10.json:', len(out['routes']), 'routes,', len(out['vias']), 'vias')


if __name__ == '__main__':
    main()
