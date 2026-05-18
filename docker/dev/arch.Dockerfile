# Arch Linux dev image. CPU-only by default; toggle ENABLE_CUDA / ENABLE_SYCL.
# Arch rolls forward aggressively — use this for the "latest toolchain" build.
FROM archlinux:latest@sha256:ceac417c19645d21630c120fa123942aa1fc5988faab14e67222013cb11f31bb

ARG ENABLE_CUDA=false
ARG ENABLE_SYCL=false

RUN pacman -Syu --noconfirm --needed \
      base-devel clang cppcheck \
      meson ninja nasm pkgconf \
      python python-pip \
      git curl wget \
      doxygen shellcheck shfmt \
      ffmpeg \
 && pacman -Scc --noconfirm

RUN if [ "$ENABLE_CUDA" = "true" ]; then pacman -S --noconfirm --needed cuda cuda-tools; fi

# oneAPI on Arch is via AUR or direct install — leave to user rather than ship AUR helper.
RUN if [ "$ENABLE_SYCL" = "true" ]; then \
      echo "oneAPI on Arch images: bind-mount /opt/intel/oneapi from host, or bake via AUR helper."; \
    fi

COPY . /vmaf
WORKDIR /vmaf

RUN meson setup build libvmaf \
      -Denable_cuda=$ENABLE_CUDA -Denable_sycl=$ENABLE_SYCL \
      --buildtype=release \
 && ninja -C build

ENV PATH="/vmaf/build/tools:${PATH}"
ENTRYPOINT ["vmaf"]
