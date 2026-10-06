#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Fifth pass: power out, signals in (DI2/DI3 first), power back last."""
import json, time

import router2 as R


def main():
    bd = R.Board()
    rt = json.load(open('routes2.json'))
    routes = [dict(net=r['net'], layer=r['layer'], w=r['w'], pts=[tuple(p) for p in r['pts']])
              for r in rt['routes'] if r['net'] not in ('+5V', '+3V3')]
    vias = [dict(net=v['net'], x=v['x'], y=v['y']) for v in rt['vias'] if v['net'] not in ('+5V', '+3V3')]
    bd.routes = routes
    bd.vias = vias
    print(f'state: routes={len(bd.routes)} vias={len(bd.vias)}', flush=True)
    nets = {}
    for p in bd.pads:
        if p.net: nets.setdefault(p.net, []).append(p)
    order = ['DI2', 'DI3', 'DI_COM', 'DIN3', 'RLY1', 'RLY2', 'RLY3',
             'GPA3', 'GPA4', 'GPA5', 'GPA6', 'GPA7', 'SDA', 'SCL', '$1N9',
             '+5V', '+3V3']
    for net in order:
        t1 = time.time()
        ok = R.route_net(bd, net, nets[net])
        print(f'{net:8s} ok={ok} segs={sum(1 for r in bd.routes if r["net"] == net)} vias={sum(1 for v in bd.vias if v["net"] == net)} {time.time()-t1:.1f}s', flush=True)
    # hand GND stubs for USB-C shell pads (SMD, hemmed in by VBUS/slots)
    for (x0, y0) in ((126.0, -1774.6), (-126.0, -1774.6)):
        bd.commit_route('GND', 0, 16, [(x0, y0), (x0, y0 - 40), (x0, -1845)])
        bd.commit_via('GND', x0, -1845)
        print(f'GND shell stub at {x0}: added', flush=True)
    out = dict(routes=[dict(net=r['net'], layer=r['layer'], w=r['w'], pts=r['pts']) for r in bd.routes],
               vias=[dict(net=v['net'], x=v['x'], y=v['y']) for v in bd.vias])
    json.dump(out, open('routes6.json', 'w'))
    print('written routes6.json:', len(out['routes']), 'routes,', len(out['vias']), 'vias')


if __name__ == '__main__':
    main()
