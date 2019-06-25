#include "typedefs.h"
#include <stdarg.h>

uint32_t xTaskGetTickCount(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec*1000+tv.tv_usec/1000;
}

pthread_mutex_t* xSemaphoreCreateMutex()
{
	pthread_mutex_t* tmp = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	
	if(NULL != tmp)
		*tmp = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	
	return tmp;
}

int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t timeout)
{
	if(portMAX_DELAY == timeout)
		return pthread_mutex_lock(mutex);
	
	struct timeval now;
	struct timespec outtime;

	gettimeofday(&now, NULL);

	now.tv_sec  += timeout/1000;
	now.tv_usec += (timeout%1000)*1000;

	now.tv_sec  += now.tv_usec/1000000;
	now.tv_usec %= 1000000;
        
	outtime.tv_sec  = now.tv_sec;
	outtime.tv_nsec = now.tv_usec*1000;
	
	return pthread_mutex_timedlock(mutex, &outtime);
}

int f_open(FILE** file, char* path, int mode)
{
	FILE* ret = NULL;
	
	if(FA_READ & mode)
		ret = fopen(path, "rb");
	else
		ret = fopen(path, "wb");
	
	if(NULL == ret)
		return -1;
	
	*file = ret;
	return 0;
}

int f_close(FILE** file)
{
	if(*file == NULL)
		return -1;
	
	fclose(*file);
	return 0;
}

int f_read(FILE** file, uint8_t* buf, int size, UINT* bytes_read)
{
	int ret;
	ret = fread(buf, 1, size, *file);
	
	if(ret < 0)
		return ret;
	
	*bytes_read = ret;
	return 0;
}

int f_lseek(FILE** file, int offset)
{
	return fseek(*file, offset, SEEK_SET);
}

int f_size(FILE** file)
{
	int back = ftell(*file);
	int ret = 0;
	
	fseek(*file, 0, SEEK_END);
	ret = ftell(*file);
	fseek(*file, back, SEEK_SET);
	
	return ret;
}

int32_t mqtt_msg_send_with_timeout(char *topic, int qos, char *buf, TickType_t xTicksToWait)
{
	return 0;
}

void log_print(const char* module, const char* level, const char* func, int line, const char* fmt, ...)
{
	static uint32_t init_time = 0;
	uint32_t cur_time;
	char str[512];
	
	if(0 == init_time) {
		init_time = xTaskGetTickCount();
	}
	cur_time = xTaskGetTickCount() - init_time;
	
	va_list args;
	va_start(args, fmt);
	vsprintf(str, fmt, args);
	va_end(args);
	
	printf("[T:%u M:%s F:%s L:%d C:%s] %s\n", cur_time, module, func, line, level, str);
}
