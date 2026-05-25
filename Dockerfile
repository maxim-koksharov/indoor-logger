FROM ubuntu:20.04

# Отключаем интерактивные диалоги при установке пакетов
ENV DEBIAN_FRONTEND=noninteractive

# Устанавливаем системные зависимости
RUN apt-get update && apt-get install -y \
    make \
    libncurses-dev \
    flex \
    bison \
    gperf \
    python3 \
    python3-pip \
    python3-setuptools \
    python3-serial \
    python3-cryptography \
    python3-future \
    python3-pyparsing \
    python3-click \
    ninja-build \
    cmake \
    libffi-dev \
    libssl-dev \
    git \
    wget \
    curl \
    xz-utils \
    xxd \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Настраиваем python3 как дефолтный python
RUN ln -s /usr/bin/python3 /usr/bin/python

# Создаем необходимую структуру директорий
RUN mkdir -p /esp/tools /esp/bin /workspace

WORKDIR /esp

# Клонируем ESP8266_RTOS_SDK со всеми подмодулями
RUN git clone --recursive https://github.com/espressif/ESP8266_RTOS_SDK.git

# Устанавливаем переменную окружения для SDK
ENV IDF_PATH=/esp/ESP8266_RTOS_SDK

# Скачиваем тулчейн, распаковываем в /esp/tools, переносим в /esp/bin и очищаем мусор
RUN wget -P /tmp https://dl.espressif.com/dl/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz \
    && tar -xzf /tmp/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz -C /esp/tools \
    && mv /esp/tools/xtensa-lx106-elf /esp/bin/ \
    && rm /tmp/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-linux-amd64.tar.gz

# === ИСПРАВЛЕННЫЙ БЛОК УСТАНОВКИ OPENCODE ===
# 1. Запускаем стандартный скрипт (он скачает бинарник в /root/.opencode/bin/opencode)
# 2. Переносим сам бинарник в /usr/local/bin, чтобы он был доступен глобально любому юзеру
# 3. Выставляем права на исполнение и удаляем временный мусор из /root
RUN curl -fsSL https://opencode.ai/install | bash \
    && mv /root/.opencode/bin/opencode /usr/local/bin/opencode \
    && chmod +x /usr/local/bin/opencode \
    && rm -rf /root/.opencode

# Прописываем пути в системный bashrc для интерактивных сессий (убран лишний путь к root)
RUN echo "export PATH=\$PATH:/esp/bin/xtensa-lx106-elf/bin" >> /etc/bash.bashrc \
    && echo "export PATH=\$PATH:/esp/ESP8266_RTOS_SDK/tools" >> /etc/bash.bashrc \
    && echo "export PATH=\$PATH:/usr/local/bin" >> /etc/bash.bashrc

# Явно задаем PATH для неинтерактивного режима сборки Docker
ENV PATH="${PATH}:/esp/bin/xtensa-lx106-elf/bin:/esp/ESP8266_RTOS_SDK/tools:/usr/local/bin"

# Устанавливаем конкретные версии python-пакетов
RUN pip3 install --upgrade pip==20.3.4 wheel \
    && pip3 install \
    "click>=5.0" \
    "pyserial>=3.0" \
    "future>=0.15.2" \
    "cryptography>=2.1.4,<35" \
    "pyparsing>=2.0.3,<2.4.0" \
    "pyelftools>=0.22"

WORKDIR /workspace
CMD ["/bin/bash"]