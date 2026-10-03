#!/usr/bin/env bash
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2

# Guard VMAF_VERSION against silently degrading to a bare commit abbreviation.
#
# core/include/meson.build derives VMAF_VERSION from
#   git describe --tags --long --match 'v*.*.*'
# and meson substitutes the vcs_tag `fallback` whenever that command fails
# (mesonbuild/scripts/vcstagger.py catches any exception from the subprocess).
#
# Adding `--always` breaks that contract: git then exits 0 even with no
# reachable tag and prints a bare abbreviated object name, which becomes
# VMAF_VERSION verbatim. Every version surface — `vmaf --version`, the JSON and
# XML `version` field, vmaf_version(), the pkg-config metadata consumers read —
# then reports something like "abafdfc" instead of a version, on any shallow CI
# checkout, tarball export, or worktree whose .git is a file.
#
# The failure is silent and intermittent rather than deterministic: it only
# becomes *visible* when the seven-character abbreviation happens to contain no
# ASCII digit, which core/test/test_output.c::test_vmaf_version asserts against.
# That is roughly one commit in a thousand — (6/16)^7 — so the defect can sit in
# tree for months and then fail an unrelated PR. It did: merge commit
# abafdfcc3c8ef40c369b4bb776c14188729ceada abbreviates to "abafdfc".
#
# The two tester publishing workflows derive the version of the published file
# and image from `git describe` too (docs/development/tester-image.md). They
# must keep the same contract: `--match 'v*.*.*'` (the tester prereleases tag
# master commits `tester-<date>-<sha8>`, and an unrestricted describe returns
# one of those) and no `--always`. They omit `--long` on purpose: a tagged
# commit yields the bare tag, as the documented `<tag>-tester` image requires.
set -euo pipefail
export LC_ALL=C

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

target=core/include/meson.build
fail=0

note() { printf '%s\n' "$*" >&2; }
bad() {
  note "error: $*"
  fail=1
}

if [ ! -f "$target" ]; then
  note "error: $target not found; cannot verify the version-string contract"
  exit 1
fi

# Isolate the vcs_tag(...) call so a stray '--always' elsewhere in the file
# (a comment explaining this very rule, for instance) is not mistaken for one
# inside the command array.
call=$(awk '
  /vcs_tag\(/           { depth = 1; buf = $0; next }
  depth > 0 {
    buf = buf "\n" $0
    n = gsub(/\(/, "(") ; depth += n
    n = gsub(/\)/, ")") ; depth -= n
    if (depth <= 0) { print buf; exit }
  }
' "$target")

if [ -z "$call" ]; then
  bad "$target contains no vcs_tag(...) call; VMAF_VERSION generation moved?"
  note "       If generation legitimately moved, update this gate to match."
  exit 1
fi

# Strip comments before matching so prose may discuss the banned flag freely.
code=$(printf '%s\n' "$call" | sed 's/#.*$//')

if printf '%s\n' "$code" | grep -q -- "--always"; then
  bad "$target passes --always to git describe."
  note "       With --always, a checkout that cannot reach a v*.*.* tag still"
  note "       exits 0 and yields a bare commit abbreviation, which becomes"
  note "       VMAF_VERSION verbatim. Drop --always so git fails and meson"
  note "       substitutes the fallback version instead."
fi

if ! printf '%s\n' "$code" | grep -qE '(^|[[:space:],])fallback[[:space:]]*:'; then
  bad "$target does not set an explicit vcs_tag fallback."
  note "       meson defaults it to meson.project_version(), but the fallback is"
  note "       load-bearing here: it is the entire tagless-checkout path. Spell"
  note "       it out so the intent survives a meson upgrade."
fi

if ! printf '%s\n' "$code" | grep -q -- "--match"; then
  bad "$target no longer restricts git describe with --match."
  note "       Without --match, any tag in the repository can supply the version."
fi

# Tester publishing workflows: every `git describe` in them is held to the
# same contract (comments stripped; a workflow with no describe at all means
# the derivation moved, so the gate would otherwise pass vacuously).
tester_workflows=(
  .github/workflows/macos-tester-bundle.yml
  .github/workflows/docker-publish-tester.yml
)
for wf in "${tester_workflows[@]}"; do
  if [ ! -f "$wf" ]; then
    bad "$wf not found; cannot verify its version-string contract."
    continue
  fi
  describes=$(sed 's/^[[:space:]]*#.*$//' "$wf" | awk '/git describe/')
  if [ -z "$describes" ]; then
    bad "$wf has no git describe; the version derivation moved? Update this gate."
    continue
  fi
  if printf '%s\n' "$describes" | grep -q -- "--always"; then
    bad "$wf passes --always to git describe (a bare sha becomes the version)."
  fi
  if printf '%s\n' "$describes" | grep -v -q -- "--match 'v\*\.\*\.\*'"; then
    bad "$wf has a git describe without --match 'v*.*.*'; a tester-<date>-<sha8> tag would supply the version."
  fi
done

if [ "$fail" -ne 0 ]; then
  note ""
  note "See core/include/meson.build and core/test/test_output.c::test_vmaf_version."
  exit 1
fi

printf 'check-vcs-version-not-bare-sha: OK (%s keeps VMAF_VERSION a real version; %s tester workflows checked)\n' "$target" "${#tester_workflows[@]}"
