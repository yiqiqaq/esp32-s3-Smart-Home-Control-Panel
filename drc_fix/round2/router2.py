#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Full-board re-router for ESP32S3 86-panel (2-layer, exact-geometry).

Grid 4 mil with 45 deg moves. Obstacles rasterized from exact pad shapes
(ELLIPSE/RECT/OVAL/POLYGON + ROUND/SLOT holes), committed tracks (capsule
dilation) and vias (disc dilation). Clearances: mains-mains 60, mains-SELV
20, SELV-SELV 6 (+4 safety, grid diag 2.83). Pours on bottom are GND-class
obstacles for foreign nets (+8 extra for pour rule margin).

Output: routes.json {routes:[{net,layer,width,pts}],vias:[{net,x,y}]}
"""
import heapq, json, math, sys, time

import numpy as np

GS = 4
HALF = 2212
N = int(2 * HALF / GS) + 1  # cells per axis
VIA_COST = 220_000
VIA_R = 24.0
SQ2 = math.sqrt(2)

MAINS = {'AC_L', 'AC_N', 'AC_L_F', 'AC_L_M', 'AC_N_M', 'OUT1', 'OUT2', 'OUT3'}
W_MAIN, W_PWR, W_VBUS, W_GND, W_SIG = 60, 40, 24, 16, 12


def width_of(net):
    if net in MAINS: return W_MAIN
    if net in ('+5V', '+3V3', 'VMAIN'): return W_PWR
    if net == 'VBUS': return 16
    if net in ('CC1', 'CC2'): return 8
    if net == 'GND': return W_GND
    return W_SIG


def cls_of(net):
    return 0 if net in MAINS else 1


def design_clr(c1, c2):
    if c1 == 0 and c2 == 0: return 60.0
    if c1 == 0 or c2 == 0: return 20.0
    return 6.0


def safety_of(net):
    return 2 if net in ('CC1', 'CC2') else 4


def idx(v): return int(round((v + HALF) / GS))
def co(i): return i * GS - HALF


# ---------------- geometry ----------------

def seg_dist_px(px, py, x1, y1, x2, y2):
    vx, vy = x2 - x1, y2 - y1
    L2 = vx * vx + vy * vy
    if L2 == 0:
        return np.hypot(px - x1, py - y1)
    t = ((px - x1) * vx + (py - y1) * vy) / L2
    t = np.clip(t, 0.0, 1.0)
    return np.hypot(px - (x1 + t * vx), py - (y1 + t * vy))


class Pad:
    __slots__ = ('pid', 'net', 'num', 'x', 'y', 'layer', 'rot', 'kind',
                 'a', 'b', 'poly', 'hole', 'cls')

    def __init__(self, line):
        f = line.rstrip('\n').split('|')
        self.pid = f[0]; self.net = f[1]; self.num = f[2]
        self.x = float(f[3]); self.y = float(f[4])
        self.layer = int(f[5]); self.rot = float(f[6])
        shp = f[7].split(',')
        self.kind = shp[0]
        if self.kind == 'POLYGON':
            vals = shp[1:]
            pts = []
            i = 0
            while i < len(vals):
                if vals[i] == 'L':
                    i += 1; continue
                pts.append((float(vals[i]), float(vals[i + 1])))
                i += 2
            self.poly = pts; self.a = self.b = 0.0
        else:
            self.a = float(shp[1]); self.b = float(shp[2]) if len(shp) > 2 else self.a
            self.poly = None
        if f[8] != '-':
            h = f[8].split(',')
            self.hole = (h[0], float(h[1]), float(h[2]) if len(h) > 2 else float(h[1]))
        else:
            self.hole = None
        self.cls = cls_of(self.net) if self.net else 1

    def layers(self):
        return (0, 1) if self.layer == 12 else ((0,) if self.layer == 1 else (1,))

    def bbox(self, r):
        if self.kind == 'POLYGON':
            xs = [p[0] for p in self.poly]; ys = [p[1] for p in self.poly]
        else:
            m = max(self.a, self.b) / 2
            xs = [self.x - m, self.x + m]; ys = [self.y - m, self.y + m]
        if self.hole:
            if self.hole[0] == 'ROUND':
                m = self.hole[1] / 2
            else:
                m = max(self.hole[1], self.hole[2]) / 2
            xs += [self.x - m, self.x + m]; ys += [self.y - m, self.y + m]
        return min(xs) - r, min(ys) - r, max(xs) + r, max(ys) + r

    def dist_mask(self, X, Y, r):
        """numpy bool mask: cells whose center within r of pad copper."""
        X, Y = np.broadcast_arrays(X, Y)
        if self.kind == 'ELLIPSE':
            return ((X - self.x) ** 2 + (Y - self.y) ** 2) <= (self.a / 2 + r) ** 2
        if self.kind == 'OVAL':
            # stadium (obround): w/h encode axis-aligned size; rot rotates the pad
            rad = min(self.a, self.b) / 2
            ext = abs(self.a - self.b) / 2
            th = math.radians(self.rot)
            c, s = math.cos(-th), math.sin(-th)
            dx, dy = X - self.x, Y - self.y
            lx = dx * c - dy * s; ly = dx * s + dy * c
            if self.a >= self.b:
                t = np.clip(lx, -ext, ext)
                d = np.hypot(lx - t, ly)
            else:
                t = np.clip(ly, -ext, ext)
                d = np.hypot(lx, ly - t)
            return d <= (rad + r)
        if self.kind == 'RECT':
            th = math.radians(self.rot)
            c, s = math.cos(-th), math.sin(-th)
            dx, dy = X - self.x, Y - self.y
            lx = dx * c - dy * s; ly = dx * s + dy * c
            return (np.abs(lx) <= self.a / 2 + r) & (np.abs(ly) <= self.b / 2 + r)
        if self.kind == 'POLYGON':
            pts = self.poly
            inside = np.zeros(X.shape, dtype=bool)
            n = len(pts)
            for i in range(n):
                x1, y1 = pts[i]; x2, y2 = pts[(i + 1) % n]
                cond = ((y1 > Y) != (y2 > Y))
                with np.errstate(divide='ignore', invalid='ignore'):
                    xint = (x2 - x1) * (Y - y1) / (y2 - y1 + 1e-12) + x1
                inside ^= cond & (X < xint)
            d = None
            for i in range(n):
                x1, y1 = pts[i]; x2, y2 = pts[(i + 1) % n]
                dd = seg_dist_px(X, Y, x1, y1, x2, y2)
                d = dd if d is None else np.minimum(d, dd)
            return inside | (d <= r)
        raise ValueError(self.kind)


# ---------------- board state ----------------

def load_pads():
    pads = []
    for fn in ('pads_1.txt', 'pads_2.txt'):
        for line in open(fn):
            line = line.strip()
            if line: pads.append(Pad(line))
    return pads


class Board:
    def __init__(self):
        self.pads = load_pads()
        # pour polygons (bottom, net GND)
        self.pours = [
            [(-480, -1610), (2185, -1610), (2185, -2185), (-480, -2185)],
            [(-480, 400), (-480, 2185), (2185, 2185), (2185, 400)],
        ]
        self.routes = []   # committed: dict(net, layer, w, pts)
        self.vias = []     # dict(net,x,y)
        self.pour_cache = {}
        self.margin = np.ones((N, N), dtype=np.uint8)
        self.margin_main = np.ones((N, N), dtype=np.uint8)
        i0s, i1s = idx(-2170), idx(2170)
        self.margin[i0s:i1s + 1, i0s:i1s + 1] = 0
        i0m, i1m = idx(-2118), idx(2118)
        self.margin_main[i0m:i1m + 1, i0m:i1m + 1] = 0
        self.margin_b = self.margin.tobytes(); self.margin_main_b = self.margin_main.tobytes()

    # -- rasterization helpers --
    def _bbox_cells(self, bb):
        x0, y0, x1, y1 = bb
        return max(idx(x0), 0), max(idx(y0), 0), min(idx(x1), N - 1), min(idx(y1), N - 1)

    def stamp_poly(self, arr, poly, r):
        xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
        c0, r0, c1, r1 = self._bbox_cells((min(xs) - r, min(ys) - r, max(xs) + r, max(ys) + r))
        if c1 < c0 or r1 < r0: return
        X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
        Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
        X, Y = np.broadcast_arrays(X, Y)
        inside = np.zeros(X.shape, dtype=bool)
        n = len(poly)
        for i in range(n):
            x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
            cond = ((y1 > Y) != (y2 > Y))
            with np.errstate(divide='ignore', invalid='ignore'):
                xint = (x2 - x1) * (Y - y1) / (y2 - y1 + 1e-12) + x1
            inside ^= cond & (X < xint)
        d = None
        for i in range(n):
            x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
            dd = seg_dist_px(X, Y, x1, y1, x2, y2)
            d = dd if d is None else np.minimum(d, dd)
        arr[r0:r1 + 1, c0:c1 + 1] |= (inside | (d <= r)).astype(np.uint8)

    def stamp_pad(self, p):
        for l in p.layers():
            for r in self.rsets[p.cls][l] if False else self.rsets[p.cls]:
                arr = self.obs[p.cls][l][r]
                x0, y0, x1, y1 = p.bbox(r)
                c0, r0, c1, r1 = self._bbox_cells((x0, y0, x1, y1))
                if c1 < c0 or r1 < r0: continue
                X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
                Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
                m = p.dist_mask(X, Y, r)
                arr[r0:r1 + 1, c0:c1 + 1] |= m.astype(np.uint8)

    def stamp_capsule(self, arr, x1, y1, x2, y2, rad):
        x0, y0, x1b, y1b = min(x1, x2) - rad, min(y1, y2) - rad, max(x1, x2) + rad, max(y1, y2) + rad
        c0, r0, c1, r1 = self._bbox_cells((x0, y0, x1b, y1b))
        if c1 < c0 or r1 < r0: return
        X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
        Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
        m = seg_dist_px(X, Y, x1, y1, x2, y2) <= rad
        arr[r0:r1 + 1, c0:c1 + 1] |= m.astype(np.uint8)

    def stamp_disc(self, arr, cx, cy, rad):
        c0, r0, c1, r1 = self._bbox_cells((cx - rad, cy - rad, cx + rad, cy + rad))
        if c1 < c0 or r1 < r0: return
        X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
        Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
        m = (X - cx) ** 2 + (Y - cy) ** 2 <= rad ** 2
        arr[r0:r1 + 1, c0:c1 + 1] |= m.astype(np.uint8)

    # -- commit new copper --
    def commit_route(self, net, layer, w, pts):
        self.routes.append(dict(net=net, layer=layer, w=w, pts=pts))

    def commit_via(self, net, x, y):
        self.vias.append(dict(net=net, x=x, y=y))

    def pour_map(self, r):
        key = round(r)
        pm = self.pour_cache.get(key)
        if pm is None:
            pm = np.zeros((N, N), dtype=np.uint8)
            for poly in self.pours:
                self.stamp_poly(pm, poly, r)
            self.pour_cache[key] = pm
        return pm

    # -- build working maps for a net --
    def own_free(self, net):
        """exact copper cells of this net's pads + committed copper."""
        free = [np.zeros((N, N), dtype=bool), np.zeros((N, N), dtype=bool)]
        for p in self.pads:
            if p.net != net: continue
            for l in p.layers():
                x0, y0, x1, y1 = p.bbox(0)
                c0, r0, c1, r1 = self._bbox_cells((x0, y0, x1, y1))
                if c1 < c0 or r1 < r0: continue
                X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
                Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
                free[l][r0:r1 + 1, c0:c1 + 1] |= p.dist_mask(X, Y, 0)
        w2 = width_of(net) / 2
        for rt in self.routes:
            if rt['net'] != net: continue
            l = rt['layer']; h = rt['w'] / 2
            for (ax, ay), (bx, by) in zip(rt['pts'], rt['pts'][1:]):
                x0, y0, x1b, y1b = min(ax, bx) - h, min(ay, by) - h, max(ax, bx) + h, max(ay, by) + h
                c0, r0, c1, r1 = self._bbox_cells((x0, y0, x1b, y1b))
                if c1 < c0 or r1 < r0: continue
                X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
                Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
                free[l][r0:r1 + 1, c0:c1 + 1] |= seg_dist_px(X, Y, ax, ay, bx, by) <= h
        for v in self.vias:
            if v['net'] != net: continue
            for l in (0, 1):
                self._disc_into(free[l], v['x'], v['y'], VIA_R)
        return free

    def _disc_into(self, fl, cx, cy, rad):
        c0, r0, c1, r1 = self._bbox_cells((cx - rad, cy - rad, cx + rad, cy + rad))
        if c1 < c0 or r1 < r0: return
        X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
        Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
        fl[r0:r1 + 1, c0:c1 + 1] |= (X - cx) ** 2 + (Y - cy) ** 2 <= rad ** 2

    def working_maps(self, net):
        """Foreign-copper-only obstacle maps (exact clearance r), bytes for A*."""
        c = cls_of(net)
        h = width_of(net) / 2
        sf = safety_of(net)
        rs = design_clr(c, 1) + h + sf
        rm = design_clr(c, 0) + h + sf
        rv_s = VIA_R + design_clr(c, 1) + sf
        rv_m = VIA_R + design_clr(c, 0) + sf
        selv = [np.zeros((N, N), np.uint8), np.zeros((N, N), np.uint8)]
        mains = [np.zeros((N, N), np.uint8), np.zeros((N, N), np.uint8)]
        vselv = [np.zeros((N, N), np.uint8), np.zeros((N, N), np.uint8)]
        vmains = [np.zeros((N, N), np.uint8), np.zeros((N, N), np.uint8)]
        for p in self.pads:
            if p.net == net: continue
            sa, va, r, rv = (selv, vselv, rs, rv_s) if p.cls == 1 else (mains, vmains, rm, rv_m)
            for l in p.layers():
                self._stamp_pad_r(p, sa[l], l, r)
                self._stamp_pad_r(p, va[l], l, rv)
        for rt in self.routes:
            if rt['net'] == net: continue
            cc = cls_of(rt['net'])
            hh = rt['w'] / 2
            sa, va, r, rv = (selv, vselv, rs, rv_s) if cc == 1 else (mains, vmains, rm, rv_m)
            for (ax, ay), (bx, by) in zip(rt['pts'], rt['pts'][1:]):
                self.stamp_capsule(sa[rt['layer']], ax, ay, bx, by, hh + r)
                self.stamp_capsule(va[rt['layer']], ax, ay, bx, by, hh + rv)
        for v in self.vias:
            if v['net'] == net: continue
            cc = cls_of(v['net'])
            sa, va, r, rv = (selv, vselv, rs, rv_s) if cc == 1 else (mains, vmains, rm, rv_m)
            for l in (0, 1):
                self.stamp_disc(sa[l], v['x'], v['y'], VIA_R + r)
                self.stamp_disc(va[l], v['x'], v['y'], VIA_R + rv)
        base, vbase = [], []
        for l in (0, 1):
            bb = selv[l] | mains[l] | self.margin
            if c == 0: bb = bb | self.margin_main
            if net != 'GND' and l == 1:
                bb = bb | self.pour_map(rs + 8)
            base.append(bb)
            vb = vselv[l] | vmains[l] | self.margin
            if net != 'GND':
                vb = vb | self.pour_map(rv_s + 8)
            vbase.append(vb)
        for p in self.pads:
            if p.net != net: continue
            for l in p.layers():
                c0, r0, c1, r1 = self._bbox_cells((p.x - 10, p.y - 10, p.x + 10, p.y + 10))
                if c1 < c0 or r1 < r0: continue
                X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
                Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
                disc = ((X - p.x) ** 2 + (Y - p.y) ** 2) <= 64
                base[l][r0:r1 + 1, c0:c1 + 1][disc] = 0
                vbase[l][r0:r1 + 1, c0:c1 + 1][disc] = 0
        blocked_b = [base[l].T.tobytes() for l in (0, 1)]
        via_b = [vbase[l].T.tobytes() for l in (0, 1)]
        return blocked_b, via_b

    def _caps_into_bool(self, fl, ax, ay, bx, by, rad):
        x0, y0, x1b, y1b = min(ax, bx) - rad, min(ay, by) - rad, max(ax, bx) + rad, max(ay, by) + rad
        c0, r0, c1, r1 = self._bbox_cells((x0, y0, x1b, y1b))
        if c1 < c0 or r1 < r0: return
        X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
        Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
        fl[r0:r1 + 1, c0:c1 + 1] |= seg_dist_px(X, Y, ax, ay, bx, by) <= rad

    def _stamp_pad_r(self, p, arr, layer, r):
        if layer not in p.layers(): return
        x0, y0, x1, y1 = p.bbox(r)
        c0, r0, c1, r1 = self._bbox_cells((x0, y0, x1, y1))
        if c1 < c0 or r1 < r0: return
        X = np.arange(c0, c1 + 1)[None, :] * GS - HALF
        Y = np.arange(r0, r1 + 1)[:, None] * GS - HALF
        arr[r0:r1 + 1, c0:c1 + 1] |= p.dist_mask(X, Y, r).astype(np.uint8)


