#!/usr/bin/env python3
"""Stream bcftools GT output into the STORMBIN flat binary format.

Reads one line per variant, each a tab-separated list of phased genotypes like
`0|1`. Emits one row per variant: the sorted positions of haplotypes carrying
the ALT allele.

Streams rather than buffering: chr20 is ~1.8M variants x 5008 haplotypes, and
materializing that as Python objects would need far more memory than the matrix
itself.
"""
import struct, sys

out_path = sys.argv[1]
max_rows = int(sys.argv[2]) if len(sys.argv) > 2 else 0

n_rows = 0
n_bits = 0
total_set = 0
with open(out_path, 'wb') as f:
    f.write(b'STORMBIN' + struct.pack('<IIII', 1, 0, 0, 0))   # header patched at the end
    for line in sys.stdin:
        gts = line.rstrip('\n').rstrip('\t').split('\t')
        if not gts or gts == ['']:
            continue
        if n_bits == 0:
            n_bits = 2 * len(gts)
        pos = []
        h = 0
        for g in gts:
            # '0|1', '1|0', '1|1', '0|0'; unphased '/' also handled. '.' -> absent.
            if len(g) >= 3:
                if g[0] == '1': pos.append(h)
                if g[2] == '1': pos.append(h + 1)
            h += 2
        f.write(struct.pack('<I', len(pos)))
        if pos:
            f.write(struct.pack('<%dI' % len(pos), *pos))
        total_set += len(pos)
        n_rows += 1
        if n_rows % 100000 == 0:
            print(f'  {n_rows} variants...', file=sys.stderr, flush=True)
        if max_rows and n_rows >= max_rows:
            break
    f.seek(8)
    f.write(struct.pack('<IIII', 1, n_rows, n_bits, 0))

print(f'wrote {out_path}: {n_rows} rows x {n_bits} haplotypes, '
      f'{total_set} set bits, mean cardinality {total_set/max(1,n_rows):.1f} '
      f'(density {total_set/max(1,n_rows)/max(1,n_bits):.6f})', file=sys.stderr)
