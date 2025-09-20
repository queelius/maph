# Master Submission Guide for maph Technical Report

## Executive Summary

This guide provides comprehensive instructions for submitting the maph technical report to recommended venues. All submission materials have been prepared and are ready for submission.

## Paper Details

- **Title**: maph: Maps Based on Perfect Hashing for Sub-Microsecond Key-Value Storage
- **Author**: Alexander Towell (atowell@siue.edu)
- **Affiliation**: Southern Illinois University Edwardsville/Carbondale
- **Length**: 7 pages
- **Status**: Complete and ready for submission

## Recommended Submission Order

### 1. arXiv/CoRR (IMMEDIATE - RECOMMENDED)
**Priority: HIGHEST**
**Timeline: 24-48 hours to publication**

**Location**: `/docs/submissions/arxiv_2025/`
- Main file: `maph_arxiv.tex`
- Abstract: `abstract.txt`
- Checklist: `SUBMISSION_CHECKLIST.md`
- Compilation: `./compile.sh`

**Why First**: 
- Establishes priority date for your work
- Immediate visibility to research community
- Permanent DOI for citations
- No review delay

**Action Steps**:
1. Create arXiv account at https://arxiv.org/submit
2. Compile PDF using provided script
3. Submit to cs.DB (primary) and cs.DC (secondary)
4. Paper appears within 24-48 hours

### 2. USENIX ;login: Magazine (OPTIONAL)
**Priority: MEDIUM**
**Timeline: 2-3 months to publication**

**Location**: `/docs/submissions/usenix_login_2025/`
- Main file: `maph_login_article.tex`
- Cover letter: `cover_letter.txt`
- Checklist: `SUBMISSION_CHECKLIST.md`

**Why Submit**:
- Reaches industry practitioners
- Less academic, more practical focus
- Good for building reputation in systems community

**Action Steps**:
1. Compile article PDF
2. Email to login@usenix.org with cover letter
3. Wait 2-4 weeks for initial response

### 3. Future Conference Venues (OPTIONAL)

Consider submitting extended versions to:

**Systems Conferences** (if you extend to 12+ pages):
- USENIX ATC (deadline: January)
- EuroSys (deadline: October)
- SOSP/OSDI (alternating years)

**Database Conferences**:
- VLDB (rolling deadline)
- SIGMOD (deadline: July)
- ICDE (deadline: October)

## Quick Start Commands

```bash
# Navigate to submissions directory
cd /home/spinoza/github/repos/rd_ph_filter/docs/submissions/

# For arXiv submission
cd arxiv_2025/
./compile.sh
# Output: maph_arxiv.pdf ready for upload

# For USENIX ;login:
cd ../usenix_login_2025/
pdflatex maph_login_article.tex
pdflatex maph_login_article.tex
# Output: maph_login_article.pdf ready for email

# Check file sizes
ls -lh */*.pdf
```

## Submission Status Tracking

| Venue | Status | Date Submitted | Response Date | Outcome | Notes |
|-------|--------|---------------|---------------|---------|-------|
| arXiv/CoRR | Ready | - | - | - | Submit ASAP |
| USENIX ;login: | Ready | - | - | - | Optional |
| Conference | Future | - | - | - | Extend to 12 pages first |

## Key Differences Between Versions

### arXiv Version
- Standard academic format
- Complete technical details
- Formal bibliography
- 7 pages, single column
- Target: Researchers

### USENIX ;login: Version
- Practitioner-friendly language
- More code examples
- Conversational tone
- ~3000 words
- Target: Industry engineers

## Important URLs

- **arXiv submission**: https://arxiv.org/submit
- **arXiv help**: https://info.arxiv.org/help/submit.html
- **USENIX ;login:**: https://www.usenix.org/publications/login
- **GitHub repo**: https://github.com/queelius/rd_ph_filter

## Pre-Submission Verification

Before submitting to any venue:

- [ ] LaTeX compiles without errors
- [ ] PDF displays correctly
- [ ] All tables/figures render properly
- [ ] Bibliography is complete
- [ ] Author information is correct
- [ ] GitHub link works
- [ ] No proprietary information included

## Common Issues and Solutions

### LaTeX Compilation Errors
```bash
# Clean build
rm -f *.aux *.log *.out *.pdf
pdflatex <filename>.tex
pdflatex <filename>.tex
```

### PDF Too Large
- Check for unnecessary packages
- Compress any images
- Remove commented code

### Missing Endorsement (arXiv)
- Request from a colleague who has published in cs.DB
- Include abstract in endorsement request
- Allow 24-48 hours for processing

## After Submission

### arXiv/CoRR
1. Note the arXiv ID (format: arXiv:YYMM.NNNNN)
2. Add to GitHub README
3. Update CV/website
4. Share on social media/academic networks

### USENIX ;login:
1. Monitor email for editor response
2. Be prepared to make revisions
3. Sign publication agreement if accepted

## Citation Format After Publication

```bibtex
@techreport{towell2025maph,
  title={maph: Maps Based on Perfect Hashing for Sub-Microsecond Key-Value Storage},
  author={Towell, Alexander},
  year={2025},
  month={September},
  institution={arXiv},
  number={arXiv:2509.XXXXX},
  url={https://arxiv.org/abs/2509.XXXXX}
}
```

## Support and Questions

For questions about:
- **arXiv submission**: help@arxiv.org
- **USENIX ;login:**: login@usenix.org
- **Technical content**: atowell@siue.edu

## Final Recommendations

1. **Submit to arXiv TODAY** - It's free, fast, and establishes priority
2. **Consider ;login: for practitioner visibility** - Good for career development
3. **Plan conference submission for 2025** - Extend paper with more evaluation

The materials are polished and ready. The main decision is whether to submit just to arXiv (recommended minimum) or also pursue ;login: magazine for broader impact.

Good luck with your submissions!