{{/*
VMAFX Helm chart helpers.
Ref: https://helm.sh/docs/chart_template_guide/named_templates/
*/}}

{{/*
Expand the name of the chart.
*/}}
{{- define "vmafx.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
Truncated at 63 characters because some Kubernetes name fields are limited.
*/}}
{{- define "vmafx.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/*
Create chart label value.
*/}}
{{- define "vmafx.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels applied to every resource.
*/}}
{{- define "vmafx.labels" -}}
helm.sh/chart: {{ include "vmafx.chart" . }}
{{ include "vmafx.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Selector labels used by Service and workload selectors.
*/}}
{{- define "vmafx.selectorLabels" -}}
app.kubernetes.io/name: {{ include "vmafx.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/*
Service account name.
*/}}
{{- define "vmafx.serviceAccountName" -}}
{{- if .Values.serviceAccount.create }}
{{- default (include "vmafx.fullname" .) .Values.serviceAccount.name }}
{{- else }}
{{- default "default" .Values.serviceAccount.name }}
{{- end }}
{{- end }}

{{/*
Resolve the Kubernetes device-plugin resource name for the requested GPU.

Usage:
  resources:
    limits:
      {{ include "vmafx.gpuResource" . }}: {{ .Values.gpu.count | quote }}

Resolution (vmafx.gpuResourceName, one definition for every workload):
  gpu.resourceName set  →  that name, verbatim (any device plugin or sharing
                           mode, e.g. gpu.intel.com/xe, nvidia.com/mig-1g.10gb)
  nvidia                →  nvidia.com/gpu                    (CUDA backend)
  amd                   →  amd.com/gpu                       (HIP backend via ROCm)
  intel                 →  gpu.intel.com/<gpu.intelDriver>   (SYCL backend;
                           i915 by default, xe for GPUs on the xe kernel driver)
  cpu                   →  (empty — no device-plugin resource inserted)

vmafx.gpuResource is the same name, empty when gpu.enabled is false.
Note: Vulkan was removed as a VMAFX backend in ADR-0726.
See docs/development/gpu-scheduling.md.
*/}}
{{- define "vmafx.gpuResourceName" -}}
{{- $vendor := .Values.gpu.vendor -}}
{{- if and .Values.gpu.resourceName (eq $vendor "cpu") -}}
{{ fail "gpu.resourceName needs a GPU vendor; gpu.vendor is cpu" }}
{{- else if .Values.gpu.resourceName -}}
{{ .Values.gpu.resourceName }}
{{- else if eq $vendor "nvidia" -}}
nvidia.com/gpu
{{- else if eq $vendor "amd" -}}
amd.com/gpu
{{- else if eq $vendor "intel" -}}
gpu.intel.com/{{ .Values.gpu.intelDriver | default "i915" }}
{{- else if eq $vendor "cpu" -}}
{{- /* CPU workload: no device-plugin resource needed */ -}}
{{- else -}}
{{ fail (printf "gpu.vendor must be one of: nvidia, amd, intel, cpu — got %q" $vendor) }}
{{- end }}
{{- end }}

{{- define "vmafx.gpuResource" -}}
{{- if .Values.gpu.enabled }}
{{- include "vmafx.gpuResourceName" . }}
{{- end }}
{{- end }}

{{/*
Resolve the VMAFX_BACKEND env-var value for the requested GPU vendor.
This tells the vmafx-server which backend to activate on startup.
*/}}
{{- define "vmafx.backendEnvValue" -}}
{{- if eq .Values.gpu.vendor "nvidia" -}}
cuda
{{- else if eq .Values.gpu.vendor "amd" -}}
hip
{{- else if eq .Values.gpu.vendor "intel" -}}
sycl
{{- else -}}
cpu
{{- end }}
{{- end }}

{{/*
Render the Go server container image reference (repository:tag).
Falls back to the canonical published release tag when .Values.image.tag is empty.
*/}}
{{- define "vmafx.image" -}}
{{- $tag := .Values.image.tag | default (include "vmafx.releaseImageTag" .) -}}
{{- printf "%s:%s" .Values.image.repository $tag -}}
{{- end }}

{{/*
Render the canonical release tag used by the operator and node publishers.
GitHub releases are tagged vX.Y.Z while Chart.AppVersion intentionally stores
the bare SemVer value for Kubernetes labels. Strip a pre-existing prefix so a
future v-prefixed AppVersion cannot render vvX.Y.Z.
*/}}
{{- define "vmafx.releaseImageTag" -}}
{{- printf "v%s" (.Chart.AppVersion | toString | trimPrefix "v") -}}
{{- end }}

