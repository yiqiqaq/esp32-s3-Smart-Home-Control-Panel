#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Exact-geometry verification of routes2.json (connectivity + clearances)."""
import json, math, sys

import numpy as np

import router2 as R


def caps_seg_dist(a1, a2, b1, b2):
    """min distance between two segments (2D)."""
    # sample-free: use vector math (closest points of two segments)
    def seg_point(p, q1, q2):
        v = q2 - q1
        L2 = v @ v
        if L2 == 0:
            return np.hypot(*(p - q1))
        t = np.clip((p - q1) @ v / L2, 0, 1)
        return np.hypot(*(p - (q1 + t * v)))
    # check all 4 point-seg + proper intersection via separation axes (approx):
    # use dense sampling for robustness here (verification only)
    d = 1e9
    for (p1, p2, q1, q2) in ((a1, a2, b1, b2), (b1, b2, a1, a2)):
        n = max(2, int(np.hypot(*(p2 - p1)) / 3))
        ts = np.linspace(0, 1, n)[:, None]
        pts = p1[None, :] * (1 - ts) + p2[None, :] * ts
        for p in pts:
            d = min(d, seg_point(p, q1, q2))
    return d


def pad_dist(px, py, pad):
    """distance from point to pad copper shape (exact-ish)."""
    m = pad.dist_mask(np.array([[px]]), np.array([[py]]), 0.0)
    if m[0, 0]:
        return 0.0
    # binary search on r
    lo, hi = 0.0, 400.0
    for _ in range(24):
        mid = (lo + hi) / 2
        if pad.dist_mask(np.array([[px]]), np.array([[py]]), mid)[0, 0]:
            hi = mid
        else:
            lo = mid
    return hi


