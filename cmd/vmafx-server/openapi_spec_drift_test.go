// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package main

import (
	"encoding/json"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/getkin/kin-openapi/openapi3"

	"github.com/VMAFx/vmafx/gen/go/oapi"
)

// specYAML is the OpenAPI contract the generated stubs are built from.
var specYAML = filepath.Join("..", "..", "api", "openapi", "vmafx-server-v1.yaml")

// canonical marshals an OpenAPI document and reads it back as plain data, so
// two documents compare by content, not by Go pointer identity.
func canonical(t *testing.T, doc *openapi3.T) map[string]any {
	t.Helper()
	raw, err := json.Marshal(doc)
	if err != nil {
		t.Fatalf("marshal spec: %v", err)
	}
	var out map[string]any
	if err := json.Unmarshal(raw, &out); err != nil {
		t.Fatalf("unmarshal spec: %v", err)
	}
	normaliseOperationIDs(out)
	return out
}

// maxSpecNodes bounds the walk of a decoded spec (HISS-02); the contract has a
// few hundred nodes.
const maxSpecNodes = 100000

// normaliseOperationIDs lower-cases the first letter of every operationId:
// oapi-codegen embeds `GetHealth` for the contract's `getHealth`, a spelling
// difference of the generator, not drift of the contract. Iterative, with an
// explicit stack (HISS-01).
func normaliseOperationIDs(root any) {
	stack := []any{root}
	for visited := 0; len(stack) > 0 && visited < maxSpecNodes; visited++ {
		node := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		switch value := node.(type) {
		case map[string]any:
			for key, child := range value {
				if id, ok := child.(string); key == "operationId" && ok && id != "" {
					value[key] = strings.ToLower(id[:1]) + id[1:]
					continue
				}
				stack = append(stack, child)
			}
		case []any:
			stack = append(stack, value...)
		}
	}
}

// TestEmbeddedSpecMatchesContract fails when gen/go/oapi was not regenerated
// after api/openapi/vmafx-server-v1.yaml changed: the server would then serve
// (GET /openapi.json, Swagger UI) a contract that is not the one in the tree.
// Regenerate with the command in api/openapi/oapi-codegen.yaml.
func TestEmbeddedSpecMatchesContract(t *testing.T) {
	embedded, err := oapi.GetSpec()
	if err != nil {
		t.Fatalf("oapi.GetSpec: %v", err)
	}
	contract, err := openapi3.NewLoader().LoadFromFile(specYAML)
	if err != nil {
		t.Fatalf("load %s: %v", specYAML, err)
	}
	got, want := canonical(t, embedded), canonical(t, contract)
	if reflect.DeepEqual(got, want) {
		return
	}
	for key := range want {
		if !reflect.DeepEqual(got[key], want[key]) {
			t.Errorf("embedded spec differs from %s in %q: regenerate gen/go/oapi", specYAML, key)
		}
	}
	for key := range got {
		if _, ok := want[key]; !ok {
			t.Errorf("embedded spec has %q, %s does not: regenerate gen/go/oapi", key, specYAML)
		}
	}
}
