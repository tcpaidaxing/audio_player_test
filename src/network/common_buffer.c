#include "common_buffer.h"

#define malloc(x)           pvPortMalloc(x)
#define free(x)             vPortFree(x)

int common_buffer_init(common_buffer_t* com_buffer, uint32_t node_size, uint32_t max_size)
{
    com_buffer->mutex     = xSemaphoreCreateMutex();
    com_buffer->count     = 0;
    com_buffer->node_size = node_size;
    com_buffer->max_size  = max_size;
    com_buffer->head      = NULL;
    com_buffer->tail      = NULL;
    
    return COMMON_BUF_SUCCESS;
}

int common_buffer_deinit(common_buffer_t* com_buffer)
{
    if(NULL != com_buffer->mutex) {
        common_buffer_clear(com_buffer);
        
        vSemaphoreDelete(com_buffer->mutex);
        com_buffer->mutex = NULL;
    }

	return COMMON_BUF_SUCCESS;
}

static int common_buffer_lock(common_buffer_t* com_buffer)
{
    xSemaphoreTake(com_buffer->mutex, portMAX_DELAY);
    return COMMON_BUF_SUCCESS;
}

static int common_buffer_unlock(common_buffer_t* com_buffer)
{
    xSemaphoreGive(com_buffer->mutex);
    return COMMON_BUF_SUCCESS;
}

static int common_buffer_create_node(common_node_t** pp_node, uint32_t size)
{
    int ret = COMMON_BUF_SUCCESS;
    common_node_t* tmp = (common_node_t*)malloc(sizeof(common_node_t));
    
    if(NULL == tmp) {
        ret = COMMON_BUF_ERR_MALLOC;
        goto END;
    }

    memset(tmp, 0, sizeof(common_node_t));
	
	tmp->size = size;
    tmp->buffer = (uint8_t*)malloc(size);
    if(NULL == tmp->buffer) {
        free(tmp);
        ret = COMMON_BUF_ERR_MALLOC;
        goto END;
    }

    memset(tmp->buffer, 0, size);

    *pp_node = tmp;

END:
    return ret;
}

static int common_buffer_destroy_node(common_node_t* node)
{
    free(node->buffer);
    free(node);
    return COMMON_BUF_SUCCESS;
}

static int common_buffer_push_node(common_node_t* node, const uint8_t* buffer, uint32_t size)
{
	uint32_t len, pos = 0;
	
	if(size > (node->size - node->count))
		size = (node->size - node->count);
	
	while(pos < size) {
		len = node->size - node->end;
		
		if(len > (size - pos))
			len = size - pos;
		
		memcpy(&node->buffer[node->end], &buffer[pos], len);
		node->end = (node->end + len) % node->size;
		pos += len;
	
	}
	
	node->count += size;
	return size;
}

static int common_buffer_pop_node(common_node_t* node, uint8_t* buffer, uint32_t size)
{
	uint32_t len, pos = 0;
	
	if(size > node->count)
		size = node->count;
	
	while(pos < size) {
		len = node->size - node->beg;
		
		if(len > (size - pos))
			len = size - pos;

        if(NULL != buffer)
		    memcpy(&buffer[pos], &node->buffer[node->beg], len);
        
		node->beg = (node->beg + len) % node->size;
		pos += len;
	}
	
	node->count -= size;
	return size;
}

int common_buffer_clear(common_buffer_t* com_buffer)
{
    int ret = COMMON_BUF_SUCCESS;
	common_node_t* tmp;
    common_buffer_lock(com_buffer);

    while(com_buffer->head) {
		tmp = com_buffer->head;
        com_buffer->head = com_buffer->head->next;

		common_buffer_destroy_node(tmp);
    }

    com_buffer->count = 0;
    com_buffer->head  = NULL;
    com_buffer->tail  = NULL;

    common_buffer_unlock(com_buffer);
    return ret;
}

int common_buffer_push(common_buffer_t* com_buffer, const uint8_t* buffer, uint32_t size)
{
    int ret = COMMON_BUF_SUCCESS;
    common_node_t* tmp;
	uint32_t len, pos = 0;

    common_buffer_lock(com_buffer);
	
	if(size > (com_buffer->max_size - com_buffer->count)) {
		ret = COMMON_BUF_ERR_SIZE;
		goto END;
	}
	
	while(pos < size)
	{
		if(NULL==com_buffer->tail || com_buffer->tail->count >= com_buffer->tail->size)
		{
			ret = common_buffer_create_node(&tmp, com_buffer->node_size);
			
			if(COMMON_BUF_SUCCESS != ret)
				break;
			
			tmp->next = NULL;
				
			if(NULL == com_buffer->head) {
				com_buffer->head = com_buffer->tail = tmp;
			}
			else {
				com_buffer->tail->next = tmp;
				com_buffer->tail = tmp;
			}
		}
		
		len = common_buffer_push_node(com_buffer->tail, &buffer[pos], size-pos);
		pos += len;
		com_buffer->count += len;
	}

END:
    common_buffer_unlock(com_buffer);
    return ret;
}

int common_buffer_pop(common_buffer_t* com_buffer, uint8_t* buffer, uint32_t* p_size)
{
    int ret = COMMON_BUF_SUCCESS;
	common_node_t* tmp;
	uint32_t len, pos = 0;
	uint32_t size = *p_size;
	
    common_buffer_lock(com_buffer);
    *p_size = 0;
	
	if(size > com_buffer->count)
		size = com_buffer->count;

	if(NULL == com_buffer->head) {
		ret = COMMON_BUF_ERR_EMPTY;
		goto END;
	}
	
	while(pos < size)
	{
		if(NULL == com_buffer->head) {
			ret = COMMON_BUF_ERR_EMPTY;
			break;
		}

        if(NULL != buffer)
		    len = common_buffer_pop_node(com_buffer->head, &buffer[pos], size-pos);
        else
            len = common_buffer_pop_node(com_buffer->head, NULL, size-pos);
        
		pos += len;
		com_buffer->count -= len;
		
		if(com_buffer->head->count <= 0)
		{
			tmp = com_buffer->head;
			com_buffer->head = com_buffer->head->next;

			if(NULL==com_buffer->head) {
				com_buffer->tail = NULL;
			}
			
			common_buffer_destroy_node(tmp);
		}
	}

END:
	*p_size = size;
    common_buffer_unlock(com_buffer);
    return ret;
}

int common_buffer_get_count(common_buffer_t* com_buffer)
{
    return com_buffer->count;
}

int common_buffer_get_free_count(common_buffer_t* com_buffer)
{
    return com_buffer->max_size - com_buffer->count;
}

