BASE_PATH := $(call my-dir)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := ttymux
#LOCAL_LDFLAGS := -static
LOCAL_CFLAGS := -Wall 	-DJSON_USE_EXCEPTION=0

LOCAL_CPPFLAGS := -Wall
LOCAL_LDLIBS := -llog

LOCAL_SRC_FILES := ttymux.cpp

JSONCPP_SRC_FILES := \
	jsoncpp/src/lib_json/json_reader.cpp \
	jsoncpp/chromium-overrides/src/lib_json/json_value.cpp \
	jsoncpp/src/lib_json/json_writer.cpp

JSON_CPP_INCLUDE_FILES := \
	$(LOCAL_PATH)/jsoncpp/chromium-overrides/include \
	$(LOCAL_PATH)/jsoncpp/include \
	$(LOCAL_PATH)/jsoncpp/src/lib_json

include $(BUILD_EXECUTABLE)

