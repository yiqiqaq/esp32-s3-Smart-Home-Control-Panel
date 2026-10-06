#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""2-layer grid router (mil). Board [-2205,2205]^2, 10 mil grid, 8 mil hard
clearance (DRC 6), soft penalty near mains copper, vias 48/24.
Own-net pad zones are freed with precise foreign-conflict filtering."""
import heapq, math, json

N, OFF, GS, CLEAR, AC_PEN, VIA_R, VIA_COST = 442, 2205, 10, 8, 40, 24, 160
HALF = {0: 6.0, 1: 20.0}
MAINS = {'AC_L', 'AC_N', 'AC_L_F', 'AC_L_M', 'AC_N_M', 'OUT1', 'OUT2', 'OUT3'}
POWER_W = {'VMAIN', 'VBUS', '+5V', '+3V3', 'GND'}

def idx(v): return int(round((v + OFF) / GS))
def coord(i): return i * GS - OFF

pads, tracks = [], []
for line in open('/Users/yiqi/Desktop/esp-32-DevKitC/drc_fix/geometry.csv'):
    line = line.strip()
    if not line: continue
    f = line.split('|')
    if f[0] == 'P':
        s = f[6].split(',')
        shape = (s[0], float(s[1]) if len(s) > 1 else 0.0, float(s[2]) if len(s) > 2 else 0.0)
        hole = None
        if f[7] != '-':
            hs = f[7].split(',')
            hole = (hs[0], float(hs[1]), float(hs[2]) if len(hs) > 2 else 0.0)
        pads.append(dict(net=f[1], x=float(f[2]), y=float(f[3]), layer=int(f[4]),
                         rot=int(f[5]), shape=shape, hole=hole))
    elif f[0] == 'T':
        pass
assert len(pads) == 198, len(pads)
_mr = json.load(open('/Users/yiqi/Desktop/esp-32-DevKitC/drc_fix/mains_routes.json'))
for _t in _mr['tracks']:
    tracks.append(dict(net=_t[0], layer=int(_t[1]), w=float(_t[2]), pts=[tuple(p) for p in _t[3]]))
assert len(tracks) == 16, len(tracks)
sc = {(p['net'], p['x'], p['y']) for p in pads}
for c in [('GND', -1487.3, -845), ('+3V3', 1835.6, 1337.5), ('AC_L_F', -1103.7, -127.2),
          ('+5V', 1770.6, 2107.5), ('OUT1', -1410, -834.2), ('INT', 1345, 1239.1)]:
    assert c in sc, c

def pad_rect(p):
    k, a, b = p['shape']
    if k == 'E': return a / 2, a / 2
    if k == 'P': return 11.8, 25.6   # USB-C shell polygons: real bbox half-extents
    w, h = a, b
    if p['rot'] % 180 == 90: w, h = h, w
    return w / 2, h / 2

def hole_r(p):
    h = p['hole']
    if not h: return 0.0
    return h[1] / 2 if h[0] == 'R' else max(h[1], h[2]) / 2

def pad_layers(p):
    return (0, 1) if p['layer'] == 12 else ((0,) if p['layer'] == 1 else (1,))

blocked  = {c: [bytearray(N * N), bytearray(N * N)] for c in (0, 1)}
via_blk  = [bytearray(N * N), bytearray(N * N)]
penalty  = [[0] * (N * N), [0] * (N * N)]

def cells_disc(cx, cy, r, lans):
    out = []
    ix0, iy0 = idx(cx), idx(cy); ir = int(math.ceil(r / GS))
    for lan in lans:
        for dx in range(-ir, ir + 1):
            for dy in range(-ir, ir + 1):
                ix, iy = ix0 + dx, iy0 + dy
                if 0 <= ix < N and 0 <= iy < N and math.hypot(coord(ix) - cx, coord(iy) - cy) <= r:
                    out.append((lan, ix, iy))
    return out

def cells_rect(cx, cy, hx, hy, lans):
    out = []
    ix0, iy0 = idx(cx), idx(cy)
    ixr, iyr = int(math.ceil(hx / GS)), int(math.ceil(hy / GS))
    for lan in lans:
        for dx in range(-ixr, ixr + 1):
            for dy in range(-iyr, iyr + 1):
                ix, iy = ix0 + dx, iy0 + dy
                if 0 <= ix < N and 0 <= iy < N and abs(coord(ix) - cx) <= hx and abs(coord(iy) - cy) <= hy:
                    out.append((lan, ix, iy))
    return out

def add_pen(cx, cy, r, lans, amt):
    ix0, iy0 = idx(cx), idx(cy); ir = int(math.ceil(r / GS))
    for lan in lans:
        m = penalty[lan]
        for dx in range(-ir, ir + 1):
            for dy in range(-ir, ir + 1):
                ix, iy = ix0 + dx, iy0 + dy
                if 0 <= ix < N and 0 <= iy < N and math.hypot(coord(ix) - cx, coord(iy) - cy) <= r:
                    j = iy * N + ix
                    if m[j] < 10: m[j] += amt

for p in pads:
    hx, hy = pad_rect(p)
    lans = pad_layers(p)
    for cls in (0, 1):
        h = HALF[cls]
        if p['shape'][0] == 'E':
            for (lan, ix, iy) in cells_disc(p['x'], p['y'], hx + CLEAR + h, lans):
                blocked[cls][lan][iy * N + ix] = 1
        else:
            for (lan, ix, iy) in cells_rect(p['x'], p['y'], hx + CLEAR + h, hy + CLEAR + h, lans):
                blocked[cls][lan][iy * N + ix] = 1
        hr = hole_r(p)
        if hr > 0:
            for (lan, ix, iy) in cells_disc(p['x'], p['y'], hr + CLEAR + h, (0, 1)):
                blocked[cls][lan][iy * N + ix] = 1
    hr = hole_r(p)
    if hr > 0:
        for (lan, ix, iy) in cells_disc(p['x'], p['y'], hr + CLEAR + VIA_R, (0, 1)):
            via_blk[lan][iy * N + ix] = 1
    for (lan, ix, iy) in cells_disc(p['x'], p['y'], max(hx, hy) + CLEAR + VIA_R + GS, lans):
        via_blk[lan][iy * N + ix] = 1
    if p['net'] in MAINS:
        add_pen(p['x'], p['y'], max(hx, hy) + AC_PEN + 20, lans, 2)

def dist_pt_seg(px, py, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L2 = dx * dx + dy * dy
    if L2 == 0: return math.hypot(px - a[0], py - a[1])
    t = max(0.0, min(1.0, ((px - a[0]) * dx + (py - a[1]) * dy) / L2))
    return math.hypot(px - (a[0] + t * dx), py - (a[1] + t * dy))

for t in tracks:
    lan = t['layer'] - 1
    half_t = t['w'] / 2.0
    pts = t['pts']
    for i in range(len(pts) - 1):
        a, b = pts[i], pts[i + 1]
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        n = max(1, int(L / (GS / 2.0)))
        for s in range(n + 1):
            tt = s / n
            cx, cy = a[0] + (b[0] - a[0]) * tt, a[1] + (b[1] - a[1]) * tt
            _mc = 20.0
            for (lan2, ix, iy) in cells_disc(cx, cy, half_t + _mc + HALF[0], (lan,)):
                blocked[0][lan2][iy * N + ix] = 1
            for (lan2, ix, iy) in cells_disc(cx, cy, half_t + _mc + HALF[1], (lan,)):
                blocked[1][lan2][iy * N + ix] = 1
            for (lan2, ix, iy) in cells_disc(cx, cy, half_t + CLEAR + VIA_R, (lan,)):
                via_blk[lan2][iy * N + ix] = 1
    xs = [q[0] for q in pts]; ys = [q[1] for q in pts]
    for ix in range(max(0, idx(min(xs) - AC_PEN - 80)), min(N - 1, idx(max(xs) + AC_PEN + 80)) + 1):
        for iy in range(max(0, idx(min(ys) - AC_PEN - 80)), min(N - 1, idx(max(ys) + AC_PEN + 80)) + 1):
            d = min(dist_pt_seg(coord(ix), coord(iy), pts[i], pts[i + 1]) for i in range(len(pts) - 1))
            if d <= half_t + AC_PEN + HALF[1]:
                j = iy * N + ix
                if penalty[lan][j] < 10: penalty[lan][j] += 3

for lan in (0, 1):
    m0 = blocked[0][lan]
    for ix in range(N):
        if abs(coord(ix)) > 2180:
            for iy in range(N): m0[iy * N + ix] = 1
    for iy in range(N):
        if abs(coord(iy)) > 2180:
            base = iy * N
            for ix in range(N): m0[base + ix] = 1

# ---------- per-pad free zones (own-net passage) ----------
# nearby obstacles per pad (precise geometric filtering)
def pad_center_rect(p):
    return pad_rect(p)

near_obstacles = []
for p in pads:
    hx, hy = pad_rect(p)
    x0, y0, x1, y1 = p['x'] - hx - 140, p['y'] - hy - 140, p['x'] + hx + 140, p['y'] + hy + 140
    fp = []
    for q in pads:
        if q is p: continue
        qhx, qhy = pad_rect(q)
        if q['x'] + qhx + 120 < x0 or q['x'] - qhx - 120 > x1: continue
        if q['y'] + qhy + 120 < y0 or q['y'] - qhy - 120 > y1: continue
        fp.append(q)
    ft = []
    for t in tracks:
        xs = [q[0] for q in t['pts']]; ys = [q[1] for q in t['pts']]
        if max(xs) + t['w'] / 2 + 120 < x0 or min(xs) - t['w'] / 2 - 120 > x1: continue
        if max(ys) + t['w'] / 2 + 120 < y0 or min(ys) - t['w'] / 2 - 120 > y1: continue
        ft.append(t)
    near_obstacles.append((fp, ft))

def foreign_hit(p, cx, cy, need, cls):
    """true if a copper disc of radius `need` at (cx,cy) violates clearance to
    any obstacle not belonging to p's net."""
    fp, ft = near_obstacles[pads.index(p)] if False else (None, None)
    return None  # replaced below

