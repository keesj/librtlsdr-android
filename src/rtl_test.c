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
	.name = "rtl_test",
	.log_level = LEVEL_TRACE,
	.log_func = default_log,
};

#define DEFAULT_SAMPLE_RATE		2048000
#define DEFAULT_ASYNC_BUF_NUMBER	32
#define DEFAULT_BUF_LENGTH		(16 * 16384)
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)

#define MHZ(x)	((x)*1000*1000)

static int do_exit = 0;
static rtlsdr_dev_t *dev = NULL;

void usage(void)
{
	log_info(&log,
		"rtl_test, a benchmark tool for RTL2832 based DVB-T receivers\n\n"
		"Usage:\n"
		"\t[-s samplerate (default: 2048000 Hz)]\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-t enable Elonics E4000 tuner benchmark]\n"
		"\t[-b output_block_size (default: 16 * 16384)]\n"
		"\t[-S force sync output (default: async)]\n");
	exit(1);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		log_warn(&log, "Signal caught, exiting!\n");
		do_exit = 1;
		rtlsdr_cancel_async(dev);
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	log_warn(&log, "Signal caught, exiting!\n");
	do_exit = 1;
	rtlsdr_cancel_async(dev);
}
#endif

uint8_t bcnt, uninit = 1;

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	uint32_t i, lost = 0;

	if (uninit) {
		bcnt = buf[0];
		uninit = 0;
	}

	for (i = 0; i < len; i++) {
		if(bcnt != buf[i]) {
			lost += (buf[i] > bcnt) ? (buf[i] - bcnt) : (bcnt - buf[i]);
			bcnt = buf[i];
		}

		bcnt++;
	}

	if (lost)
		log_info(&log, "lost at least %d bytes\n", lost);
}

