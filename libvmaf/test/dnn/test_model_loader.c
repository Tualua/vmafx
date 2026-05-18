/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "test.h"

#include "dnn/model_loader.h"

static char *test_sniff_by_extension(void)
{
    mu_assert("json → SVM", vmaf_dnn_sniff_kind("foo.json") == VMAF_MODEL_KIND_SVM);
    mu_assert("pkl → SVM", vmaf_dnn_sniff_kind("foo.pkl") == VMAF_MODEL_KIND_SVM);
    mu_assert("onnx → DNN_FR", vmaf_dnn_sniff_kind("foo.onnx") == VMAF_MODEL_KIND_DNN_FR);
    mu_assert("unknown ext → -1", vmaf_dnn_sniff_kind("foo.bin") == -1);
    mu_assert("NULL → -1", vmaf_dnn_sniff_kind(NULL) == -1);
    return NULL;
}

static char *test_size_cap(void)
{
    /* Use a regular file that exists on every supported host as a proxy for
     * "regular file, within limits". /etc/passwd ships on Linux and macOS;
     * /etc/hostname is Linux-only and made the macOS leg fail with -ENOENT
     * when this assertion expects -E2BIG (size cap) or 0 (success). */
#ifdef _WIN32
    const char *probe = "C:\\Windows\\System32\\cmd.exe";
#else
    const char *probe = "/etc/passwd";
#endif
    int err = vmaf_dnn_validate_onnx(probe, 1);
    mu_assert("expected -E2BIG for 1-byte cap", err == -E2BIG || err == 0);
    err = vmaf_dnn_validate_onnx("/definitely/does/not/exist.onnx", 0);
    mu_assert("expected errno for missing file", err < 0);
    return NULL;
}

static char *test_validate_null_path(void)
{
    const int err = vmaf_dnn_validate_onnx(NULL, 0);
    mu_assert("NULL path → -EINVAL", err == -EINVAL);
    return NULL;
}

#ifndef _WIN32
/* Write @p len bytes of @p data to a fresh temp file; returns malloc'd path. */
static char *write_temp(const unsigned char *data, size_t len)
{
    char tmpl[] = "/tmp/vmaf-dnn-validate-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0)
        return NULL;
    const ssize_t w = write(fd, data, len);
    close(fd);
    if (w != (ssize_t)len)
        return NULL;
    return strdup(tmpl);
}

/* Write @p len bytes of @p data to an exact path with user-only perms (0600).
 * Unlike fopen(path, "wb") this doesn't inherit umask and can't race into a
 * world-writable state (CodeQL cpp/world-writable-file-creation). */
static int write_file_600(const char *path, const unsigned char *data, size_t len)
{
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    const ssize_t w = write(fd, data, len);
    const int rc = close(fd);
    if (w != (ssize_t)len || rc != 0)
        return -1;
    return 0;
}
#endif /* !_WIN32 */

/* Open @p path for writing with user-only perms (0600), returning a buffered
 * FILE* via fdopen so existing fprintf-based test code keeps working. Same
 * umask-safety motivation as write_file_600. Returns NULL on any error.
 * Available on both POSIX and MinGW: open()/close() come via <fcntl.h> +
 * <io.h>, fdopen() via <stdio.h>. The 0600 mode is a no-op on Windows
 * (NTFS uses ACLs), but harmless. */
static FILE *fopen_w_600(const char *path)
{
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return NULL;
    FILE *fp = fdopen(fd, "w");
    if (!fp)
        (void)close(fd);
    return fp;
}

#ifndef _WIN32

static char *test_validate_zero_byte(void)
{
    char *path = write_temp((const unsigned char *)"", 0);
    mu_assert("temp file creation failed", path != NULL);
    const int err = vmaf_dnn_validate_onnx(path, 0);
    (void)remove(path);
    free(path);
    mu_assert("empty file → -EBADMSG", err == -EBADMSG);
    return NULL;
}

