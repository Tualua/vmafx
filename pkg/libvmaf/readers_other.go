// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// pkg/libvmaf/readers_other.go — ScoreReaders on platforms without inheritable
// pipe descriptors (exec.Cmd.ExtraFiles) or /dev/fd: it fails instead of
// writing the streams to disk behind the caller's back.

//go:build cgo && !unix

package libvmaf

import (
	"context"
	"errors"
	"io"
)

// ErrStreamInputsUnsupported is returned by ScoreReaders where the vmaf CLI
// cannot be handed a stream as a file descriptor.
var ErrStreamInputsUnsupported = errors.New("libvmaf: scoring from streams needs a Unix host (pipe descriptors and /dev/fd)")

// ScoreReaders closes both streams and returns ErrStreamInputsUnsupported.
func (s *Scorer) ScoreReaders(_ context.Context, ref, dis io.ReadCloser, _, _ string) (float64, map[string]float64, error) {
	return 0, nil, errors.Join(ErrStreamInputsUnsupported, ref.Close(), dis.Close())
}
