# AGENTS.md — pioneer-rec

## Project Purpose

`pioneer-rec` recovers video recordings from failed Pioneer DVR and Sony HXD recorder hard drives by parsing the proprietary Pioneer DVR filesystem from a raw disk image.

## Architecture

- **Single source file**: `pioneer_rec.c` — all logic lives here.
- **Four header variants**: `dvr520h.h`, `dvr545h.h`, `dvr550h.h`, `dvr633h.h` — each defines the struct layout and offset constants for one recorder family. Selected at compile time via `-DDVR=<model>`.
- **`portable.h`**: Fixed-width type aliases (`DWORD`, `WORD`, `BYTE`), byte-swap macros (`B2N_32`, `B2N_16`), and safe unaligned read helpers (`read_u32`, `read_u16`). Included by each DVR header.
- **`Makefile`**: Builds all four variants with `gcc` on macOS/Linux.

## Key Constraints

1. **Struct layout is critical**. The packed structs (`FNTENTRY`, `FATENTRY`, `FOTENTRY`, `ADDRESS`, `BLISTENTRY`, `TIMESTAMP`) must match the on-disk binary layout exactly. Any change to field order, type size, or padding will corrupt parsing. Use `_Static_assert` to validate sizes.
2. **Big-endian on-disk format**. All multi-byte fields are stored big-endian. Use `B2N_32()` / `B2N_16()` to convert. Never read a field directly without byte-swapping.
3. **64-bit offsets**. Disk images can exceed 4 GiB. All file offsets must be `off_t` (64-bit). Use `fseeko`/`ftello`, not `fseek`/`ftell`.
4. **Unaligned access**. Packed struct members may be unaligned. Use `read_u32()` / `read_u16()` helpers, not direct pointer casts.
5. **POSIX only**. The codebase targets macOS and Linux. No Windows API calls.

## Build & Test

```sh
make all          # Build all four variants
make check        # Build with ASan/UBSan
./pioneer_rec -h  # Verify it runs
```

## Style

- Indent with tabs (matching existing code).
- No new tests required for pure refactors; add tests only when changing parsing logic.
- Commit with conventional commits, including `Co-authored-by: Junie <junie@jetbrains.com>`.