static char *test_validate_allowed_onnx(void)
{
    /* Minimal ModelProto { graph { node { op_type = "Conv" } } } */
    const unsigned char buf[] = {0x3A, 0x08, 0x0A, 0x06, 0x22, 0x04, 'C', 'o', 'n', 'v'};
    char *path = write_temp(buf, sizeof(buf));
    mu_assert("temp file creation failed", path != NULL);
    const int err = vmaf_dnn_validate_onnx(path, 0);
    (void)remove(path);
    free(path);
    mu_assert("allowed Conv onnx → 0", err == 0);
    return NULL;
}

static char *test_validate_disallowed_onnx(void)
{
    /* ADR-0169 / T6-5: Loop joined the allowlist, so a bare Loop is no
     * longer an example of a forbidden op. Use Scan, which stays
     * rejected by design (see ADR-0169 § Alternatives considered). */
    const unsigned char buf[] = {0x3A, 0x08, 0x0A, 0x06, 0x22, 0x04, 'S', 'c', 'a', 'n'};
    char *path = write_temp(buf, sizeof(buf));
    mu_assert("temp file creation failed", path != NULL);
    const int err = vmaf_dnn_validate_onnx(path, 0);
    (void)remove(path);
    free(path);
    mu_assert("disallowed Scan onnx → -EPERM", err == -EPERM);
    return NULL;
}

static char *test_validate_symlink_to_dir(void)
{
    /* Path hardening: realpath() must resolve a symlink to /tmp (a
     * directory) before the S_ISREG gate, so validate must reject. */
    char link_path[] = "/tmp/vmaf-dnn-dirlink-XXXXXX";
    int fd = mkstemp(link_path);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);
    (void)remove(link_path);
    mu_assert("symlink() failed", symlink("/tmp", link_path) == 0);
    const int err = vmaf_dnn_validate_onnx(link_path, 0);
    (void)remove(link_path);
    /* /tmp is not a regular file → stat_regular returns -ENOENT. */
    mu_assert("symlink-to-dir → -ENOENT", err == -ENOENT);
    return NULL;
}

/* Allowed ONNX payload (single Conv node) reused across jail tests. */
static const unsigned char kAllowedOnnx[] = {0x3A, 0x08, 0x0A, 0x06, 0x22,
                                             0x04, 'C',  'o',  'n',  'v'};

static char *test_jail_unset_accepts_anywhere(void)
{
    /* With VMAF_TINY_MODEL_DIR unset the jail is a no-op; a valid model
     * anywhere in the filesystem must still validate. */
    unsetenv("VMAF_TINY_MODEL_DIR");
    char *path = write_temp(kAllowedOnnx, sizeof(kAllowedOnnx));
    mu_assert("temp file creation failed", path != NULL);
    const int err = vmaf_dnn_validate_onnx(path, 0);
    (void)remove(path);
    free(path);
    mu_assert("jail unset → allowed model validates", err == 0);
    return NULL;
}

static char *test_jail_accepts_model_inside(void)
{
    /* Model sits inside the jail dir → must validate. */
    char jail[] = "/tmp/vmaf-dnn-jail-XXXXXX";
    mu_assert("mkdtemp failed", mkdtemp(jail) != NULL);

    char model_path[PATH_MAX];
    (void)snprintf(model_path, sizeof(model_path), "%s/allowed.onnx", jail);
    mu_assert("write_file_600 model failed",
              write_file_600(model_path, kAllowedOnnx, sizeof(kAllowedOnnx)) == 0);

    mu_assert("setenv failed", setenv("VMAF_TINY_MODEL_DIR", jail, 1) == 0);
    const int err = vmaf_dnn_validate_onnx(model_path, 0);
    unsetenv("VMAF_TINY_MODEL_DIR");
    (void)remove(model_path);
    rmdir(jail);
    mu_assert("model inside jail → 0", err == 0);
    return NULL;
}

static char *test_jail_rejects_model_outside(void)
{
    /* Model sits outside the jail dir → must return -EACCES before any
     * stat() of the model path happens. */
    char jail[] = "/tmp/vmaf-dnn-jail-XXXXXX";
    mu_assert("mkdtemp failed", mkdtemp(jail) != NULL);

    char *outside = write_temp(kAllowedOnnx, sizeof(kAllowedOnnx));
    mu_assert("write_temp failed", outside != NULL);

    mu_assert("setenv failed", setenv("VMAF_TINY_MODEL_DIR", jail, 1) == 0);
    const int err = vmaf_dnn_validate_onnx(outside, 0);
    unsetenv("VMAF_TINY_MODEL_DIR");
    (void)remove(outside);
    free(outside);
    rmdir(jail);
    mu_assert("model outside jail → -EACCES", err == -EACCES);
    return NULL;
}

