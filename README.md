# ArtfulType

A distraction-free Markdown writing app for classic 68k Macintosh computers (System 6/7), built to run from a [BlueSCSI](https://bluescsi.com) device on a Mac Plus or similar compact Mac.

![ArtfulType running in Writer mode](screenshot1.png)

## Features

- **Writer mode** — live Markdown-to-rich-text formatting as you type (bold, italic, code, headings, links)
- **Markdown mode** — plain raw-syntax editing
- Links: type `[text](url)` inline, or select text and use Style → Link
- Cut/Copy/Paste and multi-level Undo/Redo, with standard keyboard shortcuts
- Adjustable zoom, remembered between launches
- Save/Open plain Markdown files via the classic File Manager
- A well-behaved classic app: About and desk accessories in the Apple menu, MultiFinder-friendly (dims quietly in the background), zoom preference stored in the system Preferences folder

Video overview: [Artful Type demo](https://youtu.be/HEheu_r9UGw)

## Getting Started

Every disk image on the [releases page](https://github.com/matthewdeaves/ArtfulType/releases) boots straight into System 6.0.8 with ArtfulType on it — no separate system install needed. If your Mac can use [BlueSCSI](https://bluescsi.com), use the BlueSCSI image; if it can't (or you just want a physical floppy), use the 800K floppy image instead.

### Real hardware with BlueSCSI

1. Copy `HD1_ArtfulType.hda` onto your BlueSCSI SD card — the `HD1_` prefix is BlueSCSI's naming convention for assigning an image to SCSI ID 1, so no renaming is needed. (See [BlueSCSI](https://bluescsi.com) for how to set up and image an SD card for your specific BlueSCSI hardware.)
2. Boot the Mac. The Finder appears as usual — double-click **ArtfulType** to launch it. The 20 MB volume leaves plenty of room to save your documents.

### Real hardware without BlueSCSI

Write a bootable 800K floppy and boot from it directly — no BlueSCSI required. Two forms of the same floppy are provided:
- `ArtfulType-800K.dsk` — a raw disk image, for a flux-level floppy writer (Greaseweazle, FluxEngine, Applesauce).
- `ArtfulType-800K.image` — the same floppy in DiskCopy 4.2 format, to write from **Disk Copy 4.2** on any working Mac.

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

Built with [Retro68](https://github.com/autc04/Retro68), a GCC-based cross-compiler for classic Mac OS. See `app/CMakeLists.txt` for the build configuration.

### Disk images

`build-boot-images.sh` builds every bootable disk image from scratch on Linux — no Mac required. It formats a fresh HFS volume, installs a System Folder from the committed System 6.0.8 base (`disk-base/`), and *blesses* the volume in software (`tools/bless_hfs.py` writes the boot blocks and the blessed-folder ID that `hformat` alone doesn't). The [release workflow](.github/workflows/release.yml) runs the same script, so tagging `vX.Y.Z` builds and publishes all images automatically. It needs `hfsutils`, [`djjr`](https://diskjockey.onegeekarmy.eu/djjr/), and `python3`.

`deploy.sh` / `build-bluescsi-image.sh` / `package-release.sh` are the older pipeline that deploys onto pre-built base images under `vmac/` (e.g. for a fast Mini vMac test loop); `build-boot-images.sh` supersedes them for producing release images.

## License

Code: GPLv3 — see [LICENSE](LICENSE).

Creative assets (the ArtfulType name/branding, icon, and artwork): all rights reserved — see [ASSETS_LICENSE](ASSETS_LICENSE).

## AI Disclaimer

Claude Code was used in the creation of this software.
