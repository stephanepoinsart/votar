LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

APP_OPTIM := debug

LOCAL_MODULE    := VotAR
LOCAL_SRC_FILES := VotAR.cpp
LOCAL_LDLIBS := -llog -ljnigraphics

LOCAL_FLAGS := -g -O2

LOCAL_CPPFLAGS := -std=c++0x

APP_PLATFORM := android-9

include $(BUILD_SHARED_LIBRARY)
