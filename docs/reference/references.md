<!-- markdownlint-disable MD007 MD013 -->
# References

Use this page to find the publications, talks and blog posts behind VMAF, and
the fork's own metric and model pages. For what the fork added, jump to
[Fork additions](#fork-additions).

VMAF is an on-going project. It has gone through substantial updates since its
inception, and even more so after its open sourcing on Github in June 2016. This
page attempts to maintain a (non-exhaustive) list of references on VMAF,
including tech blogs, academic papers, presentations, etc. VMAF also has a
[Wikipedia
page](https://en.wikipedia.org/wiki/Video_Multimethod_Assessment_Fusion).

## Tech Blogs

  - [Toward a practical perceptual video quality
    metric](https://medium.com/netflix-techblog/toward-a-practical-perceptual-video-quality-metric-653f208b9652),
    June 6, 2016 -- tech blog with VMAF's open sourcing on Github.
  - [Dynamic Optimizer — a perceptual video encoding optimization
    framework](https://medium.com/netflix-techblog/dynamic-optimizer-a-perceptual-video-encoding-optimization-framework-e19f1e3a277f),
    March 6, 2018 -- tech blog describing how VMAF is used in an codec-agnostic
    encoding optimization framework.
  - [Optimized shot-based encodes: now
    streaming!](https://medium.com/netflix-techblog/optimized-shot-based-encodes-now-streaming-4b9464204830),
    March 9, 2018 -- tech blog describing systems design for the Dynamic
    Optimizer.
  - [VMAF: the journey
    continues](https://medium.com/netflix-techblog/vmaf-the-journey-continues-44b51ee9ed12),
    October 25, 2018 -- second tech blog on VMAF focus on new features and best
    practices.
  - [Toward a better quality metric for the video
    community](https://netflixtechblog.com/toward-a-better-quality-metric-for-the-video-community-7ed94e752a30),
    December 7, 2020 -- third tech blog on VMAF focus on speed optimization, new
    API design and the introduction of a codec evaluation-friendly NEG mode.
  - [CAMBI, a banding artifact
    detector](https://netflixtechblog.medium.com/cambi-a-banding-artifact-detector-96777ae12fe2),
    October 12, 2021 -- tech blog introducing the CAMBI algorithm to detect
    banding artifacts. The PCS 2021 paper is mirrored in this repository:
    [CAMBI_PCS2021.pdf](papers/CAMBI_PCS2021.pdf).

## Academic Papers

Note that not all ideas in the academic papers below are implemented in the
current version of VMAF open-source package (or not yet).

### Systems and fusion

  - A. Aaron, Z. Li, M. Manohara, J.Y. Lin, E.C.-H. Wu, and C.-C. J. Kuo,
  - J. Y. Lin, C.-H. Wu, I. Katsavounidis, Z. Li, A. Aaron and C.-C. J. Kuo,
  - C. G. Bampis, Z. Li, and A. C. Bovik, [SpatioTemporal feature integration

### Components and predecessors

  - J. Y. Lin, T. J. Liu, E. C.-H. Wu and C. C. J. Kuo, [A fusion-based video
  - J. Y. Lin, R. Song, C.-H. Wu, T. Liu, H. Wang, C.-C. Jay Kuo, [MCL-V: A
  - H. Sheikh and A. Bovik, [Image information and visual
  - S. Li, F. Zhang, L. Ma, K. N. Ngan, [Image quality assessment by separately

### Subjective data and quality of experience

  - Z. Li and C. Bampis, [Recover subjective quality scores from noisy
  - C. G. Bampis, Z. Li, I. Katsavounidis and A. C. Bovik, [Recurrent and
  - C. G. Bampis, A. C. Bovik, [Learning to predict streaming video QoE:
  - J. Li, L. Krasula, P. Le Callet, Z. Li, Y. Baveye, [Quantifying the

### Independent evaluations

The papers below independently evaluate the performance of VMAF.

  - R. Rassool, [VMAF reproducibility: validating a perceptual practical video
    quality metric](https://ieeexplore.ieee.org/document/7986143/), 2017 IEEE
    International Symposium on Broadband Multimedia Systems and Broadcasting
    (BMSB), Cagliari, 2017, pp. 1-2.
  - C. Lee, S. Woo, S. Baek, J. Han, J. Chae and J. Rim, [Comparison of
    objective quality models for adaptive bit-streaming
    services](https://ieeexplore.ieee.org/document/8316385/), 2017 8th
    International Conference on Information, Intelligence, Systems &
    Applications (IISA), Larnaca, 2017.
  - N. Barman, S. Schmidt, S. Zadtootaghaj, M. Martini, S. Möller, [An
    evaluation of video quality assessment metrics for passive gaming video
    streaming](https://www.researchgate.net/publication/325285444_An_Evaluation_of_Video_Quality_Assessment_Metrics_for_Passive_Gaming_Video_Streaming),
    23rd Packet Video Workshop 2018 (PV 2018).

## White Papers

  - Z. Li, [On VMAF’s property in the presence of image enhancement
    operations](https://docs.google.com/document/d/1dJczEhXO0MZjBSNyKmd3ARiCTdFVMNPBykH4_HMPoyY/edit#heading=h.oaikhnw46pw5),
    July 13, 2020 (Updated Dec. 11, 2020), available [online]:
    [https://tinyurl.com/y34mgafa](https://tinyurl.com/y34mgafa).

## Presentations

  - [Measuring perceptual video quality at
    scale](https://www.twitch.tv/videos/94954102) by A. Aaron, at Demuxed 2016.
  - [More efficient encoding for mobile
    video](https://code.fb.com/video-engineering/video-scale-2017-recap/) by A.
    Aaron, at Video@Scale 2017.
  - [Measure perceptual video quality with VMAF](presentations/VMAF_ICIP17.pdf)
    by Z. Li, at Netflix Industry Workshop: Video Encoding at Scale, 2017 IEEE
    International Conference on Image Processing (ICIP), Beijing, 2017.
  - [A VMAF model for 4K](presentations/VQEG_SAM_2018_025_VMAF_4K.pdf) by Z. Li,
    T. Vigier and P. Le Callet, at Video Quality Experts Group (VQEG) Meeting in
    Madrid, March 2018.
  - [Quantify VMAF model variability using
    bootstrapping](presentations/VQEG_SAM_2018_023_VMAF_Variability.pdf) by Z.
    Li and I. Katsavounidis, at Video Quality Experts Group (VQEG) Meeting in
    Madrid, March 2018.
  - [VMAF: the journey
    continues](http://www.streamingmedia.com/Articles/Editorial/Featured-Articles/Video-Engineering-Summit-Netflix-to-Discuss-VMAFs-Future-128457.aspx)
    by Z. Li, at Streaming Media West, Huntington Beach, CA, November 2018.
  - [Analysis tools in the VMAF open-source
    package](presentations/VQEG_SAM_2018_111_AnalysisToolsInVMAF.pdf) by Z. Li
    and C. Bampis, at Video Quality Experts Group (VQEG) Meeting in Mountain
    View, CA, November 2018.
  - [Toward a better quality metric for the video
    community](https://atscaleconference.com/videos/video-scale-2020-vmaf/) By
    Z. Li, at Video@Scale, November 2020.

## Fork additions

The fork adds metrics and models that have their own pages. Start there for
the references of each:

- Metrics: [CAMBI](../metrics/cambi.md), [SSIMULACRA
  2](../metrics/ssimulacra2.md),
  [PSNR-HVS](../metrics/psnr-hvs.md), [Delta E ITP](../metrics/delta_e_itp.md),
  [NIQE](../metrics/niqe.md), [BRISQUE](../metrics/brisque.md),
  [SpEED-QA](../metrics/speed_qa.md), [PU21](../metrics/pu21.md); the index is
  [features](../metrics/features.md).
- Tiny-AI models: the model cards under
  [ai/models/](../ai/models/fr_regressor_v1.md)
  and the [tiny-AI overview](../ai/overview.md).