# ---------------- A* ----------------

DIRS = [(-1, 0, 10000), (1, 0, 10000), (0, -1, 10000), (0, 1, 10000),
        (-1, -1, 14142), (1, -1, 14142), (-1, 1, 14142), (1, 1, 14142)]


def octile(dx, dy):
    ax, ay = abs(dx), abs(dy)
    return (max(ax, ay) + 0.41421 * min(ax, ay))


def astar(bd, vb, start, goal, goal_layers):
    """start=(ci,cj,l). goal=(ci,cj). returns list[(ci,cj,l)] or None."""
    N2 = N
    si, sj, sl = start
    gi, gj = goal
    h0 = int(octile(si - gi, sj - gj)) * 10000
    openh = [(h0, 0, sl, si, sj)]
    came = {}
    best = {(sl, si, sj): 0}
    closed = set()
    while openh:
        f, g, l, i, j = heapq.heappop(openh)
        key = (l, i, j)
        if key in closed: continue
        closed.add(key)
        if i == gi and j == gj and l in goal_layers:
            path = [key]
            while key in came:
                key = came[key]
                path.append(key)
            return path[::-1]
        bb = bd[l]
        for di, dj, w in DIRS:
            ni, nj = i + di, j + dj
            if ni < 0 or nj < 0 or ni >= N or nj >= N: continue
            if bb[ni * N2 + nj]: continue
            ng = g + w
            nk = (l, ni, nj)
            if nk in closed: continue
            if best.get(nk, 1 << 60) <= ng: continue
            best[nk] = ng
            came[nk] = key
            heapq.heappush(openh, (ng + int(octile(ni - gi, nj - gj)) * 10000, ng, l, ni, nj))
        ov = vb[l]
        if not ov[i * N2 + j]:
            nl = 1 - l
            if not vb[nl][i * N2 + j]:
                ng = g + VIA_COST
                nk = (nl, i, j)
                if nk not in closed and best.get(nk, 1 << 60) > ng:
                    best[nk] = ng
                    came[nk] = key
                    heapq.heappush(openh, (ng + int(octile(i - gi, j - gj)) * 10000 + VIA_COST, ng, nl, i, j))
    return None


