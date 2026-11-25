#!/usr/bin/env python3
"""Independent secp256k1 Bulletproofs+ and CLSAG reference.

Group operations go through coincurve (libsecp256k1). The C++ consensus code
uses OpenSSL. Challenge weights are the explicit bit product, not the C++
cache loop. Domain strings match docs and libraries/ctcrypto.
"""
from __future__ import annotations

import hashlib
import sys
from coincurve import PublicKey
from coincurve._libsecp256k1 import ffi, lib
from coincurve.context import GLOBAL_CONTEXT

N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
H_X = 0x50929B74C1A04954B78B4B6035E97A5E078A5A0F28EC96D547BFEE9ACE803AC0
H_Y = 0x31D3C6863973926E049E637CB1B5F40A36DAC28AF1766968C30C2313F3A38904


def _b32(x: int) -> bytes:
    return x.to_bytes(32, "big")


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def reduce_hash(dig: bytes) -> int:
    x = int.from_bytes(dig, "big")
    if x >= N:
        x -= N
    return x


def hash_to_scalar(data: bytes) -> int:
    return reduce_hash(sha256(data))


class Rng:
    def __init__(self, seed: bytes):
        self.seed = seed
        self.n = 0

    def nonzero(self) -> int:
        while True:
            self.n += 1
            h = hash_to_scalar(self.seed + self.n.to_bytes(4, "little"))
            if h:
                return h


def point_from_compressed(raw: bytes) -> PublicKey | None:
    if len(raw) != 33 or raw[0] not in (2, 3):
        return None
    try:
        pk = PublicKey(raw)
    except Exception:
        return None
    if pk.format(compressed=True) != raw:
        return None
    return pk


def hash_to_point(data: bytes) -> PublicKey:
    for counter in range(256):
        buf = data + counter.to_bytes(4, "little")
        dig = sha256(buf)
        for prefix in (2, 3):
            pk = point_from_compressed(bytes([prefix]) + dig)
            if pk is not None:
                return pk
    raise RuntimeError("hash_to_point")


def G() -> PublicKey:
    return PublicKey.from_valid_secret(_b32(1))


def H() -> PublicKey:
    return PublicKey.from_point(H_X, H_Y)


def add_points(points: list[PublicKey]) -> PublicKey | None:
    pts = [p for p in points if p is not None]
    if not pts:
        return None
    out = ffi.new("secp256k1_pubkey *")
    ok = lib.secp256k1_ec_pubkey_combine(
        GLOBAL_CONTEXT.ctx, out, [p.public_key for p in pts], len(pts)
    )
    if not ok:
        return None
    return PublicKey(out, GLOBAL_CONTEXT)


def mul(p: PublicKey, s: int) -> PublicKey | None:
    s %= N
    if s == 0:
        return None
    return p.multiply(_b32(s))


def commit(blind: int, value: int) -> PublicKey:
    hb = mul(H(), value)
    gb = mul(G(), blind)
    parts = [p for p in (hb, gb) if p is not None]
    if not parts:
        raise RuntimeError("commitment at infinity")
    out = add_points(parts)
    if out is None:
        raise RuntimeError("commitment at infinity")
    return out


def smul(a: int, b: int) -> int:
    return (a * b) % N


def sadd(a: int, b: int) -> int:
    return (a + b) % N


def ssub(a: int, b: int) -> int:
    return (a - b) % N


def sinv(a: int) -> int:
    if a % N == 0:
        raise ZeroDivisionError
    return pow(a, N - 2, N)


def append_scalar(buf: bytearray, s: int) -> None:
    buf.append(1)
    buf.extend(_b32(s % N))


def append_point(buf: bytearray, p: PublicKey) -> None:
    buf.append(2)
    buf.extend(p.format(compressed=True))


