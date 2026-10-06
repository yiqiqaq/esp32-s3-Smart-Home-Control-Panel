#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Second routing pass for failed nets, against the committed state."""
import json, math, time

import router2 as R


def main():
    bd = R.Board()
    rt = json.load(open('routes2.json'))
    bd.routes = [dict(net=r['net'], layer=r['layer'], w=r['w'], pts=[tuple(p) for p in r['pts']])
                 for r in rt['routes']]
    bd.vias = [dict(net=v['net'], x=v['x'], y=v['y']) for v in rt['vias']]
    print(f'start: routes={len(bd.routes)} vias={len(bd.vias)}', flush=True)
    nets = {}
    for p in bd.pads:
        if p.net: nets.setdefault(p.net, []).append(p)
    order = ['GND', '$1N9', 'SDA', 'SCL', 'DI_COM', 'DI2', 'DI3', 'DIN1', 'DIN2', 'DIN3',
             'RLY1', 'RLY2', 'RLY3', 'GPA3', 'GPA4', 'GPA5', 'GPA6', 'GPA7']
    new_routes = []
    new_vias = []
    for net in order:
        if net not in nets: continue
        t1 = time.time()
        before = (len(bd.routes), len(bd.vias))
        ok = R.route_net(bd, net, nets[net])
        after = (len(bd.routes), len(bd.vias))
        seg = after[0] - before[0]
        via = after[1] - before[1]
        print(f'{net:8s} ok={ok} newseg={seg} newvia={via} {time.time()-t1:.1f}s', flush=True)
    out = dict(routes=[dict(net=r['net'], layer=r['layer'], w=r['w'], pts=r['pts']) for r in bd.routes],
               vias=[dict(net=v['net'], x=v['x'], y=v['y']) for v in bd.vias])
    json.dump(out, open('routes3.json', 'w'))
    print('written routes3.json:', len(out['routes']), 'routes,', len(out['vias']), 'vias')


if __name__ == '__main__':
    main()
