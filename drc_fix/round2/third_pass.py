#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Third pass: signals first, then re-route +5V/+3V3 around them."""
import json, time

import router2 as R



def main():
    bd = R.Board()
    rt = json.load(open('routes2.json'))
    routes = [dict(net=r['net'], layer=r['layer'], w=r['w'], pts=[tuple(p) for p in r['pts']])
              for r in rt['routes']]
    vias = [dict(net=v['net'], x=v['x'], y=v['y']) for v in rt['vias']]
    # remove power nets from committed state
    routes = [r for r in routes if r['net'] not in ('+5V', '+3V3')]
    vias = [v for v in vias if v['net'] not in ('+5V', '+3V3')]
    bd.routes = routes
    bd.vias = vias
    print(f'state after power removal: routes={len(bd.routes)} vias={len(bd.vias)}', flush=True)
    nets = {}
    for p in bd.pads:
        if p.net: nets.setdefault(p.net, []).append(p)
    signals = ['$1N9', 'SDA', 'SCL', 'DI_COM', 'DI2', 'DI3', 'DIN1', 'DIN2', 'DIN3',
               'RLY1', 'RLY2', 'RLY3', 'GPA3', 'GPA4', 'GPA5', 'GPA6', 'GPA7']
    for net in signals:
        t1 = time.time()
        ok = R.route_net(bd, net, nets[net])
        print(f'{net:8s} ok={ok} {time.time()-t1:.1f}s', flush=True)
    for net in ('+5V', '+3V3'):
        t1 = time.time()
        ok = R.route_net(bd, net, nets[net])
        print(f'{net:8s} ok={ok} {time.time()-t1:.1f}s', flush=True)
    # GND leftover stub for B1A12 (retry once more at the end)
    gnd_failed = [p for p in nets['GND'] if p.num == 'B1A12']
    if gnd_failed:
        ok = R.route_net(bd, 'GND', gnd_failed)
        print('GND B1A12 stub ok=', ok, flush=True)
    out = dict(routes=[dict(net=r['net'], layer=r['layer'], w=r['w'], pts=r['pts']) for r in bd.routes],
               vias=[dict(net=v['net'], x=v['x'], y=v['y']) for v in bd.vias])
    json.dump(out, open('routes4.json', 'w'))
    print('written routes4.json:', len(out['routes']), 'routes,', len(out['vias']), 'vias')


if __name__ == '__main__':
    main()
