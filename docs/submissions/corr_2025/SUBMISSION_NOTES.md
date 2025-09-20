# ACM Computing Research Repository (CoRR) Submission Notes

## Important Information

The ACM Computing Research Repository (CoRR) is **part of arXiv**, specifically the computer science section. When you submit to arXiv under the cs.* categories, your paper automatically becomes part of CoRR.

## Submission Process

**Use the same arXiv submission process** as detailed in `/arxiv_2025/SUBMISSION_CHECKLIST.md`

The only differences are:
1. Ensure you select **cs.DB** as the primary category (this automatically includes it in CoRR)
2. You may want to mention "Computing Research Repository" in announcements

## Benefits of CoRR/arXiv Submission

1. **Immediate dissemination** - Available within 24-48 hours
2. **Permanent DOI** - Citeable reference
3. **ACM visibility** - Indexed in ACM Guide to Computing Literature
4. **Google Scholar indexing** - Improves discoverability
5. **Version control** - Can update with new versions while preserving all versions

## ACM-Specific Metadata

When submitting, use these ACM Computing Classification System (CCS) concepts:

### Primary Classifications:
- **H.2.4** [Database Management]: Systems—Query processing
- **H.2.2** [Database Management]: Physical Design—Access methods

### Secondary Classifications:
- **D.4.2** [Operating Systems]: Storage Management—Main memory
- **D.1.3** [Programming Techniques]: Concurrent Programming—Parallel programming
- **C.4** [Performance of Systems]: Performance attributes

## Citation Format

Once published, the paper should be cited as:

```bibtex
@techreport{towell2025maph,
  title={maph: Maps Based on Perfect Hashing for Sub-Microsecond Key-Value Storage},
  author={Towell, Alexander},
  year={2025},
  institution={Computing Research Repository (CoRR)},
  number={arXiv:YYMM.NNNNN},
  url={https://arxiv.org/abs/YYMM.NNNNN}
}
```

## Files for CoRR Submission

The files in `/arxiv_2025/` are suitable for CoRR submission:
- `maph_arxiv.tex` - Main paper
- `abstract.txt` - Abstract and metadata
- `SUBMISSION_CHECKLIST.md` - Detailed submission steps
- `compile.sh` - Build script

## Notes

- CoRR papers are not peer-reviewed but are moderated for relevance
- Suitable for technical reports, early results, and rapid dissemination
- Can later submit revised version to conferences/journals
- No publication fees
- Open access by default