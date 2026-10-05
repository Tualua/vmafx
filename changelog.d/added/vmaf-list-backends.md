- **`vmaf --list-backends` reports which scoring backends the binary can use.**
  It prints, as JSON, every backend the CLI knows (cpu, cuda, sycl, hip,
  metal), whether it is compiled in, and whether its state initialises on this
  host, then exits without reading any input. See
  [the CLI reference](docs/usage/cli.md#which-backends-this-binary-can-use).