def gi_hi():
    g, h = [], []
    for i in range(64 * 16):
        idx = i.to_bytes(4, "little")
        g.append(hash_to_point(b"BTS-BP+-Gi-v1" + idx))
        h.append(hash_to_point(b"BTS-BP+-Hi-v1" + idx))
    return g, h


GI, HI = gi_hi()


def initial_transcript() -> int:
    return hash_to_scalar(b"BTS-BP+-TRANSCRIPT-v1")


def hash_commitments(vs: list[PublicKey]) -> int:
    buf = bytearray(b"BTS-BP+-V-v1")
    for p in vs:
        append_point(buf, p)
    return hash_to_scalar(bytes(buf))


def t_update_scalar(tr: int, u: int) -> int:
    buf = bytearray()
    append_scalar(buf, tr)
    append_scalar(buf, u)
    return hash_to_scalar(bytes(buf))


def t_update_point(tr: int, p: PublicKey) -> int:
    buf = bytearray()
    append_scalar(buf, tr)
    append_point(buf, p)
    return hash_to_scalar(bytes(buf))


def t_update_pair(tr: int, a: PublicKey, b: PublicKey) -> int:
    buf = bytearray()
    append_scalar(buf, tr)
    append_point(buf, a)
    append_point(buf, b)
    return hash_to_scalar(bytes(buf))


def vector_exp(a, b, n) -> PublicKey:
    terms = []
    for i in range(n):
        gp = mul(GI[i], a[i])
        hp = mul(HI[i], b[i])
        if gp is not None:
            terms.append(gp)
        if hp is not None:
            terms.append(hp)
    out = add_points(terms)
    if out is None:
        raise RuntimeError("infinity")
    return out


