FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# === System packages: build, flash, debug ===
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        ccache \
        cmake \
        curl \
        file \
        flex \
        bison \
        gperf \
        git \
        iputils-ping \
        libffi-dev \
        libncurses-dev \
        libssl-dev \
        make \
        net-tools \
        ninja-build \
        python3 \
        python3-pip \
        python3-setuptools \
        python3-serial \
        python3-cryptography \
        python3-future \
        python3-pyparsing \
        python3-click \
        ripgrep \
        sudo \
        unzip \
        usbutils \
        vim-tiny \
        wget \
        xxd \
        xz-utils \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && ln -s /usr/bin/python3 /usr/bin/python

# === Non-privileged user 'dev' (UID:GID 1000, dialout for USB, passwordless sudo) ===
RUN groupadd -g 1000 dev \
    && useradd -m -u 1000 -g dev -G dialout -s /bin/bash dev \
    && echo "dev ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# === Workspace and toolchain layout (chown to dev upfront) ===
RUN mkdir -p /esp/tools /esp/bin /workspace \
    && chown -R dev:dev /esp /workspace

# === ESP8266 RTOS SDK (cloned by 'dev' so file ownership is correct) ===
WORKDIR /esp
USER dev
RUN git clone --recursive https://github.com/espressif/ESP8266_RTOS_SDK.git
ENV IDF_PATH=/esp/ESP8266_RTOS_SDK

# === xtensa toolchain (installed as root for write access, then chown) ===
USER root
RUN wget -q -P /tmp https://dl.espressif.com/dl/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz \
    && tar -xzf /tmp/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz -C /esp/tools \
    && mv /esp/tools/xtensa-lx106-elf /esp/bin/ \
    && rm -f /tmp/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz \
    && chown -R dev:dev /esp/bin /esp/tools

# === opencode (global binary, accessible to all users) ===
RUN curl -fsSL https://opencode.ai/install | bash \
    && mv /root/.opencode/bin/opencode /usr/local/bin/opencode \
    && chmod 0755 /usr/local/bin/opencode \
    && rm -rf /root/.opencode

# === PATH (single source of truth, applies to both interactive and non-interactive) ===
ENV PATH="/esp/bin/xtensa-lx106-elf/bin:/esp/ESP8266_RTOS_SDK/tools:/usr/local/bin:${PATH}"

# === pip dependencies (version-pinned for ubuntu 20.04 / python 3.8) ===
RUN pip3 install --no-cache-dir --upgrade "pip==20.3.4" wheel \
    && pip3 install --no-cache-dir \
        "click>=5.0" \
        "pyserial>=3.0" \
        "future>=0.15.2" \
        "cryptography>=2.1.4,<35" \
        "pyparsing>=2.0.3,<2.4.0" \
        "pyelftools>=0.22"

# === Interactive shell niceties (sourced by bash login shells) ===
RUN { \
        echo 'export IDF_PATH=/esp/ESP8266_RTOS_SDK'; \
        echo 'alias ll="ls -alF"'; \
        echo 'alias gs="git status"'; \
    } > /etc/profile.d/dev.sh

USER dev
WORKDIR /workspace
CMD ["/bin/bash"]