def los_free(bd, l, a, b):
    (x1, y1), (x2, y2) = a, b
    dist = math.hypot(x2 - x1, y2 - y1)
    steps = max(2, int(dist / 2))
    bb = bd[l]
    for s in range(steps + 1):
        t = s / steps
        x = x1 + (x2 - x1) * t; y = y1 + (y2 - y1) * t
        ci, cj = idx(x), idx(y)
        if bb[ci * N + cj]: return False
    return True


def smooth(bd, l, pts):
    out = [pts[0]]
    i = 0
    while i < len(pts) - 1:
        j = len(pts) - 1
        while j > i + 1 and not los_free(bd, l, pts[i], pts[j]):
            j -= 1
        out.append(pts[j])
        i = j
    return out


# ---------------- routing driver ----------------

def net_pads(pads, net):
    return [p for p in pads if p.net == net]


def pad_cell(p):
    return idx(p.x), idx(p.y)


def route_edge(bd_board, net, p1, p2, force_layer=None, maps=None):
    w = width_of(net)
    bd, vb = maps if maps is not None else bd_board.working_maps(net)
    s = pad_cell(p1); g = pad_cell(p2)
    sls = p1.layers() if force_layer is None else (force_layer,)
    best = None
    for sl in sls:
        if bd[sl][s[0] * N + s[1]]: continue
        path = astar(bd, vb, (s[0], s[1], sl), g, set(p2.layers()))
        if path and (best is None or len(path) < len(best[0])):
            best = (path, sl)
    if best is None:
        return None
    path, sl = best
    # nodes are (layer, ci, cj); split by layer, duplicating transition cell
    segs = []
    cur = [path[0]]
    for k in range(1, len(path)):
        if path[k][0] != path[k - 1][0]:
            cur.append(path[k])
            segs.append(cur); cur = [path[k]]
        else:
            cur.append(path[k])
    segs.append(cur)
    out_tracks = []; out_vias = []
    prev_end = None
    for seg in segs:
        l = seg[0][0]
        pts = [(co(i), co(j)) for (l2, i, j) in seg]
        sm = smooth(bd, l, pts)
        if prev_end is not None:
            assert abs(sm[0][0] - prev_end[0]) < 0.01 and abs(sm[0][1] - prev_end[1]) < 0.01, (sm[0], prev_end)
        out_tracks.append((l, sm))
        prev_end = sm[-1]
    for k in range(1, len(segs)):
        l2, ci, cj = segs[k][0]
        out_vias.append((co(ci), co(cj)))
    return out_tracks, out_vias