def ring_cells_free(p, cls):
    """cells of p's copper+inflation ring that are not foreign-blocked."""
    hx, hy = pad_rect(p)
    lans = pad_layers(p)
    h = HALF[cls]
    res = set()
    pidx = pad_index[p['id']]
    fp, ft = near_obstacles[pidx]
    if p['shape'][0] == 'E':
        cand = cells_disc(p['x'], p['y'], hx + CLEAR + h + GS, lans)
    else:
        cand = cells_rect(p['x'], p['y'], hx + CLEAR + h + GS, hy + CLEAR + h + GS, lans)
    for (lan, ix, iy) in cand:
        cx, cy = coord(ix), coord(iy)
        ok = True
        for q in fp:
            qhx, qhy = pad_rect(q)
            if q['net'] == p['net'] and p['net']:
                continue  # same net pads don't conflict
            if q['shape'][0] == 'E':
                if math.hypot(cx - q['x'], cy - q['y']) < qhx + CLEAR + h:
                    ok = False; break
            else:
                dxo = abs(cx - q['x']) - qhx
                dyo = abs(cy - q['y']) - qhy
                d = math.hypot(max(dxo, 0), max(dyo, 0))
                if d < CLEAR + h:
                    ok = False; break
            hr = hole_r(q)
            if hr > 0 and math.hypot(cx - q['x'], cy - q['y']) < hr + CLEAR + h:
                ok = False; break
        if ok:
            for t in ft:
                half_t = t['w'] / 2.0
                if t['net'] == p['net'] and p['net']:
                    continue
                lan_t = t['layer'] - 1
                if lan_t != lan and not _th_like(t): continue
                d = min(dist_pt_seg(cx, cy, t['pts'][i], t['pts'][i + 1]) for i in range(len(t['pts']) - 1))
                if d < half_t + CLEAR + h:
                    ok = False; break
        if ok:
            res.add((lan, ix, iy))
    return res

