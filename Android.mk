# Copyright 2012 OSMOCOM Project
#
# This file is part of rtl-sdr
#
# GNU Radio is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# GNU Radio is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Radio; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.
LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := usb
LOCAL_SRC_FILES := lib/libusb.so
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../jni/include
include $(PREBUILT_SHARED_LIBRARY)

#
# The librtlsdr shared library
#
include $(CLEAR_VARS)
LOCAL_MODULE    := rtlsdr
LOCAL_SRC_FILES := \
    src/librtlsdr.c \
    src/tuner_e4k.c \
    src/tuner_fc0012.c \
    src/tuner_fc0013.c \
    src/tuner_fc2580.c
LOCAL_LDLIBS :=  -llog
LOCAL_CFLAGS := -DANDROID -DUSE_LIBLOG
LOCAL_SHARED_LIBRARIES := usb
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := rtltest
LOCAL_SRC_FILES := src/rtl_test.c 
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include $(LOCAL_PATH)/android/include
LOCAL_CFLAGS := -DANDROID -DUSE_LIBLOG
LOCAL_SHARED_LIBRARIES +=  usb
LOCAL_SHARED_LIBRARIES +=  rtlsdr
LOCAL_LDLIBS :=  -llog
include $(BUILD_SHARED_LIBRARY)

## The rtl_tcp program does compile but doesn't work yet
# it is missing a java entry point and the same kind
# of open/close override hack as found in rtl_test.c
include $(CLEAR_VARS)
LOCAL_MODULE    := rtl_tcp
LOCAL_SRC_FILES := src/rtl_tcp.c
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_SHARED_LIBRARIES += rtlsdr usb
include $(BUILD_EXECUTABLE)