def commit(bd_board, net, res):
    tracks, vias = res
    w = width_of(net)
    for l, pts in tracks:
        # merge collinear
        pts2 = [pts[0]]
        for k in range(1, len(pts) - 1):
            (x0, y0), (x1, y1), (x2, y2) = pts[k - 1], pts[k], pts[k + 1]
            c1 = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
            if abs(c1) > 1e-9:
                pts2.append((x1, y1))
        pts2.append(pts[-1])
        bd_board.commit_route(net, l, w, pts2)
    for (x, y) in vias:
        bd_board.commit_via(net, x, y)


def mst_order(pads_list):
    n = len(pads_list)
    if n <= 1: return []
    import math as _m
    dist = [[0.0] * n for _ in range(n)]
    for i in range(n):
        for j in range(i + 1, n):
            d = _m.hypot(pads_list[i].x - pads_list[j].x, pads_list[i].y - pads_list[j].y)
            dist[i][j] = dist[j][i] = d
    inT = [False] * n; edges = []; inT[0] = True
    for _ in range(n - 1):
        best = None
        for i in range(n):
            if not inT[i]: continue
            for j in range(n):
                if inT[j]: continue
                if best is None or dist[i][j] < best[0]:
                    best = (dist[i][j], i, j)
        _, i, j = best
        edges.append((i, j)); inT[j] = True
    return edges


