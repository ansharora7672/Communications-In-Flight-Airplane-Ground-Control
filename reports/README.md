# Analysis Artifacts

Generated report evidence is written to `reports/analysis/<timestamp>/` by the
analysis automation in `ci/analyze_project.py`.

Recommended workflows:

```bash
python3 ci/analyze_project.py coverage
python3 ci/analyze_project.py static-analysis
python3 ci/analyze_project.py warnings
python3 ci/analyze_project.py sanitizers
python3 ci/analyze_project.py summary --stamp <timestamp>
```

Or run the full pipeline in one session:

```bash
python3 ci/analyze_project.py all
```

Each timestamped session is intended to feed a documentation-heavy report rather
than claim formal Canadian aviation or MISRA certification. The generated
artifacts focus on:

- unit-only versus combined unit + runtime coverage
- end-to-end runtime evidence from the headless verifier
- cppcheck quality findings and MISRA-style findings when the addon is available
- strict-warning and sanitizer results for project-owned code only
- report-ready Markdown, CSV, and JSON summaries
