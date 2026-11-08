# Vendored crates

Five Rust crates copied from GalleryBrowser into `crates/` on branch `rust-refactor`. They are **forked,
not linked**: this repo owns the copy and edits it directly.

| Item | Value |
|---|---|
| Source | `https://github.com/NickPittas/GalleryBrowser` (`origin/main`) |
| Source SHA at time of copy | `55a44836d44dd5db5f079c42ce86f712f7360309` |
| Copied paths | `crates/core/`, `crates/media/`, `crates/player/`, `crates/fileops/`, `crates/library/` — each in full (`Cargo.toml`, `src/`, and `tests/` where present) |
| Copy mechanism | `cp -r`, then verified byte-identical with `diff -rq` against the source checkout (no diffs) |
| License | MIT (GalleryBrowser `LICENSE`, crate manifests `license.workspace = "MIT"`) |
| Third-party decoders | Not copied. `filmcraft-*`, `photocraft-*`, `lightcraft-*` stay pinned **git** dependencies at their original revs (`[workspace.dependencies]` in the root `Cargo.toml`); MIT OR Apache-2.0, see GalleryBrowser's `NOTICE` |
| Pinned ArtCraft revs (verified identical to source lockfile) | filmcraft `70f0fc7a75f2681bc18302e67eaf0c9d41c68873` · photocraft `872748fb5b2a53e09dd4e385e31968d985c0515b` · lightcraft `5fe74557eca64638b1f9015c9769f7e52a889fa2` |

Not copied: `crates/ui-egui` and `apps/gallerybrowser` (their egui frontend is GalleryBrowser's own; the
KAssetManager port writes its own app under `apps/` in a later milestone), and `craft-ui`.

## Divergences from upstream

1. **Planned, not yet applied:** macOS trashing becomes a move to `~/.Trash` so undo works. Upstream
   deliberately makes trashing non-undoable on macOS (`crates/fileops/src/worker.rs:336-345`, restore path
   at `:648-655`). This is the first and only planned code divergence — see `docs/RUST_PORT_PLAN.md`.
2. **Test-scope lint allows, applied where upstream left them out.** The workspace denies
   `unwrap_used` / `expect_used` / `panic` globally, and the `library` crate already handles its test
   module with `#[cfg(test)] #[allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]`
   (`crates/library/src/lib.rs:991`, same in upstream). The other four crates lacked that, so
   `cargo clippy --workspace --all-targets -D warnings` failed on test code. Applied the **same pattern**
   to 14 test scopes (7 inline `mod tests` blocks in core/fileops/media/player plus the test-only files
   `crates/media/tests/media.rs`, `crates/player/src/tests.rs`, `crates/player/src/testutil.rs`).
   Attributes only — no test body changed, and all 190 tests still pass. Consequence: CI needs just one
   strict clippy pass, and a future app binary is covered by the same wall.
3. **Repo-local toolchain pin.** `rust-toolchain.toml` (channel `1.99.1`, components clippy/rustfmt) is new
   in this repo and does not exist upstream; it selects the development toolchain, while
   `rust-version = "1.95"` stays the declared minimum for the crates.

## Update procedure

`diff -r` against the upstream checkout, apply the wanted hunks by hand, then record the new source SHA
here. Do not bump ArtCraft revs during a crate sync — bump them deliberately and re-run the M2 playback
gate (`docs/RUST_PORT_PLAN.md`).