def route_net(bd_board, net, pads_list, verbose=True):
    if len(pads_list) == 0: return True
    if net == 'GND':
        return route_gnd(bd_board, pads_list, verbose)
    if len(pads_list) == 1:
        return True
    edges = mst_order(pads_list)
    edges.sort(key=lambda e: math.hypot(pads_list[e[0]].x - pads_list[e[1]].x,
                                        pads_list[e[0]].y - pads_list[e[1]].y))
    ok = True
    for (i, j) in edges:
        res = route_edge(bd_board, net, pads_list[i], pads_list[j])
        if res is None:
            # retry reversed
            res = route_edge(bd_board, net, pads_list[j], pads_list[i])
        if res is None:
            print(f'  FAIL {net}: pad{pads_list[i].num}({pads_list[i].x},{pads_list[i].y}) -> pad{pads_list[j].num}')
            ok = False
            continue
        commit(bd_board, net, res)
    return ok


def pour_sites(bd_board):
    sites = []
    for poly in bd_board.pours:
        xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
        x0, x1, y0, y1 = max(min(xs), -480) + 90, min(max(xs), 2185) - 90, min(ys) + 90, max(ys) - 90
        x = x0
        while x <= x1:
            y = y0
            while y <= y1:
                sites.append((x, y))
                y += 100
            x += 100
    return sites


