LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

APP_OPTIM := release
#APP_OPTIM := debug

LOCAL_MODULE    := VotAR
LOCAL_SRC_FILES := android.cpp count-simple.cpp
LOCAL_LDLIBS := -llog -ljnigraphics -landroid

LOCAL_FLAGS := -O2

LOCAL_CPPFLAGS := -std=c++0x
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

include $(BUILD_SHARED_LIBRARY)