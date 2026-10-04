- **The Windows tester zips carry the Visual Studio 2026 licence terms for the
  Microsoft runtime code they ship** (`T-TESTER-WINDOWS-VS-TERMS-UNREAD-2026-10-04`).
  The terms page shows only a title and a date; the terms are a Word document it
  embeds. The licence record now pins that document by URL and SHA-256, the build
  writes its text to `licenses\texts\visual-studio-2026-license-terms.txt`, and the
  notes of both Microsoft components pass on what its Distributable Code section
  asks of a distributor.
