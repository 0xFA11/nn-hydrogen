# hydrogen — working notes

## What this repo is

`hydrogen.c` and `hydrogen.h` are [libhydrogen](https://github.com/jedisct1/libhydrogen)
flattened by hand into two files, so it can be dropped into a project without
carrying a source tree. That is the *whole* purpose: same library, one
translation unit.

It is **a vendor, not a fork.** Nothing here is meant to differ from upstream
except the flattening itself. If you find yourself wanting to change
behaviour, the change belongs upstream in jedisct1/libhydrogen, or in
[proton](https://github.com/mas-bandwidth/proton) — not here.

## The standing job: track upstream

Upstream moves; this file does not, unless someone carries the changes across.
Because the flattening is manual, **upstream commits do not merge** — each one
has to be read and applied by hand into the corresponding place in
`hydrogen.c`.

That means the risk here is silent staleness: the file keeps compiling and
keeps working, while fixes accumulate upstream that nobody notices. This is
crypto, so that matters more than it would elsewhere.

**How to check where we are:**

```
git clone --depth 100 https://github.com/jedisct1/libhydrogen.git /tmp/libhydrogen
cd /tmp/libhydrogen && git log --oneline --since=<date the source last changed>
```

Use the date of the last commit that touched `hydrogen.c` here, not the last
commit in this repo — README edits do not move the source.

**Then triage each upstream commit into one of three buckets:**

1. **Core** — touches `impl/hydrogen_p.h`, `impl/gimli-*`, `impl/core*`,
   `impl/hash*`, `impl/kx*`, `impl/secretbox*`, `impl/sign*`. These affect
   every consumer. Carry them.
2. **Platform RNG** — `impl/random/<platform>.h`. This flattening carries
   ALL platform branches (Linux, Windows, Linux-kernel, ESP32, STM32,
   Zephyr, nRF52832, RT-Thread, AVR, Arduino, pico-sdk), so these apply even
   though the projects using this file today are desktop and server. Carry
   them, and say in the review log which platform each affects.
3. **Docs, CI, badges, version strings in `library.properties`** — skip, and
   note that you skipped them.

**Record what you did.** A review log — every upstream release read, what was
in it, what was applied, and what was deliberately *not* applied and why — is
the only thing that makes the next check cheap. Keep exactly one copy of that
log, here, in the repo that owns the vendoring. Two copies of a review log is
two copies of one truth, and the copy nobody updates is the one people read.

## State as of 2026-08-13

The flattened source last changed **2025-10-31**. Upstream has **26 commits**
since. Triaged:

**Core — worth carrying:**
- `4bcc4b4` Optimize Gimli for aarch64 — adds `impl/gimli-core/aarch64.h` and
  branches to it from `impl/gimli-core.h`. Gimli is the permutation everything
  here is built on, so this is the one with real performance meaning on Apple
  Silicon and ARM servers. It is also the most invasive to flatten, since it
  is a *new file* rather than an edit.
- `cb202c9` Remove duplicate `hydro_secretbox_MACBYTES` definition — harmless
  duplicate, trivial to carry.

**Platform RNG — one of these matters more than it looks:**
- `44fddd3` **linux_kernel** — `get_random_bytes(&ctx.state, …)` →
  `get_random_bytes(ctx.state, …)`. `state` is `uint8_t
  state[gimli_BLOCKBYTES]`, so both spellings pass the same address: this is
  type correctness, **not** a behavioural bug. Carry it, do not panic about
  it. But note WHICH path it is in — `#if defined(__linux__) &&
  defined(__KERNEL__)` is the in-kernel build, and that is exactly the path
  **proton** depends on, since proton is a kernel module. The kernel RNG path
  in this file is not a dusty embedded corner; it is load-bearing for the XDP
  design. Watch it more closely than the rest.
- STM32F4/STM32L4 hangs on HAL error and RNG-init failure (`f3ab14c`,
  `dd93aa5`, `24b3f52`), RT-Thread unchecked RNG failure deterministically
  seeding the PRG (`d00b0c5`), ESP32 init failing open without verified
  entropy (`5eabaff`), AVR watchdog not restored (`33bce64`), Arduino
  unconditional delay breaking the ESP32 contract (`2de57c6`), nRF52832
  hashing more than it read (`1c23dbc`), Zephyr `rand32.h` → `random.h`
  (`cc776d5`).
  Several of these are real robustness bugs — an RNG that fails open, or a
  PRG seeded deterministically on error, is serious *on those platforms*.
  None is reachable from a desktop or server build.

**New platform, optional:**
- `5469506` + `319313d` pico-sdk support. Only worth flattening if someone
  wants this on a Pico.

**Skip:** CI permissions, badges, links, copyright year, `library.properties`.

## Who uses this

The flattened file is vendored into the private **flow** networking library as
`flow_hydrogen.cpp` — byte-for-byte the same 3,116 lines. flow is where the
consequences of a stale vendor would actually be felt, so re-vendor there
after carrying anything across.

[proton](https://github.com/mas-bandwidth/proton) is a *different* derivation:
hydrogen built as a **Linux kernel module**, exposing kfuncs that XDP programs
call. Note what that is NOT — the crypto is not compiled to eBPF bytecode and
does not face the verifier; it is ordinary native kernel code. The discipline
there is the **kfunc contract**: BTF registration, the rules for pointers
arriving from a BPF program, `__arg` annotations, and lifetime.

That is also the real reason libsodium cannot follow this path while
libhydrogen can. It is not about the verifier — it is that libhydrogen already
builds inside the kernel (upstream carries `impl/random/linux_kernel.h`, and
this file has the `__KERNEL__` branch at lines 24 and 677), whereas libsodium
assumes libc, `mmap` and `mlock` and has no in-kernel build at all.

An upstream fix landing here may or may not apply to proton — check proton's
own build rather than copying the patch across. But upstream changes to the
`__KERNEL__` RNG path are the ones most likely to matter to it.
