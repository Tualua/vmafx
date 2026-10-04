// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// pkg/storage/storage.go — rclone-backed remote storage interface.
//
// The Storage interface abstracts how a vmafx-node obtains a readable URL for
// a remote asset (S3 object, GCS blob, SFTP file, local path, etc.) and
// exposes it to ffmpeg without materialising the content to disk first.
//
// Three implementations are provided:
//   - HTTPServeStorage  — spawns "rclone serve http" per job; returns
//     http://localhost:PORT/<path>. Primary mode.
//   - FUSEMountStorage  — spawns "rclone mount" to a per-job directory;
//     returns a local POSIX path. Fallback mode.
//   - LocalStorage      — passthrough for file:// or bare paths; no rclone.
//
// The correct implementation is selected by New() based on the source URI
// scheme and the configured mode.
//
// ADR-0719: vmafx-node rclone integration (Phase 4b.5).
// ADR-0709: Phase 4b umbrella.
package storage

import (
	"context"
	"fmt"
	"log/slog"
	"net"
	"net/url"
	"os"
	"os/exec"
	"strings"
)

// Mode selects the rclone access strategy.
type Mode string

const (
	// ModeHTTPServe uses "rclone serve http" to expose the remote as an HTTP
	// server. ffmpeg reads via http://localhost:PORT/path. Recommended for
	// multi-input jobs (ref + dis simultaneously).
	ModeHTTPServe Mode = "http-serve"

	// ModeMount uses "rclone mount" to expose the remote as a FUSE filesystem.
	// ffmpeg reads from a local directory path. Fallback for sources that
	// require random access beyond what the HTTP serve mode provides.
	ModeMount Mode = "mount"

	// ModeAuto selects the best mode based on the source URI and availability
	// of FUSE in the container.
	ModeAuto Mode = "auto"
)

// Storage converts a remote asset URI into a form that ffmpeg can read without
// materialising the content to a local disk file.
type Storage interface {
	// Prepare returns a URL or path string that ffmpeg's -i flag can consume
	// directly, plus a cleanup function the caller must invoke when the asset
	// is no longer needed (kills the rclone subprocess, unmounts FUSE, etc.).
	//
	// sourceURI examples:
	//   s3://my-bucket/ref.yuv           — S3 via rclone remote "s3"
	//   gcs://bucket/path/dis.yuv        — GCS via rclone remote "gcs"
	//   rclone://s3-prod:bucket/ref.yuv  — explicit rclone remote:path syntax
	//   /local/path/to/ref.yuv           — bare local path (LocalStorage only)
	//   file:///local/path/to/ref.yuv    — file:// URI (LocalStorage only)
	Prepare(ctx context.Context, sourceURI string) (readableURL string, cleanup func(), err error)

	// Mode reports the active access strategy.
	Mode() Mode
}

// ParseMode turns a configured mode string into a Mode. Only the three modes
// this package implements are accepted; anything else is an error, never a
// silent fallback to another mode.
func ParseMode(raw string) (Mode, error) {
	switch m := Mode(strings.TrimSpace(raw)); m {
	case ModeHTTPServe, ModeMount, ModeAuto:
		return m, nil
	default:
		return "", fmt.Errorf("storage: unknown mode %q (want %s, %s or %s)", raw, ModeHTTPServe, ModeMount, ModeAuto)
	}
}

// Config holds the configuration shared by all Storage implementations.
type Config struct {
	// RcloneBin is the path to the rclone binary. Defaults to "rclone".
	RcloneBin string

	// RcloneConfig is the path to the rclone configuration file.
	// Defaults to the value of VMAFX_RCLONE_CONFIG env var, then
	// /etc/vmafx/rclone.conf, then rclone's own default (~/.config/rclone/rclone.conf).
	RcloneConfig string

	// Mode selects the access strategy (http-serve | mount | auto).
	Mode Mode

	// MountRoot is the directory under which mount mode creates its per-job
	// mount points. Empty means os.TempDir().
	MountRoot string

	// Log receives diagnostic messages from the storage layer.
	Log *slog.Logger
}

