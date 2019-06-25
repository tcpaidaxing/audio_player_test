#ifndef __TYPEDEFS_H
#define __TYPEDEFS_H

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#if 0
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long  uint32_t;
#endif

typedef pthread_t TaskHandle_t;
typedef int UINT;
typedef uint32_t StackType_t;

#define DEF_LINUX_PLATFORM

#define SemaphoreHandle_t					pthread_mutex_t*
#define xSemaphoreCreateBinary()			xSemaphoreCreateMutex()
#define vSemaphoreDelete(x)                 do { pthread_mutex_destroy(x); free(x); } while(0)
#define xSemaphoreGive(x)					pthread_mutex_unlock(x)//do { (x)++; } while(0)
#define xSemaphoreTakeFromISR(x, NULL)		xSemaphoreTake(x, portMAX_DELAY)
#define xSemaphoreGiveFromISR(x, NULL)		xSemaphoreGive(x)
#define xTaskCreate(a,b,c,d,e,f)			pthread_create(f,NULL,a,d)

#define pvPortMalloc(x)						malloc(x)
#define vPortFree(x)						free(x)
#define vTaskDelete(NULL)					return NULL
#define portMAX_DELAY                       0xFFFFFFFFUL
#define pdPASS								0
#define portTICK_RATE_MS					1
#define TickType_t							uint32_t

#define log_create_module(...)
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#define vTaskDelay(x)						usleep(1000*x)
#define xQueueGetMutexHolder(x)				1
#define xTaskGetCurrentTaskHandle()			0

#define DEVICE_ID							"123456"
#define MQTT_TOPIC_SERVER					""
#define MQTT_TOPIC_BROWSER					""
#define WIFI_CONNECTED						true
#define WIFI_UNCONNECTED					false
#define ALARM_REMIND_AUDIO					"AlmReminder.mp3"
#define g_wifi_connected_status				true
#define change_display_expression(x)
#define is_rec_do()							false
#define tf_card_audio_file_num_get()		0
#define tf_card_file_path_get(x,y)			0

typedef FILE* FIL;
typedef int FSIZE_t;
#define _T(x)								x
#define FR_OK								0
#define FA_OPEN_EXISTING					0x01
#define FA_WRITE							0x10
#define FA_READ								0x20

#define EventGroupHandle_t 					common_event_t*
#define xEventGroupCreate()					common_create_event()
#define vEventGroupDelete(x)				common_delete_event(x)
#define xEventGroupSetBits(x,y)				common_set_event(x,y)
#define xEventGroupSetBitsFromISR(x,y,t)	common_set_event(x,y)
#define xEventGroupClearBits(x,y)			common_clear_event(x,y)
#define xEventGroupClearBitsFromISR(x,y)	common_clear_event(x,y)
#define xEventGroupWaitBits(a,b,c,d,e)		common_wait_event(a,b,e)

#define LOG_I(x,...)    log_print(#x, "info", __func__, __LINE__, __VA_ARGS__)
#define LOG_W(x,...)    log_print(#x, "warn", __func__, __LINE__, __VA_ARGS__)
#define LOG_E(x,...)    log_print(#x, "error", __func__, __LINE__, __VA_ARGS__)

uint32_t xTaskGetTickCount(void);
SemaphoreHandle_t xSemaphoreCreateMutex();
int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t timeout);

int f_open(FILE** file, char* path, int mode);
int f_close(FILE** file);
int f_read(FILE** file, uint8_t* buf, int size, UINT* bytes_read);
int f_lseek(FILE** file, int offset);
int f_size(FILE** file);

int32_t mqtt_msg_send_with_timeout(char *topic, int qos, char *buf, TickType_t xTicksToWait);
void log_print(const char* module, const char* level, const char* func, int line, const char* fmt, ...);

#endif