def prove_bp(values: list[int], blinds: list[int], rng: Rng) -> tuple[bytes, list[PublicKey]]:
    count = len(values)
    log_m = 0
    while (1 << log_m) < count:
        log_m += 1
    m = 1 << log_m
    mn = m * 64
    rounds = log_m + 6
    vs = [commit(blinds[i], values[i]) for i in range(count)]
    a_l = [0] * mn
    a_r = [0] * mn
    for j in range(m):
        for i in range(64):
            bit = ((values[j] >> i) & 1) if j < count else 0
            a_l[j * 64 + i] = bit
            a_r[j * 64 + i] = ssub(bit, 1)
    for _try in range(8):
        tr = t_update_scalar(initial_transcript(), hash_commitments(vs))
        alpha = rng.nonzero()
        A = add_points([vector_exp(a_l, a_r, mn), mul(G(), alpha)])
        if A is None:
            continue
        y = t_update_point(tr, A)
        if y == 0:
            continue
        z = hash_to_scalar(_b32(y))
        if z == 0:
            continue
        tr = z
        z2 = smul(z, z)
        d = [0] * mn
        d[0] = z2
        for i in range(1, 64):
            d[i] = smul(d[i - 1], 2)
        for j in range(1, m):
            for i in range(64):
                d[j * 64 + i] = smul(d[(j - 1) * 64 + i], z2)
        y_pow = [1]
        for _ in range(mn + 1):
            y_pow.append(smul(y_pow[-1], y))
        a_l1 = [ssub(a_l[i], z) for i in range(mn)]
        a_r1 = [sadd(sadd(a_r[i], z), smul(d[i], y_pow[mn - i])) for i in range(mn)]
        alpha1 = alpha
        temp = 1
        y_mn_1 = y_pow[mn + 1]
        for j in range(count):
            temp = smul(temp, z2)
            alpha1 = sadd(alpha1, smul(smul(y_mn_1, temp), blinds[j]))
        yinv = sinv(y)
        gprime = GI[:mn]
        hprime = HI[:mn]
        aprime = a_l1[:]
        bprime = a_r1[:]
        yinvpow = [1]
        for _ in range(1, mn):
            yinvpow.append(smul(yinvpow[-1], yinv))
        L, R = [], []
        nprime = mn
        restart = False
        for _round in range(rounds):
            nprime //= 2
            c_l = 0
            yp = 1
            for i in range(nprime):
                yp = smul(yp, y)
                c_l = sadd(c_l, smul(smul(aprime[i], bprime[nprime + i]), yp))
            c_r = 0
            yp = 1
            for i in range(nprime):
                yp = smul(yp, y)
                c_r = sadd(c_r, smul(smul(smul(aprime[nprime + i], y_pow[nprime]), bprime[i]), yp))
            d_l, d_r = rng.nonzero(), rng.nonzero()
            terms_l, terms_r = [], []
            for i in range(nprime):
                gl = mul(gprime[nprime + i], smul(aprime[i], yinvpow[nprime]))
                hl = mul(hprime[i], bprime[nprime + i])
                gr = mul(gprime[i], smul(aprime[nprime + i], y_pow[nprime]))
                hr = mul(hprime[nprime + i], bprime[i])
                for t in (gl, hl):
                    if t is not None:
                        terms_l.append(t)
                for t in (gr, hr):
                    if t is not None:
                        terms_r.append(t)
            clp, crp = mul(H(), c_l), mul(H(), c_r)
            dlp, drp = mul(G(), d_l), mul(G(), d_r)
            for t in (clp, dlp):
                if t is not None:
                    terms_l.append(t)
            for t in (crp, drp):
                if t is not None:
                    terms_r.append(t)
            Lp, Rp = add_points(terms_l), add_points(terms_r)
            if Lp is None or Rp is None:
                restart = True
                break
            L.append(Lp)
            R.append(Rp)
            ch = t_update_pair(tr, Lp, Rp)
            tr = ch
            if ch == 0:
                restart = True
                break
            chi = sinv(ch)
            new_g, new_h = [], []
            for i in range(nprime):
                new_g.append(add_points([x for x in (
                    mul(gprime[i], chi),
                    mul(gprime[nprime + i], smul(yinvpow[nprime], ch)),
                ) if x is not None]))
                new_h.append(add_points([x for x in (
                    mul(hprime[i], ch),
                    mul(hprime[nprime + i], chi),
                ) if x is not None]))
            gprime, hprime = new_g, new_h
            a_new, b_new = [], []
            a_right_w = smul(chi, y_pow[nprime])
            for i in range(nprime):
                a_new.append(sadd(smul(aprime[i], ch), smul(aprime[nprime + i], a_right_w)))
                b_new.append(sadd(smul(bprime[i], chi), smul(bprime[nprime + i], ch)))
            aprime, bprime = a_new, b_new
            alpha1 = sadd(alpha1, sadd(smul(d_l, smul(ch, ch)), smul(d_r, smul(chi, chi))))
        if restart or len(aprime) != 1:
            continue
        r, s, d_, eta = rng.nonzero(), rng.nonzero(), rng.nonzero(), rng.nonzero()
        a1_h = sadd(smul(smul(r, y), bprime[0]), smul(smul(s, y), aprime[0]))
        A1 = add_points([x for x in (
            mul(gprime[0], r), mul(hprime[0], s), mul(G(), d_), mul(H(), a1_h)
        ) if x is not None])
        B = add_points([x for x in (mul(G(), eta), mul(H(), smul(smul(r, y), s))) if x is not None])
        if A1 is None or B is None:
            continue
        e = t_update_pair(tr, A1, B)
        if e == 0:
            continue
        e2 = smul(e, e)
        r1 = sadd(smul(aprime[0], e), r)
        s1 = sadd(smul(bprime[0], e), s)
        d1 = sadd(sadd(smul(d_, e), eta), smul(alpha1, e2))
        return serialize_bp(A, A1, B, r1, s1, d1, L, R), vs
    raise RuntimeError("prove failed")


