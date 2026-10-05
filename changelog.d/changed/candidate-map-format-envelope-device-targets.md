- **The first-release candidates now cover large and unusual inputs and
  device-targeted scoring ([ADR-1880](docs/adr/1880-format-envelope-device-targets.md)).**
  RC3 adds an integer-overflow audit of every extractor and twin at 8K and 16K
  with 16-bit samples and 8K exactness cells; the RC6 and RC7 capability
  tables declare, per backend and device, the supported resolutions (up to
  16K), bit depths, chroma layouts and odd or portrait sizes, each row backed
  by a test; RC8 measures throughput per resolution; RC5 adds device profiles
  (phone, tablet, laptop, TV, VR per eye) that score one decode for several
  displays. See [the roadmap](docs/roadmap.md).
