# Minimal Makefile wrapper for ESP8266 RTOS SDK projects
# Set RTOS_SDK to the path of your ESP8266_RTOS_SDK installation.
RTOS_SDK ?= $(HOME)/ESP8266_RTOS_SDK

PROJECT_NAME := indoor-logger

ifndef RTOS_SDK
$(error RTOS_SDK not set. Please set RTOS_SDK to the path of ESP8266_RTOS_SDK)
endif

include $(RTOS_SDK)/Makefile