def _th_like(t):
    return False

def via_ring_free(p):
    hx, hy = pad_rect(p)
    lans = pad_layers(p)
    res = set()
    pidx = pad_index[p['id']]
    fp, ft = near_obstacles[pidx]
    for (lan, ix, iy) in cells_disc(p['x'], p['y'], max(hx, hy) + CLEAR + VIA_R + GS, lans):
        cx, cy = coord(ix), coord(iy)
        ok = True
        for q in fp:
            if q['net'] == p['net'] and p['net']: continue
            qhx, qhy = pad_rect(q)
            if q['shape'][0] == 'E':
                if math.hypot(cx - q['x'], cy - q['y']) < qhx + CLEAR + VIA_R: ok = False; break
            else:
                dxo = abs(cx - q['x']) - qhx; dyo = abs(cy - q['y']) - qhy
                if math.hypot(max(dxo, 0), max(dyo, 0)) < CLEAR + VIA_R: ok = False; break
            hr = hole_r(q)
            if hr > 0 and math.hypot(cx - q['x'], cy - q['y']) < hr + CLEAR + VIA_R: ok = False; break
        if ok:
            for t in ft:
                if t['net'] == p['net'] and p['net']: continue
                lan_t = t['layer'] - 1
                if lan_t != lan: continue
                d = min(dist_pt_seg(cx, cy, t['pts'][i], t['pts'][i + 1]) for i in range(len(t['pts']) - 1))
                if d < t['w'] / 2 + CLEAR + VIA_R: ok = False; break
        if ok: res.add((lan, ix, iy))
    return res

