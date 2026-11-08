# Shell sizing and scrolling (behaviour spec)

Status: **decisions settled with the user; R1-R6 implemented, a real-mouse drag check outstanding on R6.**

Decisions (asked and answered): names get a **horizontal scrollbar when they do not fit** - no ellipsis policy;
minimum widths stay **fixed constants in code** (tree 150, preview 180, pane 240), not Settings fields.

Revised after seeing it running: grid cells do **not** keep the requested thumbnail size and the grid does **not**
scroll sideways - tiles shrink to fit the pane; and scrollbars stay egui's floating kind rather than always-visible,
because allocated bars put a permanent scrollbar on every pane.
The point of this file is that panel behaviour is decided once, in writing, before code changes.

## Principle

1. **A panel's width is a decision, not a consequence.** Panels keep a minimum, a default, and a ceiling; they
   never widen or shrink because a folder name happened to be long. (egui already works this way: `SidePanel`
   takes its width from stored state or its default, clamped to the range - `containers/panel.rs`,
   `outer_size()`. Nothing in egui sizes a side panel to its content.)
2. **Content that does not fit gets a scrollbar, not silence.** A name that is wider than its panel is either
   scrollable to its end or visibly elided with the whole name available on hover - never cut mid-word with no
   way to read it. Which one, per surface, is question 1 below.
3. **Minimums are floors for the listing, not suggestions.** A pane must always be able to show something useful;
   a splitter or a docked pane may not starve its neighbour to zero.
4. **Toolbars wrap, listings scroll.** Toolbar buttons already wrap to two rows in a narrow tiled pane (that is
   deliberate - there is no scrollbar on a toolbar). Scroll affordances belong to the content areas.
5. **The user's own width wins and comes back next run.** Panel edges are draggable; the width you leave them
   at is the width you get tomorrow.

## Current state (measured in the code, not assumed)

| Surface | Width today | Overflow today |
| --- | --- | --- |
| Folder tree (`Panel::left("browser_tree")`, app.rs:4097) | resizable, 150-420, default 210 | ScrollArea is **vertical only**; rows are `selectable_label` with no truncation, so a long name is cut at the panel edge and cannot be scrolled to. The "add a favorite" hint label wraps to several lines. |
| Favorites (inside tree) | same panel | same clipping; a favorite shows only its file_name. |
| Preview (`Panel::right("preview")`, app.rs:4104) | resizable, 180-460, default 300 | Image is scaled to fit ✓. Status lines are plain `ui.label`, so long file names **wrap** and make the panel taller instead of scrolling. |
| Pane list view | inside pane, min 240 | `ScrollArea::both()` + per-column widths + truncating cells ✓ already correct: horizontal scroll exists, header stays pinned. |
| Pane grid view | inside pane, min 240 | Columns are computed as `floor(available_width / cell)` (app.rs:3010) so they fit - but with a large thumbnail edge in a narrow pane, one column of cells is **wider than the pane and clipped**, no horizontal scroll. |
| Breadcrumb path segments (pane toolbar) | inside pane | Segments are laid out left-to-right; a deep path runs past the pane edge with no scroll. The new editable path bar (app.rs:2579) shows the whole path in one field, which has the same problem when not editing. |
| Global app bar / status bar | full width | Fixed rows; the path button truncates only because egui clips it - no affordance to read the tail. |

So the complaint "panels resize themselves around names" is really two different defects: content **clipped with
no scroll** (tree, favorites, breadcrumb, wide grid cells) and content **wrapping** where a name should stay on
one line (preview).

## Proposed rules

- **R1 Tree + favorites.** One line per row, no wrapping. Panel keeps 150 minimum / 420 ceiling / draggable edge.
  Overflow handled by the policy chosen in question 1. The hint sentence stays allowed to wrap (it is prose, not
  a name).
- **R2 Grid (revised by the user after trying the scrollbar).** A grid tile never scrolls the pane sideways. Columns
  are counted from the thumbnail size in Settings, then a cell takes its share of the pane's width and no more, so a
  narrow pane gets **smaller tiles** - and a pane too narrow for the requested size still shows two columns rather
  than one tile the width of the pane. The grid's scroll area is vertical only.
- **R3 Preview.** Names and status lines stop wrapping: single line, overflow per question 1's policy. The image
  keeps fitting the panel. Minimum 180 stays.
- **R4 Panes and splitter.** Minimum 240 per pane stays; the docked second pane keeps its ceiling so it cannot
  starve the first. Nothing may drive a pane below its minimum, including a window resize (today `max_size` is
  recomputed from available width each frame - that cap must not push a pane under 240).