def point_in_pour(bd_board, x, y, margin=45.0):
    for poly in bd_board.pours:
        xs = [q[0] for q in poly]; ys = [q[1] for q in poly]
        if not (min(xs) + margin <= x <= max(xs) - margin): continue
        if not (min(ys) + margin <= y <= max(ys) - margin): continue
        inside = False
        n = len(poly)
        for i in range(n):
            x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
            if (y1 > y) != (y2 > y):
                xint = (x2 - x1) * (y - y1) / (y2 - y1 + 1e-12) + x1
                if x < xint: inside = not inside
        if inside: return True
    return False


def mk_vpad(x, y):
    return Pad(f'VIRT|GND|V|{x}|{y}|12|0|ELLIPSE,1,1|-|0')


def route_gnd(bd_board, pads_list, verbose=True):
    """Stub-into-pour strategy: THT pads inside pours auto-connect; others get
    a short track + via into the nearest pour site, or a hop to a connected node."""
    sites = pour_sites(bd_board)
    connected = []
    deferred = []
    for p in pads_list:
        if p.layer == 12 and point_in_pour(bd_board, p.x, p.y):
            connected.append(('pad', p))
        else:
            deferred.append(p)
    ok = True
    # process deferred pads: nearest-first to grow the tree sensibly
    deferred.sort(key=lambda p: min((p.x - q.x) ** 2 + (p.y - q.y) ** 2
                                    for _, q in connected) if connected else 0)
    for p in deferred:
        done = False
        # (a) stub into nearest pour site (via at site)
        bdm, vb = bd_board.working_maps('GND')
        for s in sorted(sites, key=lambda s: (s[0] - p.x) ** 2 + (s[1] - p.y) ** 2)[:8]:
            vpad = mk_vpad(*s)
            res = route_edge(bd_board, 'GND', p, vpad, maps=(bdm, vb))
            if res is None: continue
            tracks, vias = res
            ex, ey = tracks[-1][1][-1]
            cell = idx(ex) * N + idx(ey)
            if vb[0][cell] or vb[1][cell]: continue
            commit(bd_board, 'GND', res)
            bd_board.commit_via('GND', ex, ey)
            connected.append(('pad', p))
            done = True
            break
        # (b) hop to nearest connected node
        if not done:
            cands = sorted(connected,
                           key=lambda kv: ((kv[1].x - p.x) ** 2 + (kv[1].y - p.y) ** 2) if kv[0] == 'pad'
                           else ((kv[1][0] - p.x) ** 2 + (kv[1][1] - p.y) ** 2))
            for kind, node in cands[:4]:
                if kind == 'pad':
                    tgt = (node.x, node.y)
                else:
                    tgt = node
                res = route_edge(bd_board, 'GND', p, mk_vpad(*tgt))
                if res is not None:
                    commit(bd_board, 'GND', res)
                    connected.append(('pad', p))
                    done = True
                    break
        if not done:
            print(f'  FAIL GND pad{p.num} ({p.x},{p.y})')
            ok = False
    return ok


