// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package main

import (
	"testing"
)

func TestVersionDefault(t *testing.T) {
	if version == "" {
		t.Errorf("expected version to be non-empty")
	}
}
