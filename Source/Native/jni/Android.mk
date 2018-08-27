LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE    := libenet
LOCAL_SRC_FILES :=\
	..\library.c

LOCAL_LDLIBS := 

#include $(BUILD_STATIC_LIBRARY)
include $(BUILD_SHARED_LIBRARY)