// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package fast

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/VMAFx/vmafx/internal/vmaftest"
)

// TestTestConfigRunsTheVMAFUnderTest holds the integration tests to the vmaf
// CLI of the build under test: a bare "vmaf" would run whatever the host has
// on PATH (T-GO-FAST-INTEGRATION-PATH-VMAF-2026-10-05).
func TestTestConfigRunsTheVMAFUnderTest(t *testing.T) {
	bin := filepath.Join(t.TempDir(), "vmaf-under-test")
	if err := os.WriteFile(bin, []byte("#!/bin/sh\nexit 0\n"), 0o700); err != nil {
		t.Fatal(err)
	}
	t.Setenv(vmaftest.EnvVar, bin)

	cfg := testConfig(t, "unused.yuv", nil)

	if cfg.VMAFBin != bin {
		t.Errorf("VMAFBin = %q, want the binary under test %q", cfg.VMAFBin, bin)
	}
}
