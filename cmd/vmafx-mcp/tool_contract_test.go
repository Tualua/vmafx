// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

// tool_contract_test.go compares the tools this server serves with the
// Python server's tool list.
//
// The list is mcp-server/vmaf-mcp/tool-contract.json, which the Python server
// derives from its own _list_tools() (`python3 -m vmaf_mcp.tool_contract
// --write`) and its test suite keeps current
// (mcp-server/vmaf-mcp/tests/test_tool_contract.py). Every Python tool must be
// served here with the same property names, JSON types and required
// arguments; every other tool served here must be declared in goOnlyTools.

package main

import (
	"context"
	"encoding/json"
	"fmt"
	"maps"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"sort"
	"testing"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

// toolContractPath is the Python server's tool list, relative to this package.
var toolContractPath = filepath.Join("..", "..", "mcp-server", "vmaf-mcp", "tool-contract.json")

// goOnlyTools are the gRPC control-plane tools only this server serves
// (ADR-1184; cmd/vmafx-mcp/AGENTS.md invariant 17).
var goOnlyTools = []string{"cancel_job", "get_job", "list_jobs", "submit_job", "vmaf_score_remote"}

// contractTool is one tool's entry: property name to JSON type (null when a
// property declares none) and the sorted required argument names.
type contractTool struct {
	Properties map[string]any `json:"properties"`
	Required   []string       `json:"required"`
}

type toolContract struct {
	SchemaVersion int                     `json:"schema_version"`
	Tools         map[string]contractTool `json:"tools"`
}

func loadToolContract(t *testing.T) toolContract {
	t.Helper()
	raw, err := os.ReadFile(toolContractPath)
	if err != nil {
		t.Fatalf("read %s: %v", toolContractPath, err)
	}
	var contract toolContract
	if err := json.Unmarshal(raw, &contract); err != nil {
		t.Fatalf("parse %s: %v", toolContractPath, err)
	}
	if contract.SchemaVersion != 1 || len(contract.Tools) == 0 {
		t.Fatalf("%s: schema_version %d with %d tools; expected version 1 and a tool list",
			toolContractPath, contract.SchemaVersion, len(contract.Tools))
	}
	return contract
}

// servedTools boots the server in process and returns its tools/list result.
func servedTools(t *testing.T) []*mcp.Tool {
	t.Helper()
	srv, err := buildServer(nil)
	if err != nil {
		t.Fatalf("buildServer: %v", err)
	}
	client := mcp.NewClient(&mcp.Implementation{Name: "test-client", Version: "0.0.1"}, nil)
	serverSide, clientSide := mcp.NewInMemoryTransports()
	ctx := context.Background()
	if _, err := srv.Connect(ctx, serverSide, nil); err != nil {
		t.Fatalf("server.Connect: %v", err)
	}
	session, err := client.Connect(ctx, clientSide, nil)
	if err != nil {
		t.Fatalf("client.Connect: %v", err)
	}
	t.Cleanup(func() {
		if err := session.Close(); err != nil {
			t.Logf("session.Close: %v", err)
		}
	})
	result, err := session.ListTools(ctx, &mcp.ListToolsParams{})
	if err != nil {
		t.Fatalf("ListTools: %v", err)
	}
	return result.Tools
}

// contractEntry reduces a served input schema to the contract's form.
func contractEntry(inputSchema any) (contractTool, error) {
	raw, err := json.Marshal(inputSchema)
	if err != nil {
		return contractTool{}, fmt.Errorf("marshal inputSchema: %w", err)
	}
	var schema struct {
		Properties map[string]map[string]any `json:"properties"`
		Required   []string                  `json:"required"`
	}
	if err := json.Unmarshal(raw, &schema); err != nil {
		return contractTool{}, fmt.Errorf("parse inputSchema: %w", err)
	}
	entry := contractTool{Properties: map[string]any{}, Required: []string{}}
	for name, spec := range schema.Properties {
		entry.Properties[name] = spec["type"]
	}
	entry.Required = append(entry.Required, schema.Required...)
	sort.Strings(entry.Required)
	return entry, nil
}

func servedContract(t *testing.T, tools []*mcp.Tool) map[string]contractTool {
	t.Helper()
	served := make(map[string]contractTool, len(tools))
	for _, tool := range tools {
		entry, err := contractEntry(tool.InputSchema)
		if err != nil {
			t.Fatalf("tool %q: %v", tool.Name, err)
		}
		served[tool.Name] = entry
	}
	return served
}

// toolSetProblems lists the Python tools this server does not serve and the
// served tools that are neither Python tools nor declared Go-only.
func toolSetProblems(served map[string]contractTool, contract toolContract, goOnly []string) []string {
	var problems []string
	for _, name := range slices.Sorted(maps.Keys(contract.Tools)) {
		if _, ok := served[name]; !ok {
			problems = append(problems, fmt.Sprintf("missing Python tool %q", name))
		}
	}
	for _, name := range slices.Sorted(maps.Keys(served)) {
		_, shared := contract.Tools[name]
		if !shared && !slices.Contains(goOnly, name) {
			problems = append(problems, fmt.Sprintf("tool %q is neither in %s nor in goOnlyTools", name, toolContractPath))
		}
	}
	for _, name := range goOnly {
		if _, ok := served[name]; !ok {
			problems = append(problems, fmt.Sprintf("declared Go-only tool %q is not served", name))
		}
	}
	return problems
}

// schemaProblems lists the Python tools whose properties, JSON types or
// required arguments differ here.
func schemaProblems(served map[string]contractTool, contract toolContract) []string {
	var problems []string
	for _, name := range slices.Sorted(maps.Keys(contract.Tools)) {
		got, ok := served[name]
		want := contract.Tools[name]
		if !ok || reflect.DeepEqual(got, want) {
			continue
		}
		problems = append(problems, fmt.Sprintf("tool %q: served %+v, Python %+v", name, got, want))
	}
	return problems
}

func TestToolListMatchesPython(t *testing.T) {
	t.Parallel()
	contract := loadToolContract(t)
	served := servedContract(t, servedTools(t))
	for _, problem := range toolSetProblems(served, contract, goOnlyTools) {
		t.Error(problem)
	}
}

func TestToolSchemasMatchPython(t *testing.T) {
	t.Parallel()
	contract := loadToolContract(t)
	served := servedContract(t, servedTools(t))
	for _, problem := range schemaProblems(served, contract) {
		t.Error(problem)
	}
}

// TestToolContractComparisonRefusesDrift plants each kind of drift into the
// served list and requires the comparisons to report it.
func TestToolContractComparisonRefusesDrift(t *testing.T) {
	t.Parallel()
	contract := loadToolContract(t)
	base := servedContract(t, servedTools(t))
	plants := map[string]func(map[string]contractTool){
		"python tool missing":   func(s map[string]contractTool) { delete(s, "vmaf_vpl") },
		"undeclared extra tool": func(s map[string]contractTool) { s["vmaf_extra"] = contractTool{} },
		"go-only tool missing":  func(s map[string]contractTool) { delete(s, "submit_job") },
		"required dropped": func(s map[string]contractTool) {
			s["vmaf_score"] = contractTool{Properties: s["vmaf_score"].Properties, Required: []string{}}
		},
		"property type changed": func(s map[string]contractTool) {
			props := maps.Clone(s["vmaf_roi"].Properties)
			props["width"] = "string"
			s["vmaf_roi"] = contractTool{Properties: props, Required: s["vmaf_roi"].Required}
		},
	}
	for name, plant := range plants {
		served := maps.Clone(base)
		plant(served)
		problems := append(toolSetProblems(served, contract, goOnlyTools), schemaProblems(served, contract)...)
		if len(problems) == 0 {
			t.Errorf("planted drift %q was not reported", name)
		}
	}
	if problems := append(toolSetProblems(base, contract, goOnlyTools), schemaProblems(base, contract)...); len(problems) != 0 {
		t.Errorf("unplanted list reported %v", problems)
	}
}