# ids
for i, p in enumerate(pads): p['id'] = i
pad_index = {p['id']: i for i, p in enumerate(pads)}

pad_free = {}    # (pid, cls) -> set
pad_vfree = {}   # pid -> set
for i, p in enumerate(pads):
    for cls in (0, 1):
        pad_free[(i, cls)] = ring_cells_free(p, cls)
    pad_vfree[i] = via_ring_free(p)

net_pads = {}
for p in pads:
    if p['net'] and p['net'] not in MAINS:
        net_pads.setdefault(p['net'], []).append(p)

# ---------- dynamic occupancy ----------
dyn_blk = {c: [set(), set()] for c in (0, 1)}
dyn_via = {c: [set(), set()] for c in (0, 1)}
net_copper = {}

def stamp_seg(net, lan, a, b, w):
    half_t = w / 2.0
    cls = 1 if net in POWER_W else 0
    L = math.hypot(b[0] - a[0], b[1] - a[1])
    n = max(1, int(L / (GS / 2.0)))
    for s in range(n + 1):
        tt = s / n
        cx, cy = a[0] + (b[0] - a[0]) * tt, a[1] + (b[1] - a[1]) * tt
        for (l2, ix, iy) in cells_disc(cx, cy, half_t, (lan,)):
            net_copper.setdefault(net, set()).add((l2, ix, iy))
        for (l2, ix, iy) in cells_disc(cx, cy, half_t + CLEAR + HALF[0], (lan,)):
            dyn_blk[0][l2].add((ix, iy))
        for (l2, ix, iy) in cells_disc(cx, cy, half_t + CLEAR + HALF[1], (lan,)):
            dyn_blk[1][l2].add((ix, iy))
        for (l2, ix, iy) in cells_disc(cx, cy, half_t + CLEAR + VIA_R + HALF[0], (lan,)):
            dyn_via[0][l2].add((ix, iy))
        for (l2, ix, iy) in cells_disc(cx, cy, half_t + CLEAR + VIA_R + HALF[1], (lan,)):
            dyn_via[1][l2].add((ix, iy))

def stamp_via(net, vx, vy):
    for lan in (0, 1):
        for (l2, ix, iy) in cells_disc(vx, vy, VIA_R, (lan,)):
            net_copper.setdefault(net, set()).add((l2, ix, iy))
        for (l2, ix, iy) in cells_disc(vx, vy, VIA_R + CLEAR + HALF[0], (lan,)):
            dyn_blk[0][l2].add((ix, iy))
        for (l2, ix, iy) in cells_disc(vx, vy, VIA_R + CLEAR + HALF[1], (lan,)):
            dyn_blk[1][l2].add((ix, iy))
        for (l2, ix, iy) in cells_disc(vx, vy, VIA_R + CLEAR + VIA_R + HALF[0], (lan,)):
            dyn_via[0][l2].add((ix, iy))
        for (l2, ix, iy) in cells_disc(vx, vy, VIA_R + CLEAR + VIA_R + HALF[1], (lan,)):
            dyn_via[1][l2].add((ix, iy))

DIRS = [(1,0,10),(-1,0,10),(0,1,10),(0,-1,10),(1,1,14),(1,-1,14),(-1,1,14),(-1,-1,14)]

