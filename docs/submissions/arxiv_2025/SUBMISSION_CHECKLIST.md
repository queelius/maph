# arXiv Submission Checklist for maph Technical Report

## Pre-Submission Requirements

### 1. arXiv Account Setup
- [ ] Create arXiv account at https://arxiv.org/user-register
- [ ] Wait for endorsement in cs.DB category (if first submission)
  - Alternative: Request endorsement from a colleague who has published in cs.DB
- [ ] Verify email address and complete profile

### 2. File Preparation
- [x] Main LaTeX file: `maph_arxiv.tex` 
- [ ] Compile LaTeX to PDF using pdflatex:
  ```bash
  cd /home/spinoza/github/repos/rd_ph_filter/docs/submissions/arxiv_2025
  pdflatex maph_arxiv.tex
  pdflatex maph_arxiv.tex  # Run twice for references
  ```
- [ ] Verify PDF is under 10MB (arXiv limit)
- [ ] Check PDF renders correctly with embedded fonts
- [ ] Ensure all figures are included (if any)

### 3. Metadata Preparation
- [x] Title: "maph: Maps Based on Perfect Hashing for Sub-Microsecond Key-Value Storage"
- [x] Authors: Alexander Towell
- [x] Abstract: See `abstract.txt`
- [x] Primary category: cs.DB (Databases)
- [x] Secondary category: cs.DC (Distributed, Parallel, and Cluster Computing)
- [x] Comments: "7 pages, 8 tables, technical report"

### 4. License Selection
- [ ] Choose license (recommended: CC BY 4.0 for maximum visibility)
- [ ] Alternative: arXiv non-exclusive license

## Submission Process

### Step 1: Start New Submission
1. Log in to https://arxiv.org/submit
2. Click "Start New Submission"
3. Select "Submit an article"

### Step 2: Upload Files
1. Choose upload method: "Upload LaTeX source files"
2. Upload `maph_arxiv.tex`
3. Let arXiv auto-compile the PDF
4. If compilation fails, fix errors and re-upload

### Step 3: Fill Metadata
1. **Title**: Copy from abstract.txt
2. **Authors**: 
   - Name: Alexander Towell
   - Affiliation: Southern Illinois University Edwardsville/Carbondale
   - Email: atowell@siue.edu
3. **Abstract**: Copy from abstract.txt (without title/author lines)
4. **Categories**: 
   - Primary: cs.DB
   - Cross-list: cs.DC
5. **ACM Classification**: 
   - H.2.4 [Database Management]: Systems—Query processing
   - D.4.2 [Operating Systems]: Storage Management
6. **Report Number**: Leave blank or add "SIUE-CS-2025-01" if desired
7. **Journal Reference**: Leave blank (technical report)
8. **DOI**: Leave blank
9. **Comments**: "7 pages, 8 tables, technical report"

### Step 4: Preview and Submit
1. Preview the submission
2. Verify all information is correct
3. Check PDF compilation successful
4. Submit

### Step 5: Post-Submission
1. Note the arXiv identifier (format: arXiv:YYMM.NNNNN)
2. Submission appears publicly next business day at 8PM ET
3. Add arXiv ID to GitHub README
4. Share link: https://arxiv.org/abs/YYMM.NNNNN

## Verification Checklist

### Content Verification
- [ ] Paper is self-contained (no missing references)
- [ ] All tables and algorithms render correctly
- [ ] Code listings are properly formatted
- [ ] Bibliography is complete
- [ ] No proprietary or confidential information

### Format Verification
- [ ] Uses standard LaTeX article class (arXiv preference)
- [ ] Font size 11pt (readable)
- [ ] Single column format for technical report
- [ ] Page numbers included
- [ ] Margins at least 1 inch

### Technical Verification
- [ ] LaTeX compiles without errors
- [ ] No overfull hboxes causing text overflow
- [ ] All citations resolve correctly
- [ ] Math notation renders properly
- [ ] Algorithms display correctly

## Common Issues and Solutions

### Issue: Endorsement Required
**Solution**: Request endorsement from a colleague or advisor who has published in cs.DB. Include paper abstract in request.

### Issue: LaTeX Compilation Fails
**Solution**: 
1. Remove conference-specific packages
2. Use standard article class
3. Test locally with TeX Live 2020+ (arXiv's version)

### Issue: PDF Too Large
**Solution**:
1. Compress images if present
2. Use vector graphics instead of raster
3. Remove unnecessary packages

### Issue: Bibliography Errors
**Solution**:
1. Use \begin{thebibliography} instead of BibTeX (simpler for arXiv)
2. Ensure all citations are defined

## Timeline

- **Submission**: Any time (24/7)
- **Processing**: 1-2 hours for initial checks
- **Public Availability**: Next business day at 8PM ET
- **Revision Window**: Can update version anytime after initial publication

## Contact Information

- **arXiv Help**: https://arxiv.org/help
- **Technical Issues**: help@arxiv.org
- **Author Support**: https://info.arxiv.org/help/submit.html

## Notes

- arXiv submissions are permanent and cannot be removed
- Updates create new versions but old versions remain accessible
- Consider submitting after final proofreading as v1 is most cited
- Include GitHub link for reproducibility
- Technical reports don't require peer review, making arXiv ideal for rapid dissemination