def serialize_bp(A, A1, B, r1, s1, d1, L, R) -> bytes:
    out = bytearray(b"BP+\x01")
    for p in (A, A1, B):
        out.extend(p.format(compressed=True))
    for s in (r1, s1, d1):
        out.extend(_b32(s % N))
    out.append(len(L))
    for p in L + R:
        out.extend(p.format(compressed=True))
    return bytes(out)


def parse_bp(raw: bytes):
    if len(raw) < 5 or raw[:4] != b"BP+\x01":
        return None
    rounds_at = 4 + 33 * 3 + 32 * 3
    if len(raw) < rounds_at + 1:
        return None
    rounds = raw[rounds_at]
    if rounds == 0 or rounds > 10 or len(raw) != rounds_at + 1 + rounds * 66:
        return None
    off = 4
    pts = []
    for _ in range(3):
        p = point_from_compressed(raw[off:off + 33])
        if p is None:
            return None
        pts.append(p)
        off += 33
    sc = []
    for _ in range(3):
        x = int.from_bytes(raw[off:off + 32], "big")
        if x >= N:
            return None
        sc.append(x)
        off += 32
    off += 1
    L, R = [], []
    for _ in range(rounds):
        p = point_from_compressed(raw[off:off + 33])
        if p is None:
            return None
        L.append(p)
        off += 33
    for _ in range(rounds):
        p = point_from_compressed(raw[off:off + 33])
        if p is None:
            return None
        R.append(p)
        off += 33
    return pts[0], pts[1], pts[2], sc[0], sc[1], sc[2], L, R


def sum_powers(x, n) -> int:
    s, p = 0, x % N
    for _ in range(1, n + 1):
        s = sadd(s, p)
        p = smul(p, x)
    return s


def sum_even_powers(x, n) -> int:
    s, p, step = 0, smul(x, x), smul(x, x)
    e = 2
    while e <= n:
        s = sadd(s, p)
        p = smul(p, step)
        e += 2
    return s


def bit_weight(index: int, ch: list[int], inv: list[int]) -> int:
    """Same product as the C++ cache, with challenge j on bit (rounds-1-j)."""
    rounds = len(ch)
    w = 1
    for j, (c, iv) in enumerate(zip(ch, inv)):
        bit = (index >> (rounds - 1 - j)) & 1
        w = smul(w, c if bit else iv)
    return w