static char *test_jail_rejects_sibling_prefix(void)
{
    /* Classic prefix-matching trap: if the jail prefix "/tmp/foo" accepted
     * "/tmp/foobar/x.onnx" via naive strncmp, a sibling dir could escape.
     * The trailing-separator normalisation in enforce_tiny_model_jail must
     * reject this. */
    char jail[] = "/tmp/vmaf-dnn-sibprefix-XXXXXX";
    mu_assert("mkdtemp failed", mkdtemp(jail) != NULL);

    char sibling_dir[PATH_MAX];
    (void)snprintf(sibling_dir, sizeof(sibling_dir), "%s-sibling", jail);
    mu_assert("sibling mkdir failed", mkdir(sibling_dir, 0700) == 0);

    char model_path[PATH_MAX];
    (void)snprintf(model_path, sizeof(model_path), "%s/escape.onnx", sibling_dir);
    mu_assert("write_file_600 sibling model failed",
              write_file_600(model_path, kAllowedOnnx, sizeof(kAllowedOnnx)) == 0);

    mu_assert("setenv failed", setenv("VMAF_TINY_MODEL_DIR", jail, 1) == 0);
    const int err = vmaf_dnn_validate_onnx(model_path, 0);
    unsetenv("VMAF_TINY_MODEL_DIR");
    (void)remove(model_path);
    rmdir(sibling_dir);
    rmdir(jail);
    mu_assert("sibling prefix must be rejected → -EACCES", err == -EACCES);
    return NULL;
}

static char *test_jail_rejects_symlink_escape(void)
{
    /* Symlink-escape attempt: place a symlink *inside* the jail pointing
     * at a file *outside* the jail. realpath() on the model argument
     * resolves the symlink to the outside target, which must then fail
     * the prefix check. */
    char jail[] = "/tmp/vmaf-dnn-jailsym-XXXXXX";
    mu_assert("mkdtemp failed", mkdtemp(jail) != NULL);

    char *outside = write_temp(kAllowedOnnx, sizeof(kAllowedOnnx));
    mu_assert("write_temp failed", outside != NULL);

    char link_path[PATH_MAX];
    (void)snprintf(link_path, sizeof(link_path), "%s/escape.onnx", jail);
    mu_assert("symlink() failed", symlink(outside, link_path) == 0);

    mu_assert("setenv failed", setenv("VMAF_TINY_MODEL_DIR", jail, 1) == 0);
    const int err = vmaf_dnn_validate_onnx(link_path, 0);
    unsetenv("VMAF_TINY_MODEL_DIR");
    (void)remove(link_path);
    (void)remove(outside);
    free(outside);
    rmdir(jail);
    mu_assert("symlink escape must be rejected → -EACCES", err == -EACCES);
    return NULL;
}

static char *test_jail_rejects_nonexistent_jail(void)
{
    /* Fails closed on a misconfigured jail: if VMAF_TINY_MODEL_DIR points
     * at a path that does not exist, every validation returns -EACCES. */
    char *model = write_temp(kAllowedOnnx, sizeof(kAllowedOnnx));
    mu_assert("write_temp failed", model != NULL);

    mu_assert("setenv failed",
              setenv("VMAF_TINY_MODEL_DIR", "/tmp/vmaf-does-not-exist-zzzyx", 1) == 0);
    const int err = vmaf_dnn_validate_onnx(model, 0);
    unsetenv("VMAF_TINY_MODEL_DIR");
    (void)remove(model);
    free(model);
    mu_assert("nonexistent jail dir → -EACCES", err == -EACCES);
    return NULL;
}

