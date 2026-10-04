// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package hwdevice

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// fakeDRI builds a /dev/dri + /sys/class/drm view: node name -> PCI vendor.
func fakeDRI(t *testing.T, nodes map[string]string) (string, string) {
	t.Helper()
	root := t.TempDir()
	dri, sys := filepath.Join(root, "dri"), filepath.Join(root, "drm")
	for name, vendor := range nodes {
		if err := os.MkdirAll(dri, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dri, name), nil, 0o600); err != nil {
			t.Fatal(err)
		}
		dev := filepath.Join(sys, name, "device")
		if err := os.MkdirAll(dev, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dev, "vendor"), []byte(vendor+"\n"), 0o600); err != nil {
			t.Fatal(err)
		}
	}
	return dri, sys
}

func TestResolveVAAPIDeviceOrder(t *testing.T) {
	dri, sys := fakeDRI(t, map[string]string{
		"renderD128": "0x10de", "renderD129": "0x1002", "renderD130": "0x8086",
	})
	t.Setenv(VAAPIDeviceEnv, "")
	if got := resolveIn("auto", dri, sys); got != filepath.Join(dri, "renderD130") {
		t.Errorf("auto = %q, want the Intel node renderD130", got)
	}
	t.Setenv(VAAPIDeviceEnv, "/dev/dri/renderD140")
	if got := resolveIn("", dri, sys); got != "/dev/dri/renderD140" {
		t.Errorf("env override = %q", got)
	}
	if got := resolveIn("/dev/dri/renderD141", dri, sys); got != "/dev/dri/renderD141" {
		t.Errorf("explicit = %q, want it to beat the env", got)
	}
}

func TestResolveVAAPIDeviceFallsBackWithoutIntel(t *testing.T) {
	dri, sys := fakeDRI(t, map[string]string{"renderD128": "0x10de"})
	t.Setenv(VAAPIDeviceEnv, "")
	if got := resolveIn("auto", dri, sys); got != FallbackVAAPIDevice {
		t.Errorf("no Intel node: %q, want %q", got, FallbackVAAPIDevice)
	}
}

func TestQSVInitArgsUseTheQSVDeviceForFilters(t *testing.T) {
	t.Parallel()
	got := strings.Join(QSVInitArgs("/dev/dri/renderD130"), " ")
	want := "-init_hw_device vaapi=va:/dev/dri/renderD130 -init_hw_device qsv=qsv_dev@va " +
		"-filter_hw_device qsv_dev"
	if got != want {
		t.Errorf("QSVInitArgs = %q, want %q", got, want)
	}
}

func TestNeedsQSVChain(t *testing.T) {
	t.Parallel()
	for enc, want := range map[string]bool{
		"h264_qsv": true, "hevc_qsv": true, "av1_qsv": true,
		"h264_nvenc": false, "libx264": false, "qsv": false,
	} {
		if NeedsQSVChain(enc) != want {
			t.Errorf("NeedsQSVChain(%q) != %v", enc, want)
		}
	}
}
