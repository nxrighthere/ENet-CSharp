LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE    := libenet
LOCAL_SRC_FILES := ../enet.c

ifdef ENET_DEBUG
	LOCAL_CFLAGS += -DENET_DEBUG
endif

ifdef ENET_STATIC
	include $(BUILD_STATIC_LIBRARY)
else
	include $(BUILD_SHARED_LIBRARY)
endif
