#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Fourth pass: keep power as committed; route remaining signals in dependency order."""
import json, time

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
    order = ['DI2', 'DI3', 'RLY1', 'RLY2', 'RLY3', 'GPA3', 'GPA4', 'GPA5', 'GPA6', 'GPA7',
             'DIN3', 'SDA', 'SCL', 'GND']
    for net in order:
        t1 = time.time()
        plist = nets[net] if net != 'GND' else [p for p in nets['GND'] if p.num == 'B1A12']
        ok = R.route_net(bd, net, plist)
        print(f'{net:8s} ok={ok} seg+={sum(1 for r in bd.routes if r["net"] == net) - (0 if net == "GND" else 0)} {time.time()-t1:.1f}s', flush=True)
    out = dict(routes=[dict(net=r['net'], layer=r['layer'], w=r['w'], pts=r['pts']) for r in bd.routes],
               vias=[dict(net=v['net'], x=v['x'], y=v['y']) for v in bd.vias])
    json.dump(out, open('routes5.json', 'w'))
    print('written routes5.json:', len(out['routes']), 'routes,', len(out['vias']), 'vias')


if __name__ == '__main__':
    main()
