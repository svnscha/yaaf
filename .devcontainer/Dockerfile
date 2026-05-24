FROM alpine:3.20

ARG CMAKE_VERSION=4.3.2
ARG NINJA_VERSION=1.13.2
ARG LLVM_VERSION=18

RUN apk add --no-cache \
        bash \
        build-base \
        ca-certificates \
        clang${LLVM_VERSION} \
        clang${LLVM_VERSION}-extra-tools \
        cmake \
        curl \
        git \
        lld \
        lldb \
        linux-headers \
        ninja-build \
        perl \
        pkgconf \
        py3-pip \
        py3-virtualenv \
        python3 \
        sudo \
        tar \
        unzip \
        xz \
        zip \
    && ln -sf /usr/bin/clang-${LLVM_VERSION} /usr/bin/clang \
    && ln -sf /usr/bin/clang++-${LLVM_VERSION} /usr/bin/clang++ \
    && ln -sf /usr/bin/clangd-${LLVM_VERSION} /usr/bin/clangd \
    && ln -sf /usr/bin/clang-scan-deps-${LLVM_VERSION} /usr/bin/clang-scan-deps \
    && addgroup -g 1000 vscode \
    && adduser -D -u 1000 -G vscode -s /bin/bash vscode \
    && mkdir -p /etc/sudoers.d /workspaces \
    && echo 'vscode ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/vscode \
    && chmod 0440 /etc/sudoers.d/vscode \
    && chown vscode:vscode /workspaces \
    && python3 -m pip install --break-system-packages --no-cache-dir "cmake==${CMAKE_VERSION}" \
    && cmake_bin_dir="$(python3 -c 'import cmake; print(cmake.CMAKE_BIN_DIR)')" \
    && ln -sf "${cmake_bin_dir}/cmake" /usr/local/bin/cmake \
    && ln -sf "${cmake_bin_dir}/ccmake" /usr/local/bin/ccmake \
    && ln -sf "${cmake_bin_dir}/cpack" /usr/local/bin/cpack \
    && ln -sf "${cmake_bin_dir}/ctest" /usr/local/bin/ctest \
    && curl -fsSL "https://github.com/ninja-build/ninja/archive/refs/tags/v${NINJA_VERSION}.tar.gz" -o /tmp/ninja-src.tar.gz \
    && tar -xzf /tmp/ninja-src.tar.gz -C /tmp \
    && cd "/tmp/ninja-${NINJA_VERSION}" \
    && python3 configure.py --bootstrap \
    && install -m 0755 ninja /usr/local/bin/ninja \
    && ln -sf /usr/local/bin/ninja /usr/bin/ninja \
    && rm -rf /tmp/ninja-src.tar.gz "/tmp/ninja-${NINJA_VERSION}" \
    && curl -LsSf https://astral.sh/uv/install.sh | env UV_INSTALL_DIR=/usr/local/bin sh

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
