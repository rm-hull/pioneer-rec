---
sessionId: session-261025-125358-j569
---

# Requirements

### Overview
Port the `pioneer-rec` C project from Windows-only (MSVC/Visual Studio 6) to build and run on macOS/Linux using gcc.

### Current State
- Single source file `pioneer_rec.c` with heavy Windows API dependency
- MSVC inline assembly for byte-swapping (`B2N_32`, `B2N_16`)
- Windows file I/O (`CreateFile`, `ReadFile`, `WriteFile`, `SetFilePointer`, etc.)
- Windows directory traversal (`FindFirstFile`, `FindNextFile`)
- Windows types (`DWORD`, `WORD`, `BYTE`, `HANDLE`, `BOOL`, `LARGE_INTEGER`)
- Windows privilege APIs (`OpenProcessToken`, `AdjustTokenPrivileges`)
- Windows string conversion (`MultiByteToWideChar`, `WideCharToMultiByte`)
- Windows path separators (`\`)
- Build via `.dsp`/`.dsw` (Visual Studio 6)
- **Critical 64-bit portability bugs** (will cause silent misbehavior on macOS/Linux):
  - `FNTENTRY` structs use `long unk1` — 8 bytes on LP64 (macOS/Linux), 4 bytes on Win32, shifting all subsequent fields and corrupting parsed data
  - `*((unsigned long*)buf)` reads 8 bytes on LP64 instead of 4, so `VOB_MAGIC` comparison never matches
  - `DWORD` as `unsigned long` has the same 64-bit size problem
  - `block * BLOCK_SIZE` overflows 32-bit when computing offsets for images >4 GB
  - Unparenthesized macros like `FNT_OFFSET BLOCK_SIZE+32+...` can produce wrong values
  - Unaligned pointer casts like `*((unsigned long*)&buf[4])` can fault on some architectures

### Goals
- Build with `gcc` on macOS and Linux via a `Makefile`
- Preserve all existing functionality (extract, merge, list, dump modes)
- Support all four DVR model variants (520, 545, 550, 633) via `-DDVR=N` flag
- **Ensure Sony RDR-HDX995 support** — it uses the `dvr550h.h` header; the `make dvr550` target must build and run correctly
- Use POSIX APIs for file I/O, directory traversal, and byte-swapping

### Out of Scope
- Windows build support (the `.dsp`/`.dsw` files can remain but won't be modified)
- Cross-compilation from Linux to Windows


# README and Documentation

### README.md Updates
The current README is Windows-focused with Windows-style path examples. It needs:

**Recovery Workflow Section** — Add a step-by-step reference workflow at the top of the README so users can follow it directly:
- `ddrescue -r3 /dev/sdX hdd.img hdd.map` — image the PATA disk first
- `make dvr550` — build for your recorder model (e.g., dvr550 for Sony RDR-HDX995)
- `./pioneer_rec_550 -l hdd.img` — list recordings, compare to recorder menu
- `./pioneer_rec_550 -d hdd.img ./dump` — dump recordings (filesystem-aware)
- `-p` (extract + merge by SCR) is the fallback if directory structures are damaged
- `ffprobe ./dump/*/*.mpg` — verify output files
- Explain each step briefly (ddrescue for imaging, `-l` for listing, `-d` for filesystem-aware dump, `-p` as fallback for damaged filesystems)
- Note that the recorder model determines which `make` target to use
- Mention `ffprobe` for verifying the extracted MPEG files

**Other README changes:**
- macOS/Linux build instructions (`make`, `make dvr520`, etc.)
- Updated usage examples with POSIX-style paths (`./pioneer_rec -d image.dd ./dump`)
- Build requirements (gcc, make)
- Keep the existing compatibility matrix and project description

### AGENTS.md
Add an `AGENTS.md` constitution file at the project root with:
- Project purpose and context
- Coding conventions (C99, POSIX APIs, no Windows-specific code)
- Build instructions
- File structure overview
- Porting guidelines for future changes


# Technical Design

### Current Implementation
The code in `pioneer_rec.c` is structured around three main operations:
- **`extract()`** — scans a disk image for MPEG chunks and writes them to files
- **`merge()`** — merges MPEG chunks by SCR timestamp in a directory
- **`dir()`** — parses the Pioneer DVR filesystem to list/dump recordings

All file I/O uses Windows API (`CreateFile`/`ReadFile`/`WriteFile`/`SetFilePointer`/`GetFileSize`/`CloseHandle`). Directory traversal uses `FindFirstFile`/`FindNextFile` with `WIN32_FIND_DATA`. Byte-swapping uses MSVC inline assembly. Types like `DWORD`, `WORD`, `BYTE`, `HANDLE`, `BOOL`, `LARGE_INTEGER` come from `windows.h`.

### Proposed Changes

**1. Replace Windows types with fixed-width POSIX equivalents**
- `BYTE` → `uint8_t`, `WORD` → `uint16_t`, `DWORD` → `uint32_t`
- `BOOL` → `int` (or keep as `int` with `TRUE`/`FALSE` macros)
- `HANDLE` for files → `int` (file descriptor)
- `LARGE_INTEGER` → `off_t`
- `HMODULE` → `void*` (for `dlopen`)

**1a. Fix 64-bit portability bugs in structs and type usage**
- Replace all `long` fields in header structs (`FNTENTRY`, `FATENTRY`, `FOTENTRY`) with `int32_t` to ensure consistent 4-byte layout across platforms. Affected fields: `long unk1` in `dvr633h.h`, `dvr520h.h`, `dvr545h.h`, `dvr550h.h`; `long unk1` in `dvr545h.h` FNTENTRY
- Replace all `unsigned long` casts for VOB_MAGIC checks with `uint32_t`: `*((unsigned long*)buf)` → `*((uint32_t*)buf)` (lines 190, 193, 869 of `pioneer_rec.c`)
- Replace `unsigned long` parameters in `B2N_32()` with `uint32_t`
- Cast `block * BLOCK_SIZE` to `uint64_t` before multiplication to avoid 32-bit overflow for images >4 GB (e.g., `offset.QuadPart = (uint64_t)B2N_32(fntaddr.block) * BLOCK_SIZE`)
- Wrap all offset macros in parentheses: `FNT_OFFSET`, `FAT_OFFSET`, `FOT_OFFSET` in all header files (e.g., `#define FNT_OFFSET (BLOCK_SIZE+32+(3*sizeof(ADDRESS)))`)
- Replace unaligned pointer casts like `*((unsigned long*)&buf[4])` with `memcpy`-based reads to avoid alignment faults on strict architectures

**2. Create `portable.h` header with type definitions and byte-swap macros**
- Create `portable.h` including `<stdint.h>`, `<stdbool.h>`, `<arpa/inet.h>`
- Define `DWORD` as `uint32_t`, `WORD` as `uint16_t`, `BYTE` as `uint8_t` to preserve the original code's naming
- Define `B2N_32(x)` as `ntohl(x)` and `B2N_16(x)` as `ntohs(x)` — these correctly convert from network (big-endian) to host byte order on both little-endian and big-endian hosts, since the on-disk fields are big-endian
- Include `portable.h` in `pioneer_rec.c` instead of `<windows.h>` and `<io.h>`

**3. Replace Windows file I/O with POSIX**
- `CreateFile()` → `open()`/`fopen()`
- `ReadFile()` → `read()`/`fread()`
- `WriteFile()` → `write()`/`fwrite()`
- `SetFilePointer()` → `lseek()`
- `GetFileSize()` → `fstat()`
- `CloseHandle()` → `close()`
- `DeleteFile()` → `unlink()`

**4. Replace directory traversal**
- `FindFirstFile`/`FindNextFile`/`FindClose` → `opendir()`/`readdir()`/`closedir()`
- `WIN32_FIND_DATA` → `struct dirent`

**5. Replace Windows-specific APIs**
- `FormatMessage`/`GetLastError` → `perror()`/`strerror(errno)`
- `CreateDirectory` → `mkdir()`
- `SetFileTime` → `utimensat()` or `futimens()`
- `MultiByteToWideChar`/`WideCharToMultiByte` → `mbstowcs()`/`wcstombs()` or remove wide char handling (simplify for POSIX)
- `LoadLibrary`/`GetProcAddress`/`FreeLibrary` → `dlopen`/`dlsym`/`dlclose` (or remove — `PathCleanupSpec` is Windows-only, the custom `myPathCleanupSpec` already provides the fallback)
- `OpenProcessToken`/`AdjustTokenPrivileges` → remove (backup privileges are Windows-only, not needed on POSIX)
- `LocalFileTimeToFileTime` → remove (POSIX uses UTC directly)
- `access()` from `<io.h>` → `access()` from `<unistd.h>`

**6. Path separators**
- Replace hardcoded `\\` with `/` (POSIX standard)

**7. Header files**
- Update `dvr*.h` files to use `uint8_t`/`uint16_t`/`uint32_t` instead of `BYTE`/`WORD`/`DWORD`
- Add `#include <stdint.h>`
- Replace all `long` fields with `int32_t` to ensure consistent struct layout on LP64 (macOS/Linux) vs Win32
- Wrap all offset macros (`FNT_OFFSET`, `FAT_OFFSET`, `FOT_OFFSET`) in parentheses to prevent operator precedence bugs

**8. Makefile**
- Create a `Makefile` with `CC ?= cc`, `CFLAGS += -std=gnu99 -Wall -Wextra -D_FILE_OFFSET_BITS=64`
- Default target builds all four variants: `pioneer_rec_520`, `pioneer_rec_545`, `pioneer_rec_550`, `pioneer_rec_633`
- Variant targets: `pioneer_rec_%` rule with `-DDVR=$*`
- Add `check` target that builds with `-fsanitize=address,undefined` for validation
- Add `clean` target
- Usage: `make pioneer_rec_550` for Sony RDR-HDX995

**9. Add `_Static_assert` struct size validation**
- Add `_Static_assert(sizeof(FNTENTRY) == 256, ...)` and similar for `FATENTRY` (128), `FOTENTRY` (32), `ADDRESS` (8), `TIMESTAMP` (8), `BLISTENTRY` (8) to catch layout mismatches at compile time
- Verify arithmetic matches the 2048-byte block maths (8 FNT entries and 16 FAT entries per block)

**10. Fix latent bugs (Step 7)**
- **Format-string mismatches**: `"Cannot write to destination dir %s: "` at lines 174 and 380 is passed two arguments but has only one `%s`. The final progress printf in `extract()` at line 225 is missing `imgsz` arguments. `sprintf(pFile, find.cFileName)` at lines 269 and 280 uses a filename as a format string.
- **Unsequenced read/write**: `vob=ReadFile(hFile,&vob,...)?B2N_32(vob):0` at line 274 reads and writes `vob` in the same expression. Same pattern with `vob2` at line 286.
- **`progname[64]` NUL-termination**: The `progname` field may not be NUL-terminated when read from disk, causing buffer overreads in `printf`/`snprintf`.
- **`case 'l'` double increment**: Lines 996–998 call `argv[++i]` twice, incrementing `i` past the source file argument on the fallback path.
- **Multi-part recordings**: `concat.txt` only writes entries for `k > 0`, so the first file (`00000000.mpg`) is never listed.
- **`concat.bat` → `concat.sh`**: Replace the Windows batch script with a POSIX shell script using `ffmpeg -f concat -safe 0 -i concat.txt -c copy out.vob`.
- **Compiler warnings**: Resolve all `-Wall -Wextra` warnings including packed-member address warnings (use `memcpy`), sign-comparison warnings, and format specifier mismatches.

### Key Decisions
- **Use raw file descriptors with `rd_at()` helper**: The Win32 code repeats `SetFilePointer(...) && ReadFile(...)` many times. Introduce a single `static bool rd_at(int fd, uint64_t off, void *buf, size_t n)` helper using `pread()` — this avoids the seek-then-read race and is cleaner than `FILE*` with `fseeko`/`ftello`. Output files use `fopen`/`fwrite`.
- **Remove wide char / `PathCleanupSpec` dependency**: The `myPathCleanupSpec` function already handles invalid filename characters. The wide char conversion via `MultiByteToWideChar`/`WideCharToMultiByte` is primarily for Windows codepage handling. On POSIX, we can simplify by working with UTF-8 directly and using `myPathCleanupSpec` on regular strings.
- **Remove backup privilege code**: The `SE_BACKUP_NAME` privilege is Windows-specific. On POSIX, file access is controlled by standard Unix permissions — no equivalent privilege escalation is needed.
- **Port function by function**: Order by dependency: `dumpFile()` → `extract()` → `dir()` (start with `-l` which is read-only) → `merge()` → `main()`. This allows incremental testing as each function lands.


# Delivery Steps

###   Step 0: Housekeeping — convert line endings and add .gitignore
Normalize repository file formatting so subsequent diffs are clean and build artifacts are ignored.
- Convert all source files (`pioneer_rec.c`, `dvr520h.h`, `dvr545h.h`, `dvr550h.h`, `dvr633h.h`, `README.md`) from CRLF to LF line endings using `sed -i '' 's/\r$//'` or `dos2unix`
- Create `.gitignore` with entries for the build artifact (`pioneer_rec`), object files (`*.o`), and common macOS/IDE artifacts (`*.swp`, `.DS_Store`)
- Leave `.dsp`/`.dsw` files in the repository untouched (they are source-level build configs, not artifacts)
- **After completing this step, perform a git commit**


###   Step 1: Replace Windows types, inline assembly, and fix 64-bit portability bugs
Make the codebase portable by removing Windows-specific type definitions, MSVC inline assembly, and fixing critical 64-bit portability bugs that cause silent misbehavior on macOS/Linux.

**Type replacements:**
- Replace `BYTE`/`WORD`/`DWORD`/`BOOL`/`HANDLE`/`LARGE_INTEGER`/`HMODULE` with POSIX equivalents (`uint8_t`, `uint16_t`, `uint32_t`, `int`, `int` fd, `off_t`, `void*`)
- Replace `#include <windows.h>` and `#include <io.h>` with `<stdint.h>`, `<unistd.h>`, `<dirent.h>`, `<sys/stat.h>`, `<sys/types.h>`, `<fcntl.h>`, `<errno.h>`, `<dlfcn.h>`
- Create `portable.h` with POSIX type definitions (`uint32_t` as `DWORD`, `uint16_t` as `WORD`, `uint8_t` as `BYTE`) and byte-swap macros using `ntohl()`/`ntohs()` from `<arpa/inet.h>` (portable across endianness)
- Include `portable.h` instead of `<windows.h>` and `<io.h>`
- Update `B2N_32()` parameter from `unsigned long` to `uint32_t`
- Remove `#define snprintf _snprintf` (not needed on POSIX)
- Remove `WINAPI` calling convention from function pointer typedefs

**64-bit portability fixes (critical for correct runtime behavior):**
- Replace all `long` fields in header structs with `int32_t` to ensure consistent 4-byte layout on LP64 (macOS/Linux) vs Win32. Affected: `long unk1` in `FNTENTRY` across all four headers (`dvr633h.h`, `dvr520h.h`, `dvr545h.h`, `dvr550h.h`)
- Replace `*((unsigned long*)buf)` with `*((uint32_t*)buf)` for VOB_MAGIC checks (lines 190, 193, 869 of `pioneer_rec.c`) to read exactly 4 bytes instead of 8 on LP64
- Replace unaligned pointer casts like `*((unsigned long*)&buf[4])` with `memcpy`-based reads (e.g., `uint32_t val; memcpy(&val, &buf[4], 4); vob = B2N_32(val)`) to avoid alignment faults on strict architectures
- Cast `block * BLOCK_SIZE` to `uint64_t` before multiplication to avoid 32-bit overflow for images >4 GB (e.g., `offset.QuadPart = (uint64_t)B2N_32(fntaddr.block) * BLOCK_SIZE`). Affected locations: all `offset.QuadPart *= BLOCK_SIZE` patterns in `pioneer_rec.c`
- Change `dwOffset` in `main()` from `DWORD` to `uint64_t` so `-o` can accept offsets beyond 4 GB
- Wrap all offset macros in parentheses in all header files: `FNT_OFFSET`, `FAT_OFFSET`, `FOT_OFFSET` (e.g., `#define FNT_OFFSET (BLOCK_SIZE+32+(3*sizeof(ADDRESS)))`) to prevent operator precedence bugs
- Update header files (`dvr520h.h`, `dvr545h.h`, `dvr550h.h`, `dvr633h.h`) to use `stdint.h` types and add `#include <stdint.h>`
- **After completing this step, perform a git commit**


###   Step 2: Port file I/O and directory traversal to POSIX APIs
Replace all Windows API calls with POSIX equivalents throughout the code. Port function by function in dependency order: `dumpFile()` → `extract()` → `dir()` (start with `-l` which is read-only) → `merge()` → `main()`.

**I/O layer:**
- Introduce `static bool rd_at(int fd, uint64_t off, void *buf, size_t n)` helper using `pread()` — replaces all `SetFilePointer`+`ReadFile` patterns and avoids seek-then-read races
- Image file: `CreateFile`/`SetFilePointer`/`ReadFile`/`GetFileSize` → `open(O_RDONLY)` + `rd_at()` + `fstat`/`lseek(SEEK_END)` for size
- Output files in `extract()`: `CreateFile` with `CREATE_NEW` → `open(O_WRONLY|O_CREAT|O_EXCL, 0666)` with `EEXIST` retry loop; `CREATE_ALWAYS` → `fopen(path, "wb")`
- Replace `WriteFile`/`CloseHandle` with `fwrite`/`fclose` for output files

**Directory and filesystem:**
- Replace `FindFirstFile`/`FindNextFile`/`FindClose` with `opendir`/`readdir`/`closedir` (skip `.`/`..`, require regular files via `stat`)
- Replace `CreateDirectory` with `mkdir(p, 0777)`, ignoring `EEXIST`
- Replace `DeleteFile` with `unlink`
- Replace `SetFileTime`/`SystemTimeToFileTime`/`LocalFileTimeToFileTime` with `mktime` (with `tm_isdst = -1`) + `utimensat` (birth time cannot be set; acceptable)
- Replace `FormatMessage`/`GetLastError` in `ShowError()` with `strerror(errno)`
- Replace `access()` from `<io.h>` with `access()` from `<unistd.h>`
- Replace all `\\` path separators with `/`

**Remove Windows-specific code:**
- Remove `LoadLibrary`/`GetProcAddress`/`FreeLibrary` for `PathCleanupSpec` (not available on POSIX; the custom `myPathCleanupSpec` handles invalid chars)
- Remove `OpenProcessToken`/`LookupPrivilegeValue`/`AdjustTokenPrivileges` (Windows-specific backup privileges)
- Remove `MultiByteToWideChar`/`WideCharToMultiByte` wide char path logic — use `myPathCleanupSpec` on regular strings with pass-through bytes for `progname`
- **After completing this step, perform a git commit**


###   Step 3: Create Makefile and verify build
Add a Makefile to build the project with gcc on macOS/Linux.
- Create `Makefile` with `CC ?= cc`, `CFLAGS ?= -O2 -g`, `CFLAGS += -std=gnu99 -Wall -Wextra -D_FILE_OFFSET_BITS=64`
- Default target builds all four variants: `pioneer_rec_520`, `pioneer_rec_545`, `pioneer_rec_550`, `pioneer_rec_633`
- Pattern rule: `pioneer_rec_%: pioneer_rec.c dvr%h.h` with `$(CC) $(CFLAGS) -DDVR=$* -o $@ $<`
- Add `check` target that builds with `-fsanitize=address,undefined` for ASan/UBSan validation
- Add `clean` target
- Usage: `make pioneer_rec_550` for Sony RDR-HDX995
- Verify the build compiles without errors on the current system
- **After completing this step, perform a git commit**


###   Step 4: Revamp README and add AGENTS.md
Update project documentation for macOS/Linux users and add an agent constitution.
- Revamp `README.md` with build instructions (`make`, `make dvr520`, etc.)
- **Add explicit Sony RDR-HDX995 section** — explain that `make dvr550` builds the variant for the HDX995, and include a usage example
- Add a **Supported Devices** section listing all Pioneer and Sony models, with the HDX995 prominently featured
- Replace Windows path examples (`c:\image.dd`) with POSIX examples (`./image.dd`)
- Add build requirements section (gcc, make)
- Keep existing compatibility matrix and project description
- Create `AGENTS.md` with project purpose, coding conventions, build instructions, and file structure
- **After completing this step, perform a git commit**


###   Step 5: Add GitHub Actions CI build workflow
Add continuous integration that builds and smoke-tests the project on multiple platforms.
- Create `.github/workflows/build.yml`
- Trigger on push and pull_request events
- Matrix: `ubuntu-latest` and `macos-latest` × models `520 545 550 633`
- Run `make` for each model variant, then `make check` (ASan/UBSan build)
- Add a smoke test: generate a small file of 2048-byte blocks beginning `00 00 01 BA` (+ big-endian counter), run `-x`, assert `.mpg` output exists
- **After completing this step, perform a git commit**


###   Step 6: Add `_Static_assert` struct size validation
Add compile-time assertions to catch struct layout mismatches early.
- Add `_Static_assert(sizeof(FNTENTRY) == 256, "FNTENTRY size mismatch")` and similar for `FATENTRY` (128), `FOTENTRY` (32), `ADDRESS` (8), `TIMESTAMP` (8), `BLISTENTRY` (8)
- Verify arithmetic matches the 2048-byte block maths (8 FNT entries and 16 FAT entries per block)
- Test with `make check` to ensure assertions pass under sanitizer builds
- **After completing this step, perform a git commit**

###   Step 7: Fix latent bugs and resolve all compiler warnings
Fix format-string bugs, unsequenced access, NUL-termination issues, and other latent bugs identified by code review. Resolve all `-Wall -Wextra` warnings.

**Format-string mismatches:**
- Line 174 and 380: `"Cannot write to destination dir %s: "` is passed two arguments (`pszDstDir, strerror(errno)`) but has only one `%s` — add a second `%s` for the error message: `"Cannot write to destination dir %s: %s"`
- Line 225: final progress printf in `extract()` has format specifiers `%X%08X/%X%08X` but is missing the `imgsz` arguments — add `imgsz.HighPart, imgsz.LowPart` to match the pattern used at line 188
- Line 269: `sprintf(pFile, find.cFileName)` uses a filename as a format string (format-string vulnerability) — change to `sprintf(pFile, "%s", find.cFileName)`
- Line 280: same issue with `sprintf(pFile, find2.cFileName)` — change to `sprintf(pFile, "%s", find2.cFileName)`

**Unsequenced read/write of `vob`:**
- Line 274: `vob=ReadFile(hFile,&vob,sizeof(vob),&read,NULL)?B2N_32(vob):0` reads and writes `vob` in the same expression without a sequence point — split into two statements: first read into a temporary `DWORD vob_raw`, then `vob = ReadFile(...) ? B2N_32(vob_raw) : 0`
- Line 286: same pattern with `vob2` — apply the same fix

**`progname[64]` NUL-termination:**
- The `progname` field in `FNTENTRY` is a 64-byte char array that may not be NUL-terminated when read from disk — add explicit NUL-termination before any `printf` or `snprintf` that uses it (e.g., `fnt->progname[63] = '\0'` after reading, or in `printFNTEntry()` before use)

**`case 'l'` double increment of `i`:**
- Lines 996–998: `argv[++i]` is called twice (once in the `dir()` call, once in the fallback `dir()` call) — the fallback path increments `i` past the source file argument. Fix by using a local variable: `char *src = argv[++i];` then pass `src` to both `dir()` calls

**Multi-part recordings — `concat.txt` omits the first file:**
- Lines 556–564: the `concat.txt` file is only written when `k > 0`, so `00000000.mpg` (the first part) is never listed in the concat file. Verify against a real multi-part recording before changing, but the likely fix is to write the first entry when `k == 0` as well

**Replace `concat.bat` with `concat.sh`:**
- Lines 567–573: replace the generated `concat.bat` with a POSIX-compatible `concat.sh` that runs `ffmpeg -f concat -safe 0 -i concat.txt -c copy out.vob`
- Make the script executable (`chmod +x`) and use a `#!/bin/sh` shebang

**Resolve all `-Wall -Wextra` warnings:**
- Fix packed-member address warnings (e.g., taking the address of a packed struct member for `ReadFile`) by using `memcpy` instead of passing pointers directly
- Fix any sign-comparison warnings between signed/unsigned types
- Fix unused parameter warnings
- Ensure all format strings use correct specifiers (e.g., `PRIu64` for `uint64_t` values, or cast to `unsigned long long` with `%llu`)
- **After completing this step, perform a git commit**
