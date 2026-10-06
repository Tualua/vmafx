- A second `float_adm` instance with `debug=true` is refused when it is registered,
  with a message naming the key `adm` both would write, instead of failing at the
  first frame with "problem reading pictures". The unsuffixed `adm` key stays (the
  Netflix tests read it); the CUDA, SYCL and HIP `float_adm` twins now file it
  unsuffixed like the CPU, where they added the option suffix.
