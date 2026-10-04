// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2
//
// pkg/corpus/resolution_parity_test.go — pins SelectVMAFModelVersion and
// NegModelFor to the golden table that vmaftune.resolution's own test reads
// (tools/vmaf-tune/tests/test_resolution.py::test_golden_table_matches_model_rule),
// so the two languages cannot choose a different model for the same rung.

package corpus

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

type resolutionGoldenCase struct {
	Width    int    `json:"width"`
	Height   int    `json:"height"`
	Model    string `json:"model"`
	NegModel string `json:"neg_model"`
}

type resolutionGolden struct {
	Cases    []resolutionGoldenCase `json:"cases"`
	Rejected []resolutionGoldenCase `json:"rejected"`
}

func loadResolutionGolden(t *testing.T) resolutionGolden {
	t.Helper()
	path := filepath.Join("..", "..", "tools", "vmaf-tune", "tests", "data",
		"resolution_model_table.json")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read the shared golden table: %v", err)
	}
	var table resolutionGolden
	if err := json.Unmarshal(data, &table); err != nil {
		t.Fatalf("parse %s: %v", path, err)
	}
	if len(table.Cases) == 0 || len(table.Rejected) == 0 {
		t.Fatalf("%s has no cases or no rejected sizes", path)
	}
	return table
}

func TestResolutionGoldenTableParity(t *testing.T) {
	t.Parallel()

	table := loadResolutionGolden(t)
	for _, tc := range table.Cases {
		got, err := SelectVMAFModelVersion(tc.Width, tc.Height)
		if err != nil {
			t.Errorf("SelectVMAFModelVersion(%d, %d): %v", tc.Width, tc.Height, err)
			continue
		}
		if got != tc.Model {
			t.Errorf("SelectVMAFModelVersion(%d, %d) = %q, Python rule says %q",
				tc.Width, tc.Height, got, tc.Model)
		}
		if neg := NegModelFor(got); neg != tc.NegModel {
			t.Errorf("NegModelFor(%q) = %q, Python rule says %q", got, neg, tc.NegModel)
		}
	}
	for _, tc := range table.Rejected {
		if got, err := SelectVMAFModelVersion(tc.Width, tc.Height); err == nil {
			t.Errorf("SelectVMAFModelVersion(%d, %d) = %q, want an error",
				tc.Width, tc.Height, got)
		}
	}
}
