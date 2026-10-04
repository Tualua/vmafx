// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// pkg/storage/rclone_mount_test.go — mount mode against a real rclone and a
// real FUSE mount: the asset reads back byte for byte under MountRoot and the
// cleanup unmounts and removes the mount point. Needs rclone, /dev/fuse and
// fusermount3 (declared test dependencies; go-ci.yml installs them).

package storage

import (
	"bytes"
	"context"
	"log/slog"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestFUSEMountStorage_RealRclone(t *testing.T) {
	rclone := requireRclone(t)
	if why := fuseUnavailable(); why != "" {
		t.Fatalf("FUSE unavailable (a declared test dependency): %s", why)
	}
	src := t.TempDir()
	want := bytes.Repeat([]byte("vmafx"), 100000)
	if err := os.WriteFile(filepath.Join(src, "ref.y4m"), want, 0o600); err != nil {
		t.Fatal(err)
	}
	root := t.TempDir()
	s, err := Open(Config{Mode: ModeMount, RcloneBin: rclone, MountRoot: root, Log: slog.New(slog.DiscardHandler)})
	if err != nil {
		t.Fatalf("Open(mount): %v", err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()
	path, cleanup, err := s.Prepare(ctx, ":local:"+src+"/ref.y4m")
	if err != nil {
		t.Fatalf("Prepare: %v", err)
	}
	got, readErr := os.ReadFile(path)
	mountDir := filepath.Dir(path)
	cleanup()
	if readErr != nil || !bytes.Equal(got, want) {
		t.Fatalf("read %s: %d bytes (err %v), want %d", path, len(got), readErr, len(want))
	}
	if !strings.HasPrefix(path, root+string(os.PathSeparator)) {
		t.Fatalf("mount point %q not under %q", path, root)
	}
	if _, err := os.Stat(mountDir); !os.IsNotExist(err) {
		t.Fatalf("mount point %s still exists after cleanup (stat err %v)", mountDir, err)
	}
}
