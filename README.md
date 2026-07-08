# ArtfulType

A distraction-free Markdown writing app for classic 68k Macintosh computers (System 6/7), built to run from a [BlueSCSI](https://bluescsi.com) device on a Mac Plus or similar compact Mac.

![ArtfulType running in Writer mode](screenshot1.png)

> **This is a fork.** ArtfulType was created by **Action Retro (Sean Malseed)** — the original lives at **[ActionRetro/ArtfulType](https://github.com/ActionRetro/ArtfulType)**. This fork keeps the original's spirit and adds real strikethrough and nested inline styles, printing, a pure host-tested engine gated by CI, restored System 6 support, and a from-scratch bootable-disk build. See [**What this fork adds**](#what-this-fork-adds) for the full list. The *ArtfulType* name, icon, and artwork remain © Sean Malseed (Action Retro) and are **not** covered by the code's GPLv3 license — see [ASSETS_LICENSE](ASSETS_LICENSE).

## Features

- **Writer mode** — live Markdown-to-rich-text formatting as you type (bold, italic, code, strikethrough, headings, links)
- **Combine styles** — bold, italic, code, strikethrough and links nest freely (e.g. a struck bold word, or a bold link) and survive round-tripping between Writer and Markdown mode
- **Markdown mode** — plain raw-syntax editing
- Links: type `[text](url)` inline, or select text and use Style → Link
- **Print** — Page Setup and Print through the classic Printing Manager (works on System 6 and System 7)
- Cut/Copy/Paste and multi-level Undo/Redo, with standard keyboard shortcuts
- Adjustable zoom, remembered between launches
- Save/Open plain Markdown files via the classic File Manager
- A well-behaved classic app: About and desk accessories in the Apple menu, MultiFinder-friendly (dims quietly in the background), zoom preference stored in the system Preferences folder

Video overview: [Artful Type demo](https://youtu.be/HEheu_r9UGw)

## What this fork adds

Everything below is new relative to the [upstream project](https://github.com/ActionRetro/ArtfulType); the app's design and branding are unchanged.

**Editing & rendering**
- Real **strikethrough** (`~~text~~`) drawn in Writer mode, and inline styles that **nest and combine** — a struck bold word, a bold link, `***bold italic***` — all round-tripping losslessly between Writer and Markdown modes.
- **Printing**: Page Setup and Print via the classic Printing Manager, one code path on System 6 and System 7, with correct pagination across variable-height headings.

**Correctness & engineering**
- A pure, Toolbox-free core engine (**`mdcore`**) — the Markdown strip / emit / live-detect / paginate logic — extracted behind a **host unit-test harness** and gated by **CI** (host tests, `cppcheck`, and a real 68k build).
- Hardened Memory Manager usage, a guarded 32K TextEdit limit that stops silent save failures, and reclaimed link IDs so long sessions don't exhaust the table.
- Ported to Apple's **MPW/Universal interfaces**.

**Compatibility & citizenship**
- Runs again on **System 6** on real hardware (a Mac SE): System 7-only traps are gated behind a runtime version check.
- A well-behaved **System 7 / MultiFinder** citizen — standard system menu bar, hides quietly when suspended, zoom preference stored in the Preferences folder — with standard keyboard handling in the link dialog.

**Distribution & tooling**
- **Bootable disk images built from scratch on Linux** — no Mac required — formatted and *blessed* in software: an 800K floppy (raw + DiskCopy 4.2), a 20 MB volume, and a BlueSCSI/PiSCSI `.hda`.
- A **tag-triggered release workflow** that builds and publishes every image automatically.
- A **[floppy-writer tool](tools/floppy-writer/)** that writes a bootable ArtfulType floppy on a bare Mac (e.g. a Mac SE with only a System), plus a per-file custom icon so the app shows its icon without a Desktop database.

## Getting Started

Every disk image on the [releases page](https://github.com/matthewdeaves/ArtfulType/releases) boots straight into System 6.0.8 with ArtfulType on it — no separate system install needed. If your Mac can use [BlueSCSI](https://bluescsi.com), use the BlueSCSI image; if it can't (or you just want a physical floppy), use the 800K floppy image instead.

### Real hardware with BlueSCSI

1. Copy `HD1_ArtfulType.hda` onto your BlueSCSI SD card — the `HD1_` prefix is BlueSCSI's naming convention for assigning an image to SCSI ID 1, so no renaming is needed. (See [BlueSCSI](https://bluescsi.com) for how to set up and image an SD card for your specific BlueSCSI hardware.)
2. Boot the Mac. The Finder appears as usual — double-click **ArtfulType** to launch it. The 20 MB volume leaves plenty of room to save your documents.

### Real hardware without BlueSCSI

Write a bootable 800K floppy and boot from it directly — no BlueSCSI required. Two forms of the same floppy are provided:
- `ArtfulType-800K.dsk` — a raw disk image, for a flux-level floppy writer (Greaseweazle, FluxEngine, Applesauce).
- `ArtfulType-800K.image` — the same floppy in DiskCopy 4.2 format, to write from **Disk Copy 4.2** on any working Mac.

If all you have is a bare Mac with a System and the [Retro68](https://github.com/autc04/Retro68) app launcher, the [`tools/floppy-writer`](tools/floppy-writer/) app can write the floppy for you — it carries the 800K image inside itself and writes it to an inserted disk.

### In an emulator (Mini vMac)

For trying ArtfulType without real hardware, use [Mini vMac](https://www.gryphel.com/c/minivmac/) configured for a Mac Plus, with either:
- `ArtfulType-20MB.dsk` — a bootable 20 MB volume (System 6.0.8) with the app and room to work
- `ArtfulType-800K.dsk` — a bootable 800K floppy (System 6.0.8) with just the app

## Usage

ArtfulType has two views, toggled from the View menu:

- **Writer** (default) — markdown syntax is hidden; text is shown styled (bold, italic, headings, etc.)
- **Markdown** — the raw markdown source, unstyled

Saved files are plain `.md` text, editable in any text editor.

### Keyboard shortcuts

| Action | Shortcut |
|---|---|
| New / Open / Save | ⌘N / ⌘O / ⌘S |
| Quit | ⌘Q |
| Undo / Redo | ⌘Z / ⇧⌘Z |
| Cut / Copy / Paste | ⌘X / ⌘C / ⌘V |
| Select All | ⌘A |
| Bold / Italic / Code | ⌘B / ⌘I / ⌘K |
| Heading 1 / 2 / 3 | ⌘1 / ⌘2 / ⌘3 |
| Link | ⌘L |
| Zoom In / Out / Default | ⌘= / ⌘- / ⌘0 |

## Building

Built with [Retro68](https://github.com/autc04/Retro68), a GCC-based cross-compiler for classic Mac OS. This fork compiles against **Apple's MPW/Universal interfaces** (the real Apple headers), so it needs a Retro68 built with those rather than the stock open-source *multiversal* interfaces; CI uses a container image that ships them. See [`app/CMakeLists.txt`](app/CMakeLists.txt) for the build configuration and [`CLAUDE.md`](CLAUDE.md) for the toolchain notes.

Host unit tests for the pure core run anywhere with a native C compiler: `make -C tests check`.

### Disk images

[`build-boot-images.sh`](build-boot-images.sh) builds every bootable disk image from scratch on Linux — no Mac required. It formats a fresh HFS volume, installs a System Folder from the committed System 6.0.8 base ([`disk-base/`](disk-base/)), and *blesses* the volume in software ([`tools/bless_hfs.py`](tools/bless_hfs.py) writes the boot blocks and the blessed-folder ID that `hformat` alone doesn't). The [release workflow](.github/workflows/release.yml) runs the same script, so tagging `vX.Y.Z` builds and publishes all images automatically. It needs `hfsutils`, [`djjr`](https://diskjockey.onegeekarmy.eu/djjr/), and `python3`.

[`deploy.sh`](deploy.sh) / [`build-bluescsi-image.sh`](build-bluescsi-image.sh) / [`package-release.sh`](package-release.sh) are the older pipeline that deploys onto pre-built base images under `vmac/` (e.g. for a fast Mini vMac test loop); `build-boot-images.sh` supersedes them for producing release images.

[`tools/floppy-writer/`](tools/floppy-writer/) is a small companion app for making a physical floppy on a real Mac that has nothing but a System and the [Retro68](https://github.com/autc04/Retro68) app launcher: it embeds the 800K image and writes it to an inserted disk, so a bare compact Mac like the Mac SE can create its own bootable ArtfulType floppy. See [its README](tools/floppy-writer/README.md).

## License

Code: GPLv3 — see [LICENSE](LICENSE). This fork inherits the upstream project's GPLv3 license.

Creative assets (the ArtfulType name/branding, icon, and artwork): all rights reserved by Sean Malseed (Action Retro) — see [ASSETS_LICENSE](ASSETS_LICENSE). These are **not** covered by the GPLv3 and are not relicensed by this fork.

## AI Disclaimer

Claude Code was used in the creation of this software.