def verify_bp(raw: bytes, commits: list[PublicKey]) -> bool:
    parsed = parse_bp(raw)
    if parsed is None or not commits or len(commits) > 16:
        return False
    A, A1, B, r1, s1, d1, L, R = parsed
    count = len(commits)
    log_m = 0
    while (1 << log_m) < count:
        log_m += 1
    m = 1 << log_m
    if m < count or m > 16:
        return False
    rounds = log_m + 6
    if len(L) != rounds:
        return False
    tr = t_update_scalar(initial_transcript(), hash_commitments(commits))
    y = t_update_point(tr, A)
    if y == 0:
        return False
    z = hash_to_scalar(_b32(y))
    if z == 0:
        return False
    tr = z
    ch = []
    for j in range(rounds):
        c = t_update_pair(tr, L[j], R[j])
        tr = c
        if c == 0:
            return False
        ch.append(c)
    e = t_update_pair(tr, A1, B)
    if e == 0:
        return False
    mn = m * 64
    y_mn = y
    t = mn
    while t > 1:
        y_mn = smul(y_mn, y_mn)
        t //= 2
    y_mn_1 = smul(y_mn, y)
    e2 = smul(e, e)
    z2 = smul(z, z)
    d = [0] * mn
    d[0] = z2
    for i in range(1, 64):
        d[i] = sadd(d[i - 1], d[i - 1])
    for j in range(1, m):
        for i in range(64):
            d[j * 64 + i] = smul(d[(j - 1) * 64 + i], z2)
    inv = [sinv(c) for c in ch]
    weights = [bit_weight(i, ch, inv) for i in range(mn)]
    terms: list[PublicKey] = []
    vcoeff = smul(smul(ssub(0, e2), y_mn_1), 1)
    for j in range(count):
        vcoeff = smul(vcoeff, z2)
        term = mul(commits[j], vcoeff)
        if term is not None:
            terms.append(term)
    for p, s in ((B, ssub(0, 1)), (A1, smul(ssub(0, 1), e)), (A, smul(smul(ssub(0, 1), e), e))):
        term = mul(p, s)
        if term is not None:
            terms.append(term)
    g_scalar = d1
    sum_d = smul((1 << 64) - 1, sum_even_powers(z, 2 * m))
    sum_y = sum_powers(y, mn)
    h_inner = smul(ssub(z2, z), sum_y)
    h_inner = sadd(h_inner, smul(smul(y_mn_1, z), sum_d))
    h_inner = smul(h_inner, e2)
    h_inner = sadd(h_inner, smul(smul(r1, y), s1))
    h_scalar = h_inner
    yinv = sinv(y)
    e_r1_y = smul(e, r1)
    minus_e2_y = smul(ssub(0, e2), y_mn)
    for i in range(mn):
        g_s = sadd(smul(e_r1_y, weights[i]), e2 * z % N)
        h_s = sadd(smul(smul(e, s1), weights[(~i) & (mn - 1)]), ssub(0, smul(e2, z)))
        h_s = sadd(h_s, smul(minus_e2_y, d[i]))
        gp, hp = mul(GI[i], g_s), mul(HI[i], h_s)
        if gp is not None:
            terms.append(gp)
        if hp is not None:
            terms.append(hp)
        e_r1_y = smul(e_r1_y, yinv)
        minus_e2_y = smul(minus_e2_y, yinv)
    gg, hh = mul(G(), g_scalar), mul(H(), h_scalar)
    if gg is not None:
        terms.append(gg)
    if hh is not None:
        terms.append(hh)
    for j in range(rounds):
        lp = mul(L[j], smul(smul(ch[j], ch[j]), smul(ssub(0, 1), e2)))
        rp = mul(R[j], smul(smul(inv[j], inv[j]), smul(ssub(0, 1), e2)))
        if lp is not None:
            terms.append(lp)
        if rp is not None:
            terms.append(rp)
    return add_points(terms) is None


def hp_point(spend: PublicKey) -> PublicKey:
    buf = bytearray(b"BTS-CLSAG-Hp-v1")
    append_point(buf, spend)
    return hash_to_point(bytes(buf))


def clsag_mu(domain: bytes, pubs, commits, I, D, pseudo) -> int:
    buf = bytearray(domain)
    for p in list(pubs) + list(commits):
        append_point(buf, p)
    append_point(buf, I)
    append_point(buf, D)
    append_point(buf, pseudo)
    return hash_to_scalar(bytes(buf))


def clsag_round(pubs, commits, pseudo, message: bytes, L, R) -> int:
    buf = bytearray(b"BTS-CLSAG-ROUND-v1")
    for p in list(pubs) + list(commits):
        append_point(buf, p)
    append_point(buf, pseudo)
    buf.append(1)
    buf.extend(message)
    append_point(buf, L)
    append_point(buf, R)
    return hash_to_scalar(bytes(buf))


