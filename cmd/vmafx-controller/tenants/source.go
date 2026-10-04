// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/tenants/source.go — where the controller reads its
// tenants from: VmafxTenant resources in the Kubernetes API, or a file of
// VmafxTenant documents for deployments outside Kubernetes.
//
// Both sources yield the same auth.NamedTenantSpec values; validation and
// enforcement are auth.TenantRegistry's. A spec with a field the CRD does not
// define is refused, so a misspelt key ("allowedRole") fails instead of
// silently taking the default. A resource that cannot be decoded is returned
// with its error set (the registry refuses it); only an unreadable source, or
// a file that is not YAML or JSON, fails the load as a whole.
//
// ADR-1519: tenant registry.

// Package tenants loads the controller's tenant configuration.
package tenants

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"
	"time"

	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime/schema"
	utilyaml "k8s.io/apimachinery/pkg/util/yaml"
	"k8s.io/client-go/dynamic"
	"k8s.io/client-go/rest"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth"
)

const (
	// APIVersion and Kind identify a VmafxTenant document.
	APIVersion = "vmafx.dev/v1"
	Kind       = "VmafxTenant"

	// maxFileBytes bounds a tenant file.
	maxFileBytes = 4 << 20
	// maxDocuments bounds the documents of a tenant file and the items of a
	// list.
	maxDocuments = 4096
	// defaultListTimeout bounds one list call against the Kubernetes API.
	defaultListTimeout = 10 * time.Second
	// serviceAccountNamespaceFile holds the pod's namespace in a cluster.
	serviceAccountNamespaceFile = "/var/run/secrets/kubernetes.io/serviceaccount/namespace"
)

// GVR is the resource of the VmafxTenant CRD.
var GVR = schema.GroupVersionResource{Group: "vmafx.dev", Version: "v1", Resource: "vmafxtenants"}

// Source loads the tenant specs.
type Source interface {
	Load(ctx context.Context) ([]auth.NamedTenantSpec, error)
	String() string
}

// FileSource reads a YAML or JSON stream of VmafxTenant documents, or of
// lists of them ("kind: List" or "kind: VmafxTenantList", as
// `kubectl get vmafxtenants -o yaml` writes).
type FileSource struct {
	Path string
}

// String names the source in logs.
func (f FileSource) String() string { return "file " + f.Path }

// Load reads and parses the file.
func (f FileSource) Load(_ context.Context) ([]auth.NamedTenantSpec, error) {
	fh, err := os.Open(f.Path)
	if err != nil {
		return nil, fmt.Errorf("tenants: %w", err)
	}
	defer fh.Close() //nolint:errcheck // read-only file
	data, err := io.ReadAll(io.LimitReader(fh, maxFileBytes+1))
	if err != nil {
		return nil, fmt.Errorf("tenants: read %s: %w", f.Path, err)
	}
	if len(data) > maxFileBytes {
		return nil, fmt.Errorf("tenants: %s is larger than %d bytes", f.Path, maxFileBytes)
	}
	return ParseDocuments(f.Path, data)
}

// document is the part of a Kubernetes object the parser reads.
type document struct {
	APIVersion string `json:"apiVersion"`
	Kind       string `json:"kind"`
	Metadata   struct {
		Name string `json:"name"`
	} `json:"metadata"`
	Spec  json.RawMessage   `json:"spec"`
	Items []json.RawMessage `json:"items"`
}

// ParseDocuments parses a YAML or JSON stream of VmafxTenant documents and
// lists of them. name labels the specs ("<name>#<n>/<resource name>").
func ParseDocuments(name string, data []byte) ([]auth.NamedTenantSpec, error) {
	dec := utilyaml.NewYAMLOrJSONDecoder(bytes.NewReader(data), 4096)
	var out []auth.NamedTenantSpec
	for n := 1; n <= maxDocuments; n++ {
		var raw json.RawMessage
		err := dec.Decode(&raw)
		if errors.Is(err, io.EOF) {
			return out, nil
		}
		if err != nil {
			return nil, fmt.Errorf("tenants: %s document %d: %w", name, n, err)
		}
		specs, err := parseDocument(fmt.Sprintf("%s#%d", name, n), raw)
		if err != nil {
			return nil, err
		}
		out = append(out, specs...)
	}
	return nil, fmt.Errorf("tenants: %s has more than %d documents", name, maxDocuments)
}

// parseDocument parses one document: a VmafxTenant, a list of them, or an
// empty document.
func parseDocument(label string, raw json.RawMessage) ([]auth.NamedTenantSpec, error) {
	if len(bytes.TrimSpace(raw)) == 0 || string(bytes.TrimSpace(raw)) == "null" {
		return nil, nil
	}
	var doc document
	if err := json.Unmarshal(raw, &doc); err != nil {
		return nil, fmt.Errorf("tenants: %s: %w", label, err)
	}
	switch doc.Kind {
	case Kind:
		return []auth.NamedTenantSpec{tenantFromDocument(label, doc)}, nil
	case "List", Kind + "List":
		return tenantsFromItems(label, doc.Items)
	default:
		err := fmt.Errorf("kind %q is not %s or a list of them", doc.Kind, Kind)
		return []auth.NamedTenantSpec{{Source: label, Err: err}}, nil
	}
}

