# Two audiences, one driver source

`synaptics-00dc` is for users of the known sensor: C driver, build, installation,
offline tests, support scope. REA-FS is for investigation: panel, supervision,
protocol research, private experiments and the driver as an example.

The driver is a Git submodule at `drivers/syna_tod`. REA-FS records its URL and
exact commit instead of another copy of its files. A sibling development clone
can live in the maintainer's `github/` directory. Two checkouts are not two source
projects: publish driver changes there and update the parent pin deliberately.

```sh
# First checkout or missing submodule
git submodule update --init --recursive

# To develop inside the submodule, create a branch rather than committing detached
git -C drivers/syna_tod switch -c codex/my-driver-change

# After driver changes have been reviewed, committed and pushed:
git add drivers/syna_tod
git commit -m "Update Synaptics driver revision"
```

Do not use `git submodule update --remote` indiscriminately: it changes the version
under test. Commit/push the driver before publishing a parent pin. A sibling clone
and a submodule do not synchronize uncommitted edits automatically.

## Why keep Rust for the GUI?

The working egui UI already has event state tests and native rendering. Rewriting
it in Python would reduce the number of languages but would require a GUI toolkit
and retesting process supervision, state transitions and desktop integration.
PySide6 also brings Qt/Shiboken runtime dependencies; Tk is a different visual and
interaction implementation. Fewer languages is not automatically a smaller app.

Rust/Cargo is a build dependency; the launcher now allows an up-to-date panel
binary to run without Cargo. The standalone driver requires neither Rust nor the
Python experiment supervisor. This boundary reduces the end user's dependencies
without an unnecessary rewrite. RAM/CPU savings have not been measured.

## Folder choices

Keep the existing `panel`, `tools`, `scripts`, `tests`, `docs`, and `examples`
boundaries. Broad renaming would break historical experiment paths without adding
much clarity. Driver-specific installation and test maintenance belongs in the
driver repository. Historical research/checkpoints belong here. Private session
data stays ignored; it is not moved into the submodule's tracked files.