def prove_clsag(message: bytes, pubs, spend: int, commits, blind_diff: int, pseudo: PublicKey, real: int, rng: Rng):
    n = len(pubs)
    C = []
    for i in range(n):
        neg = mul(pseudo, N - 1)
        C.append(add_points([commits[i], neg] if neg is not None else [commits[i]]))
    Hp = hp_point(pubs[real])
    for _try in range(8):
        I = mul(Hp, spend)
        D = mul(Hp, blind_diff)
        if I is None or D is None:
            continue
        a = rng.nonzero()
        aG, aH = mul(G(), a), mul(Hp, a)
        mu_p = clsag_mu(b"BTS-CLSAG-AGG-P-v1", pubs, commits, I, D, pseudo)
        mu_c = clsag_mu(b"BTS-CLSAG-AGG-C-v1", pubs, commits, I, D, pseudo)
        if mu_p == 0 or mu_c == 0 or aG is None or aH is None:
            continue
        c = clsag_round(pubs, commits, pseudo, message, aG, aH)
        if c == 0:
            continue
        s = [0] * n
        i = (real + 1) % n
        c1 = c if i == 0 else None
        bad = False
        while i != real:
            s[i] = rng.nonzero()
            c_p, c_c = smul(mu_p, c), smul(mu_c, c)
            L = add_points([x for x in (mul(G(), s[i]), mul(pubs[i], c_p), mul(C[i], c_c)) if x is not None])
            Hi = hp_point(pubs[i])
            R = add_points([x for x in (mul(Hi, s[i]), mul(I, c_p), mul(D, c_c)) if x is not None])
            if L is None or R is None:
                bad = True
                break
            c = clsag_round(pubs, commits, pseudo, message, L, R)
            if c == 0:
                bad = True
                break
            i = (i + 1) % n
            if i == 0:
                c1 = c
        if bad or c1 is None:
            continue
        s[real] = ssub(a, smul(c, sadd(smul(mu_p, spend), smul(mu_c, blind_diff))))
        raw = bytearray(b"CL\x01")
        raw.extend(I.format(compressed=True))
        raw.extend(D.format(compressed=True))
        raw.extend(_b32(c1))
        raw.append(n)
        for sj in s:
            raw.extend(_b32(sj % N))
        return bytes(raw)
    raise RuntimeError("clsag prove failed")


def parse_clsag(raw: bytes):
    if len(raw) < 3 + 33 + 33 + 32 + 1 or raw[:3] != b"CL\x01":
        return None
    I = point_from_compressed(raw[3:36])
    D = point_from_compressed(raw[36:69])
    if I is None or D is None:
        return None
    c1 = int.from_bytes(raw[69:101], "big")
    if c1 >= N or c1 == 0:
        return None
    n = raw[101]
    if n == 0 or n > 64 or len(raw) != 102 + 32 * n:
        return None
    s = []
    off = 102
    for _ in range(n):
        x = int.from_bytes(raw[off:off + 32], "big")
        if x >= N:
            return None
        s.append(x)
        off += 32
    return I, D, c1, s


def verify_clsag(message: bytes, raw: bytes, pubs, commits, pseudo: PublicKey) -> bool:
    parsed = parse_clsag(raw)
    if parsed is None or len(pubs) != len(commits):
        return False
    I, D, c1, s = parsed
    if len(s) != len(pubs):
        return False
    C = []
    neg = mul(pseudo, N - 1)
    for i in range(len(pubs)):
        C.append(add_points([commits[i]] if neg is None else [commits[i], neg]))
        if C[-1] is None:
            return False
    mu_p = clsag_mu(b"BTS-CLSAG-AGG-P-v1", pubs, commits, I, D, pseudo)
    mu_c = clsag_mu(b"BTS-CLSAG-AGG-C-v1", pubs, commits, I, D, pseudo)
    if mu_p == 0 or mu_c == 0:
        return False
    c = c1
    for i in range(len(pubs)):
        c_p, c_c = smul(mu_p, c), smul(mu_c, c)
        L = add_points([x for x in (mul(G(), s[i]), mul(pubs[i], c_p), mul(C[i], c_c)) if x is not None])
        Hi = hp_point(pubs[i])
        R = add_points([x for x in (mul(Hi, s[i]), mul(I, c_p), mul(D, c_c)) if x is not None])
        if L is None or R is None:
            return False
        c = clsag_round(pubs, commits, pseudo, message, L, R)
        if c == 0:
            return False
    return c == c1


def _read_blocks(prefix: str) -> list[str]:
    out = []
    for line in sys.stdin:
        if line.startswith(prefix):
            out.append(line.split(None, 1)[1].strip())
    return out


