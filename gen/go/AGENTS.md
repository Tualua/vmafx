<!-- markdownlint-disable MD013 -->
# gen/go — generated protobuf stubs

Directory contents: machine output. `buf generate` (see `buf.gen.yaml`) writes from `proto/`. `DO NOT EDIT` banner in each `*.pb.go`: hand edit survives until next regeneration, then vanishes without trace.

## Rebase-sensitive invariant: HISS-09 SAFETY proofs

`protoc-gen-go` emits two raw-descriptor aliases per file: `unsafe.Slice(unsafe.StringData(file_*_rawDesc), len(file_*_rawDesc))`, once in `file_*_rawDescGZIP`, once in `DescBuilder` literal of `file_*_init`. HISS-09 requires `// SAFETY:` proof directly above every `unsafe.*` use; upstream generator lacks hook.

Proof produced by post-generation pass, not hand:

```bash
buf generate                                          # writes gen/go/**
python3 scripts/proto/postprocess_gen_go.py           # re-applies the proofs
python3 scripts/proto/postprocess_gen_go.py --check   # verify only
```

Pass idempotent: run after regeneration reproduces committed tree byte-for-byte. **Do not add comments by hand, do not delete to "clean up" generated output** — change `scripts/proto/postprocess_gen_go.py`, re-run instead.

If future `protoc-gen-go` changes call shape, script `RAW_DESC_CALL` needle stops matching; `--check` passes while audit reports HISS-09. Mismatch signals updating needle, not suppressing finding.
