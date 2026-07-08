# ADR 0001 — Keep a standard menu bar; strip only what the Toolbox lets us

- **Status:** Accepted
- **Date:** 2026-07-07
- **Scope:** `MakeMenu` in `app/main.c`

## Context

The Artful Type is a distraction-free editor: a full-screen `plainDBox` window,
no chrome, nothing to look at but the text. The menu bar is the one piece of
system furniture that stays on screen, so it's a natural target for the
"remove everything that isn't the writing" instinct — the appeal of a bare, even
blacked-out, bar.

Pulling the other way is wanting this to be a *real* Mac application — a
"mac-assed Mac app" that honours the platform's conventions rather than fighting
them. On a classic Mac that means, above all, a working **Apple menu**: About,
and the installed desk accessories (`AppendResMenu 'DRVR'` + `OpenDeskAcc`). A
distraction-free app that can't open the Calculator or the Chooser, or that hides
the application switcher, isn't distraction-free — it's just broken and un-Mac-like.

So the question isn't "menu bar or no menu bar." It's: given that we keep a
standard, native menu bar, **how much of the system-owned right-hand side can we
actually remove**, and what are we forced to live with?

The Toolbox reality (Inside Macintosh VI, "With the System Menus"):

- On System 7 the Menu Manager's MBDF auto-inserts up to three system menus at
  the right of the bar: the **Application** (switcher) menu, the **Help** (`?`)
  menu, and — only when more than one script system is installed — a **Keyboard**
  menu. They appear the first time `DrawMenuBar` runs with an Apple menu present.
- The **Application menu is always displayed and cannot be removed.** Its constant
  presence is also what keeps the MBDF's "calc" routine from re-adding the full
  system set after we delete one.
- The **menu-bar clock (System 7.5+) is not a menu.** It's painted on a timer by
  the Date & Time control panel (the folded-in SuperClock) and has **no Toolbox
  off-switch** — an app cannot hide the clock without hiding the whole bar. It's
  turned off, if at all, by the user in Date & Time.
- System 6 has none of these: no Help menu, no switcher menu, no clock.

## Decision

Keep a **standard, native, visible menu bar with the Apple menu**, and remove only
what the Menu Manager actually permits — no custom MBDF, no painting over the bar,
no hidden bar.

Concretely, in `MakeMenu`:

1. Build the app's own menus (Apple, File, Edit, Style, View) and `DrawMenuBar`.
   That first draw is what triggers the MBDF to insert the system menus.
2. On System 7 only (`HasSystem7()`), `DeleteMenu(kHMHelpMenuID)` and `DrawMenuBar`
   again to drop the Help menu. It stays gone because the ever-present Application
   menu keeps "calc" from re-adding the set. `DeleteMenu`/`DrawMenuBar` are original
   traps, safe on every target; the System 7 gate is only because System 6 has no
   Help menu to remove.
3. Keep the Apple menu and the Application/switcher menu deliberately — they're
   what make this a good Mac citizen.
4. Accept the clock on System 7.5+ as outside our control.

## Alternatives considered

- **Hidden / blacked-out menu bar (full-screen takeover, or a custom MBDF that
  paints the bar black).** Rejected. It loses the Apple menu, desk accessories and
  the switcher — the things that make it a proper Mac app — and it's fragile: we'd
  be fighting the Menu Manager's own redraws, and on System 7.5+ the clock's timer
  repaints over anything we draw anyway. The minimal, native bar is both cleaner in
  spirit and far more robust than a hand-rolled one.
- **Removing the Application menu.** Not possible — it is always displayed (IM VI).
  Attempting it also removes the anchor that stops the Help/Keyboard menus from
  coming back.

## Consequences

- The bar is as quiet as the Toolbox allows: app menus plus the Apple and
  Application menus, Help gone on System 7.
- The app stays a first-class Mac citizen: Apple menu, desk accessories, and the
  MultiFinder switcher all work.
- On System 7.5+ the user's menu-bar clock may still show; that's their setting in
  Date & Time, not something the app can override, and we accept it.
- The **Keyboard menu** is *not* removed. It only appears under a multi-script
  (e.g. multiple non-Roman script systems) install, which is uncommon on the
  target vintage hardware; if it ever proves a problem it would be dropped the same
  way as Help, via its own menu ID.
- System 6 targets are unaffected — there are no system menus and no clock to
  begin with.
