#!/usr/bin/env python3
"""Independently verify a STORMBIN file's structural invariants and report
the regime statistics (universe, row count, mean cardinality, density, skew).

Checks performed (the "triage" tools/sets2bin.py --stats would do, adapted
since sets2bin.py has no VCF/STORMBIN reader of its own):
  1. header parses: magic b'STORMBIN', version==1
  2. every row's positions are strictly ascending (implies deduplicated)
  3. no position in any row is >= n_bits (universe)
  4. reports mean |Xi|, density, max |Xi|, and the top-1%-of-rows mass share
"""
import argparse
import struct
import sys

import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('path')
    args = ap.parse_args()

    with open(args.path, 'rb') as f:
        magic = f.read(8)
        assert magic == b'STORMBIN', f'bad magic: {magic!r}'
        ver, n_rows_hdr, n_bits, pad = struct.unpack('<IIII', f.read(16))
        print(f'header OK: version={ver} n_rows={n_rows_hdr} n_bits={n_bits} pad={pad}')
        assert ver == 1, f'unexpected version {ver}'

        cards = np.empty(n_rows_hdr, dtype=np.int64)
        total_set = 0
        max_card = 0
        n_rows = 0
        bad_ascending = 0
        bad_range = 0

        while True:
            hdr = f.read(4)
            if len(hdr) < 4:
                break
            (count,) = struct.unpack('<I', hdr)
            if count:
                pos = np.frombuffer(f.read(4 * count), dtype='<u4')
            else:
                pos = np.empty(0, dtype='<u4')

            if count >= 2:
                if not np.all(pos[1:] > pos[:-1]):
                    bad_ascending += 1
            if count and (pos[-1] >= n_bits or pos.max() >= n_bits):
                bad_range += 1

            if n_rows < len(cards):
                cards[n_rows] = count
            else:
                cards = np.append(cards, count)
            total_set += int(count)
            max_card = max(max_card, int(count))
            n_rows += 1

    assert n_rows == n_rows_hdr, f'row count mismatch: header says {n_rows_hdr}, read {n_rows}'
    cards = cards[:n_rows]

    print(f'rows read: {n_rows} (matches header: {n_rows == n_rows_hdr})')
    print(f'strictly-ascending violations: {bad_ascending}')
    print(f'out-of-range (>= n_bits={n_bits}) violations: {bad_range}')

    mean_card = total_set / max(1, n_rows)
    density = mean_card / max(1, n_bits)
    top1pct_n = max(1, n_rows // 100)
    sorted_cards = np.sort(cards)[::-1]
    top1pct_mass = sorted_cards[:top1pct_n].sum() / max(1, total_set)

    print()
    print(f'universe m (n_bits)     : {n_bits}')
    print(f'rows (variant sites)    : {n_rows}')
    print(f'total set bits          : {total_set}')
    print(f'mean |Xi|               : {mean_card:.3f}')
    print(f'mean density            : {density:.8f}')
    print(f'max |Xi|                : {max_card}')
    print(f'top-1% rows ({top1pct_n}) hold : {100*top1pct_mass:.2f}% of all elements')
    print()
    regime_ok = (n_bits >= 1_000_000) and (density <= 1e-3)
    print(f'regime check: universe >= 1e6 AND mean density <= 1e-3 : '
          f'{"PASS" if regime_ok else "FAIL"}')
    print(f'  universe >= 1e6 : {n_bits >= 1_000_000} ({n_bits})')
    print(f'  density <= 1e-3 : {density <= 1e-3} ({density:.8f})')

    ok = (bad_ascending == 0) and (bad_range == 0) and (n_rows == n_rows_hdr)
    print()
    print('STRUCTURAL VERIFICATION:', 'PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
