// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/tenants/source_test.go — the file and Kubernetes
// tenant sources and the refresher (ADR-1519).

package tenants_test

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"k8s.io/apimachinery/pkg/apis/meta/v1/unstructured"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/runtime/schema"
	dynamicfake "k8s.io/client-go/dynamic/fake"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth"
	"github.com/VMAFx/vmafx/cmd/vmafx-controller/tenants"
)

const twoTenants = `
apiVersion: vmafx.dev/v1
kind: VmafxTenant
metadata: {name: acme}
spec:
  tenantId: acme
  enabled: false
  oidc: {issuer: "https://acme.example.com/", jwksEndpoint: "https://acme.example.com/keys", tenantClaim: org_id}
  rbac: {defaultRole: "vmafx:reader", allowedRoles: ["vmafx:reader", "vmafx:writer"]}
---
apiVersion: v1
kind: List
items:
  - apiVersion: vmafx.dev/v1
    kind: VmafxTenant
    metadata: {name: rival}
    spec: {tenantId: rival, oidc: {issuer: "https://rival.example.com", jwksEndpoint: "https://rival.example.com/keys"}}
`

func TestParseDocumentsReadsTenantsAndLists(t *testing.T) {
	specs, err := tenants.ParseDocuments("tenants.yaml", []byte(twoTenants))
	if err != nil {
		t.Fatalf("ParseDocuments: %v", err)
	}
	if len(specs) != 2 || specs[0].Spec.TenantID != "acme" || specs[1].Spec.TenantID != "rival" {
		t.Fatalf("specs = %+v, want acme and rival", specs)
	}
	acme := specs[0]
	if acme.Err != nil || acme.Spec.Enabled == nil || *acme.Spec.Enabled ||
		acme.Spec.OIDC.TenantClaim != "org_id" || len(acme.Spec.RBAC.AllowedRoles) != 2 {
		t.Errorf("acme decoded as %+v (err %v)", acme.Spec, acme.Err)
	}
	if !strings.Contains(acme.Source, "tenants.yaml#1/acme") || !strings.Contains(specs[1].Source, "#2/items[0]/rival") {
		t.Errorf("sources = %q, %q", acme.Source, specs[1].Source)
	}
	if specs, err := tenants.ParseDocuments("empty", []byte("---\n")); err != nil || len(specs) != 0 {
		t.Errorf("an empty stream: %d specs, err %v", len(specs), err)
	}
}

// brokenDocuments are documents a source decodes into entries with Err set.
var brokenDocuments = map[string]string{
	"misspelt field": `{apiVersion: vmafx.dev/v1, kind: VmafxTenant, metadata: {name: x},
  spec: {tenantId: acme, oidc: {issuer: "https://a.example.com", jwksEndpoint: "https://a.example.com/k"}, rbac: {allowedRole: ["vmafx:reader"]}}}`,
	"wrong apiVersion": `{apiVersion: vmafx.dev/v2, kind: VmafxTenant, metadata: {name: x}, spec: {tenantId: acme}}`,
	"other kind":       `{apiVersion: v1, kind: ConfigMap, metadata: {name: x}}`,
	"no spec":          `{apiVersion: vmafx.dev/v1, kind: VmafxTenant, metadata: {name: x}}`,
	"list of others":   `{apiVersion: v1, kind: List, items: [{apiVersion: v1, kind: Secret}]}`,
}

func TestBrokenDocumentsAreRefusedTenantsNotLoadFailures(t *testing.T) {
	for name, doc := range brokenDocuments {
		specs, err := tenants.ParseDocuments("f.yaml", []byte(doc))
		if err != nil || len(specs) != 1 || specs[0].Err == nil {
			t.Errorf("%s: specs %+v err %v, want one entry with Err", name, specs, err)
			continue
		}
		reg, _ := auth.NewTenantRegistry(time.Minute, nil)
		if lerr := reg.Load(specs); lerr == nil {
			t.Errorf("%s: the registry accepted the entry", name)
		}
	}
	if _, err := tenants.ParseDocuments("f.yaml", []byte("tenantId: [unclosed")); err == nil {
		t.Error("a file that is not YAML loaded")
	}
}

func TestFileSource(t *testing.T) {
	path := filepath.Join(t.TempDir(), "tenants.yaml")
	if err := os.WriteFile(path, []byte(twoTenants), 0o600); err != nil {
		t.Fatal(err)
	}
	specs, err := tenants.FileSource{Path: path}.Load(context.Background())
	if err != nil || len(specs) != 2 {
		t.Fatalf("Load: %d specs, err %v", len(specs), err)
	}
	if _, err := (tenants.FileSource{Path: path + ".missing"}).Load(context.Background()); err == nil {
		t.Error("a missing file loaded")
	}
}

// tenantObject returns an unstructured VmafxTenant.
func tenantObject(ns, name string, spec map[string]any) *unstructured.Unstructured {
	return &unstructured.Unstructured{Object: map[string]any{
		"apiVersion": tenants.APIVersion,
		"kind":       tenants.Kind,
		"metadata":   map[string]any{"name": name, "namespace": ns},
		"spec":       spec,
	}}
}

