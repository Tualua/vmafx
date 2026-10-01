// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package pyjson

import (
	"strings"
	"testing"
)

func TestPyjsonCharacterisation(t *testing.T) {
	m := map[string]int{"b": 2, "a": 1}
	got1, err := Marshal(m, 0)
	if err != nil {
		t.Fatalf("Marshal(map) unexpected error: %v", err)
	}
	if !strings.Contains(got1, `"a": 1`) || !strings.Contains(got1, `"b": 2`) {
		t.Errorf("Marshal(map) = %q, want keys a, b", got1)
	}

	gotMust := MustMarshal(map[string]string{"key": "val"}, 0)
	if gotMust != `{"key": "val"}` {
		t.Errorf("MustMarshal = %q, want {\"key\": \"val\"}", gotMust)
	}

	if f := FormatFloat(3.14); f != "3.14" {
		t.Errorf("FormatFloat(3.14) = %q, want 3.14", f)
	}

	if s := EncodeString("hello"); s != `"hello"` {
		t.Errorf("EncodeString(\"hello\") = %q, want \"hello\"", s)
	}
}