// Open validates cfg and returns the Storage for its mode. Unlike New it
// refuses an unknown or empty mode, and it resolves ModeAuto to the concrete
// mode the host supports (mount when FUSE is usable, else http-serve) and logs
// which one it chose and why. Mode() of the result reports the concrete mode.
func Open(cfg Config) (Storage, error) {
	mode, err := ParseMode(string(cfg.Mode))
	if err != nil {
		return nil, err
	}
	log := cfg.Log
	if log == nil {
		log = slog.Default()
	}
	if mode == ModeAuto {
		mode = resolveAuto(log)
	}
	if mode == ModeMount {
		if why := fuseUnavailable(); why != "" {
			return nil, fmt.Errorf("storage: mode %s needs FUSE: %s", ModeMount, why)
		}
	}
	cfg.Mode, cfg.Log = mode, log
	return New(cfg), nil
}

// resolveAuto picks the concrete mode for ModeAuto and says why.
func resolveAuto(log *slog.Logger) Mode {
	if why := fuseUnavailable(); why != "" {
		log.Info("storage mode auto resolved", "mode", ModeHTTPServe, "reason", why)
		return ModeHTTPServe
	}
	log.Info("storage mode auto resolved", "mode", ModeMount, "reason", "FUSE device and fusermount available")
	return ModeMount
}

// fuseUnavailable returns why FUSE mounts cannot work here, or "" when the
// device node and an unmount helper exist.
func fuseUnavailable() string {
	if _, err := os.Stat(fuseDevice); err != nil {
		return fuseDevice + " is not available: " + err.Error()
	}
	for _, bin := range []string{"fusermount3", "fusermount"} {
		if _, err := exec.LookPath(bin); err == nil {
			return ""
		}
	}
	return "neither fusermount3 nor fusermount is on PATH"
}

// fuseDevice is the FUSE device node; a variable so tests can point it away.
var fuseDevice = "/dev/fuse"

// IsHTTP reports whether sourceURI is an http(s) URL, which a reader can
// stream directly without rclone.
func IsHTTP(sourceURI string) bool {
	u, err := url.Parse(sourceURI)
	return err == nil && (u.Scheme == "http" || u.Scheme == "https") && u.Host != ""
}

// New returns the Storage implementation for cfg.Mode without validating it.
//
// Deprecated: New maps an empty, unknown or auto mode to HTTPServeStorage
// without saying so. Use Open, which refuses an unknown mode and resolves
// auto against the host. New stays for existing callers (HISS-14).
func New(cfg Config) Storage {
	log := cfg.Log
	if log == nil {
		log = slog.Default()
	}
	rcloneBin := cfg.RcloneBin
	if rcloneBin == "" {
		rcloneBin = "rclone"
	}

	mode := cfg.Mode
	if mode == ModeAuto || mode == "" {
		mode = ModeHTTPServe
	}

	switch mode {
	case ModeMount:
		return &FUSEMountStorage{
			rcloneBin:    rcloneBin,
			rcloneConfig: cfg.RcloneConfig,
			mountRoot:    cfg.MountRoot,
			log:          log,
		}
	default:
		// ModeHTTPServe and ModeAuto both resolve to HTTPServeStorage.
		return &HTTPServeStorage{
			rcloneBin:    rcloneBin,
			rcloneConfig: cfg.RcloneConfig,
			log:          log,
		}
	}
}

// LocalStorage is a no-op Storage for assets that are already locally
// accessible (file:// URIs or bare paths). No rclone subprocess is spawned.
type LocalStorage struct{}

// Mode returns ModeAuto as a sentinel indicating no rclone is used.
func (s *LocalStorage) Mode() Mode { return ModeAuto }

// Prepare strips the file:// prefix if present and returns the bare path.
// The cleanup function is a no-op.
func (s *LocalStorage) Prepare(_ context.Context, sourceURI string) (string, func(), error) {
	path, err := localPath(sourceURI)
	if err != nil {
		return "", func() {}, err
	}
	return path, func() {}, nil
}

