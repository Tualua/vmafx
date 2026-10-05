// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

// object_embed.go — where the generated eBPF object enters the binary
// (ADR-1622). The object is compiled at build time by
// scripts/dev/gen-node-bpf.sh and is not committed; the bpf2go binding reads it
// through embeddedObject() (embed_generated_object.sh rewrites the binding's
// own embed). The pattern below matches the committed notice file, so the
// package compiles on a checkout that has not generated the object; the node
// then refuses to start the tracker and names the generator (requireObject).

package bpf

import (
	"embed"
	"errors"
)

// objectFS holds the generated object (rclonebypass_bpfel.o, git-ignored) and
// the committed notice file that keeps the pattern satisfiable without it.
//
//go:embed rclonebypass_bpfel.o*
var objectFS embed.FS

// objectName is the file the generator writes next to this one.
const objectName = "rclonebypass_bpfel.o"

// embeddedObject returns the embedded object, or nil when this build has none.
func embeddedObject() []byte {
	object, err := objectFS.ReadFile(objectName)
	if err != nil {
		return nil
	}
	return object
}

// errObjectMissing is returned by Start, and so stops the node, when the build
// embedded no object: a tracker that was asked for must run or the node must
// refuse to start, never run without it.
var errObjectMissing = errors.New("ebpf: this build embeds no eBPF object " +
	"(it is generated, not committed): run `make node-bpf` or scripts/dev/gen-node-bpf.sh " +
	"before building, see docs/development/node-ebpf-build.md")

// requireObject fails with errObjectMissing when object is empty.
func requireObject(object []byte) error {
	if len(object) == 0 {
		return errObjectMissing
	}
	return nil
}
