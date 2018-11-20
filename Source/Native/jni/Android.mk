LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE    := libenet
LOCAL_SRC_FILES := ..\enet.c

ifdef ENET_LZ4
	LOCAL_CFLAGS += -DENET_LZ4
	LOCAL_SRC_FILES += ..\lz4\lz4.c
endif

#include $(BUILD_STATIC_LIBRARY)
include $(BUILD_SHARED_LIBRARY)
