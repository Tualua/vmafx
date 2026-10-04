// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package corpus

import (
	"context"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

// x265Request is a libx265 cell at CRF 28 writing under dir.
func x265Request(dir string) EncodeRequest {
	req := baseEncodeRequest()
	req.Encoder = "libx265"
	req.CRF = 28
	req.Output = filepath.Join(dir, "out.mp4")
	return req
}

// argvAfter returns the argument after flag, or "".
func argvAfter(argv []string, flag string) string {
	if i := slices.Index(argv, flag); i >= 0 && i+1 < len(argv) {
		return argv[i+1]
	}
	return ""
}

// TestRunTwoPassEncodeX265IsPass1AtCRFThenABR is ADR-1565: libx265 exits 183 on
// a pass 2 that keeps -crf, so pass 1 runs at the CRF and writes a real file,
// ffprobe measures it, and pass 2 is ABR at that bitrate. Python:
// tests/test_codec_adapter_x265_two_pass.py::
// test_run_two_pass_encode_drives_both_passes_in_order.
func TestRunTwoPassEncodeX265IsPass1AtCRFThenABR(t *testing.T) {
	t.Parallel()

	ResetEncoderVersionProbeCache()
	dir := t.TempDir()
	req := x265Request(dir)

	var seen [][]string
	stub := func(_ context.Context, argv []string) RunResult {
		seen = append(seen, argv)
		if argv[0] == "ffprobe" {
			return RunResult{Stdout: "179532\n"}
		}
		if len(argv) > 1 && argv[1] == "-version" {
			return RunResult{}
		}
		if err := os.WriteFile(argv[len(argv)-1], []byte("bits"), 0o600); err != nil {
			return RunResult{ReturnCode: 1, Stderr: err.Error()}
		}
		return RunResult{Stderr: "x265 [info]: HEVC encoder version 3.5+1\n"}
	}
	got := RunTwoPassEncode(context.Background(), req, "ffmpeg", stub, dir)
	if got.ExitStatus != 0 {
		t.Fatalf("ExitStatus = %d (%s), want 0", got.ExitStatus, got.StderrTail)
	}
	var names []string
	for _, a := range seen {
		names = append(names, a[0])
	}
	if !slices.Equal(names, []string{"ffmpeg", "ffprobe", "ffmpeg"}) {
		t.Fatalf("invocations = %v, want ffmpeg, ffprobe, ffmpeg", names)
	}
	pass1, probe, pass2 := seen[0], seen[1], seen[2]
	if argvAfter(pass1, "-crf") != "28" {
		t.Errorf("pass 1 argv %v lacks -crf 28", pass1)
	}
	if !strings.HasPrefix(argvAfter(pass1, "-x265-params"), "pass=1:stats=") {
		t.Errorf("pass 1 argv %v lacks pass=1", pass1)
	}
	if last := pass1[len(pass1)-1]; last == "-" || probe[len(probe)-1] != last {
		t.Errorf("pass 1 must write a real file that ffprobe then reads; pass1 %v probe %v",
			pass1, probe)
	}
	if slices.Contains(pass2, "-crf") {
		t.Errorf("pass 2 argv %v carries -crf, which x265 refuses", pass2)
	}
	if argvAfter(pass2, "-b:v") != "180k" {
		t.Errorf("pass 2 argv %v: want -b:v 180k (179532 bit/s)", pass2)
	}
	if !strings.HasPrefix(argvAfter(pass2, "-x265-params"), "pass=2:stats=") {
		t.Errorf("pass 2 argv %v lacks pass=2", pass2)
	}
	// The cell records its rate control; its crf stays the pass-1 CRF.
	if n := len(got.Request.ExtraParams); n < 2 ||
		got.Request.ExtraParams[n-2] != "-b:v" || got.Request.ExtraParams[n-1] != "180k" {
		t.Errorf("ExtraParams = %v, want to end with -b:v 180k", got.Request.ExtraParams)
	}
	if got.Request.CRF != 28 {
		t.Errorf("CRF = %d, want the pass-1 CRF 28", got.Request.CRF)
	}
	if _, err := os.Stat(pass1[len(pass1)-1]); err == nil {
		t.Error("the pass-1 bitstream was not removed")
	}
}

func TestRunTwoPassEncodeX265FailsWithoutAPass1Bitrate(t *testing.T) {
	t.Parallel()

	for name, probe := range map[string]RunResult{
		"not a number":   {Stdout: "N/A\n"},
		"ffprobe failed": {ReturnCode: 1},
		"zero":           {Stdout: "0\n"},
	} {
		t.Run(name, func(t *testing.T) {
			t.Parallel()

			ResetEncoderVersionProbeCache()
			dir := t.TempDir()
			ffmpegCalls := 0
			stub := func(_ context.Context, argv []string) RunResult {
				if argv[0] == "ffprobe" {
					return probe
				}
				if len(argv) > 1 && argv[1] == "-version" {
					return RunResult{}
				}
				ffmpegCalls++
				_ = os.WriteFile(argv[len(argv)-1], []byte("b"), 0o600)
				return RunResult{}
			}
			got := RunTwoPassEncode(context.Background(), x265Request(dir), "ffmpeg", stub, dir)
			if got.ExitStatus != 1 || !strings.Contains(got.StderrTail, "pass 1 bitrate unavailable") {
				t.Errorf("got status %d tail %q, want 1 and the bitrate marker",
					got.ExitStatus, got.StderrTail)
			}
			if ffmpegCalls != 1 {
				t.Errorf("ran %d ffmpeg encodes, want 1 (pass 2 must not run)", ffmpegCalls)
			}
		})
	}
}

func TestFFprobeSitsNextToACustomFFmpeg(t *testing.T) {
	t.Parallel()

	for bin, want := range map[string]string{
		"ffmpeg":             "ffprobe",
		"":                   "ffprobe",
		"/opt/x/bin/ffmpeg":  "/opt/x/bin/ffprobe",
		"/opt/x/bin/encoder": "ffprobe",
	} {
		if got := ffprobeFor(bin); got != want {
			t.Errorf("ffprobeFor(%q) = %q, want %q", bin, got, want)
		}
	}
}