static char *test_jail_rejects_non_directory(void)
{
    /* Jail env pointing at a regular file (not a directory) must also fail
     * closed — a file is not a prefix anything can sit under. */
    char *jail_file = write_temp((const unsigned char *)"x", 1u);
    mu_assert("write_temp failed", jail_file != NULL);
    char *model = write_temp(kAllowedOnnx, sizeof(kAllowedOnnx));
    mu_assert("write_temp failed", model != NULL);

    mu_assert("setenv failed", setenv("VMAF_TINY_MODEL_DIR", jail_file, 1) == 0);
    const int err = vmaf_dnn_validate_onnx(model, 0);
    unsetenv("VMAF_TINY_MODEL_DIR");
    (void)remove(model);
    (void)remove(jail_file);
    free(model);
    free(jail_file);
    mu_assert("jail-is-file → -EACCES", err == -EACCES);
    return NULL;
}

static char *test_jail_accepts_trailing_slash(void)
{
    /* Normalisation check: a trailing '/' on the env value must not
     * introduce a double-separator that breaks the prefix match. */
    char jail[] = "/tmp/vmaf-dnn-jailslash-XXXXXX";
    mu_assert("mkdtemp failed", mkdtemp(jail) != NULL);

    char jail_with_slash[PATH_MAX];
    (void)snprintf(jail_with_slash, sizeof(jail_with_slash), "%s/", jail);

    char model_path[PATH_MAX];
    (void)snprintf(model_path, sizeof(model_path), "%s/allowed.onnx", jail);
    mu_assert("write_file_600 model failed",
              write_file_600(model_path, kAllowedOnnx, sizeof(kAllowedOnnx)) == 0);

    mu_assert("setenv failed", setenv("VMAF_TINY_MODEL_DIR", jail_with_slash, 1) == 0);
    const int err = vmaf_dnn_validate_onnx(model_path, 0);
    unsetenv("VMAF_TINY_MODEL_DIR");
    (void)remove(model_path);
    rmdir(jail);
    mu_assert("jail with trailing slash → 0", err == 0);
    return NULL;
}
#endif /* !_WIN32 */

static char *test_sidecar_parses(void)
{
#ifdef _WIN32
    char tmpl[MAX_PATH];
    char tmpdir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpdir);
    snprintf(tmpl, sizeof tmpl, "%svmaf-dnn-sidecar-test", tmpdir);
    FILE *tmpf = fopen(tmpl, "w");
    mu_assert("temp file creation failed", tmpf != NULL);
    fclose(tmpf);
#else
    char tmpl[] = "/tmp/vmaf-dnn-sidecar-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);
#endif

    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    /* Touch an empty onnx so sidecar_load doesn't key off its existence. */
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);

    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    (void)fprintf(s, "{\n"
                     "  \"name\": \"vmaf_tiny_fr_v1\",\n"
                     "  \"kind\": \"fr\",\n"
                     "  \"onnx_opset\": 17,\n"
                     "  \"input_name\":  \"features\",\n"
                     "  \"output_name\": \"score\"\n"
                     "}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("sidecar_load failed", err == 0);
    mu_assert("kind FR", meta.kind == VMAF_MODEL_KIND_DNN_FR);
    mu_assert("opset 17", meta.opset == 17);
    mu_assert("name set", meta.name && !strcmp(meta.name, "vmaf_tiny_fr_v1"));
    mu_assert("input set", meta.input_name && !strcmp(meta.input_name, "features"));
    mu_assert("output set", meta.output_name && !strcmp(meta.output_name, "score"));
    vmaf_dnn_sidecar_free(&meta);

    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}

static char *test_sidecar_rejects_null_args(void)
{
    /* NULL onnx_path or NULL out → -EINVAL (line 171). */
    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(NULL, &meta);
    mu_assert("NULL onnx_path rejected", err == -EINVAL);
    err = vmaf_dnn_sidecar_load("model.onnx", NULL);
    mu_assert("NULL out rejected", err == -EINVAL);
    return NULL;
}

static char *test_sidecar_free_null_is_noop(void)
{
    /* free(NULL) must be a no-op (line 241). */
    vmaf_dnn_sidecar_free(NULL);
    mu_assert("sidecar_free(NULL) returned without crashing", 1);
    return NULL;
}

