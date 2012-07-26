/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <Windows.h>
#include "getopt/getopt.h"
#endif

#include "rtl-sdr.h"

#include "log.h"

static struct logger log = {
	.name = "libusbhelper",
	.log_level = LEVEL_TRACE,
	.log_func = default_log,
};
#ifdef ANDROID

#include <jni.h>


#define MAX_OPEN  10
static JNIEnv *env;
static jobject launcherActivity = NULL;

struct fdlist {
	int fd;
        int is_native;
	jobject connection;
};

static struct  fdlist fd_list[MAX_OPEN] = {
	{ .fd = -1, 0 , .connection = NULL, },
	{ .fd = -1, 0 , .connection = NULL, },
	{ .fd = -1, 0 , .connection = NULL, },
	{ .fd = -1, 0 , .connection = NULL, },
	{ .fd = -1, 0 , .connection = NULL, },
	{ .fd = -1, 0 , .connection = NULL, },
	{ .fd = -1, 0 , .connection = NULL, },
	{ .fd = -1, 0 , .connection = NULL, },
	{ .fd = -1, 0 , .connection = NULL, },
	{ .fd = -1, 0 , .connection = NULL, },
};


static jfieldID field_context;

static struct usb_device* get_device_from_object(JNIEnv* env, jobject connection)
{

    return (struct usb_device*)(*env)->GetIntField(env, connection, field_context);
}

static int android_java_usbdevice_open(const char *pathname, int mode, ...)
{
  jint retval = 0;
  int i;
  if (strncmp(pathname,"/sys",4) == 0 ) {
	int retval  = open(pathname,mode);
	  for (i =0 ; i < MAX_OPEN ; i++){ /* pick the first free slot */
		if (fd_list[i].fd == -1){
			fd_list[i].fd = retval;
			fd_list[i].is_native =1;
			fd_list[i].connection = NULL;
			break;
		}
	  }
	  if (i == MAX_OPEN){
		log_warn(&log,"MAX_OPEN REACHED\n");
		return -1;
  }
	  return retval;
  }
  log_debug(&log,"OPEN  %s, %d\n",pathname,mode);
  jclass cls = (*env)->GetObjectClass(env, launcherActivity);
  jmethodID mid = (*env)->GetMethodID(env, cls, "open", "(Ljava/lang/String;)Landroid/hardware/usb/UsbDeviceConnection;");
  if (mid == 0)
    return;
  jstring text = (*env)->NewStringUTF(env, pathname);
  jobject o  = (*env)->CallObjectMethod(env, launcherActivity, mid, text);

  if (o == NULL){
	return -1;
  }


  jobject conn = (*env)->NewGlobalRef(env, o);
  if ( conn == NULL){
  	log_warn(&log, "usb device connection == null\n");
	return -1;
  }

  jclass connectionClass =  (*env)->GetObjectClass(env, o);
  if (  connectionClass == NULL){
  	log_warn(&log,"Connection class === null\n");
	return -1;
  }
  mid = (*env)->GetMethodID(env, connectionClass, "getFileDescriptor", "()I");
  if (mid == 0){
  	log_warn(&log,"Mid  == 0\n");
	return -1;
  }
  retval   = (*env)->CallIntMethod(env, conn, mid);
  if (retval < 0){
  	log_warn(&log,"hanlde(%d) is invalid\n",retval);
  }
  retval = dup(retval);
  if (retval < 0){
  	log_warn(&log,"dup hanlde(%d) is invalid\n",retval);
  }
  for (i =0 ; i < MAX_OPEN ; i++){ /* pick the first free slot */
	if (fd_list[i].fd == -1){
		fd_list[i].fd = retval;
		fd_list[i].is_native = 0;
		fd_list[i].connection = conn;
		break;
	}
  }
  if (i == MAX_OPEN){
  	log_warn(&log,"MAX_OPEN REACHED\n");
	return -1;
  }
  return retval;
}

static int android_java_usbdevice_close(int fd)
{
  int i;

  for(i= 0; i < MAX_OPEN; i++){
	if (fd_list[i].fd == fd){
		if (fd_list[i].is_native){
			close(fd);
			fd_list[i].fd = -1; 
			fd_list[i].is_native = 0; 
  			fd_list[i].connection = NULL;
			return 0;
		} else {
			/* call java close */
			jclass connectionClass =  (*env)->GetObjectClass(env, fd_list[i].connection);
			if (  connectionClass == NULL){
				log_warn(&log,"Connection class === null\n");
				return -1;
			}
			jmethodID mid = (*env)->GetMethodID(env, connectionClass, "close", "()V");
			if (mid == 0){
				log_warn(&log,"Mid  == 0\n");
				return -1;
			}
			(*env)->CallVoidMethod(env, fd_list[i].connection , mid);
			fd_list[i].fd = -1; 
			(*env)->DeleteGlobalRef(env, fd_list[i].connection);
			fd_list[i].connection = NULL;
			return 0;
		}
	}
  }
  log_warn(&log,"Close was called with fh %d but no handle was found\n",fd);
  return -1;
}

void init_libusbhelper(JNIEnv *envp, jobject objp)
{
  env = envp; /* put env in a global.. for resuse in the same THREAD */
  launcherActivity = (*env)->NewGlobalRef(env, objp);
  usb_device_set_open_close_func(android_java_usbdevice_open, android_java_usbdevice_close);
}

#endif

