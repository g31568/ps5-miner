FROM ubuntu:24.04

# Native ARM host tools cross-compile PS5 x86-64 code; no emulated compiler is needed.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential clang-18 llvm-18-dev lld-18 libclang-18-dev \
    libclc-18 libclc-18-dev libllvmspirvlib-18-dev \
    bison flex pkg-config cmake ninja-build git wget curl unzip rsync ca-certificates \
    python3 python3-venv python3-dev zlib1g-dev libzstd-dev libexpat1-dev \
    libdrm-dev libelf-dev \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /builder && /builder/bin/pip install --no-cache-dir \
    meson==1.10.2 mako==1.3.10 PyYAML==6.0.3 ply==3.11 packaging==26.0
ENV PATH="/builder/bin:/usr/lib/llvm-18/bin:${PATH}"
ENV LLVM_CONFIG=/usr/lib/llvm-18/bin/llvm-config
# Mesa's host OpenCL compiler also links SPIRV-Tools validation/optimization libraries.
RUN apt-get update && apt-get install -y --no-install-recommends spirv-tools glslang-tools \
    && rm -rf /var/lib/apt/lists/*
# RADV's PS5 link needs x86-64 emulated TLS builtins even on an ARM Linux builder.
RUN set -eu; if [ "$(dpkg --print-architecture)" = arm64 ]; then \
      sed -i '/^Types: deb$/a Architectures: arm64' /etc/apt/sources.list.d/ubuntu.sources; \
      dpkg --add-architecture amd64; \
      echo 'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble main universe' \
        > /etc/apt/sources.list.d/x86-runtime.list; \
      apt-get update; mkdir /tmp/x86-runtime; cd /tmp/x86-runtime; \
      apt-get download libclang-rt-18-dev:amd64; \
      dpkg-deb -x ./*.deb extracted; \
      mkdir -p /usr/lib/llvm-18/lib/clang/18/lib/linux; \
      install -m 644 extracted/usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.builtins-x86_64.a \
        /usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.builtins-x86_64.a; \
      rm -rf /tmp/x86-runtime /var/lib/apt/lists/*; \
    else apt-get update && apt-get install -y --no-install-recommends libclang-rt-18-dev \
      && rm -rf /var/lib/apt/lists/*; fi
WORKDIR /work/ps5-vulkan-radv