void e4k_benchmark(void)
{
	uint32_t freq, gap_start = 0, gap_end = 0;
	uint32_t range_start = 0, range_end = 0;

	log_info(&log, "Benchmarking E4000 PLL...\n");

	/* find tuner range start */
	for (freq = MHZ(70); freq > MHZ(1); freq -= MHZ(1)) {
		if (rtlsdr_set_center_freq(dev, freq) < 0) {
			range_start = freq;
			break;
		}
	}

	/* find tuner range end */
	for (freq = MHZ(2000); freq < MHZ(2300UL); freq += MHZ(1)) {
		if (rtlsdr_set_center_freq(dev, freq) < 0) {
			range_end = freq;
			break;
		}
	}

	/* find start of L-band gap */
	for (freq = MHZ(1000); freq < MHZ(1300); freq += MHZ(1)) {
		if (rtlsdr_set_center_freq(dev, freq) < 0) {
			gap_start = freq;
			break;
		}
	}

	/* find end of L-band gap */
	for (freq = MHZ(1300); freq > MHZ(1000); freq -= MHZ(1)) {
		if (rtlsdr_set_center_freq(dev, freq) < 0) {
			gap_end = freq;
			break;
		}
	}

	log_info(&log, "E4K range: %i to %i MHz\n",
		range_start/MHZ(1) + 1, range_end/MHZ(1) - 1);

	log_info(&log, "E4K L-band gap: %i to %i MHz\n",
		gap_start/MHZ(1), gap_end/MHZ(1));
}
int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	int n_read;
	int r, opt;
	int i, tuner_benchmark = 0;
	int sync_mode = 0;
	uint8_t *buffer;
	uint32_t dev_index = 0;
	uint32_t samp_rate = DEFAULT_SAMPLE_RATE;
	uint32_t out_block_size = DEFAULT_BUF_LENGTH;
	int device_count;
	int count;
	int gains[100];

	while ((opt = getopt(argc, argv, "d:s:b:tS::")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = atoi(optarg);
			break;
		case 's':
			samp_rate = (uint32_t)atof(optarg);
			break;
		case 'b':
			out_block_size = (uint32_t)atof(optarg);
			break;
		case 't':
			tuner_benchmark = 1;
			break;
		case 'S':
			sync_mode = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if(out_block_size < MINIMAL_BUF_LENGTH ||
	   out_block_size > MAXIMAL_BUF_LENGTH ){
		log_warn(&log,
			"Output block size wrong value, falling back to default\n");
		log_warn(&log,
			"Minimal length: %u\n", MINIMAL_BUF_LENGTH);
		log_warn(&log,
			"Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
		out_block_size = DEFAULT_BUF_LENGTH;
	}

	buffer = malloc(out_block_size * sizeof(uint8_t));

	device_count = rtlsdr_get_device_count();
	if (!device_count) {
		log_warn(&log, "No supported devices found.\n");
		exit(1);
	}

	log_info(&log, "Found %d device(s):\n", device_count);
	for (i = 0; i < device_count; i++)
		log_info(&log, "  %d:  %s\n", i, rtlsdr_get_device_name(i));

	log_info(&log, "Using device %d: %s\n",
		dev_index,
		rtlsdr_get_device_name(dev_index));

	r = rtlsdr_open(&dev, dev_index);
	if (r < 0) {
		log_warn(&log, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}
#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif
	count = rtlsdr_get_tuner_gains(dev, NULL);
	log_info(&log, "Supported gain values (%d): ", count);

	count = rtlsdr_get_tuner_gains(dev, gains);
	for (i = 0; i < count; i++)
		log_info(&log, "%.1f ", gains[i] / 10.0);

	/* Set the sample rate */
	r = rtlsdr_set_sample_rate(dev, samp_rate);
	if (r < 0)
		log_warn(&log, "WARNING: Failed to set sample rate.\n");

	if (tuner_benchmark) {
		if (rtlsdr_get_tuner_type(dev) == RTLSDR_TUNER_E4000)
			e4k_benchmark();
		else
			log_warn(&log, "No E4000 tuner found, aborting.\n");

		goto exit;
	}

	/* Enable test mode */
	r = rtlsdr_set_testmode(dev, 1);

	/* Reset endpoint before we start reading from it (mandatory) */
	r = rtlsdr_reset_buffer(dev);
	if (r < 0)
		log_warn(&log, "WARNING: Failed to reset buffers.\n");

	if (sync_mode) {
		log_info(&log, "Reading samples in sync mode...\n");
		while (!do_exit) {
			r = rtlsdr_read_sync(dev, buffer, out_block_size, &n_read);
			if (r < 0) {
				log_warn(&log, "WARNING: sync read failed.\n");
				break;
			}

			if ((uint32_t)n_read < out_block_size) {
				log_warn(&log, "Short read, samples lost, exiting!\n");
				break;
			}
		}
	} else {
		log_info(&log, "Reading samples in async mode...\n");
		r = rtlsdr_read_async(dev, rtlsdr_callback, NULL,
				      DEFAULT_ASYNC_BUF_NUMBER, out_block_size);
	}

	if (do_exit)
		log_info(&log, "\nUser cancel, exiting...\n");
	else
		log_warn(&log, "\nLibrary error %d, exiting...\n", r);

exit:
	rtlsdr_close(dev);
	free (buffer);
out:
	return r >= 0 ? r : -r;
}

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

struct usb_device* get_device_from_object(JNIEnv* env, jobject connection)
{

    return (struct usb_device*)(*env)->GetIntField(env, connection, field_context);
}

int android_java_usbdevice_open(const char *pathname, int mode, ...)
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

int android_java_usbdevice_close(int fd)
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

JNIEXPORT jint JNICALL Java_rtlsdr_android_MainActivity_nativeMain(JNIEnv *envp, jobject objp)
{
  log_info(&log, "Starting native\n");
  struct sigaction sigact;
  env = envp; /* put env in a global.. for resuse in the same THREAD */
  //cls = (*env)->GetObjectClass(env, obj); /* put the calling class also there */
  launcherActivity = (*env)->NewGlobalRef(env, objp);
  /* inject our open method in the android usbhost lib. the same can probably be done for libusb */
  usb_device_set_open_close_func(android_java_usbdevice_open, android_java_usbdevice_close);
  char * args[] = { "rtltest" , "-t" };
  return main(2,args);
}

#endif

