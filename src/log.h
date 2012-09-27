#ifndef __LOG_H__
#define __LOG_H__

#define LEVEL_NONE 0  /* do not log anything */
#define LEVEL_WARN 1  /* Information that needs to be known  */
#define LEVEL_INFO 2  /* Basic information like startup messages and occasional events */
#define LEVEL_DEBUG 3 /* debug statements about things happening that are less expected */
#define LEVEL_TRACE 4 /* way to much information for anybody */


static const char *level_string[5] = {
		"none",
		"warn",
		"info",
		"debug",
		"trace"
};

/*
 * struct to be initialized by the user of the logging system.
 *
 * name:
 * 	The name attribute is used in loggging statements do diffrencate drivers
 *
 * log_level
 * 	The level attribute describes the requested logging level. a level of 1 will
 * 	only print warnings while a level of 4 will print all the trace information.
 *
 * log_func
 * 	The logging function to use to log, log.h provides default_log to display
 * 	information on the kernel output buffer. As a bonus if the requested log level
 * 	is debug or trace the method , file and line number will be printed to the steam.
 *
 */
struct logger {
	const char *name;
	int log_level;

	/* the logging function itself */
	void (*log_func)(struct logger *driver, int level,const char *file, const char *function, int line, const char * fmt, ...);

};

#define __log(driver,log_level, fmt, args...) \
		((driver)->log_func(driver,log_level,  __FILE__, __FUNCTION__, __LINE__,fmt, ## args))

/* Log a warning */
#define log_warn(driver, fmt, args...) \
		__log(driver, LEVEL_WARN, fmt, ## args)

/* Log an information message  */
#define log_info(driver, fmt, args...) \
		__log(driver, LEVEL_INFO, fmt, ## args)

/* log debugging output  */
#define log_debug(driver, fmt, args...) \
		__log(driver, LEVEL_DEBUG, fmt, ## args)

/* log trace output  */
#define log_trace(driver, fmt, args...) \
		__log(driver, LEVEL_TRACE, fmt, ## args)


#ifdef USE_LIBLOG
#define LOG_TAG "usbhost"
#include <android/log.h>


static int android_level_mapping[5] = {
	ANDROID_LOG_SILENT, /* LEVEL_NONE  */
	ANDROID_LOG_WARN,    /* LEVEL_WARN  */
	ANDROID_LOG_INFO,    /* LEVEL_INFO  */
	ANDROID_LOG_DEBUG,   /* LEVEL_DEBUG */
	ANDROID_LOG_VERBOSE, /* LEVEL_TRACE */
};

void default_log (struct logger *driver, int level, const char *file, const char *function , int line, const char * fmt, ...)
{
	va_list args;

	if (level > driver->log_level){
		return;
	}
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	__android_log_vprint(android_level_mapping[level],driver->name, fmt, args);
}
#else
void default_log (struct logger *driver, int level, const char *file, const char *function , int line, const char * fmt, ...)
{
	va_list args;

	if (level > driver->log_level){
		return;
	}
	va_start(args, fmt);
	/* warnings are printed to stderr and the rest to stdout */
	vfprintf((level == LEVEL_WARN)?stderr:stdout, fmt, args);
	va_end(args);
}
#endif /* ! USB_LIBLOG */
#endif /* __LOG_H__ */
