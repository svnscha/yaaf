FROM alpine:3.20

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
        ninja-build \
        pkgconf \
        py3-pip \
        py3-virtualenv \
        python3 \
        sudo \
        tar \
        unzip \
        xz \
        zip \
    && ln -sf /usr/lib/ninja-build/bin/ninja /usr/bin/ninja \
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
    && curl -LsSf https://astral.sh/uv/install.sh | env UV_INSTALL_DIR=/usr/local/bin sh

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
