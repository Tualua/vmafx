# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Retry behaviour of the test-resource download helper (fork-added)."""

import http.client
import unittest
import urllib.error
from unittest import mock

from vmaf import config


class DownloadRetryTest(unittest.TestCase):
    def setUp(self):
        patcher = mock.patch.object(config.time, "sleep")
        self.sleep = patcher.start()
        self.addCleanup(patcher.stop)

    def test_transient_errors_are_retried_until_success(self):
        failures = [
            urllib.error.URLError(TimeoutError(110, "Connection timed out")),
            http.client.RemoteDisconnected("Remote end closed connection"),
            urllib.error.ContentTooShortError("short", None),
        ]
        calls = []

        def fake(remote, tmp):
            calls.append((remote, tmp))
            if failures:
                raise failures.pop(0)

        with mock.patch.object(config.urllib.request, "urlretrieve", side_effect=fake):
            config._urlretrieve_with_retries("https://example/x.yuv", "/tmp/x.part")
        self.assertEqual(len(calls), 4)
        self.assertEqual(self.sleep.call_count, 3)

    def test_gives_up_after_the_last_attempt(self):
        err = urllib.error.URLError(TimeoutError(110, "Connection timed out"))
        with mock.patch.object(config.urllib.request, "urlretrieve", side_effect=err) as fake:
            with self.assertRaises(urllib.error.URLError):
                config._urlretrieve_with_retries("https://example/x.yuv", "/tmp/x.part")
        self.assertEqual(fake.call_count, config._DOWNLOAD_ATTEMPTS)

    def test_http_errors_are_not_retried(self):
        err = urllib.error.HTTPError("https://example/x.yuv", 404, "Not Found", None, None)
        with mock.patch.object(config.urllib.request, "urlretrieve", side_effect=err) as fake:
            with self.assertRaises(urllib.error.HTTPError):
                config._urlretrieve_with_retries("https://example/x.yuv", "/tmp/x.part")
        self.assertEqual(fake.call_count, 1)
        self.sleep.assert_not_called()


if __name__ == "__main__":
    unittest.main()
