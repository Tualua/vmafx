<!-- markdownlint-disable MD013 -->
# MATLAB usage

Three reference metrics, ST-MAD, ST-RRED and SpEED-QA, run through MATLAB from
the Python harness. They are available only there: the `vmaf` CLI does not
compute them. BRISQUE needs no MATLAB, because libvmaf computes it natively.

## Prerequisites

1. Install and activate [MATLAB](https://www.mathworks.com/).
2. Create the optional file
   [`compat/python-vmaf/externals.py`](../../compat/python-vmaf/config.py), which
   `config.py` reads, and set `MATLAB_PATH` in it to your MATLAB binary:

    ```python
    MATLAB_PATH = "/path/to/matlab"
    ```

    For example on macOS:

    ```python
    MATLAB_PATH = "/Applications/MATLAB_R2017a.app/bin/matlab"
    ```

    To use the MATLAB Runtime instead of a full MATLAB, set
    `MATLAB_RUNTIME_PATH` in the same file.

## Available algorithms

| Algorithm | `quality_type` | Notes |
| --- | --- | --- |
| ST-MAD [1] | `STMAD` | |
| ST-RRED [2] | `STRRED` | |
| ST-RRED, optimised | `STRREDOpt` | Computationally efficient variant with numerically identical results. |
| SpEED-QA [3] | `SpEED_Matlab` | |
| BRISQUE [4] | n/a | Native libvmaf feature; see below. |

Run the MATLAB algorithms with the `run_testing` script:

```bash
python -m vmaf.script.run_testing <quality_type> <dataset_file>
```

The dataset file follows the format described in [python.md](python.md).

## BRISQUE

BRISQUE runs without MATLAB, as a libvmaf feature extractor:

```bash
vmaf --reference ref.yuv --distorted dis.yuv --width 1920 --height 1080 \
     --pixel_format 420 --bitdepth 8 --feature brisque
```

The trained model ships embedded in the binary. See
[../metrics/brisque.md](../metrics/brisque.md) for the options, including an
on-disk model such as `model/other_models/brisque_live.model`.

## References

[1] P. V. Vu, C. T. Vu, and D. M. Chandler, "A spatiotemporal mostapparent-distortion model for video quality assessment," IEEE Int’l Conf. Image Process., pp. 2505–2508, 2011.

[2] R. Soundararajan and A. C. Bovik, "Video quality assessment by reduced reference spatio-temporal entropic differencing," IEEE Trans. Circ. Syst. Video Technol., vol. 23, no. 4, pp. 684–694, Apr. 2013.

[3] C. G. Bampis, P. Gupta, R. Soundararajan, and A. C. Bovik, "SpEEDQA: Spatial efficient entropic differencing for image and video quality," IEEE Signal Process. Lett., vol. 24, no. 9, pp. 1333–1337, 2017.

[4] A. Mittal, A. K. Moorthy, and A. C. Bovik, "No-reference image quality assessment in the spatial domain," IEEE Trans. Image Process., vol. 21, no. 12, pp. 4695–4708, Dec. 2012.
