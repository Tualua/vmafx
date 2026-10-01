// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package predictor

import (
	"testing"
)

func TestPredictorCharacterisation(t *testing.T) {
	p, err := New("", nil)
	if err != nil {
		t.Fatalf("New(\"\", nil) unexpected error: %v", err)
	}
	if p == nil {
		t.Fatal("New(\"\", nil) returned nil predictor")
	}

	if rc := ResolutionClass(1080); rc != "hd" {
		t.Errorf("ResolutionClass(1080) = %q, want hd", rc)
	}
	if rc := ResolutionClass(720); rc != "hd_ready" {
		t.Errorf("ResolutionClass(720) = %q, want hd_ready", rc)
	}
	if rc := ResolutionClass(2160); rc != "uhd" {
		t.Errorf("ResolutionClass(2160) = %q, want uhd", rc)
	}
}
