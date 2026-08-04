# libalgebra — vendored

Upstream: <https://github.com/mklarqvist/libalgebra>
Vendored from commit `bff182e8f603d6bfe87fa3bbac19738be5a3d42c` (2019-08-20).
Licence: Apache-2.0 (see `LICENSE`). Header-only — only `libalgebra.h` is used.

## Why vendored rather than a submodule

The submodule pinned an upstream commit that **does not compile on arm64**. The fix below lived
only in the local working tree, so the pin still referenced broken code and a fresh clone failed
to build. Committing the fix inside the submodule would have been worse: it would create a SHA
that exists on one machine, and `git submodule update` would fail with *"did not contain \<sha\>"*.

libalgebra is a single header, authored by the same person as this repository, dormant since
2019, and its kernels are scheduled for replacement by the pairing-matrix work
(`PROBLEM_STATEMENT.md`). A submodule bought nothing but friction.

## Local patch

One change against upstream `bff182e`:

```diff
 // portable version of  posix_memalign
-#ifndef _MSC_VER
-#include <x86intrin.h>
-#endif
+#include <stdlib.h>
+
+#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
+#ifndef _MSC_VER
+#include <x86intrin.h>
+#endif
+#endif
```

Upstream guarded `<x86intrin.h>` on `_MSC_VER` alone — a test of the *compiler*, not the *target
architecture*. Any non-x86 target built with a non-MSVC compiler (clang on arm64, for instance)
pulled in the x86 intrinsic headers and failed with *"This header is only meant to be used on x86
and x64 architecture"*.

It also relied on `<x86intrin.h>` to transitively declare `posix_memalign` and `free` — the
comment in the original acknowledges the resulting implicit-declaration warning. `<stdlib.h>` is
the correct header and is now included directly.

If this is ever pushed upstream, the patch applies cleanly to `bff182e`.

## Known limitation

**There is no NEON code path.** Despite the file header listing NEON among the supported
instruction sets, the word appears only in that comment — there is no `arm_neon.h` include and no
NEON implementation anywhere in the file. On AArch64 the library is correct but runs scalar
fallbacks. See `../LANDSCAPE.md` §8.2.
