#!/bin/bash

# Compilation script for arXiv submission
# Author: Alexander Towell
# Date: 2025

echo "Compiling maph technical report for arXiv submission..."

# Clean previous builds
rm -f *.aux *.log *.out *.pdf *.bbl *.blg

# Compile the document (run twice for references)
pdflatex maph_arxiv.tex
if [ $? -ne 0 ]; then
    echo "Error: First LaTeX compilation failed"
    exit 1
fi

pdflatex maph_arxiv.tex
if [ $? -ne 0 ]; then
    echo "Error: Second LaTeX compilation failed"
    exit 1
fi

# Check if PDF was created
if [ -f "maph_arxiv.pdf" ]; then
    echo "Success: PDF generated successfully"
    
    # Check file size
    FILE_SIZE=$(stat -c%s "maph_arxiv.pdf" 2>/dev/null || stat -f%z "maph_arxiv.pdf" 2>/dev/null)
    FILE_SIZE_MB=$((FILE_SIZE / 1048576))
    
    echo "PDF size: ${FILE_SIZE} bytes (~${FILE_SIZE_MB} MB)"
    
    if [ $FILE_SIZE -gt 10485760 ]; then
        echo "Warning: PDF exceeds 10MB arXiv limit!"
    else
        echo "PDF size is within arXiv limits (< 10MB)"
    fi
    
    # Count pages
    if command -v pdfinfo &> /dev/null; then
        PAGES=$(pdfinfo maph_arxiv.pdf | grep Pages | awk '{print $2}')
        echo "Page count: $PAGES"
    fi
else
    echo "Error: PDF generation failed"
    exit 1
fi

# Clean intermediate files but keep PDF
rm -f *.aux *.log *.out *.bbl *.blg

echo "Compilation complete. Ready for arXiv submission."
echo "Next steps:"
echo "1. Review maph_arxiv.pdf for correctness"
echo "2. Follow SUBMISSION_CHECKLIST.md for submission process"