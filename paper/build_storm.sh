#!/bin/sh
# Build the StormBitmaps manuscript (storm.tex).
# No -shell-escape needed: minted is not used.
set -e
cd "$(dirname "$0")"
PATH="/Library/TeX/texbin:$PATH"
export PATH

if command -v latexmk >/dev/null 2>&1; then
  latexmk -pdf -interaction=nonstopmode -halt-on-error storm.tex
else
  pdflatex -interaction=nonstopmode -halt-on-error storm.tex
  bibtex storm || true
  pdflatex -interaction=nonstopmode -halt-on-error storm.tex
  pdflatex -interaction=nonstopmode -halt-on-error storm.tex
fi

echo "built: $(pwd)/storm.pdf"
