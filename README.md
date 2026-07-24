# ArtfulType

A distraction-free Markdown editor for classic 68k Macintoshes — System 6 & 7, on a compact Mac or over [BlueSCSI](https://bluescsi.com).

![ArtfulType running in Writer mode](screenshot1.png)

> **A fork of [ActionRetro/ArtfulType](https://github.com/ActionRetro/ArtfulType)** by Action Retro (Sean Malseed). Same app, same spirit — this fork adds strikethrough and nested styles, printing, System 6 support, a host-tested core under CI, and bootable disk images built on Linux with no Mac (see [Under the hood](#under-the-hood)). The *ArtfulType* name, icon, and artwork remain © Sean Malseed and are **not** covered by the code's GPLv3 — see [ASSETS_LICENSE](ASSETS_LICENSE).

Video overview: [Artful Type demo](https://youtu.be/HEheu_r9UGw)

## Features

One document, two views toggled from the View menu — **Writer** (Markdown rendered live as styled text) and **Markdown** (the raw source). Saved files are always plain `.md`, editable anywhere.

- **Live styling** — bold, italic, `code`, headings, links, **strikethrough**, and **`==highlight==`**, as you type or from the Style menu ★
- **Block Markdown** — horizontal rules (`---`/`***`/`___`), blockquotes (`> `, nestable), fenced code blocks (```` ``` ````), and lists — bullets (`- `/`* `/`+ ` → •), numbered (`1. `), nested, and task checkboxes (`- [ ]`/`- [x]`) — all rendered live in Writer mode, all round-tripping to clean Markdown ★
- **Styles nest and combine** — a struck bold word, a bold link, a highlighted phrase, `***bold italic***` — round-tripping losslessly between Writer and Markdown ★
- **Print** — Page Setup and Print through the classic Printing Manager — the formatted Writer view or the raw Markdown source — on System 6 and System 7, both tested on a real LaserWriter ★
- **Links** — type `[text](url)` inline, or select text → Style → Link
- **Full-screen or windowed** — stay in the distraction-free full-screen page, or switch to a real resizable, draggable window from the View menu ★
- **A real Preferences dialog** — set the default window mode, view, body font, and zoom; remembered between launches on **both System 6 and 7** ★
- **Find & Replace** and **Word Count** — from the Edit menu (⌘F / ⌘G) ★
- **Clean imports** — opening a file written on a modern machine folds its UTF-8 smart quotes, dashes and line endings to Mac equivalents ★
- **`@today` / `@time`** — type either and it expands to the current date/time ★
- Cut/Copy/Paste with Markdown preserved, 15 levels of Undo/Redo, and Home/End/Page-key scrolling
- A well-behaved classic app — Apple-menu About and desk accessories, MultiFinder-friendly, preferences in the System Folder

★ = new in this fork.

## Under the hood

The ★-marked features above (printing included) are new in this fork; beneath them it is also a near-total engineering rework:

- **A pure, testable core.** The Markdown strip / emit / detect / paginate logic lives in `mdcore` — Toolbox-free C, run under a host unit-test harness and gated by **CI** (host tests, `cppcheck`, and a real 68k build on every push).
- **Correctness & robustness.** Hardened Memory Manager usage, a guarded 32K TextEdit limit that stops silent save failures, and reclaimed link IDs so long sessions don't exhaust the table.
- **Runs on System 6, not just 7.** System 7-only Toolbox traps are gated behind a runtime version check, so ArtfulType works on original 68000 hardware like the Mac SE — including **preferences persistence**, which locates the System Folder via the `BootDrive` low-memory global on System 6 (the way Inside Macintosh prescribes) rather than the System-7-only `FindFolder`.
- **Ported to Apple's MPW/Universal interfaces** — the real Apple headers, not the open-source multiversal ones.
- **Bootable disk images built on Linux — no Mac.** Volumes are formatted and *blessed* entirely in software: an 800K floppy (raw + DiskCopy 4.2), a 20 MB volume, and a BlueSCSI/PiSCSI `.hda`. A tag-triggered workflow builds and publishes them all.
- **[floppy-writer](tools/floppy-writer/)** — a tiny companion app that writes a bootable ArtfulType floppy from a bare Mac (e.g. a Mac SE with only a System), so hardware with no other tooling can make its own boot disk.

## Get it

Every disk image on the [**releases page**](https://github.com/matthewdeaves/ArtfulType/releases) boots straight into System 6.0.8 with ArtfulType on it — no separate system install needed.

| You have | Use |
|---|---|
| **[BlueSCSI](https://bluescsi.com) / PiSCSI** | `HD1_ArtfulType.hda` — copy to the SD card as-is (the `HD1_` prefix assigns SCSI ID 1). 20 MB, room to save your work. |
| **A flux floppy writer** (Greaseweazle, Applesauce…) or **Disk Copy 4.2** | `ArtfulType-800K.dsk` (raw) or `ArtfulType-800K.image` (DiskCopy 4.2) |
| **Mini vMac** (Mac Plus config) | `ArtfulType-20MB.dsk` or `ArtfulType-800K.dsk` |
| **Basilisk II / SheepShaver** | `ArtfulType.bin` — add the app to your existing System; decode the MacBinary with Stuffit Expander or [binUnpk](https://www.gryphel.com/c/minivmac/extras/binunpk/) (ships as a plain zipped disk image, so nothing needs decoding to bootstrap it) |
| **A bare Mac, no imaging tools** | Run [floppy-writer](tools/floppy-writer/) — it carries the 800K image and writes a bootable floppy itself |

## Using it

Write in **Writer** mode (Markdown hidden, text shown styled); switch to **Markdown** mode for raw-syntax editing. Toggle either from the View menu.

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

Built with [Retro68](https://github.com/autc04/Retro68). This fork compiles against **Apple's MPW/Universal interfaces**, so it needs a Retro68 built with those rather than the stock multiversal ones (CI uses a container image that ships them); see [`app/CMakeLists.txt`](app/CMakeLists.txt) and [`CLAUDE.md`](CLAUDE.md). Host tests for the pure core run anywhere with a native C compiler: `make -C tests check`.

**Disk images:** [`build-boot-images.sh`](build-boot-images.sh) builds every bootable image from scratch on Linux — it formats a fresh HFS volume, installs a System Folder from the committed System 6.0.8 base ([`disk-base/`](disk-base/)), and *blesses* it in software ([`tools/bless_hfs.py`](tools/bless_hfs.py) writes the boot blocks and blessed-folder ID that `hformat` alone doesn't). The [release workflow](.github/workflows/release.yml) runs the same script, so tagging `vX.Y.Z` builds and publishes every image. Needs `hfsutils`, [`djjr`](https://diskjockey.onegeekarmy.eu/djjr/), and `python3`.

## License

**Code:** GPLv3 — see [LICENSE](LICENSE), inherited from the upstream project.

**Creative assets** (the ArtfulType name, icon, and artwork): all rights reserved by Sean Malseed (Action Retro) — see [ASSETS_LICENSE](ASSETS_LICENSE). These are **not** covered by the GPLv3 and are not relicensed by this fork.

## AI disclaimer

Claude Code was used in the creation of this software.
