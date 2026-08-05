#!/bin/sh
# Build the positional-popcount kernel paper (main.tex) to main.pdf.
# Single document: main.tex + references.bib. No appendices, no pdfpages.
# minted is disabled in this draft, so NO -shell-escape / Pygments is required.
# Requires a TeX Live installation (pdflatex, bibtex). On macOS/MacTeX the
# toolchain lives in /Library/TeX/texbin, which we add to PATH defensively.
set -e
cd "$(dirname "$0")"

# Ensure the MacTeX symlink dir is on PATH (harmless if already present/absent).
case ":$PATH:" in
  *:/Library/TeX/texbin:*) ;;
  *) PATH="/Library/TeX/texbin:$PATH" ;;
esac
export PATH

DOC=main

# Prefer latexmk (handles the pdflatex/bibtex passes automatically).
if command -v latexmk >/dev/null 2>&1; then
  latexmk -pdf -interaction=nonstopmode -halt-on-error "$DOC.tex"
else
  # Manual fallback: pdflatex -> bibtex -> pdflatex x2 to resolve refs/cites.
  pdflatex -interaction=nonstopmode -halt-on-error "$DOC.tex"
  bibtex "$DOC" || true   # references.bib entries are draft; tolerate warnings
  pdflatex -interaction=nonstopmode -halt-on-error "$DOC.tex"
  pdflatex -interaction=nonstopmode -halt-on-error "$DOC.tex"
fi

echo "Done: $DOC.pdf"
