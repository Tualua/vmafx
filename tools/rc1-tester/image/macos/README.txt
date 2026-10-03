VMAFx tester bundle for macOS on Apple silicon

Run:        ./run.sh > report.json
Sends:      report.json (see docs/usage/tester-image.md in the VMAFx repository)
Delete:     remove this directory; nothing else was written anywhere.

What is in here
  run.sh                 the one command; starts the report with the bundled interpreter
  runtime/               a Python 3.13 interpreter (python-build-standalone, pinned by SHA-256)
  tester/                the report program: plain Python source, tools/rc1-tester/ in the repository
  build/tools/vmaf       the VMAFx command line tool, libvmaf linked in, Metal on, no ONNX Runtime
  tests/                 unit test executables run by the report
  python/test/resource/  Netflix test videos (checked against pinned SHA-256 values)
  reference/             scores recorded by this build on the hosted runner
  image/                 manifests: fixtures, unit tests, build information

The report reads these files and runs vmaf and the tests from tests/. It needs no
network; run.sh denies network access to the run with sandbox-exec when macOS offers it.
It prints no host name, user name, serial number or UUID.