{{/*
Render the vmafx-operator image reference. Explicit user tags remain verbatim;
the chart default follows the canonical tag published by the release workflow.
*/}}
{{- define "vmafx.operatorImage" -}}
{{- $tag := .Values.operator.image.tag | default (include "vmafx.releaseImageTag" .) -}}
{{- printf "%s:%s" .Values.operator.image.repository $tag -}}
{{- end }}

{{/*
Render the vmafx-node container image reference.
The shipped values name the release repository explicitly. The repository
fallback remains for custom chart values that intentionally derive a sibling
node package. Override via .Values.node.image.repository / .Values.node.image.tag.
*/}}
{{- define "vmafx.nodeImage" -}}
{{- $repo := .Values.node.image.repository | default (printf "%s-node" .Values.image.repository) -}}
{{- $tag  := .Values.node.image.tag        | default (include "vmafx.releaseImageTag" .) -}}
{{- printf "%s:%s" $repo $tag -}}
{{- end }}

{{/*
Shared pod-spec fragments used by Deployment, Job, and StatefulSet.
Extracted here to avoid triplicating the container spec.
*/}}
{{- define "vmafx.containerSpec" -}}
- name: {{ include "vmafx.name" . }}
  image: {{ include "vmafx.image" . }}
  imagePullPolicy: {{ .Values.image.pullPolicy }}
  ports:
    - name: http
      containerPort: {{ .Values.service.targetPort }}
      protocol: TCP
  env:
    - name: VMAFX_BACKEND
      value: {{ include "vmafx.backendEnvValue" . | quote }}
  {{- range $k, $v := .Values.env }}
    - name: {{ $k | quote }}
      value: {{ $v | quote }}
  {{- end }}
  envFrom:
    - configMapRef:
        name: {{ include "vmafx.fullname" . }}-config
  {{- with .Values.envFrom }}
  {{- toYaml . | nindent 4 }}
  {{- end }}
  resources:
    limits:
      cpu: {{ .Values.resources.limits.cpu | quote }}
      memory: {{ .Values.resources.limits.memory | quote }}
    {{- if and .Values.gpu.enabled (include "vmafx.gpuResource" .) }}
      {{ include "vmafx.gpuResource" . }}: {{ .Values.gpu.count | quote }}
    {{- end }}
    requests:
      cpu: {{ .Values.resources.requests.cpu | quote }}
      memory: {{ .Values.resources.requests.memory | quote }}
  securityContext:
    {{- toYaml .Values.securityContext | nindent 4 }}
  volumeMounts:
    - name: tmp
      mountPath: /tmp
  {{- if .Values.persistence.corpus.enabled }}
    - name: corpus
      mountPath: {{ .Values.persistence.corpus.mountPath }}
      readOnly: true
  {{- end }}
  {{- if .Values.persistence.output.enabled }}
    - name: output
      mountPath: {{ .Values.persistence.output.mountPath }}
  {{- end }}
  {{- if .Values.persistence.models.enabled }}
    - name: models
      mountPath: {{ .Values.persistence.models.mountPath }}
      readOnly: true
  {{- end }}
{{- end }}

{{/*
Shared volume list used by all workload types.
*/}}
{{- define "vmafx.volumes" -}}
- name: tmp
  emptyDir: {}
{{- if .Values.persistence.corpus.enabled }}
- name: corpus
  persistentVolumeClaim:
    claimName: {{ include "vmafx.fullname" . }}-corpus
    readOnly: true
{{- end }}
{{- if .Values.persistence.output.enabled }}
- name: output
  persistentVolumeClaim:
    claimName: {{ include "vmafx.fullname" . }}-output
{{- end }}
{{- if .Values.persistence.models.enabled }}
- name: models
  persistentVolumeClaim:
    claimName: {{ include "vmafx.fullname" . }}-models
    readOnly: true
{{- end }}
{{- end }}

{{/*
Shared pod-level spec (security context, affinity, tolerations, etc.).
Rendered inside spec.template.spec for all workload types.
*/}}
{{- define "vmafx.podSpec" -}}
serviceAccountName: {{ include "vmafx.serviceAccountName" . }}
securityContext:
  {{- toYaml .Values.podSecurityContext | nindent 2 }}
{{- with .Values.imagePullSecrets }}
imagePullSecrets:
  {{- toYaml . | nindent 2 }}
{{- end }}
{{- with .Values.nodeSelector }}
nodeSelector:
  {{- toYaml . | nindent 2 }}
{{- end }}
{{- with .Values.affinity }}
affinity:
  {{- toYaml . | nindent 2 }}
{{- end }}
{{- with .Values.tolerations }}
tolerations:
  {{- toYaml . | nindent 2 }}
{{- end }}
{{- with .Values.topologySpreadConstraints }}
topologySpreadConstraints:
  {{- toYaml . | nindent 2 }}
{{- end }}
containers:
  {{- include "vmafx.containerSpec" . | nindent 2 }}