def main():
    rt = json.load(open('routes2.json'))
    routes = rt['routes']
    vias = rt['vias']
    bd = R.Board()
    pads = bd.pads
    CLR = R.design_clr
    tol = 0.4
    issues = []

    # ---- connectivity ----
    parent = {}

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    def near_node(x, y, layer):
        """register a track endpoint; attach to pad/via if touching."""
        nid = ('pt', round(x, 2), round(y, 2), layer)
        parent.setdefault(nid, nid)
        for p in pads:
            if not p.net: continue
            if p.layer not in (p.layer,):
                pass
            if (p.layer == 12 or (p.layer == 1 and layer == 0) or (p.layer == 2 and layer == 1)) and p.net:
                if abs(p.x - x) < 1e-6 and abs(p.y - y) < 1e-6:
                    pid = ('pad', p.pid)
                    parent.setdefault(pid, pid)
                    union(nid, pid)
        for v in vias:
            if abs(v['x'] - x) < 0.6 and abs(v['y'] - y) < 0.6:
                vid = ('via', v['net'], v['x'], v['y'])
                parent.setdefault(vid, vid)
                union(nid, vid)
        return nid

    # attach pads as nodes
    for p in pads:
        if p.net:
            parent.setdefault(('pad', p.pid), ('pad', p.pid))
    for v in vias:
        parent.setdefault(('via', v['net'], v['x'], v['y']), ('via', v['net'], v['x'], v['y']))

    for rt2 in routes:
        pts = [tuple(q) for q in rt2['pts']]
        for k, (x, y) in enumerate(pts):
            if k in (0, len(pts) - 1):
                nid = near_node(x, y, rt2['layer'])
            # pads passed through mid-path also connect (THT)
        # mid-path pad crossings
        for p in pads:
            if p.net != rt2['net']: continue
            for k in range(len(pts) - 1):
                (ax, ay), (bx, by) = pts[k], pts[k + 1]
                if min(ax, bx) - 60 > p.x or max(ax, bx) + 60 < p.x: continue
                if min(ay, by) - 60 > p.y or max(ay, by) + 60 < p.y: continue
                d = pad_dist(p.x, p.y, p)  # unused
            # endpoint-on-pad check only (conservative)
        n0 = near_node(pts[0][0], pts[0][1], rt2['layer'])
        n1 = near_node(pts[-1][0], pts[-1][1], rt2['layer'])
        union(n0, n1)
    # pour-aware: GND vias inside the same pour region are one group
    import router2 as R2
    pours = bd.pours
    via_pour_group = {}
    for v in vias:
        if v['net'] != 'GND': continue
        for pi, poly in enumerate(pours):
            xs = [q[0] for q in poly]; ys = [q[1] for q in poly]
            if min(xs) <= v['x'] <= max(xs) and min(ys) <= v['y'] <= max(ys):
                inside = False
                n = len(poly)
                for i in range(n):
                    x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
                    if (y1 > v['y']) != (y2 > v['y']):
                        xint = (x2 - x1) * (v['y'] - y1) / (y2 - y1 + 1e-12) + x1
                        if v['x'] < xint: inside = not inside
                if inside:
                    via_pour_group[('via', v['net'], v['x'], v['y'])] = ('pour', pi)
    for a, b in via_pour_group.items():
        parent.setdefault(a, a); parent.setdefault(b, b)
        union(a, b)
    # THT pads inside pour auto-connect to pour
    for p in pads:
        if p.net != 'GND' or p.layer != 12: continue
        for pi, poly in enumerate(pours):
            xs = [q[0] for q in poly]; ys = [q[1] for q in poly]
            if min(xs) <= p.x <= max(xs) and min(ys) <= p.y <= max(ys):
                parent.setdefault(('pour', pi), ('pour', pi))
                parent.setdefault(('pad', p.pid), ('pad', p.pid))
                union(('pad', p.pid), ('pour', pi))
    # via-pad attachment (via inside pad of same net)
    for v in vias:
        vid = ('via', v['net'], v['x'], v['y'])
        for p in pads:
            if p.net != v['net'] or not p.net: continue
            if p.layer != 12: continue
            if pad_dist(v['x'], v['y'], p) <= R.VIA_R + 1.0:
                union(vid, ('pad', p.pid))
    # track passing over same-net pad (endpoint inside pad)
    for rt2 in routes:
        pts = [tuple(q) for q in rt2['pts']]
        for x, y in (pts[0], pts[-1]):
            for p in pads:
                if p.net != rt2['net'] or not p.net: continue
                if p.layer == 12 or (p.layer == 1 and rt2['layer'] == 0) or (p.layer == 2 and rt2['layer'] == 1):
                    if pad_dist(x, y, p) <= 1.0:
                        union(('pt', round(x, 2), round(y, 2), rt2['layer']), ('pad', p.pid))
    nets = {}
    for p in pads:
        if p.net: nets.setdefault(p.net, []).append(('pad', p.pid))
    bad = 0
    for net, nodes in nets.items():
        if net == 'INT': continue
        groups = {}
        for n in nodes:
            groups.setdefault(find(n), []).append(n)
        if len(groups) > 1:
            bad += 1
            print(f'NET {net}: {len(groups)} disconnected groups: '
                  + '; '.join(str(g[:3]) for g in list(groups.values())[:4]))
    print(f'connectivity: {"OK" if bad == 0 else str(bad) + " nets broken"}')

    # ---- clearances ----
    def segs(rt2):
        pts = [tuple(q) for q in rt2['pts']]
        return [(np.array(a), np.array(b)) for a, b in zip(pts, pts[1:])]

    nviol = 0
    # track vs pad
    for rt2 in routes:
        h = rt2['w'] / 2
        for p in pads:
            if p.net == rt2['net']: continue
            clr = CLR(R.cls_of(rt2['net']), p.cls) if p.net else 6.0
            # layer overlap?
            if rt2['layer'] == 0 and p.layer == 2: continue
            if rt2['layer'] == 1 and p.layer == 1: continue
            x0, y0, x1, y1 = p.bbox(h + clr + 2)
            if not any(x0 - 2 <= q[0] <= x1 + 2 and y0 - 2 <= q[1] <= y1 + 2 for q in rt2['pts']): continue
            for (a, b) in segs(rt2):
                # coarse bbox check
                if min(a[0], b[0]) > x1 + h or max(a[0], b[0]) < x0 - h: continue
                if min(a[1], b[1]) > y1 + h or max(a[1], b[1]) < y0 - h: continue
                n = max(2, int(np.hypot(*(b - a)) / 4))
                ts = np.linspace(0, 1, n)[:, None]
                pts = a[None, :] * (1 - ts) + b[None, :] * ts
                dmin = 1e9
                for q in pts:
                    dmin = min(dmin, pad_dist(q[0], q[1], p))
                gap = dmin - h
                if gap < clr - tol:
                    nviol += 1
                    print(f'CLEAR track({rt2["net"]})-pad({p.net or "mech"}:{p.num}) gap={gap:.2f} need={clr} at~({q[0]:.0f},{q[1]:.0f})')
                    break
    # track vs track
    for i in range(len(routes)):
        for j in range(i + 1, len(routes)):
            r1, r2 = routes[i], routes[j]
            if r1['net'] == r2['net']: continue
            if r1['layer'] != r2['layer']: continue
            clr = CLR(R.cls_of(r1['net']), R.cls_of(r2['net']))
            h = r1['w'] / 2 + r2['w'] / 2
            bb1 = (min(q[0] for q in r1['pts']), min(q[1] for q in r1['pts']),
                   max(q[0] for q in r1['pts']), max(q[1] for q in r1['pts']))
            bb2 = (min(q[0] for q in r2['pts']), min(q[1] for q in r2['pts']),
                   max(q[0] for q in r2['pts']), max(q[1] for q in r2['pts']))
            if bb1[0] > bb2[2] + h or bb2[0] > bb1[2] + h: continue
            if bb1[1] > bb2[3] + h or bb2[1] > bb1[3] + h: continue
            hit = False
            for (a, b) in segs(r1):
                for (c, d) in segs(r2):
                    if min(a[0], b[0]) > max(c[0], d[0]) + h or min(c[0], d[0]) > max(a[0], b[0]) + h: continue
                    if min(a[1], b[1]) > max(c[1], d[1]) + h or min(c[1], d[1]) > max(a[1], b[1]) + h: continue
                    dd = caps_seg_dist(a, b, c, d)
                    if dd < h + clr - tol:
                        nviol += 1
                        print(f'CLEAR track({r1["net"]})-track({r2["net"]}) dist={dd - h:.2f} need={clr} L{r1["layer"]}')
                        hit = True
                        break
                if hit: break
    # via vs pads / tracks / vias
    for v in vias:
        for p in pads:
            if p.net == v['net']: continue
            clr = CLR(R.cls_of(v['net']), p.cls) if p.net else 6.0
            if p.layer == 2 and False: pass
            d = pad_dist(v['x'], v['y'], p)
            gap = d - R.VIA_R
            if gap < clr - tol:
                nviol += 1
                print(f'CLEAR via({v["net"]}@{v["x"]:.0f},{v["y"]:.0f})-pad({p.net or "mech"}:{p.num}) gap={gap:.2f} need={clr}')
        for rt2 in routes:
            if rt2['net'] == v['net']: continue
            clr = CLR(R.cls_of(v['net']), R.cls_of(rt2['net']))
            hh = R.VIA_R + rt2['w'] / 2
            for (a, b) in segs(rt2):
                n = max(2, int(np.hypot(*(b - a)) / 4))
                ts = np.linspace(0, 1, n)[:, None]
                pts = a[None, :] * (1 - ts) + b[None, :] * ts
                dmin = np.hypot(pts[:, 0] - v['x'], pts[:, 1] - v['y']).min()
                if dmin < hh + clr - tol:
                    nviol += 1
                    print(f'CLEAR via({v["net"]})-track({rt2["net"]}) gap={dmin - hh:.2f} need={clr}')
                    break
        for v2 in vias:
            if v2 is v or v2['net'] != v['net'] and False: continue
            if v2['net'] == v['net']: continue
            if (v2['x'], v2['y']) <= (v['x'], v['y']): continue
            d = math.hypot(v['x'] - v2['x'], v['y'] - v2['y']) - R.VIA_R * 2
            clr = CLR(R.cls_of(v['net']), R.cls_of(v2['net']))
            if d < clr - tol:
                nviol += 1
                print(f'CLEAR via-via gap={d:.2f}')
    print(f'clearance violations: {nviol}')


if __name__ == '__main__':
    main()
