#!/bin/sh
# Fail if repo-internal references have leaked into the manuscript.
#
# Internal planning documents, scratch paths, and internal finding IDs are not
# citable and must never appear in manuscript prose -- not in body text, not in
# captions, and not inside \draftnote either. \draftnote vanishes outside draft
# mode, so it is safe for the SUBMITTED pdf, but it still renders in every draft
# a co-author reads, and a draft that cites RESEARCH_PLAN is a draft that
# teaches the reader to expect sources they cannot see.
#
# Provenance belongs in the narrative document, never in the manuscript.
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"

# LaTeX escapes underscores, so match with the underscore optional.
pattern='RESEARCH.?_?PLAN|NARRATIVE|LANDSCAPE|PROBLEM.?_?STATEMENT|DISPLAY.?_?ITEMS|SECTION.?_?SPEC|ROARING.?_?PROPOSALS|BIB.?_?STATUS|NEXT\.md|AGENTS\.md|CLAUDE\.md|scratchpad|/tmp/|GAP ?[0-9]+|\bC[0-9]{2}[a-z]?\b'

# Strip LaTeX comments before matching. A source comment pointing a maintainer
# at an internal document is legitimate -- it never reaches a reader. Only
# rendered content is contamination. Escaped \% is not a comment.
#
# Comments are blanked rather than deleted so line numbers stay true, and the
# pattern is passed to grep as an argument rather than interpolated into a perl
# program -- interpolating it silently broke the match and the check passed on
# contaminated input.
hits=$(for f in sections/*.tex figures/*.tex storm.tex; do
    [ -f "$f" ] || continue
    perl -pe 's/(?<!\\)%.*$//' "$f" | grep -noE "$pattern" | sed "s|^|$f:|"
done 2>/dev/null || true)

if [ -n "$hits" ]; then
    echo "FAIL — repo-internal references found in the manuscript:"
    echo "$hits" | sed 's/^/  /'
    echo
    echo "Move the provenance to the narrative document and state the fact"
    echo "in the manuscript on its own terms."
    exit 1
fi

echo "PASS — no repo-internal references in the manuscript."
