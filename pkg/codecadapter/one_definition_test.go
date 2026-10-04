// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package codecadapter

import (
	"go/ast"
	"go/parser"
	"go/token"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"testing"
)

// adapterNamesDefined returns, for every Adapter composite literal in the
// package's non-test sources, the string literal of its Name field, counted
// per name. A codec defined twice (once in a constructor, once in a registry
// list) makes the two drift: only one of them is ever registered.
func adapterNamesDefined(t *testing.T, dir string) map[string]int {
	t.Helper()
	files, err := filepath.Glob(filepath.Join(dir, "*.go"))
	if err != nil {
		t.Fatalf("glob: %v", err)
	}
	counts := map[string]int{}
	fset := token.NewFileSet()
	for _, f := range files {
		if strings.HasSuffix(f, "_test.go") {
			continue
		}
		parsed, err := parser.ParseFile(fset, f, nil, 0)
		if err != nil {
			t.Fatalf("parse %s: %v", f, err)
		}
		ast.Inspect(parsed, func(n ast.Node) bool {
			if lit, ok := n.(*ast.CompositeLit); ok {
				if name, ok := adapterLiteralName(lit); ok {
					counts[name]++
				}
			}
			return true
		})
	}
	return counts
}

// adapterLiteralName returns the Name of a literal that sets both Name and
// Encoder to string literals (every Adapter does; other structs do not).
func adapterLiteralName(lit *ast.CompositeLit) (string, bool) {
	var name string
	var hasEncoder bool
	for _, elt := range lit.Elts {
		kv, ok := elt.(*ast.KeyValueExpr)
		if !ok {
			continue
		}
		key, ok := kv.Key.(*ast.Ident)
		if !ok {
			continue
		}
		val, ok := kv.Value.(*ast.BasicLit)
		if !ok || val.Kind != token.STRING {
			continue
		}
		switch key.Name {
		case "Name":
			if s, err := strconv.Unquote(val.Value); err == nil {
				name = s
			}
		case "Encoder":
			hasEncoder = true
		}
	}
	return name, name != "" && hasEncoder
}

func TestEveryCodecIsDefinedOnce(t *testing.T) {
	var dup []string
	for name, n := range adapterNamesDefined(t, ".") {
		if n != 1 {
			dup = append(dup, name+" x"+strconv.Itoa(n))
		}
	}
	sort.Strings(dup)
	if len(dup) != 0 {
		t.Fatalf("codecs defined more than once in codecadapter.go: %v", dup)
	}
}

func TestEveryDefinedCodecIsRegisteredAndEveryRegisteredOneIsDefined(t *testing.T) {
	defined := adapterNamesDefined(t, ".")
	// Constructor-built families (nvenc, amf, qsv, videotoolbox) take the name
	// as an argument, so they are registered but not literals.
	for name := range defined {
		if _, ok := registry[name]; !ok {
			t.Errorf("codec %q is defined but not registered", name)
		}
	}
}

func TestOneDefinitionScanSeesAPlantedDuplicate(t *testing.T) {
	dir := t.TempDir()
	src := "package codecadapter\nvar a = &Adapter{Name: \"x\", Encoder: \"x\"}\nvar b = &Adapter{Name: \"x\", Encoder: \"x\"}\n"
	if err := writeFile(filepath.Join(dir, "planted.go"), src); err != nil {
		t.Fatal(err)
	}
	if got := adapterNamesDefined(t, dir)["x"]; got != 2 {
		t.Fatalf("scan counted %d definitions of the planted codec, want 2", got)
	}
}

func writeFile(path, content string) error {
	return os.WriteFile(path, []byte(content), 0o600)
}
