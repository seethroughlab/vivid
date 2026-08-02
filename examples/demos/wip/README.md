# Work-in-progress demos (not bundled, not shippable as-is)

These are dev-path demos parked out of the shipped example set (**UX audit Ph6 F1**). Each one runs on
the author's machine but **breaks on a clean checkout and/or cannot legally ship** in the product,
because it depends on media that is dev-local or not redistributable. The curated, self-contained
demos that *do* ship live in `../projects/` and are copied into the app's **File ▸ Open Example** list.

Only `bloom/` is committed to git; the rest are dev-local and intentionally **not committed** (their
assets are non-redistributable — see below). Nothing here is copied into the app bundle: the build
copies `../projects/` only, so this directory needs no CMake denylist.

**Revived (no longer here):** `drift` and `grid` — their non-redistributable Dan Mayo drum loops were
replaced with royalty-free loops bundled under `<project>/media/` (project-relative), the dead
paid-plugin "EZdrummer 3" track was dropped, and both were promoted to `../projects/`. (Their melodic
tracks were already Surge, not the paid plugins their labels implied.)

## Why each is parked

| Demo | Blocker | Path to shipping |
| --- | --- | --- |
| `bloom` | References a title-text file (`BLOOM`) by an **absolute path into a stale git worktree**. Also known to be visually weak (a near-static loop). Has a nice project-local C++ operator (`petals.cpp`), so it's worth reviving as an operator example. | Inline the text (or bundle it project-relative), then re-evaluate the visuals. |
| `chop` | `src_path` points at `examples/demos/media/break90.wav` (dev-repo media, **not bundled**; license unverified). | Bundle a **known-licensed** drum loop into the project folder + a relative ref. |
| `mirror` | References a title-text file (`MIRROR`) by an absolute dev-repo path. | Inline the text (or bundle it project-relative). |
| `signal` | Uses the **`frank` 3D model, licensed CC-BY-NC-SA-4.0** (non-commercial) — cannot ship in the product — plus a dev-repo `loop.mp4` referenced by absolute path. | Swap in a commercially-licensed model + bundle a licensed video, project-relative. |

## Reviving one

To promote a demo back into the shippable set:

1. Make every media reference **project-relative** and copy the media **into the project folder** (so
   the folder is self-contained), replacing any non-redistributable / non-commercial asset with a
   properly-licensed one.
2. Move the folder to `../projects/<name>/`.
3. Confirm it loads cleanly (File ▸ Open Example) with no absolute paths and no missing media.