def main():
    t0 = time.time()
    bd = Board()
    print(f'pads={len(bd.pads)} init={time.time()-t0:.1f}s', flush=True)
    nets = {}
    for p in bd.pads:
        if p.net: nets.setdefault(p.net, []).append(p)
    order = ['AC_L', 'AC_N', 'AC_L_M', 'AC_N_M', 'AC_L_F', 'OUT1', 'OUT2', 'OUT3',
             '+5V', '+3V3', 'VMAIN', 'VBUS', 'GND',
             'DI_COM', 'DIN1', 'DIN2', 'DIN3', 'DI1', 'DI2', 'DI3',
             'SW1', 'SW2', 'SW3', 'SWB1', 'SWB2', 'SWB3',
             'RLY1', 'RLY2', 'RLY3', 'GPA3', 'GPA4', 'GPA5', 'GPA6', 'GPA7',
             'SDA', 'SCL', 'CC1', 'CC2', '$1N9']
    all_ok = True
    for net in order:
        if net not in nets:
            continue
        t1 = time.time()
        ok = route_net(bd, net, nets[net])
        nseg = sum(1 for r in bd.routes if r['net'] == net)
        nvia = sum(1 for v in bd.vias if v['net'] == net)
        print(f'{net:8s} pads={len(nets[net]):2d} seg={nseg:3d} via={nvia:2d} ok={ok} {time.time()-t1:.1f}s', flush=True)
        all_ok &= ok
    # any nets not in order list
    for net in sorted(nets):
        if net not in order:
            ok = route_net(bd, net, nets[net])
            print(f'{net:8s} (extra) pads={len(nets[net])} ok={ok}', flush=True)
            all_ok &= ok
    print(f'total routes={len(bd.routes)} vias={len(bd.vias)} all_ok={all_ok} {time.time()-t0:.1f}s', flush=True)
    out = dict(routes=[dict(net=r['net'], layer=r['layer'], w=r['w'], pts=r['pts']) for r in bd.routes],
               vias=[dict(net=v['net'], x=v['x'], y=v['y']) for v in bd.vias])
    json.dump(out, open('routes2.json', 'w'))
    print('written routes2.json')


if __name__ == '__main__':
    main()
