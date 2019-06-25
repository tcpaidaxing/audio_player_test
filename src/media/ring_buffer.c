#include "ring_buffer.h"
#include <string.h>

#define malloc(x)           pvPortMalloc(x)
#define free(x)             vPortFree(x)

#define isPowerOfTwo(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))
#define min(a, b) (((a) < (b)) ? (a) : (b))

static int roundUpToPowerOfTwo(int i) 
{
    i--; // If input is a power of two, shift its high-order bit right.

    // "Smear" the high-order bit all the way to the right.
    i |= i >> 1;
    i |= i >> 2;
    i |= i >> 4;
    i |= i >> 8;
    i |= i >> 16;

    return i + 1;
}

int ring_buffer_init(ring_buffer_t* ring_buffer, uint32_t size)
{
    if (!isPowerOfTwo(size))
    {
        if(size > 0x80000000)                //max size 2G
            return RING_BUF_ERR_SIZE;
        size = roundUpToPowerOfTwo(size);
    }
        
	ring_buffer->buffer    = (uint8_t*)malloc(size);
    if(NULL == ring_buffer->buffer) 
    {
        return RING_BUF_ERR_MALLOC;
    }
    memset(ring_buffer->buffer, 0, size);
    
    ring_buffer->mutex     = xSemaphoreCreateMutex();
	ring_buffer->in       = 0;
	ring_buffer->out       = 0;
	ring_buffer->size  = size;
    
    return RING_BUF_SUCCESS;
}

int ring_buffer_deinit(ring_buffer_t* ring_buffer)
{
	if(NULL != ring_buffer->buffer) {
		free(ring_buffer->buffer);
		ring_buffer->buffer = NULL;
	}
	
    if(NULL != ring_buffer->mutex) {
        vSemaphoreDelete(ring_buffer->mutex);
        ring_buffer->mutex = NULL;
    }

	return RING_BUF_SUCCESS;
}

static int ring_buffer_lock(ring_buffer_t* ring_buffer, bool from_isr)
{
    if(true == from_isr)
        xSemaphoreTakeFromISR(ring_buffer->mutex, NULL);
    else
        xSemaphoreTake(ring_buffer->mutex, portMAX_DELAY);
    
    return RING_BUF_SUCCESS;
}

static int ring_buffer_unlock(ring_buffer_t* ring_buffer, bool from_isr)
{
    if(true == from_isr)
        xSemaphoreGiveFromISR(ring_buffer->mutex, NULL);
    else
        xSemaphoreGive(ring_buffer->mutex);
    
    return RING_BUF_SUCCESS;
}

uint32_t ring_buffer_push(ring_buffer_t* ring_buffer, const void* buffer, uint32_t size, bool from_isr)
{
    uint32_t len = 0;

    ring_buffer_lock(ring_buffer, from_isr);
	
	size = min(size, ring_buffer->size - ring_buffer->in + ring_buffer->out);
    
    /* first put the data starting from fifo->in to buffer out */
    len  = min(size, ring_buffer->size - (ring_buffer->in & (ring_buffer->size - 1)));
    memcpy(ring_buffer->buffer + (ring_buffer->in & (ring_buffer->size - 1)), buffer, len);
    /* then put the rest (if any) at the beginning of the buffer */
    memcpy(ring_buffer->buffer, buffer + len, size - len);
	
    ring_buffer->in += size;
 
    ring_buffer_unlock(ring_buffer, from_isr);
    return size;
}

uint32_t ring_buffer_pop(ring_buffer_t* ring_buffer, void* buffer, uint32_t size, bool from_isr)
{
    uint32_t len = 0;

    ring_buffer_lock(ring_buffer, from_isr);

    size = min(size, ring_buffer->in - ring_buffer->out);
    
    /* first get the data from fifo->out until the out of the buffer */
    len = min(size, ring_buffer->size - (ring_buffer->out & (ring_buffer->size - 1)));
    memcpy(buffer, ring_buffer->buffer + (ring_buffer->out & (ring_buffer->size - 1)), len);
    /* then get the rest (if any) from the beginning of the buffer */
    memcpy(buffer + len, ring_buffer->buffer, size - len);

    ring_buffer->out += size;
    
    ring_buffer_unlock(ring_buffer, from_isr);
    return size;
}

int ring_buffer_clear(ring_buffer_t* ring_buffer, bool from_isr)
{
    ring_buffer_lock(ring_buffer, from_isr);
    
    ring_buffer->in   = 0;
    ring_buffer->out   = 0;

    ring_buffer_unlock(ring_buffer, from_isr);
    return RING_BUF_SUCCESS;
}

int ring_buffer_get_count(ring_buffer_t* ring_buffer)
{
    return (ring_buffer->in - ring_buffer->out);
}

int ring_buffer_get_free_count(ring_buffer_t* ring_buffer)
{
    return (ring_buffer->size - ring_buffer->in + ring_buffer->out);
}