func oidc(host string) map[string]any {
	return map[string]any{"issuer": "https://" + host, "jwksEndpoint": "https://" + host + "/keys"}
}

func TestKubernetesSourceListsItsNamespace(t *testing.T) {
	scheme := runtime.NewScheme()
	listKinds := map[schema.GroupVersionResource]string{tenants.GVR: tenants.Kind + "List"}
	client := dynamicfake.NewSimpleDynamicClientWithCustomListKinds(scheme, listKinds,
		tenantObject("vmafx", "acme", map[string]any{"tenantId": "acme", "oidc": oidc("acme.example.com")}),
		tenantObject("vmafx", "typo", map[string]any{"tenantId": "typo", "oidc": oidc("t.example.com"), "rbacc": map[string]any{}}),
		tenantObject("elsewhere", "rival", map[string]any{"tenantId": "rival", "oidc": oidc("rival.example.com")}),
	)
	specs, err := tenants.KubernetesSource{Client: client, Namespace: "vmafx"}.Load(context.Background())
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	got := map[string]error{}
	for _, s := range specs {
		got[s.Source] = s.Err
	}
	if len(got) != 2 || got["vmafxtenant vmafx/acme"] != nil || got["vmafxtenant vmafx/typo"] == nil {
		t.Errorf("specs = %v, want acme decoded and typo refused, nothing from another namespace", got)
	}
}

// flakySource returns its specs, or err when set.
type flakySource struct {
	specs []auth.NamedTenantSpec
	err   error
	loads int
}

func (f *flakySource) Load(context.Context) ([]auth.NamedTenantSpec, error) {
	f.loads++
	return f.specs, f.err
}
func (f *flakySource) String() string { return "flaky" }

func spec(id string) auth.NamedTenantSpec {
	return auth.NamedTenantSpec{Source: id, Spec: auth.TenantSpec{
		TenantID: id, OIDC: auth.TenantOIDC{Issuer: "https://" + id + ".example.com", JWKSEndpoint: "https://" + id + ".example.com/k"},
	}}
}

func TestRefresherStrictStartLenientRefresh(t *testing.T) {
	reg, _ := auth.NewTenantRegistry(time.Minute, nil)
	bad := spec("broken")
	bad.Spec.RBAC = &auth.TenantRBAC{AllowedRoles: []string{"vmafx:root"}}
	src := &flakySource{specs: []auth.NamedTenantSpec{spec("acme"), bad}}
	r := tenants.NewRefresher(src, reg, time.Hour, nil)
	if err := r.LoadInitial(context.Background()); err == nil {
		t.Fatal("LoadInitial accepted an invalid tenant")
	}
	src.specs = []auth.NamedTenantSpec{spec("acme")}
	if err := r.LoadInitial(context.Background()); err != nil || reg.Count() != 1 {
		t.Fatalf("LoadInitial of a valid set: %v (%d tenants)", err, reg.Count())
	}
	src.specs = []auth.NamedTenantSpec{spec("acme"), spec("rival"), bad}
	errs, err := r.RefreshOnce(context.Background())
	if err != nil || len(errs) != 1 || reg.Count() != 2 {
		t.Fatalf("RefreshOnce: errs %v err %v, %d tenants; want broken dropped, 2 kept", errs, err, reg.Count())
	}
	src.err = errors.New("api down")
	if _, err := r.RefreshOnce(context.Background()); err == nil || reg.Count() != 2 {
		t.Fatalf("a failed refresh: err %v, %d tenants; want the error and the set kept", err, reg.Count())
	}
	src.err = errors.New("unreachable")
	if err := tenants.NewRefresher(src, reg, time.Hour, nil).LoadInitial(context.Background()); err == nil {
		t.Error("LoadInitial succeeded on an unreachable source")
	}
}

func TestRefresherLoopReloadsUntilClosed(t *testing.T) {
	reg, _ := auth.NewTenantRegistry(time.Minute, nil)
	src := &flakySource{specs: []auth.NamedTenantSpec{spec("acme")}}
	r := tenants.NewRefresher(src, reg, 10*time.Millisecond, nil)
	if err := r.LoadInitial(context.Background()); err != nil {
		t.Fatal(err)
	}
	src.specs = []auth.NamedTenantSpec{spec("acme"), spec("rival")}
	r.Start()
	deadline := time.Now().Add(5 * time.Second)
	for reg.Count() != 2 && time.Now().Before(deadline) {
		time.Sleep(5 * time.Millisecond)
	}
	r.Close()
	r.Close()
	if reg.Count() != 2 {
		t.Fatalf("the loop did not reload: %d tenants", reg.Count())
	}
	loads := src.loads
	time.Sleep(50 * time.Millisecond)
	if src.loads != loads {
		t.Errorf("the loop kept loading after Close: %d -> %d", loads, src.loads)
	}
	tenants.NewRefresher(src, reg, time.Hour, nil).Close() // Close without Start returns
}
