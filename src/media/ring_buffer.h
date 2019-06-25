#ifndef __RING_BUFFER_H
#define __RING_BUFFER_H

#include "typedefs.h"

typedef enum {
    RING_BUF_SUCCESS = 0,
    RING_BUF_ERR_EMPTY,
    RING_BUF_ERR_MALLOC,
	RING_BUF_ERR_SIZE,

} ring_buffer_return_t;

typedef struct {
    SemaphoreHandle_t mutex;
    uint8_t* buffer;      /* the buffer holding the data */
	uint32_t in;         /* data is added at offset (in % size) */
	uint32_t out;         /* data is extracted from off. (out % size) */
	uint32_t size;    /* the size of the allocated buffer */

} ring_buffer_t;

int ring_buffer_init(ring_buffer_t* ring_buffer, uint32_t size);
int ring_buffer_deinit(ring_buffer_t* ring_buffer);
int ring_buffer_clear(ring_buffer_t* ring_buffer, bool from_isr);
//int ring_buffer_push(ring_buffer_t* ring_buffer, const uint8_t* buffer, uint32_t size, bool from_isr);
uint32_t ring_buffer_push(ring_buffer_t* ring_buffer, const void* buffer, uint32_t size, bool from_isr);
uint32_t ring_buffer_pop(ring_buffer_t* ring_buffer, void* buffer, uint32_t size, bool from_isr);
//int ring_buffer_pop(ring_buffer_t* ring_buffer, uint8_t* buffer, uint32_t* p_size, bool from_isr);
int ring_buffer_get_count(ring_buffer_t* ring_buffer);
int ring_buffer_get_free_count(ring_buffer_t* ring_buffer);


#endif

