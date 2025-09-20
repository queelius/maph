# USENIX ;login: Magazine Submission Checklist

## About ;login: Magazine

USENIX ;login: is a practitioner-focused magazine covering systems, security, and programming. Articles should be accessible to working professionals while maintaining technical depth.

## Submission Requirements

### Article Specifications
- [x] Length: ~3,000 words (current: ~2,800 words)
- [x] Style: Practitioner-focused, accessible language
- [x] Code examples: Included and well-commented
- [x] Performance data: Concrete benchmarks with context
- [x] Practical focus: When/how to use techniques

### Files Prepared
- [x] Main article: `maph_login_article.tex`
- [x] Cover letter: `cover_letter.txt`
- [ ] Author bio (150 words)
- [ ] Article abstract (100 words for web)

## Pre-Submission Checklist

### Content Review
- [ ] Article tells a complete story
- [ ] Technical concepts explained clearly
- [ ] Code examples compile and run
- [ ] Performance claims backed by data
- [ ] Practical takeaways clearly stated

### Format Requirements
- [ ] LaTeX compiles without errors
- [ ] Figures/tables properly captioned
- [ ] References formatted consistently
- [ ] No proprietary information
- [ ] GitHub link included and working

### Style Guidelines
- [ ] Active voice preferred
- [ ] Short paragraphs (3-5 sentences)
- [ ] Technical terms defined on first use
- [ ] Acronyms expanded initially
- [ ] Consistent terminology throughout

## Submission Process

### Step 1: Compile Article
```bash
cd /home/spinoza/github/repos/rd_ph_filter/docs/submissions/usenix_login_2025
pdflatex maph_login_article.tex
pdflatex maph_login_article.tex  # Run twice for references
```

### Step 2: Create Author Bio
Create a 150-word biography covering:
- Current position and institution
- Research interests
- Relevant experience
- Contact information

### Step 3: Prepare Submission Email
**To:** login@usenix.org
**Subject:** Article Submission - "maph: Achieving Sub-Microsecond Key-Value Storage"
**Attachments:**
1. maph_login_article.pdf
2. maph_login_article.tex (if requested)
3. cover_letter.txt (in email body)

### Step 4: Submit
1. Send email with PDF attached
2. Include cover letter as email body
3. Mention article is exclusive to ;login:
4. Provide GitHub repository link

## Response Timeline

- **Initial Response:** 2-4 weeks
- **Review Process:** 4-6 weeks
- **Revisions:** 2-3 weeks if requested
- **Publication:** 2-3 months from acceptance

## ;login: Specific Guidelines

### What ;login: Wants
✓ Practical, implementable ideas
✓ Performance improvements with measurements
✓ Open source implementations
✓ Lessons learned from real systems
✓ Clear explanations of complex topics

### What to Avoid
✗ Pure research without practical application
✗ Marketing or product promotion
✗ Unsubstantiated performance claims
✗ Overly academic writing style
✗ Proprietary or closed-source focus

## Post-Submission

### If Accepted
1. Work with editors on revisions
2. Provide high-resolution figures if needed
3. Sign publication agreement
4. Promote article on acceptance

### If Rejected
1. Consider feedback for improvements
2. Submit to alternative venues:
   - ACM Queue
   - IEEE Computer
   - Communications of the ACM
   - InfoQ

## Alternative Submission Formats

;login: also accepts:
- **Short Topics** (1,500 words): Quick tips, tools, techniques
- **Columns**: Regular contributions on specific topics
- **Book Reviews**: Technical book evaluations
- **Conference Reports**: Summaries of USENIX events

## Contact Information

**;login: Editorial Office**
- Email: login@usenix.org
- Web: https://www.usenix.org/publications/login
- Submission Guidelines: https://www.usenix.org/publications/login/writing

**Editor-in-Chief**: [Current editor name]
**Submissions Editor**: [Current submissions editor]

## Notes for This Submission

### Strengths
- Performance improvements are dramatic (10× faster than Redis)
- Open source implementation available
- Practical applications clearly identified
- Code examples demonstrate concepts
- Writing style appropriate for practitioners

### Potential Concerns
- Single-author paper (consider acknowledging contributors)
- Limited to single-machine deployment
- Fixed slot size may limit applicability

### Differentiation
This article differs from typical ;login: content by:
- Focusing on nanosecond-scale optimization
- Combining three techniques rarely integrated together
- Providing immediately usable code
- Including production-ready benchmarks

## Revision Ideas if Requested

If editors request changes, consider:
1. Adding a case study from production deployment
2. Comparing with more systems (e.g., ScyllaDB, FoundationDB)
3. Discussing integration with existing applications
4. Adding troubleshooting/debugging section
5. Expanding on limitations and when NOT to use maph