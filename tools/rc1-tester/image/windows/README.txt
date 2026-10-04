VMAFx tester zip for Windows (x64 or arm64, the name of the zip says which)

Run:        .\run.cmd          (PowerShell)    or    run.cmd    (Command Prompt)
            from this folder; it writes report.json next to it (a few minutes)
Sends:      report.json (see docs/usage/tester-image.md in the VMAFx repository)
Delete:     remove this folder and the zip; nothing else was written anywhere.

What is in here
  run.cmd                the one command; starts the report with the bundled interpreter
  runtime\               a Python 3.13 interpreter (python-build-standalone, pinned by
                         SHA-256) with the Microsoft Visual C++ runtime DLL it needs
  tester\                the report program: plain Python source, tools/rc1-tester/ in
                         the repository
  build\tools\vmaf.exe   the VMAFx command line tool, libvmaf and the C runtime linked in
  tests\                 unit test programs run by the report
  python\test\resource\  Netflix test videos (checked against pinned SHA-256 values)
  reference\             scores recorded by this build on the hosted runner
  image\                 manifests: fixtures, unit tests, build information
  licenses\              THIRD_PARTY_NOTICES.txt: the licence of everything in here,
                         and the licence texts

The report reads these files and runs vmaf.exe and the programs in tests\. It needs no
network and contains no network code. It prints no host name, user name, serial number
or UUID. The programs are not signed: see "Windows SmartScreen" in the tester guide
before you run them, and check the zip's checksum first.