static char *test_sidecar_missing_returns_enoent(void)
{
    /* Sidecar absent → -ENOENT (errno propagation through fopen). */
    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load("/tmp/vmaf-no-such-model-zzz.onnx", &meta);
    mu_assert("missing sidecar → -ENOENT", err == -ENOENT);
    return NULL;
}

#ifndef _WIN32
static char *test_sidecar_parses_kind_nr(void)
{
    /* kind == "nr" branch (line 224). */
    char tmpl[] = "/tmp/vmaf-dnn-sidecar-nr-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);

    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);

    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    (void)fprintf(s, "{\"kind\": \"nr\"}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("sidecar_load nr failed", err == 0);
    mu_assert("kind NR", meta.kind == VMAF_MODEL_KIND_DNN_NR);
    vmaf_dnn_sidecar_free(&meta);

    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}

/* ADR-0173 / T5-3: optional `quant_mode` field defaults to FP32 when
 * absent, parses the four valid strings, and falls back to FP32 on any
 * unknown value. */
static char *test_sidecar_quant_mode_default_fp32(void)
{
    char tmpl[] = "/tmp/vmaf-dnn-quant-default-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);
    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);
    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    /* No quant_mode field — default branch. */
    (void)fprintf(s, "{\"kind\": \"fr\"}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("sidecar_load default failed", err == 0);
    mu_assert("absent quant_mode → FP32", meta.quant_mode == VMAF_QUANT_FP32);
    vmaf_dnn_sidecar_free(&meta);
    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}

static char *test_sidecar_quant_mode_dynamic(void)
{
    char tmpl[] = "/tmp/vmaf-dnn-quant-dyn-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);
    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);
    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    (void)fprintf(s, "{\"kind\": \"fr\", \"quant_mode\": \"dynamic\"}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("sidecar_load dynamic failed", err == 0);
    mu_assert("quant_mode dynamic", meta.quant_mode == VMAF_QUANT_DYNAMIC);
    vmaf_dnn_sidecar_free(&meta);
    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}

static char *test_sidecar_quant_mode_unknown_falls_back(void)
{
    char tmpl[] = "/tmp/vmaf-dnn-quant-bad-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);
    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);
    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    (void)fprintf(s, "{\"kind\": \"fr\", \"quant_mode\": \"int4\"}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("sidecar_load unknown_mode failed", err == 0);
    mu_assert("unknown quant_mode → FP32 (fail-safe)", meta.quant_mode == VMAF_QUANT_FP32);
    vmaf_dnn_sidecar_free(&meta);
    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}