def astar(net, cls, sources, goals, win, hint, own_free, own_via):
    blk, dbl = blocked[cls], dyn_blk[cls]
    dvl = dyn_via[cls]
    copper = net_copper.setdefault(net, set())
    ix0, ix1, iy0, iy1 = win
    hix, hiy = hint
    dist = {s: 0 for s in sources}
    prev = {}
    pq = [(10 * (abs(s[1] - hix) + abs(s[2] - hiy)), 0, s) for s in sources]
    heapq.heapify(pq)
    gset = goals
    expansions = 0
    while pq:
        f, g, key = heapq.heappop(pq)
        if dist.get(key, 1 << 60) < g: continue
        if key in gset: return key, prev, expansions
        expansions += 1
        if expansions > 600000: return None, None
        lan, ix, iy = key
        for ddx, ddy, cost in DIRS:
            nx, ny = ix + ddx, iy + ddy
            if not (ix0 <= nx <= ix1 and iy0 <= ny <= iy1): continue
            nkey = (lan, nx, ny)
            j = ny * N + nx
            in_own = nkey in own_free and (nx, ny) not in dbl[lan]
            if nkey not in copper and not in_own:
                if blk[lan][j] or (nx, ny) in dbl[lan]: continue
            if ddx and ddy:
                a = (lan, ix + ddx, iy); b = (lan, ix, iy + ddy)
                def _side_ok(c):
                    if c in copper: return True
                    if c in own_free and (c[1], c[2]) not in dbl[lan]: return True
                    cj = c[2] * N + c[1]
                    return not (blk[lan][cj] or (c[1], c[2]) in dbl[lan])
                if not (_side_ok(a) and _side_ok(b)): continue
            ng = g + cost + penalty[lan][j] + (3 if lan == 1 else 0)
            if ng < dist.get(nkey, 1 << 60):
                dist[nkey] = ng; prev[nkey] = key
                heapq.heappush(pq, (ng + 10 * (abs(nx - hix) + abs(ny - hiy)), ng, nkey))
        nkey = (1 - lan, ix, iy)
        j = iy * N + ix
        stat = via_blk[lan][j] or via_blk[1 - lan][j]
        own = (key in own_via and (ix, iy) not in dvl[lan]) or (nkey in own_via and (nx, ny) not in dvl[1 - lan])
        own_cop = key in copper or nkey in copper
        dyn = (ix, iy) in dvl[lan] or (ix, iy) in dvl[1 - lan]
        if own_cop or own or not (stat or dyn):
            ng = g + VIA_COST
            if ng < dist.get(nkey, 1 << 60):
                dist[nkey] = ng; prev[nkey] = ('V', lan, ix, iy)
                heapq.heappush(pq, (ng + 10 * (abs(ix - hix) + abs(iy - hiy)), ng, nkey))
    return None, prev, expansions

def pad_cells(p):
    hx, hy = pad_rect(p)
    if p['shape'][0] == 'E':
        return set(cells_disc(p['x'], p['y'], hx, pad_layers(p)))
    return set(cells_rect(p['x'], p['y'], hx, hy, pad_layers(p)))

def simplify(points):
    out = [points[0]]
    for q in points[1:]:
        if abs(q[0] - out[-1][0]) > 0.01 or abs(q[1] - out[-1][1]) > 0.01:
            out.append(q)
    i = 1
    while i < len(out) - 1:
        ax, ay = out[i - 1]; bx, by = out[i]; cx, cy = out[i + 1]
        if abs((bx - ax) * (cy - by) - (by - ay) * (cx - bx)) < 1e-9:
            out.pop(i)
        else:
            i += 1
    return out

def route_connection(net, tgt, sources, goals, win, hint, own_free, own_via, cls):
    gk, prev, expc = astar(net, cls, sources, goals, win, hint, own_free, own_via)
    if gk is None:
        return None, None, None
    path = [gk]
    k = gk
    while k in prev:
        k = prev[k]; path.append(k)
    path.reverse()
    segs = {}; vias = []
    cur_lan = path[0][0]
    segs.setdefault(cur_lan, []).append((path[0][1], path[0][2]))
    for item in path[1:]:
        if item[0] == 'V':
            _, fl, ix, iy = item
            vias.append((ix, iy))
            cur_lan = 1 - fl
            segs.setdefault(cur_lan, []).append((ix, iy))
        else:
            lan, ix, iy = item
            segs.setdefault(lan, []).append((ix, iy))
            cur_lan = lan
    return segs, vias, cls

