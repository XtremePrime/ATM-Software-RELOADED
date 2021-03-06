LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := atm-software-reloaded
LOCAL_SRC_FILES := ../../../../../main.cpp

# Debug dependencies
LOCAL_SHARED_LIBRARIES := sfml-system-d
LOCAL_SHARED_LIBRARIES += sfml-window-d
LOCAL_SHARED_LIBRARIES += sfml-graphics-d
LOCAL_SHARED_LIBRARIES += sfml-audio-d
LOCAL_SHARED_LIBRARIES += sfml-network-d
LOCAL_SHARED_LIBRARIES += sfml-activity-d
LOCAL_SHARED_LIBRARIES += openal
LOCAL_WHOLE_STATIC_LIBRARIES := sfml-main-d

# Release dependencies
# LOCAL_SHARED_LIBRARIES := sfml-system
# LOCAL_SHARED_LIBRARIES += sfml-window
# LOCAL_SHARED_LIBRARIES += sfml-graphics
# LOCAL_SHARED_LIBRARIES += sfml-audio
# LOCAL_SHARED_LIBRARIES += sfml-network
# LOCAL_SHARED_LIBRARIES += sfml-activity
# LOCAL_SHARED_LIBRARIES += openal
# LOCAL_WHOLE_STATIC_LIBRARIES := sfml-main

include $(BUILD_SHARED_LIBRARY)

$(call import-module,third_party/sfml)
