#!/bin/sh
# Run the manuscript-architecture referential-integrity audit correctly.
#
# Why this wrapper exists. The checker resolves \input relative to the file
# doing the including, but this manuscript uses paths relative to paper/
# (\input{figures/fig1_overview_body} from inside sections/results_a.tex).
# Run bare, the checker therefore never descends into theory_block.tex or any
# figure body, and silently UNDER-reports. Passing every .tex explicitly is
# also wrong in the opposite direction: each file is then treated as its own
# document, so every legitimate cross-file \ref is reported as DANGLING (58
# phantom failures, all of which pdflatex resolves without complaint).
#
# The fix is a symlink mirror in which paper-root-relative paths resolve from
# any depth, so the checker walks exactly the document pdflatex builds.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
checker="$HOME/.claude/skills/manuscript-architecture/scripts/check_display_items.py"

[ -f "$checker" ] || { echo "checker not found: $checker" >&2; exit 2; }

mirror=$(mktemp -d)
trap 'rm -rf "$mirror"' EXIT

cp "$here/storm.tex" "$mirror/"
cp -R "$here/sections" "$here/figures" "$mirror/"

# Make root-relative \input paths resolve from within any subdirectory.
for d in "$mirror/sections" "$mirror/figures"; do
    ln -sfn "$mirror/sections" "$d/sections"
    ln -sfn "$mirror/figures"  "$d/figures"
done

cd "$mirror"
python3 "$checker" storm.tex "$@"