- **R5 Breadcrumb.** Long paths scroll sideways within their row (or elide the middle with the tail visible, per
  question 1's policy), and the editable path bar keeps showing the **end** of the path when it does not fit -
  that is where the useful part of a path is.
- **R6 Persistence.** Dragged widths persist across runs (tree, preview, pane splitter). egui keeps a dragged
  panel width only for the run it was given, and eframe's save hook was already seen not to fire on Wayland close,
  so the three widths live in `settings.json` beside the remembered folders: each panel is defaulted from its
  setting and measured back from its own laid-out rect every frame, and a change is written once the pointer has
  rested for three quarters of a second - not one disk write per dragged frame.

## Acceptance checks (how each rule gets verified)

- A tree at its default 210 px, with a folder whose name is 40 characters: the row shows an affordance (scroll
  bar or ellipsis + tooltip), and the panel is still 210 px wide - not wider.
- Same folder name in favorites: same behaviour as the tree row.
- Preview panel at 180 px with a long filename selected: panel height unchanged from an ordinary file's, name on
  one line.
- Grid with thumbnail edge 256 in a 240 px pane: sideways scrollbar appears; no cell is cut invisibly.
- Drag the tree edge to 300 and quit; next start opens at 300.
- Tiled window (929x1038 logical, as this box runs) with dual panes: both panes >= 240 px, tree >= 150 px, and
  nothing is clipped that the user cannot scroll to.

## The questions as asked (answered above)

1. **Overflow policy for names** (tree, favorites, preview, breadcrumb): a real horizontal scrollbar you can
   drag, or an ellipsis with the whole name in a tooltip? Explorer elides; a file *manager* panel you have to
   scroll feels heavy for 20 px of overflow. My recommendation: **elide + tooltip as the default, and let the
   breadcrumb and tree scroll only when there is no room for even the tail**. You asked for a scrollbar, so the
   honest alternative is "always scrollable" - your call.
2. **Grid with oversized thumbnails**: horizontal scrollbar (R2 as written), or clamp the cell to the pane width
   so it always fits and never scrolls?
3. **Minimum widths** (tree 150, preview 180, pane 240): keep as fixed constants, or expose them in Settings
   under Layout?

## What was built, and what the box proved

R1, R2, R3 and R5 are implemented; R4 needed no change (the minimums were already constants with a splitter cap).

- **Tree and favorites (R1).** The panel is still 150-420 px with a draggable edge, default 210. With folder names
  of ~50 characters the rows stay one line, the panel does not widen, and a horizontal scrollbar appears at the
  bottom of the tree whose handle is narrower than its track - content really is wider than the panel, and you can
  scroll to the end of it. (`/tmp/layout2-tree.png`)
- **Grid (R2, after revision).** Thumbnail edge 400 px in a pane far narrower than that: two columns of tiles
  shrunk to fit, fully inside the pane, nothing cut mid-image and no sideways scroll at all; at the usual 64 px the
  same pane shows four small columns. (`/tmp/grid-s400-wide.png`, `/tmp/grid-s64-crop.png`)
- **Preview caption (R3).** A 60-character video name was wrapping to two lines and making the panel taller; it is
  now one line, cut at the panel edge without an ellipsis, with a scrollbar under it you can drag to the end - and
  the panel height did not change. (`/tmp/layout6-preview.png`)
- **Breadcrumb (R5)** already scrolled before this change and still does; the global path button now sits in a
  scroll area that takes what is left of the row after Settings and Folders. At 929 px logical width today's path
  fits, so that bar is there by construction but was not exercised live.
- **List view unaffected:** pinned header still lines up with rows under always-visible bars. (`/tmp/layout6-list.png`)

### egui facts worth keeping (each cost a screenshot to learn)

- `SidePanel` width comes from stored state or its default, clamped to the range - it never sizes itself to
  content. So "panel resized itself around names" was really content clipped with no scroll, plus one caption that
  wrapped (`containers/panel.rs`, `outer_size`).
- egui 0.36 has no `ScrollbarVisibility`; bars are floating (overlay, shown while scrolling) by default. Making them
  always-visible means `style.spacing.scroll.floating = false` via `Context::all_styles_mut` (`style.rs`,
  `ScrollStyle::floating`). That was tried and **reverted at the user's request**: with bars allocated, every pane
  had a scrollbar on screen all the time, which reads as clutter. Bars float again and appear while you scroll.
- A label inside a horizontal `ScrollArea` still wraps at the viewport width, and `.truncate()` elides at it. To
  make text scrollable it has to extend the layout: `TextWrapMode::Extend` (`epaint`, `text_layout_types.rs`).

## Still open

- **R6 persistence, the drag itself.** Both ends of the persistence are demonstrated without a pointer: widths in
  `settings.json` are honored at startup, and a width that cannot be laid out (a second pane remembered at 4000 px
  in a window that fits 240) is written back as the 240 actually used. What has not been watched is a human dragging
  an edge - pointer injection on this box cannot produce one - so the gesture itself is still user-confirmed.
- **Where names are still only clipped:** the pane list truncates inside its column widths by design (columns are
  resizable and the table scrolls sideways), which is not the same defect as a panel sizing to a name.
