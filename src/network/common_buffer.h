#ifndef __COMMON_BUFFER_H
#define __COMMON_BUFFER_H

#include "typedefs.h"

#define COMMON_BUF_HTTP_MAX_SIZE      (640*1024)
#define COMMON_BUF_HTTP_NODE_SIZE     10240

typedef enum {
    COMMON_BUF_SUCCESS = 0,
    COMMON_BUF_ERR_EMPTY,
    COMMON_BUF_ERR_MALLOC,
	COMMON_BUF_ERR_SIZE,

} common_buffer_return_t;

typedef struct common_node_s {
    uint8_t* buffer;
	uint32_t beg;
	uint32_t end;
	uint32_t count;
	uint32_t size;
	
    struct common_node_s* next;

} common_node_t;

typedef struct {
    SemaphoreHandle_t mutex;
    uint32_t          count;
    uint32_t          node_size;
    uint32_t          max_size;
    common_node_t*    head;
    common_node_t*    tail;

} common_buffer_t;

int common_buffer_init(common_buffer_t* com_buffer, uint32_t node_size, uint32_t max_size);
int common_buffer_deinit(common_buffer_t* com_buffer);
int common_buffer_clear(common_buffer_t* com_buffer);
int common_buffer_push(common_buffer_t* com_buffer, const uint8_t* buffer, uint32_t size);
int common_buffer_pop(common_buffer_t* com_buffer, uint8_t* buffer, uint32_t* p_size);
int common_buffer_get_count(common_buffer_t* com_buffer);
int common_buffer_get_free_count(common_buffer_t* com_buffer);


#endif

