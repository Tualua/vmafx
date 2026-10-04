// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// pkg/scoringscope/scope_test.go — positive, negative and boundary cases of
// the scoring roots, traversal and symlink escapes included (ADR-1577).

package scoringscope

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func mustParse(t *testing.T, raw ...string) Roots {
	t.Helper()
	r, err := Parse(raw)
	if err != nil {
		t.Fatalf("Parse(%q): %v", raw, err)
	}
	return r
}

func TestParseRefusesWhatCannotBeCompared(t *testing.T) {
	bad := []string{"", "relative/dir", "./media", "/data/../etc", "https://media.example.com/a/../b", "s3:bucket/../other", "no-colon-no-slash"}
	for _, raw := range bad {
		if _, err := Parse([]string{raw}); err == nil {
			t.Errorf("Parse accepted %q", raw)
		}
	}
	many := make([]string, MaxRoots+1)
	for i := range many {
		many[i] = "/data"
	}
	if _, err := Parse(many[:MaxRoots]); err != nil {
		t.Errorf("%d roots refused: %v", MaxRoots, err)
	}
	if _, err := Parse(many); err == nil {
		t.Errorf("%d roots accepted", len(many))
	}
}

func TestNoRootsRefuseEverything(t *testing.T) {
	r := mustParse(t)
	if err := r.Check("/data/acme/ref.y4m"); !errors.Is(err, ErrOutside) {
		t.Fatalf("Check without roots = %v, want ErrOutside (deny by default)", err)
	}
}

func TestCheckLexical(t *testing.T) {
	r := mustParse(t, "/data/acme", "https://Media.Example.com/acme/", "s3:bucket/acme", ":local:/srv/acme")
	admitted := []string{
		"/data/acme/ref.y4m", "/data/acme", "file:///data/acme/sub/dis.y4m", "/data/acme//x.y4m",
		"https://media.example.com/acme/a.y4m", "s3:bucket/acme/k/ref.y4m", "s3://bucket/acme/ref.y4m",
		"rclone://s3:bucket/acme/x", ":local:/srv/acme/ref.y4m",
	}
	for _, in := range admitted {
		if err := r.Check(in); err != nil {
			t.Errorf("Check(%q) refused: %v", in, err)
		}
	}
	refused := []string{
		"/data/acme/../rival/ref.y4m",             // traversal
		"/data/acme-other/ref.y4m",                // prefix without a separator
		"/data/rival/ref.y4m",                     // other tenant
		"/data",                                   // parent of the root
		"relative/ref.y4m",                        // relative
		"https://media.example.com/acme/../x",     // URL traversal
		"https://media.example.com/acme/%2e%2e/x", // encoded traversal
		"https://evil.example.com/acme/a.y4m",     // other host
		"http://media.example.com/acmex/a.y4m",    // prefix without a separator
		"s3:bucket/rival/x",                       // other prefix
		"gcs:bucket/acme/x",                       // other remote
		":local:/srv/acme/../rival/x",             // remote traversal
	}
	for _, in := range refused {
		if err := r.Check(in); err == nil {
			t.Errorf("Check(%q) admitted it", in)
		}
	}
}

func TestSlashRootAdmitsEveryLocalPath(t *testing.T) {
	r := mustParse(t, "/")
	if err := r.Check("/anything/at/all.y4m"); err != nil {
		t.Fatalf("root / refused a local path: %v", err)
	}
	if err := r.Check("s3:bucket/x"); err == nil {
		t.Fatal("root / admitted a remote")
	}
}

// tree builds <tmp>/acme/ref.y4m and <tmp>/rival/secret.y4m and returns tmp.
func tree(t *testing.T) string {
	t.Helper()
	dir, err := filepath.EvalSymlinks(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	for _, p := range []string{"acme/ref.y4m", "rival/secret.y4m"} {
		full := filepath.Join(dir, p)
		if err := os.MkdirAll(filepath.Dir(full), 0o700); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte("YUV4MPEG2"), 0o600); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

func TestResolveFollowsSymlinks(t *testing.T) {
	dir := tree(t)
	acme := filepath.Join(dir, "acme")
	r := mustParse(t, acme)

	got, err := r.Resolve(filepath.Join(acme, "ref.y4m"))
	if err != nil || got != filepath.Join(acme, "ref.y4m") {
		t.Fatalf("Resolve(plain file) = %q, %v", got, err)
	}

	inside := filepath.Join(acme, "alias.y4m")
	if err := os.Symlink(filepath.Join(acme, "ref.y4m"), inside); err != nil {
		t.Fatal(err)
	}
	if got, err := r.Resolve(inside); err != nil || got != filepath.Join(acme, "ref.y4m") {
		t.Fatalf("Resolve(link inside the root) = %q, %v; want the real path", got, err)
	}

	escape := filepath.Join(acme, "steal.y4m")
	if err := os.Symlink(filepath.Join(dir, "rival", "secret.y4m"), escape); err != nil {
		t.Fatal(err)
	}
	if err := r.Check(escape); err != nil {
		t.Fatalf("lexical Check must admit the link's own path: %v", err)
	}
	if _, err := r.Resolve(escape); !errors.Is(err, ErrOutside) || !strings.Contains(err.Error(), "rival") {
		t.Fatalf("Resolve(link out of the root) = %v, want ErrOutside naming the real path", err)
	}

	dirEscape := filepath.Join(acme, "rivaldir")
	if err := os.Symlink(filepath.Join(dir, "rival"), dirEscape); err != nil {
		t.Fatal(err)
	}
	if _, err := r.Resolve(filepath.Join(dirEscape, "secret.y4m")); !errors.Is(err, ErrOutside) {
		t.Fatalf("Resolve(through a directory link out of the root) = %v, want ErrOutside", err)
	}
	if _, err := r.Resolve(filepath.Join(acme, "missing.y4m")); err == nil {
		t.Fatal("a missing file resolved")
	}
}

func TestResolveThroughALinkedRoot(t *testing.T) {
	dir := tree(t)
	link := filepath.Join(t.TempDir(), "media")
	if err := os.Symlink(filepath.Join(dir, "acme"), link); err != nil {
		t.Fatal(err)
	}
	r := mustParse(t, link)
	if got, err := r.Resolve(filepath.Join(link, "ref.y4m")); err != nil || got != filepath.Join(dir, "acme", "ref.y4m") {
		t.Fatalf("Resolve under a linked root = %q, %v", got, err)
	}
}

func TestResolveLeavesRemotesUnchanged(t *testing.T) {
	r := mustParse(t, "s3:bucket/acme")
	if got, err := r.Resolve("s3://bucket/acme/x.y4m"); err != nil || got != "s3://bucket/acme/x.y4m" {
		t.Fatalf("Resolve(remote) = %q, %v", got, err)
	}
}

func TestStringsKeepsTheConfiguredForm(t *testing.T) {
	r := mustParse(t, " /data/acme ", "s3://bucket/acme")
	got := r.Strings()
	if len(got) != 2 || got[0] != "/data/acme" || got[1] != "s3://bucket/acme" {
		t.Fatalf("Strings() = %q", got)
	}
}