// tenantsFromItems parses the items of a list; each must be a VmafxTenant.
func tenantsFromItems(label string, items []json.RawMessage) ([]auth.NamedTenantSpec, error) {
	if len(items) > maxDocuments {
		return nil, fmt.Errorf("tenants: %s: list has more than %d items", label, maxDocuments)
	}
	out := make([]auth.NamedTenantSpec, 0, len(items))
	for i, raw := range items {
		var doc document
		itemLabel := fmt.Sprintf("%s/items[%d]", label, i)
		if err := json.Unmarshal(raw, &doc); err != nil {
			out = append(out, auth.NamedTenantSpec{Source: itemLabel, Err: err})
			continue
		}
		if doc.Kind != Kind {
			err := fmt.Errorf("kind %q is not %s", doc.Kind, Kind)
			out = append(out, auth.NamedTenantSpec{Source: itemLabel, Err: err})
			continue
		}
		out = append(out, tenantFromDocument(itemLabel, doc))
	}
	return out, nil
}

// tenantFromDocument checks the API version and decodes the spec strictly;
// a failure is returned in the entry's Err.
func tenantFromDocument(label string, doc document) auth.NamedTenantSpec {
	source := label + "/" + doc.Metadata.Name
	if doc.APIVersion != APIVersion {
		err := fmt.Errorf("apiVersion %q is not %s", doc.APIVersion, APIVersion)
		return auth.NamedTenantSpec{Source: source, Err: err}
	}
	spec, err := DecodeSpec(doc.Spec)
	return auth.NamedTenantSpec{Source: source, Spec: spec, Err: err}
}

// DecodeSpec decodes a VmafxTenant spec, refusing fields the CRD does not
// define and a missing spec.
func DecodeSpec(raw json.RawMessage) (auth.TenantSpec, error) {
	var spec auth.TenantSpec
	if len(bytes.TrimSpace(raw)) == 0 || string(bytes.TrimSpace(raw)) == "null" {
		return spec, errors.New("spec is missing")
	}
	dec := json.NewDecoder(bytes.NewReader(raw))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&spec); err != nil {
		return spec, fmt.Errorf("spec: %w", err)
	}
	return spec, nil
}

// KubernetesSource lists the VmafxTenant resources of one namespace.
type KubernetesSource struct {
	Client    dynamic.Interface
	Namespace string
	Timeout   time.Duration
}

// NewInClusterSource returns a KubernetesSource using the pod's service
// account. An empty namespace means the pod's own namespace.
func NewInClusterSource(namespace string) (KubernetesSource, error) {
	cfg, err := rest.InClusterConfig()
	if err != nil {
		return KubernetesSource{}, fmt.Errorf("tenants: Kubernetes source needs an in-cluster service account: %w", err)
	}
	client, err := dynamic.NewForConfig(cfg)
	if err != nil {
		return KubernetesSource{}, fmt.Errorf("tenants: Kubernetes client: %w", err)
	}
	if namespace == "" {
		raw, rerr := os.ReadFile(serviceAccountNamespaceFile)
		if rerr != nil {
			return KubernetesSource{}, fmt.Errorf("tenants: read the pod namespace: %w", rerr)
		}
		namespace = strings.TrimSpace(string(raw))
	}
	return KubernetesSource{Client: client, Namespace: namespace}, nil
}

// String names the source in logs.
func (k KubernetesSource) String() string { return "vmafxtenants in namespace " + k.Namespace }

// Load lists the namespace's VmafxTenant resources.
func (k KubernetesSource) Load(ctx context.Context) ([]auth.NamedTenantSpec, error) {
	timeout := k.Timeout
	if timeout <= 0 {
		timeout = defaultListTimeout
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	list, err := k.Client.Resource(GVR).Namespace(k.Namespace).List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, fmt.Errorf("tenants: list %s: %w", k, err)
	}
	if len(list.Items) > maxDocuments {
		return nil, fmt.Errorf("tenants: %s holds %d resources, more than %d", k, len(list.Items), maxDocuments)
	}
	out := make([]auth.NamedTenantSpec, 0, len(list.Items))
	for _, item := range list.Items {
		source := fmt.Sprintf("vmafxtenant %s/%s", k.Namespace, item.GetName())
		raw, err := json.Marshal(item.Object["spec"])
		if err != nil {
			out = append(out, auth.NamedTenantSpec{Source: source, Err: err})
			continue
		}
		spec, err := DecodeSpec(raw)
		out = append(out, auth.NamedTenantSpec{Source: source, Spec: spec, Err: err})
	}
	return out, nil
}