def route_net(net):
    plist = net_pads.get(net, [])
    if len(plist) < 2: return [], [], ['skip single %s' % net]
    base_cls = 1 if net in POWER_W else 0
    own_free_by = {}; own_via = set()
    for cls in (0, 1):
        own_free_by[cls] = set()
        for p in plist:
            own_free_by[cls] |= pad_free[(pad_index[p['id']], cls)]
    for p in plist:
        own_via |= pad_vfree[pad_index[p['id']]]
    remaining = list(range(1, len(plist)))
    tree = [0]
    tr_out, via_out, notes = [], [], []
    while remaining:
        best, bd = None, 1 << 60
        for ri in remaining:
            for ti in tree:
                d = abs(plist[ri]['x'] - plist[ti]['x']) + abs(plist[ri]['y'] - plist[ti]['y'])
                if d < bd: bd, best = d, ri
        tgt = plist[best]
        xs = [plist[i]['x'] for i in tree] + [tgt['x']]
        ys = [plist[i]['y'] for i in tree] + [tgt['y']]
        margin = 700 if base_cls == 1 else 500
        win = (max(0, idx(min(xs) - margin)), min(N - 1, idx(max(xs) + margin)),
               max(0, idx(min(ys) - margin)), min(N - 1, idx(max(ys) + margin)))
        hint = (idx(tgt['x']), idx(tgt['y']))
        done = False
        for cls in ([base_cls, 0] if base_cls == 1 else [0]):
            sources = set()
            for ti in tree: sources |= pad_cells(plist[ti])
            goals = pad_cells(tgt)
            segs, vias, used = route_connection(net, tgt, sources, goals, win, hint,
                                                own_free_by[cls], own_via, cls)
            if segs is None: continue
            w = 40 if used == 1 else 12
            for lan, cells in segs.items():
                ppts = simplify([(coord(ix), coord(iy)) for (ix, iy) in cells])
                if len(ppts) >= 2:
                    tr_out.append([net, lan + 1, w, ppts])
                    for i in range(len(ppts) - 1):
                        stamp_seg(net, lan, ppts[i], ppts[i + 1], w)
            for (ix, iy) in vias:
                via_out.append([net, coord(ix), coord(iy)])
                stamp_via(net, coord(ix), coord(iy))
            if used != base_cls:
                notes.append('downgraded %s -> (%s,%s) to w12' % (net, tgt['x'], tgt['y']))
            done = True
            break
        if not done:
            notes.append('FAIL %s -> (%s,%s)' % (net, tgt['x'], tgt['y']))
        tree.append(best)
        remaining.remove(best)
    return tr_out, via_out, notes

def _near(plist, tree, cls):
    # cells near tree pads within some radius are valid sources (own ring cells)
    res = set()
    for ti in tree:
        p = plist[ti]
        i = pad_index[p['id']]
        res |= pad_free[(i, cls)]
    return res

ORDER = ['VMAIN', 'VBUS', '+5V', '+3V3', 'GND',
         '$1N9', 'CC1', 'CC2', 'SWB1', 'SWB2', 'SWB3', 'SW1', 'SW2', 'SW3',
         'RLY1', 'RLY2', 'RLY3', 'DI1', 'DI2', 'DI3', 'DI_COM',
         'DIN1', 'DIN2', 'DIN3', 'SDA', 'SCL',
         'GPA3', 'GPA4', 'GPA5', 'GPA6', 'GPA7']

all_tr, all_vi, all_notes = [], [], []
for net in ORDER:
    tr, vi, no = route_net(net)
    if net == 'RLY1':
        import math as _m
        px, py = -1310.0, -560.0
        print('objects within 40 of (%s,%s):' % (px, py))
        for p in pads:
            hx, hy = pad_rect(p)
            eff = 8 + HALF[0]
            dxo = max(abs(px - p['x']) - hx, 0); dyo = max(abs(py - p['y']) - hy, 0)
            if p['shape'][0] == 'E':
                d = _m.hypot(px - p['x'], py - p['y']) - hx
            else:
                d = _m.hypot(dxo, dyo)
            if d < 40:
                print('  PAD %s (%s,%s) d=%.1f staticBlocked=%s' % (p['net'] or '-', p['x'], p['y'], d, bool(blocked[0][0][idx(py)*N+idx(px)])))
        for t in tracks:
            for i in range(len(t['pts']) - 1):
                d = dist_pt_seg(px, py, t['pts'][i], t['pts'][i+1])
                if d < 60:
                    print('  TRK %s L%d w%d d=%.1f' % (t['net'], t['layer'], t['w'], d))
        print('  in dyn:', (idx(px), idx(py)) in dyn_blk[0][0])
        break
    all_tr += tr; all_vi += vi; all_notes += no
    print('%-7s %2d tracks %2d vias %s' % (net, len(tr), len(vi), no), flush=True)

json.dump({'tracks': all_tr, 'vias': all_vi, 'notes': all_notes},
          open('/Users/yiqi/Desktop/esp-32-DevKitC/drc_fix/routes.json', 'w'))
print('TOTAL %d tracks, %d vias, %d notes' % (len(all_tr), len(all_vi), len(all_notes)))
