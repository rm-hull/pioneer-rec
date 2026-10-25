# pioneer-rec (linux edition) ![tux](./docs/tux-80px.png)

> [!NOTE]
> This Windows-to-POSIX port was performed entirely with [Junie CLI](https://jetbrains.com/junie).

Recover recordings from failed Pioneer DVR and Sony HXD recorder hard drives.

> So there you are with your Pioneer DVR633H-S, DVR545H, DVR-550H, DVR-520H, or Sony HXD showing you an HDD Error and refusing to boot. This usually indicates that the hard drive in the recorder went bad. But don't panic — there may be hope!
>
> Unlike other utilities for this task, this program can reconstruct movies from the hard drive by parsing the filesystem structure of the Pioneer DVR instead of just dumping MPEG chunks.

## Installing

The quickest way is via [Homebrew](https://brew.sh/):

```sh
brew install rm-hull/tap/pioneer-rec
```

This installs all four variants: `pioneer_rec`, `pioneer_rec_520`, `pioneer_rec_545`, and `pioneer_rec_550`.

## Building

Requires `gcc` (or `clang`) and `make`.

```sh
# Default build (DVR-633H)
make

# Build for a specific recorder model
make pioneer_rec_550   # Sony RDR-HDX995, DVR-550H, etc.
make pioneer_rec_545   # DVR-545H, DVR-640H-S
make pioneer_rec_520   # DVR-520H

# Build all variants
make all

# Build with AddressSanitizer + UBSan (for debugging)
make check
```

## Quick Start

Assuming you have a disk image `image.dd` and a recovery destination directory `./dump`:

```sh
./pioneer_rec -d image.dd ./dump
```

## Usage

```
./pioneer_rec [-s<x|m> <SCR threshold>] [-o <Offset>] [-c <Start-Cnt>]
	<-l> <src file> <-D>
	<-d|-x|-p|-L> <src file> <dest dir>
	<-m> <dest dir>
```

| Option | Description |
|--------|-------------|
| `-l` | List names of recordings found in the image |
| `-L` | Create directories for found recordings |
| `-d` | Parse directory list and dump all movies (recommended) |
| `-x` | Extract MPEG chunks from image |
| `-m` | Merge MPEG chunks by SCR timestamp |
| `-p` | Process (extract + merge) — fallback for damaged images |
| `-D` | Use dub mode (keeps developer editings) |
| `-sx <n>` | SCR threshold for extract (default: 1536) |
| `-sm <n>` | SCR threshold for merge (default: 5) |
| `-o <offset>` | Start offset in source file |
| `-c <count>` | Start counter for MPEG file naming |

If your directory structure and image isn't garbled, use `-d`. In case your image is severely damaged, try `-p` instead.

## Recovery Workflow

```sh
# 1. Image the failing disk (do this first!)
ddrescue -r3 /dev/sdX hdd.img hdd.map

# 2. Build the correct variant for your recorder
make pioneer_rec_550

# 3. List recordings (compare to recorder menu)
./pioneer_rec_550 -l hdd.img

# 4. Dump recordings (filesystem-aware, recommended)
./pioneer_rec_550 -d hdd.img ./dump

# 5. If directory structures are damaged, use extract+merge fallback
./pioneer_rec_550 -p hdd.img ./extracted
./pioneer_rec_550 -m ./extracted

# 6. Verify output
ffprobe ./dump/*/*.mpg
```

## Compatibility Matrix

| Header | Pioneer | Sony |
|--------|---------|------|
| dvr520h.h | DVR-520H | |
| dvr545h.h | DVR-545H, DVR-640H-S | |
| dvr550h.h | DVR-550H | **RDR-HDX870**, RDR-HDX90, **RDR-HDX995**, RDR-HDX1070 |
| dvr633h.h | DVR-433H, DVR-530H, DVR-633H, DMR-XW300 | |

### Sony RDR-HDX995

The Sony RDR-HDX995 uses the **DVR-550H** filesystem format. Build with:

```sh
make pioneer_rec_550
```

Then use `./pioneer_rec_550` for all operations (`-l`, `-d`, `-x`, `-p`, `-m`).

## Multi-stream recordings

For recordings consisting of multiple streams, a `concat.sh` script is generated alongside the `.mpg` files:

```sh
cd ./dump/[001]Recording Name/
bash concat.sh
```

This produces `out.vob` using ffmpeg.

## Original Windows Build

The original `.dsp` / `.dsw` Visual Studio 6 project files are preserved in the `archive` folder for reference. This project now targets macOS and Linux via the `Makefile`.

## Links

- Discussion: [Blog post](http://hardwarefetish.com/584-pioneer-dvr-recorder-harddisk-recovery)
- Original README: [README.txt](archive/README.txt)
- Panasonic DVR recorders: [panasonic-rec](https://github.com/leecher1337/panasonic-rec)