static char *test_sidecar_no_dot_onnx_extension(void)
{
    /* Path that does not end in ".onnx" → falls through to the
     * memcpy(sidecar + len, ".json", 6) branch (line 185). With this
     * branch, foo.bin → foo.bin.json. */
    char tmpl[] = "/tmp/vmaf-dnn-noext-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);

    char model[1024];
    char sidecar[1024];
    (void)snprintf(model, sizeof model, "%s.bin", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.bin.json", tmpl);
    FILE *f = fopen_w_600(model);
    if (f)
        (void)fclose(f);

    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    (void)fprintf(s, "{\"kind\": \"fr\"}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(model, &meta);
    mu_assert("non-onnx sidecar suffix branch", err == 0);
    vmaf_dnn_sidecar_free(&meta);

    (void)remove(sidecar);
    (void)remove(model);
    (void)remove(tmpl);
    return NULL;
}

static char *test_sidecar_oversized_path(void)
{
    /* Path of length > sizeof(sidecar) - 6 → -ENAMETOOLONG (line 178).
     * sizeof(sidecar) is 4096; we need a path > 4090 chars. */
    char *huge = (char *)malloc(4100u);
    mu_assert("alloc failed", huge != NULL);
    memset(huge, 'a', 4096u);
    huge[4096] = '\0';
    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(huge, &meta);
    free(huge);
    mu_assert("oversized path → -ENAMETOOLONG", err == -ENAMETOOLONG);
    return NULL;
}

static char *test_sidecar_malformed_keys_default(void)
{
    /* Sidecar JSON missing keys / malformed values: extract_string and
     * extract_int return NULL/error and the loader falls back to defaults
     * without erroring. Drives the no-key / non-string / non-int branches
     * in extract_string (lines 117-138) and extract_int (lines 147-163). */
    char tmpl[] = "/tmp/vmaf-dnn-malformed-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);

    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);

    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    /* "kind" present but not a string (number) → extract_string returns
     * NULL via "no opening quote" branch. "name" missing entirely →
     * extract_string returns NULL via strstr-miss branch. "onnx_opset"
     * present but not a number → extract_int returns -EINVAL via the
     * endp == p branch. */
    (void)fprintf(s, "{\"kind\": 42, \"onnx_opset\": \"abc\"}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("malformed sidecar still loads with defaults", err == 0);
    /* kind defaults to FR when "kind" is non-string. */
    mu_assert("kind defaults to FR", meta.kind == VMAF_MODEL_KIND_DNN_FR);
    /* opset stays 0 when extract_int rejects the value. */
    mu_assert("opset defaults to 0", meta.opset == 0);
    /* No name in JSON → out->name is NULL. */
    mu_assert("missing name stays NULL", meta.name == NULL);
    vmaf_dnn_sidecar_free(&meta);

    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}

static char *test_sidecar_extract_string_no_close_quote(void)
{
    /* extract_string with a key that has an opening quote on the value but
     * no closing quote → strchr(p, '"') returns NULL, line 132 branch. */
    char tmpl[] = "/tmp/vmaf-dnn-noclose-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);

    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);

    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    /* "name" opens a quote that never closes before EOF. */
    (void)fputs("{\"name\": \"unterminated", s);
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("malformed sidecar still loads", err == 0);
    mu_assert("unterminated string returns NULL", meta.name == NULL);
    vmaf_dnn_sidecar_free(&meta);

    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}

/* ADR-0517 regression: sidecar carrying a feature-vector schema
 * (feature_order / feature_mean / feature_std as written by
 * train_fr_regressor_v2.py) populates VmafModelSidecar.n_features,
 * feature_names[], feature_mean[], feature_std[], and
 * has_feature_scaler. */
static char *test_sidecar_feature_vector_canonical6(void)
{
    char tmpl[] = "/tmp/vmaf-dnn-fv-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);
    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);
    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    (void)fprintf(s, "{\n"
                     "  \"kind\": \"fr\",\n"
                     "  \"feature_order\": [\"adm2\", \"vif_scale0\", "
                     "\"vif_scale1\", \"vif_scale2\", \"vif_scale3\", \"motion2\"],\n"
                     "  \"feature_mean\": [0.86, 0.37, 0.73, 0.82, 0.87, 8.95],\n"
                     "  \"feature_std\":  [0.10, 0.16, 0.19, 0.17, 0.14, 6.24]\n"
                     "}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("sidecar_load canonical-6 failed", err == 0);
    mu_assert("n_features == 6", meta.n_features == 6u);
    mu_assert("feature_names[0] == adm2",
              meta.feature_names[0] && strcmp(meta.feature_names[0], "adm2") == 0);
    mu_assert("feature_names[5] == motion2",
              meta.feature_names[5] && strcmp(meta.feature_names[5], "motion2") == 0);
    mu_assert("has_feature_scaler", meta.has_feature_scaler);
    mu_assert("feature_mean[0] ~ 0.86",
              meta.feature_mean[0] > 0.85f && meta.feature_mean[0] < 0.87f);
    mu_assert("feature_std[5] ~ 6.24", meta.feature_std[5] > 6.2f && meta.feature_std[5] < 6.3f);
    vmaf_dnn_sidecar_free(&meta);
    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}

/* ADR-0517: the alternative `features` / `input_mean` / `input_std`
 * field names (used by vmaf_tiny_v* trainers) populate the same
 * VmafModelSidecar fields. */
static char *test_sidecar_feature_vector_vmaf_tiny_field_names(void)
{
    char tmpl[] = "/tmp/vmaf-dnn-fv2-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);
    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);
    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    (void)fprintf(s, "{\n"
                     "  \"kind\": \"fr\",\n"
                     "  \"features\": [\"adm2\", \"vif_scale0\"],\n"
                     "  \"input_mean\": [0.5, 0.5],\n"
                     "  \"input_std\":  [0.1, 0.1]\n"
                     "}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("sidecar_load tiny-v4-style failed", err == 0);
    mu_assert("n_features == 2", meta.n_features == 2u);
    mu_assert("alt-field has_feature_scaler", meta.has_feature_scaler);
    vmaf_dnn_sidecar_free(&meta);
    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}

/* ADR-0517: sidecar with feature_order but no scaler arrays — the
 * loader honours the names but flags has_feature_scaler false so the
 * runtime knows to skip the (mean, std) transform. */
static char *test_sidecar_feature_vector_no_scaler(void)
{
    char tmpl[] = "/tmp/vmaf-dnn-fv3-XXXXXX";
    int fd = mkstemp(tmpl);
    mu_assert("mkstemp failed", fd >= 0);
    close(fd);
    char onnx[1024];
    char sidecar[1024];
    (void)snprintf(onnx, sizeof onnx, "%s.onnx", tmpl);
    (void)snprintf(sidecar, sizeof sidecar, "%s.json", tmpl);
    FILE *f = fopen_w_600(onnx);
    if (f)
        (void)fclose(f);
    FILE *s = fopen_w_600(sidecar);
    mu_assert("fopen sidecar failed", s != NULL);
    (void)fprintf(s, "{\n"
                     "  \"kind\": \"fr\",\n"
                     "  \"feature_order\": [\"adm2\", \"vif_scale0\"]\n"
                     "}\n");
    (void)fclose(s);

    VmafModelSidecar meta;
    int err = vmaf_dnn_sidecar_load(onnx, &meta);
    mu_assert("sidecar_load no-scaler failed", err == 0);
    mu_assert("n_features == 2", meta.n_features == 2u);
    mu_assert("no scaler arrays → has_feature_scaler false", meta.has_feature_scaler == false);
    vmaf_dnn_sidecar_free(&meta);
    (void)remove(sidecar);
    (void)remove(onnx);
    (void)remove(tmpl);
    return NULL;
}
#endif /* !_WIN32 */

char *run_tests(void)
{
    mu_run_test(test_sniff_by_extension);
    mu_run_test(test_size_cap);
    mu_run_test(test_validate_null_path);
#ifndef _WIN32
    mu_run_test(test_validate_zero_byte);
    mu_run_test(test_validate_allowed_onnx);
    mu_run_test(test_validate_disallowed_onnx);
    mu_run_test(test_validate_symlink_to_dir);
    mu_run_test(test_jail_unset_accepts_anywhere);
    mu_run_test(test_jail_accepts_model_inside);
    mu_run_test(test_jail_rejects_model_outside);
    mu_run_test(test_jail_rejects_sibling_prefix);
    mu_run_test(test_jail_rejects_symlink_escape);
    mu_run_test(test_jail_rejects_nonexistent_jail);
    mu_run_test(test_jail_rejects_non_directory);
    mu_run_test(test_jail_accepts_trailing_slash);
#endif
    mu_run_test(test_sidecar_parses);
    mu_run_test(test_sidecar_rejects_null_args);
    mu_run_test(test_sidecar_free_null_is_noop);
    mu_run_test(test_sidecar_missing_returns_enoent);
#ifndef _WIN32
    mu_run_test(test_sidecar_parses_kind_nr);
    mu_run_test(test_sidecar_quant_mode_default_fp32);
    mu_run_test(test_sidecar_quant_mode_dynamic);
    mu_run_test(test_sidecar_quant_mode_unknown_falls_back);
    mu_run_test(test_sidecar_no_dot_onnx_extension);
    mu_run_test(test_sidecar_oversized_path);
    mu_run_test(test_sidecar_malformed_keys_default);
    mu_run_test(test_sidecar_extract_string_no_close_quote);
    mu_run_test(test_sidecar_feature_vector_canonical6);
    mu_run_test(test_sidecar_feature_vector_vmaf_tiny_field_names);
    mu_run_test(test_sidecar_feature_vector_no_scaler);
#endif
    return NULL;
}
