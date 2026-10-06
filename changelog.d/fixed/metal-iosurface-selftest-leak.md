- **The Linux self-test of the Metal IOSurface import no longer leaks its
  fixtures.** `test_metal_selftest_iosurface_import` kept the planar pictures
  it imported from, and the AddressSanitizer job aborted on the leak report;
  the self-test now releases them as the device build does.