def main(argv: list[str]) -> int:
    cmd = argv[1] if len(argv) > 1 else ""
    if cmd == "self-test":
        rng = Rng(b"self-test")
        raw, vs = prove_bp([1, 2], [3, 9], rng)
        if not verify_bp(raw, vs):
            print("bp self-test failed", file=sys.stderr)
            return 1
        pubs = [mul(G(), 20 + i) for i in range(4)]
        pseudo = commit(5, 7)
        commits = [commit(30 + i, 7) for i in range(4)]
        pubs[1] = mul(G(), 11)
        commits[1] = add_points([pseudo, mul(G(), 13)])
        msg = bytes([9, 8, 7, 6]) + bytes(28)
        sig = prove_clsag(msg, pubs, 11, commits, 13, pseudo, 1, Rng(b"clsag-self"))
        if not verify_clsag(msg, sig, pubs, commits, pseudo):
            print("clsag self-test failed", file=sys.stderr)
            return 1
        print("OK")
        return 0
    if cmd == "prove-bp":
        values = [int(x) for x in argv[2].split(",")]
        blinds = [int(x) for x in argv[3].split(",")]
        raw, vs = prove_bp(values, blinds, Rng(b"py-prove-bp"))
        print("PROOF", raw.hex())
        for v in vs:
            print("COMMIT", v.format(compressed=True).hex())
        return 0
    if cmd == "verify-bp":
        proof = ""
        commits = []
        for line in sys.stdin:
            if line.startswith("PROOF "):
                proof = line.split(None, 1)[1].strip()
            elif line.startswith("COMMIT "):
                commits.append(point_from_compressed(bytes.fromhex(line.split(None, 1)[1].strip())))
        if not proof or any(c is None for c in commits) or not verify_bp(bytes.fromhex(proof), commits):
            print("verify failed", file=sys.stderr)
            return 3
        print("OK")
        return 0
    if cmd == "prove-clsag":
        pubs = [mul(G(), 20 + i) for i in range(4)]
        pseudo = commit(5, 7)
        commits = [commit(30 + i, 7) for i in range(4)]
        pubs[1] = mul(G(), 11)
        commits[1] = add_points([pseudo, mul(G(), 13)])
        msg = bytes([9, 8, 7, 6]) + bytes(28)
        # Match the C++ prove-clsag message, which is 32 bytes starting 09 08 07 06 and then zeros.
        sig = prove_clsag(msg, pubs, 11, commits, 13, pseudo, 1, Rng(b"py-clsag"))
        print("MESSAGE", msg.hex())
        for p in pubs:
            print("PUB", p.format(compressed=True).hex())
        for p in commits:
            print("COMMIT", p.format(compressed=True).hex())
        print("PSEUDO", pseudo.format(compressed=True).hex())
        print("SIG", sig.hex())
        return 0
    if cmd == "verify-clsag":
        message = b""
        pubs, commits = [], []
        pseudo = None
        sig = ""
        for line in sys.stdin:
            if line.startswith("MESSAGE "):
                message = bytes.fromhex(line.split(None, 1)[1].strip())
            elif line.startswith("PUB "):
                pubs.append(point_from_compressed(bytes.fromhex(line.split(None, 1)[1].strip())))
            elif line.startswith("COMMIT "):
                commits.append(point_from_compressed(bytes.fromhex(line.split(None, 1)[1].strip())))
            elif line.startswith("PSEUDO "):
                pseudo = point_from_compressed(bytes.fromhex(line.split(None, 1)[1].strip()))
            elif line.startswith("SIG "):
                sig = line.split(None, 1)[1].strip()
        if not verify_clsag(message, bytes.fromhex(sig), pubs, commits, pseudo):
            print("verify failed", file=sys.stderr)
            return 3
        print("OK")
        return 0
    print("usage", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