// directSource handles the sources that need no rclone: a local path is
// returned as a path and an http(s) URL unchanged. ok is false for an rclone
// remote.
func directSource(sourceURI string) (string, bool, error) {
	if IsHTTP(sourceURI) {
		return sourceURI, true, nil
	}
	if !IsLocal(sourceURI) {
		return "", false, nil
	}
	lp, err := localPath(sourceURI)
	return lp, true, err
}

// IsLocal returns true if the URI is a local filesystem path (file:// or no scheme).
func IsLocal(sourceURI string) bool {
	if strings.HasPrefix(sourceURI, "/") || strings.HasPrefix(sourceURI, "./") {
		return true
	}
	u, err := url.Parse(sourceURI)
	if err != nil {
		return false
	}
	return u.Scheme == "" || u.Scheme == "file"
}

// localPath returns the filesystem path for a local URI.
func localPath(sourceURI string) (string, error) {
	if strings.HasPrefix(sourceURI, "/") || strings.HasPrefix(sourceURI, "./") {
		return sourceURI, nil
	}
	u, err := url.Parse(sourceURI)
	if err != nil {
		return "", fmt.Errorf("storage: parse URI %q: %w", sourceURI, err)
	}
	if u.Scheme == "" || u.Scheme == "file" {
		// file:///abs/path → /abs/path
		return u.Path, nil
	}
	return "", fmt.Errorf("storage: %q is not a local path", sourceURI)
}

// pickFreePort returns an available TCP port on localhost.
func pickFreePort() (int, error) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return 0, fmt.Errorf("storage: find free port: %w", err)
	}
	port := ln.Addr().(*net.TCPAddr).Port
	if closeErr := ln.Close(); closeErr != nil {
		return 0, fmt.Errorf("storage: close probe listener: %w", closeErr)
	}
	return port, nil
}

// rcloneRemotePath splits a sourceURI into the rclone remote:path form.
//
// Supported URI schemes:
//   - rclone://remote:bucket/path   → "remote:bucket/path"
//   - s3://bucket/key               → "s3:bucket/key" (rclone S3-remote convention)
//   - gcs://bucket/key              → "gcs:bucket/key"
//   - azblob://container/blob       → "azblob:container/blob"
//   - sftp://user@host:22/path      → "sftp:user@host:22/path"
//   - http://host/path              → passed through unchanged
//   - https://host/path             → passed through unchanged
//   - remote:path                   → passed through unchanged (already rclone syntax)
func rcloneRemotePath(sourceURI string) (string, error) {
	// Already in rclone remote:path form (no //).
	if !strings.Contains(sourceURI, "://") {
		return sourceURI, nil
	}

	// Special-case the rclone:// scheme before calling url.Parse because the
	// host component can contain a colon (e.g. "s3-prod:bucket"), which
	// url.Parse misinterprets as an invalid port number.
	if after, ok := strings.CutPrefix(sourceURI, "rclone://"); ok {
		rest := after
		// rest = "s3-prod:bucket/path" → pass through as-is.
		return rest, nil
	}

	u, err := url.Parse(sourceURI)
	if err != nil {
		return "", fmt.Errorf("storage: parse URI %q: %w", sourceURI, err)
	}

	switch u.Scheme {
	case "http", "https":
		// Already a URL ffmpeg can consume natively.
		return sourceURI, nil
	case "sftp", "ftp":
		// sftp://user@host:port/path
		// url.Parse puts "user@host:port" in Host and the userinfo separately in User.
		// rclone sftp syntax is "sftp:user@host:port/path" — reconstruct it.
		userHost := u.Host
		if u.User != nil && u.User.Username() != "" {
			userHost = u.User.Username() + "@" + u.Host
		}
		return u.Scheme + ":" + userHost + u.Path, nil
	default:
		// s3, gcs, azblob, b2, dropbox, onedrive, box, and any other rclone scheme:
		// map "scheme://bucket/key" → "scheme:bucket/key".
		return u.Scheme + ":" + u.Host + u.Path, nil
	}
}