volumes:
  {{- include "vmafx.volumes" . | nindent 2 }}
{{- end }}

{{/*
vmafx.tenantSource — "kubernetes" when the controller reads its tenants from
VmafxTenant resources (auth.tenantSource, implied by a non-empty auth.tenants),
else "". ADR-1519.
*/}}
{{- define "vmafx.tenantSource" -}}
{{- if .Values.auth.tenantSource -}}
{{ .Values.auth.tenantSource }}
{{- else if .Values.auth.tenants -}}
kubernetes
{{- end -}}
{{- end }}

{{/*
The controller's own service account (ADR-1592): the only account bound to
the VmafxTenant reader Role. The server, job and node pods keep
vmafx.serviceAccountName, which holds no RBAC.
*/}}
{{- define "vmafx.controllerServiceAccountName" -}}
{{- printf "%s-controller" (include "vmafx.serviceAccountName" .) -}}
{{- end }}

{{/*
vmafx-controller image (ADR-1589); the tag defaults to the release tag.
*/}}
{{- define "vmafx.controllerImage" -}}
{{- $tag := .Values.controller.image.tag | default (include "vmafx.releaseImageTag" .) -}}
{{- printf "%s:%s" .Values.controller.image.repository $tag -}}
{{- end }}

{{/*
In-cluster host of the chart's controller Service.
*/}}
{{- define "vmafx.controllerHost" -}}
{{- printf "%s-controller.%s.svc" (include "vmafx.fullname" .) .Release.Namespace -}}
{{- end }}

{{/*
gRPC address the nodes use: node.controllerAddr, else the chart's controller
when controller.enabled, else empty (standalone nodes).
*/}}
{{- define "vmafx.nodeControllerAddr" -}}
{{- if .Values.node.controllerAddr -}}
{{ .Values.node.controllerAddr }}
{{- else if .Values.controller.enabled -}}
{{ printf "%s:%v" (include "vmafx.controllerHost" .) .Values.controller.grpcPort }}
{{- end -}}
{{- end }}

{{/*
Controller token volume and mount (vmafx.controllerTokenVolume /
vmafx.controllerTokenMount) for a workload whose <component>.controllerToken
names a Secret; call with (dict "token" .Values.node.controllerToken).
*/}}
{{- define "vmafx.controllerTokenPath" -}}
/var/run/secrets/vmafx/controller-token/token
{{- end }}
{{- define "vmafx.controllerTokenVolume" -}}
{{- with .token.secretName }}
- name: controller-token
  secret:
    secretName: {{ . }}
    defaultMode: 0400
    items:
      - key: {{ $.token.key | default "token" }}
        path: token
{{- end }}
{{- end }}
{{- define "vmafx.controllerTokenMount" -}}
{{- if .token.secretName }}
- name: controller-token
  mountPath: /var/run/secrets/vmafx/controller-token
  readOnly: true
{{- end }}
{{- end }}

{{/*
Auth environment of the controller (ADR-0794, ADR-1519, ADR-1577). With a
tenant registry the controller reads VmafxTenants and gets no global provider
setting (it refuses them next to a registry); otherwise the single provider and
the scoring roots.
*/}}
{{- define "vmafx.controllerAuthEnv" -}}
- name: VMAFX_AUTH_DISABLED
  value: {{ .Values.auth.disabled | toString | quote }}
{{- if eq (include "vmafx.tenantSource" .) "kubernetes" }}
- name: VMAFX_AUTH_TENANTS_SOURCE
  value: "kubernetes"
- name: VMAFX_AUTH_TENANTS_NAMESPACE
  value: {{ .Release.Namespace | quote }}
{{- else }}
{{- with .Values.auth.jwksEndpoint }}
- name: VMAFX_JWKS_ENDPOINT
  value: {{ . | quote }}
{{- end }}
{{- with .Values.auth.issuer }}
- name: VMAFX_AUTH_ISSUER
  value: {{ . | quote }}
{{- end }}
{{- with .Values.auth.audience }}
- name: VMAFX_AUTH_AUDIENCE
  value: {{ . | quote }}
{{- end }}
{{- with .Values.auth.tenantClaim }}
- name: VMAFX_AUTH_TENANT_CLAIM
  value: {{ . | quote }}
{{- end }}
{{- with .Values.auth.rolesClaim }}
- name: VMAFX_AUTH_ROLES_CLAIM
  value: {{ . | quote }}
{{- end }}
{{- with .Values.auth.scoringRoots }}
- name: VMAFX_SCORING_ROOTS
  value: {{ join "," . | quote }}
{{- end }}
{{- end }}
{{- end }}
