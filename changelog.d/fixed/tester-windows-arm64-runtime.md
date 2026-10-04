- **The Arm64 Windows tester zip builds**
  (`T-TESTER-WINDOWS-ARM64-X64-VCRUNTIME-2026-10-04`). The Arm64 interpreter
  archive carries an x64 `vcruntime140_1.dll` that no program loads, and the
  zip's import check refused it, so the first hosted run published no Arm64
  zip. The build now leaves out every interpreter runtime DLL nothing imports.
  Each zip is also verified when its own build passed, and the build log shows
  the output of a unit test the zip's report counts as failed.
