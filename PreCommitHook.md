# Pre-commit hook

Every commit on this repository runs through a single shell-script
hook that checks the staged C/C++ files with `clang-format` and
`clang-tidy`. The hook lives **in the repository itself** rather
than in `.git/hooks/`, so every clone — plain CLI, CLion, VSCode,
CI — runs the same checks without anyone curating per-clone files.

## Installing

After cloning, run once:

```bash
scripts/install-hooks
```

This points `git config core.hooksPath` at `scripts/git-hooks/`,
which is the directory git looks in for hook scripts going forward.
The script is idempotent (rerun any time), and as a courtesy it
removes any stale `.git/hooks/pre-commit` symlink left over from
the pre-`core.hooksPath` workflow — git ignores `.git/hooks/`
entirely once a custom hooks path is set, so the cleanup isn't
load-bearing, just tidy.

## What it checks

`scripts/git-hooks/pre-commit` is a symlink to `scripts/check-format`.
On every commit:

- **`clang-format --dry-run --Werror`** runs against every staged
  C/C++ file (`*.c`, `*.cpp`, `*.cc`, `*.cxx`, `*.h`, `*.hpp`).
  A diff against `.clang-format` blocks the commit.
- **`clang-tidy --quiet`** runs against every staged `.cpp` /
  `.cc` / `.cxx` / `.c` file using the build directory's
  `compile_commands.json`. The project's `.clang-tidy` configures
  `WarningsAsErrors: '*'`, so any tidy diagnostic blocks the
  commit too.

Files under `tests/` are **skipped by `clang-tidy`** — gtest macros
trip rules like `bugprone-unchecked-optional-access` (the
`EXPECT_EQ` inside an `ASSERT_TRUE`-guarded block confuses the
nullness analysis) and `bugprone-easily-swappable-parameters` fires
on every `TEST()`. Tests still go through `clang-format`.

The hook locates `compile_commands.json` by walking
`cmake-build-gnu/`, `cmake-build-llvm/`, `cmake-build-gnu-tidy/`,
`cmake-build-llvm-tidy/` in that order and using the first one it
finds. **You must have configured at least one preset** for the
hook to work; otherwise it errors loudly with a hint.

## Running it manually

`scripts/check-format` is the same script the hook invokes. Run it
directly any time to dry-run the same checks against your staged
changes:

```bash
scripts/check-format         # check only — same as the hook
scripts/check-format --fix   # apply clang-format -i and re-stage
```

`--fix` is a convenience for the manual flow: it runs
`clang-format -i` on every staged file, `git add`s the formatted
result back into the index, then re-runs the checks. `clang-tidy`
is **not** auto-fixed by `--fix` — tidy diagnostics usually need
eyeballing; the dedicated `fix-tidy` CMake target is the right tool
when you really want it (`cmake --build cmake-build-gnu --target
fix-tidy`).

`--fix` is silently ignored when the script runs as a git hook (it
detects the hook context by the presence of `GIT_INDEX_FILE`,
which git sets for any hook that touches the index). Auto-mutating
the index mid-commit is surprising even when technically harmless,
and a developer who wants the auto-fix flow runs `scripts/check-format
--fix` themselves before committing.

## Adding a new hook

Drop a script into `scripts/git-hooks/` named after the git hook
you're targeting (`commit-msg`, `pre-push`, `pre-rebase`, …) and
make it executable. After a fresh `scripts/install-hooks`, the new
hook is live for everyone with the next clone.

The convention in this repo is for hooks to be thin wrappers
(symlinks or two-line shell scripts) over a checked-in script in
`scripts/`. That keeps the hook surface tiny and the actual logic
runnable manually outside of git.

## When the hook blocks a commit

The script exits non-zero with a hint:

```
Commit blocked.
Re-run `scripts/check-format --fix` to apply clang-format,
fix any clang-tidy diagnostics by hand, and stage the changes again.
```

For a clang-format violation, `scripts/check-format --fix` is the
fastest path forward. For a clang-tidy diagnostic, fix it by hand
or invoke `cmake --build <build-dir> --target fix-tidy` and
re-stage the result.

The hook deliberately has no `--no-verify` carve-out documented
here. `git commit --no-verify` does still bypass it (git itself
provides the flag), but doing so will fail CI on the same checks,
so it only buys you a moment.
