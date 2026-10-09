# syntax=docker/dockerfile:1

FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        clang-18 \
        clang-format-18 \
        clang-tidy-18 \
        clang-tools-18 \
        libclang-rt-18-dev \
        g++-14 \
        gcc-14 \
        git \
        lcov \
        libc++-18-dev \
        libc++abi-18-dev \
        libsqlite3-dev \
        libpq-dev \
        llvm-18 \
        ninja-build \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
        postgresql-client \
        sqlite3 \
    && ln -sf /usr/bin/clang-18 /usr/local/bin/clang \
    && ln -sf /usr/bin/clang++-18 /usr/local/bin/clang++ \
    && ln -sf /usr/bin/clang-format-18 /usr/local/bin/clang-format \
    && ln -sf /usr/bin/clang-tidy-18 /usr/local/bin/clang-tidy \
    && ln -sf /usr/bin/llvm-cov-18 /usr/local/bin/llvm-cov \
    && ln -sf /usr/bin/llvm-profdata-18 /usr/local/bin/llvm-profdata \
    && ln -sf /usr/bin/gcc-14 /usr/local/bin/gcc \
    && ln -sf /usr/bin/g++-14 /usr/local/bin/g++ \
    && rm -rf /var/lib/apt/lists/*

COPY tools/requirements-dev.txt /tmp/orm-cxx-requirements-dev.txt

RUN python3 -m venv /opt/orm-cxx-tools \
    && /opt/orm-cxx-tools/bin/pip install --no-cache-dir \
        --requirement /tmp/orm-cxx-requirements-dev.txt cmake==3.31.6 ninja==1.13.0 \
    && rm /tmp/orm-cxx-requirements-dev.txt \
    && git config --system --add safe.directory /workspaces/orm-cxx

ENV PATH="/opt/orm-cxx-tools/bin:${PATH}"

WORKDIR /workspaces/orm-cxx
USER vscode

CMD ["sleep", "infinity"]
