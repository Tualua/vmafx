- **`psnr_hvs` on HIP and SYCL scores 4:4:4 pictures past 16K.** The twins'
  scan of the per-block term counts stopped at 32,768 chunks of 256 blocks,
  so from 16384x8640 in 4:4:4 on the later chunks had no offset and their
  terms were written past the device buffer. The scan visits every chunk
  now, as the CUDA twin does; smaller pictures are unchanged.
