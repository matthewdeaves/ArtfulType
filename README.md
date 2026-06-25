# ArtfulType

A distraction-free Markdown writing app for classic 68k Macintosh computers (System 6/7), built to run from a [BlueSCSI](https://bluescsi.com) device on a Mac Plus or similar compact Mac.

## Features

- **Writer mode** — live Markdown-to-rich-text formatting as you type (bold, italic, code, headings, links)
- **Markdown mode** — plain raw-syntax editing
- Links: type `[text](url)` inline, or select text and use Style → Link
- Cut/Copy/Paste and multi-level Undo/Redo, with standard keyboard shortcuts
- Adjustable zoom, remembered between launches
- Save/Open plain Markdown files via the classic File Manager

## Getting Started

### On real hardware (BlueSCSI)

1. Write `ArtfulType.hda` to your BlueSCSI SD card as the device image for your Mac. (See [BlueSCSI](https://bluescsi.com) for how to set up and image an SD card for your specific BlueSCSI hardware.)
2. Boot the Mac. ArtfulType launches automatically in place of the Finder.
3. To write a physical 800K floppy: open `Utilities/Disk Copy 4.2` (already on the disk image), and use it to write `ArtfulType 800K` (also already on the disk image, in proper DiskCopy 4.2 format) to a blank floppy in your Mac's floppy drive.

### In an emulator (Mini vMac)

For trying ArtfulType without real hardware, use [Mini vMac](https://www.gryphel.com/c/minivmac/) configured for a Mac Plus, with either:
- `ArtfulType-20MB.dsk` — the full HD setup (System 7.1, stripped down, with the app, Disk Copy, and the embedded floppy image)
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
| Bold / Italic / Code | ⌘B / ⌘I / ⌘K |
| Heading 1 / 2 / 3 | ⌘1 / ⌘2 / ⌘3 |
| Link | ⌘L |
| Zoom In / Out / Default | ⌘= / ⌘- / ⌘0 |

## Known Limitations

- Double-clicking a `.md` file in the Finder to launch ArtfulType doesn't work yet.
- Saving writes to the boot volume's own HFS filesystem — there's no direct-to-SD-card save that bypasses the disk image.
- No 1.44MB floppy image yet, so SuperDrive-equipped Macs (SE/30, IIx, and later) aren't supported as a separate target — use the 800K floppy or the 20MB BlueSCSI image instead.
- Copying styled text (bold/italic/etc.) in Writer mode and pasting it back loses the styling — only plain text round-trips through the clipboard, a limitation of the cross-compiler toolchain this app is built with.

## Building

Built with [Retro68](https://github.com/autc04/Retro68), a GCC-based cross-compiler for classic Mac OS. See `app/CMakeLists.txt` for the build configuration, and `deploy.sh` / `build-bluescsi-image.sh` / `package-release.sh` for the build-to-disk-image pipeline.

## License

GPLv3 — see [LICENSE](LICENSE).

## AI Disclaimer

Claude Code was used in the creation of this software.
