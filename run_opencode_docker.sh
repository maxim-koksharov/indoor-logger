#!/bin/bash

docker run -it \
  --device=/dev/ttyUSB0:/dev/ttyUSB0 \
  -v "$PWD":/esp/project \
  -v ~/.local/share/opencode:/root/.local/share/opencode \
  -v ~/.config/opencode:/root/.config/opencode \
  esp8266-env \
  opencode