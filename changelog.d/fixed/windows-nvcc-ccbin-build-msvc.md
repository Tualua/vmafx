- **The native Windows CUDA build compiles again, with one MSVC toolset.**
  NVCC's host compiler was the first `cl.exe` a recursive search of the
  Visual Studio install found, an older toolset (14.29 in Visual Studio 2026)
  than the one building the rest of the library; its standard library cannot
  compile the C++20 `<numbers>` header the CUDA `ciede` kernel uses. NVCC now
  uses the build's own `cl.exe` when the build compiles with MSVC, otherwise the
  newest installed toolset (`docs/getting-started/building-on-windows.md`).
