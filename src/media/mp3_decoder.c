#include "mp3_decoder.h"
#include "typedefs.h"
#include "mad.h"
#include "id3tag.h"
#include <string.h>

#define malloc(x)   pvPortMalloc(x)
#define free(x)     vPortFree(x)

log_create_module(mp3_decoder, PRINT_LEVEL_INFO);

#define MP3_DECODER_MAX_WAIT_TIME       (30000/portTICK_RATE_MS)
#define MP3_DECODER_INPUT_SIZE          (10*1024)
#define MP3_DECODER_OUTPUT_SIZE     (10*1024)
#define MP3_DECODER_TASK_STACK_SIZE (10*1024/sizeof(StackType_t))
#define MP3_DECODER_MAX_ERR_COUNT   (20)

static void mp3_decoder_task(void* param);
static uint32_t mp3_decoder_wait_event(mp3_decoder_t* mp3_decoder, uint32_t events, uint32_t timeout);
static void mp3_decoder_set_event(mp3_decoder_t* mp3_decoder, uint32_t events, bool from_isr);

mp3_decoder_return_t mp3_decoder_init(mp3_decoder_t* mp3_decoder)
{
    mp3_decoder->cur_state       = MP3_DECODER_STA_IDLE;
    mp3_decoder->event_handle    = xEventGroupCreate();

    mp3_decoder->input_callback  = NULL;
    mp3_decoder->input_param     = NULL;
    mp3_decoder->error_callback  = NULL;
    mp3_decoder->error_param     = NULL;
    mp3_decoder->output_callback = NULL;
    mp3_decoder->output_param    = NULL;
    mp3_decoder->input_done      = false;
    mp3_decoder->output_done     = false;

    xTaskCreate(
        mp3_decoder_task, 
        "mp3_decoder_task", 
        MP3_DECODER_TASK_STACK_SIZE, 
        mp3_decoder,
        TASK_PRIORITY_HIGH,
        &mp3_decoder->task_handle);
    
    return MP3_DECODER_SUCCESS;
}

mp3_decoder_return_t mp3_decoder_deinit(mp3_decoder_t* mp3_decoder)
{
    if(MP3_DECODER_STA_EXIT != mp3_decoder->cur_state)
    {
        mp3_decoder_set_event(mp3_decoder, MP3_DECODER_EVENT_EXIT, false);
        
        if(MP3_DECODER_EVENT_NONE == mp3_decoder_wait_event(mp3_decoder, 
            MP3_DECODER_EVENT_EXIT_DONE, MP3_DECODER_MAX_WAIT_TIME))
        {
            LOG_E(mp3_decoder, "mp3_decoder_wait_event timeout!");
        }

        if(NULL != mp3_decoder->event_handle) {
            vEventGroupDelete(mp3_decoder->event_handle);
            mp3_decoder->event_handle = NULL;
        }
		
#ifdef DEF_LINUX_PLATFORM
        pthread_join(mp3_decoder->task_handle, NULL);
#endif
    }

    return MP3_DECODER_SUCCESS;
}

mp3_decoder_return_t mp3_decoder_start(mp3_decoder_t* mp3_decoder)
{
    if(MP3_DECODER_STA_IDLE == mp3_decoder->cur_state) {
        mp3_decoder->input_done = false;
        mp3_decoder->output_done = false;

        mp3_decoder_set_event(mp3_decoder, MP3_DECODER_EVENT_START, false);
    }

    return MP3_DECODER_SUCCESS;
}

mp3_decoder_return_t mp3_decoder_stop(mp3_decoder_t* mp3_decoder)
{
    if(MP3_DECODER_STA_IDLE != mp3_decoder->cur_state)
    {
        mp3_decoder_set_event(mp3_decoder, MP3_DECODER_EVENT_STOP, false);
        
        if(MP3_DECODER_EVENT_NONE == mp3_decoder_wait_event(mp3_decoder, 
            MP3_DECODER_EVENT_STOP_DONE, MP3_DECODER_MAX_WAIT_TIME))
        {
            LOG_E(mp3_decoder, "mp3_decoder_wait_event timeout!");
        }
    }
    
    return MP3_DECODER_SUCCESS;
}

mp3_decoder_return_t mp3_decoder_pause(mp3_decoder_t* mp3_decoder, bool from_isr)
{
    if(MP3_DECODER_STA_RUN == mp3_decoder->cur_state) {
        mp3_decoder_set_event(mp3_decoder, MP3_DECODER_EVENT_PAUSE, from_isr);
    }

    return MP3_DECODER_SUCCESS;
}

mp3_decoder_return_t mp3_decoder_resume(mp3_decoder_t* mp3_decoder, bool from_isr)
{
    if(MP3_DECODER_STA_PAUSE == mp3_decoder->cur_state) {
        mp3_decoder_set_event(mp3_decoder, MP3_DECODER_EVENT_RESUME, from_isr);
    }

    return MP3_DECODER_SUCCESS;
}

mp3_decoder_return_t mp3_decoder_register_input_callback(mp3_decoder_t* mp3_decoder, p_decoder_input_callback callback, void* param)
{
    mp3_decoder->input_callback = callback;
    mp3_decoder->input_param    = param;
    
    return MP3_DECODER_SUCCESS;
}

mp3_decoder_return_t mp3_decoder_register_error_callback(mp3_decoder_t* mp3_decoder, p_decoder_error_callback callback, void* param)
{
    mp3_decoder->error_callback = callback;
    mp3_decoder->error_param    = param;
    
    return MP3_DECODER_SUCCESS;
}

mp3_decoder_return_t mp3_decoder_register_output_callback(mp3_decoder_t* mp3_decoder, p_decoder_output_callback callback, void* param)
{
    mp3_decoder->output_callback = callback;
    mp3_decoder->output_param    = param;
    
    return MP3_DECODER_SUCCESS;
}

void mp3_decoder_set_input_done(mp3_decoder_t* mp3_decoder)
{
    mp3_decoder->input_done = true;
}

bool mp3_decoder_is_output_done(mp3_decoder_t* mp3_decoder)
{
    return mp3_decoder->output_done;
}

bool mp3_decoder_is_pause(mp3_decoder_t* mp3_decoder)
{
    return (MP3_DECODER_STA_PAUSE==mp3_decoder->cur_state) ?true :false;
}

static uint32_t mp3_decoder_wait_event(mp3_decoder_t* mp3_decoder, uint32_t events, uint32_t timeout)
{
    if(NULL==mp3_decoder->event_handle)
        return 0;

    return xEventGroupWaitBits(mp3_decoder->event_handle, events, pdTRUE, pdFALSE, timeout);
}

static void mp3_decoder_set_event(mp3_decoder_t* mp3_decoder, uint32_t events, bool from_isr)
{
    if(NULL==mp3_decoder->event_handle)
        return;

    if(true == from_isr)
        xEventGroupSetBitsFromISR(mp3_decoder->event_handle, events, NULL);
    else
        xEventGroupSetBits(mp3_decoder->event_handle, events);
}

static void mp3_decoder_clear_event(mp3_decoder_t* mp3_decoder, uint32_t events, bool from_isr)
{
    if(NULL==mp3_decoder->event_handle)
        return;

    if(true == from_isr)
        xEventGroupClearBitsFromISR(mp3_decoder->event_handle, events);
    else
        xEventGroupClearBits(mp3_decoder->event_handle, events);
}

static signed int scale(mad_fixed_t sample)
{
    /* round */
    sample += (1L << (MAD_F_FRACBITS - 16));

    /* clip */
    if (sample >= MAD_F_ONE)
        sample = MAD_F_ONE - 1;
    else if (sample < -MAD_F_ONE)
        sample = -MAD_F_ONE;

    /* quantize */
    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

typedef struct {
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;
    audio_decoder_info_t decoder_info;

    uint8_t input_buffer[MP3_DECODER_INPUT_SIZE];

    uint8_t* output_buffer;
    int output_size;
    int output_total;

    int decoder_error;

} mp3_decoder_memory_t;

static void mp3_decoder_dealloc_memory(mp3_decoder_memory_t** pp_mem)
{
    if(NULL == pp_mem || NULL == (*pp_mem))
        return;

    mp3_decoder_memory_t* tmp = *pp_mem;

    mad_synth_finish(&tmp->synth);
    mad_frame_finish(&tmp->frame);
    mad_stream_finish(&tmp->stream);

    if(NULL != tmp->output_buffer) {
        free(tmp->output_buffer);
        tmp->output_buffer = NULL;
    }

    free(tmp);
    *pp_mem = NULL;
}

static void mp3_decoder_alloc_memory(mp3_decoder_memory_t** pp_mem)
{
    if(NULL == pp_mem)
        return;

    mp3_decoder_dealloc_memory(pp_mem);

    mp3_decoder_memory_t* tmp = (mp3_decoder_memory_t*)malloc(sizeof(mp3_decoder_memory_t));
    if(NULL == tmp) {
        LOG_E(mp3_decoder, "alloc memory failed!");
        return;
    }

    memset(tmp, 0, sizeof(mp3_decoder_memory_t));

    mad_stream_init(&tmp->stream);
    mad_frame_init(&tmp->frame);
    mad_synth_init(&tmp->synth);

    *pp_mem = tmp;
}

static bool mp3_decoder_output_handler(mp3_decoder_t* mp3_decoder, mp3_decoder_memory_t* mem)
{
    if(NULL == mem->output_buffer)
        return true;

    int size = mp3_decoder->output_callback(mp3_decoder->output_param, &mem->decoder_info, mem->output_buffer, mem->output_size);
    mem->output_total += size;

    if(size < mem->output_size)
    {
        mem->output_size -= size;
        memmove(mem->output_buffer, &mem->output_buffer[size], mem->output_size);
        return false;
    }

    free(mem->output_buffer);
    mem->output_buffer = NULL;
    mem->output_size = 0;

    return true;
}

static void mp3_decoder_error_handler(mp3_decoder_t* mp3_decoder, mp3_decoder_memory_t* mem)
{
    if(NULL == mem || NULL == mp3_decoder->error_callback)
        return;

    if(mem->decoder_error >= MP3_DECODER_MAX_ERR_COUNT) {
        mp3_decoder->error_callback(mp3_decoder->error_param, mem->stream.error);
    }
    else if(true == mp3_decoder->output_done && mem->output_total <= 0) {
        mp3_decoder->error_callback(mp3_decoder->error_param, mem->stream.error);
    }
}

static void mp3_decoder_task(void* param)
{
    mp3_decoder_t* mp3_decoder = (mp3_decoder_t*)param;
    mp3_decoder_memory_t* mem = NULL;
    uint32_t events;

    mp3_decoder->cur_state = MP3_DECODER_STA_IDLE;
    
    while(1)
    {
        /* ---------------- [step 1] events handler ---------------- */
        events = 
            MP3_DECODER_EVENT_EXIT |
            MP3_DECODER_EVENT_STOP |
            MP3_DECODER_EVENT_START |
            MP3_DECODER_EVENT_RESUME |
            MP3_DECODER_EVENT_PAUSE;

        mp3_decoder_error_handler(mp3_decoder, mem);

        if(MP3_DECODER_STA_RUN == mp3_decoder->cur_state)
            events = mp3_decoder_wait_event(mp3_decoder, events, 0);
        else
            events = mp3_decoder_wait_event(mp3_decoder, events, portMAX_DELAY);

        if(MP3_DECODER_EVENT_EXIT & events) {
            mp3_decoder->cur_state = MP3_DECODER_STA_EXIT;
            mp3_decoder_set_event(mp3_decoder, MP3_DECODER_EVENT_EXIT_DONE, false);
            LOG_I(mp3_decoder, "MP3_DECODER_EVENT_EXIT");
            break;
        }
        else if(MP3_DECODER_EVENT_STOP & events) {
            mp3_decoder->cur_state = MP3_DECODER_STA_IDLE;
            mp3_decoder_dealloc_memory(&mem);
        
            mp3_decoder_set_event(mp3_decoder, MP3_DECODER_EVENT_STOP_DONE, false);
            LOG_I(mp3_decoder, "MP3_DECODER_EVENT_STOP");
        }
        else if(MP3_DECODER_EVENT_START & events) {
            mp3_decoder_alloc_memory(&mem);

            mp3_decoder->cur_state = MP3_DECODER_STA_RUN;
            LOG_I(mp3_decoder, "MP3_DECODER_EVENT_START");
        }
        else if(MP3_DECODER_EVENT_RESUME & events) {
            mp3_decoder->cur_state = MP3_DECODER_STA_RUN;
            //LOG_I(mp3_decoder, "MP3_DECODER_EVENT_RESUME");
        }
        else if(MP3_DECODER_EVENT_PAUSE & events) {
            mp3_decoder->cur_state = MP3_DECODER_STA_PAUSE;
            //LOG_I(mp3_decoder, "MP3_DECODER_EVENT_PAUSE");
        }

        if(MP3_DECODER_STA_RUN != mp3_decoder->cur_state || NULL == mem)
            continue;

        if(false == mp3_decoder_output_handler(mp3_decoder, mem)) {
            mp3_decoder->cur_state = MP3_DECODER_STA_PAUSE;
            continue;
        }
    
        /* ---------------- [step 2] input mp3 ---------------- */
        if(NULL == mem->stream.buffer || MAD_ERROR_BUFLEN == mem->stream.error)
        {
            int input_size = MP3_DECODER_INPUT_SIZE;
            int remain_size = 0;

            if(true == mp3_decoder->input_done) {
                mp3_decoder->output_done = true;
                mp3_decoder->cur_state = MP3_DECODER_STA_PAUSE;
                LOG_I(mp3_decoder, "mp3_decoder input done");
                continue;
            }

            if(NULL != mem->stream.next_frame)
            {
                remain_size = mem->stream.bufend - mem->stream.next_frame;
                input_size  = MP3_DECODER_INPUT_SIZE - remain_size;
                memmove(mem->input_buffer, mem->stream.next_frame, remain_size);
            }

            input_size = mp3_decoder->input_callback(mp3_decoder->input_param, &mem->input_buffer[remain_size], input_size);
            
            if(input_size > 0) {
                mad_stream_buffer(&mem->stream, mem->input_buffer, input_size + remain_size);
            }
            else {
                mp3_decoder->cur_state = MP3_DECODER_STA_PAUSE;
                LOG_I(mp3_decoder, "mp3_decoder pause because no input");
                continue;
            }

            mem->stream.error = MAD_ERROR_NONE;
        }

        if(mem->decoder_info.tag_size <= 0) {
            mem->decoder_info.tag_size = id3_tag_query(mem->stream.this_frame, mem->stream.bufend - mem->stream.this_frame);
        }

        /* ---------------- [step 3] mp3 to pcm ---------------- */
        if(MAD_ERROR_NONE != mad_frame_decode(&mem->frame, &mem->stream))
        {
            if(MAD_ERROR_BUFLEN == mem->stream.error) {
                continue;
            }
            else if(MAD_ERROR_LOSTSYNC == mem->stream.error) {
                int tagsize = id3_tag_query(mem->stream.this_frame, mem->stream.bufend - mem->stream.this_frame);
                if(tagsize > 0)
                    mad_stream_skip(&mem->stream, tagsize);
            }
            else if(MAD_RECOVERABLE(mem->stream.error)) {
            }
            else {
                mp3_decoder->cur_state = MP3_DECODER_STA_PAUSE;
                LOG_E(mp3_decoder, "mp3_decoder error 0x%04x (%s)", mem->stream.error, mad_stream_errorstr(&mem->stream));
            }

            mem->decoder_error++;
            continue;
        }
        else
        {
            mem->decoder_error = 0;
        }

        /* ---------------- [step 4] output pcm ---------------- */
        mad_synth_frame(&mem->synth, &mem->frame);
        mem->decoder_info.sample_rate = mem->frame.header.samplerate;
        mem->decoder_info.bit_rate    = mem->frame.header.bitrate;
        mem->decoder_info.channels    = mem->synth.pcm.channels;

        mem->output_size = mem->synth.pcm.length*mem->synth.pcm.channels*sizeof(uint16_t);
        mem->output_buffer = (uint8_t*)malloc(mem->output_size);

        if(NULL != mem->output_buffer)
        {
            int i, j, tmp, count = 0;
            for(i = 0; i < mem->synth.pcm.length; i++)
            {
                for(j = 0; j < mem->synth.pcm.channels; j++) {
                    tmp = scale(mem->synth.pcm.samples[j][i]);
                    mem->output_buffer[count++] = tmp;
                    mem->output_buffer[count++] = tmp >> 8;
                }
            }
        }

        if(false == mp3_decoder_output_handler(mp3_decoder, mem)) {
            mp3_decoder->cur_state = MP3_DECODER_STA_PAUSE;
        }
    }

    mp3_decoder_dealloc_memory(&mem);
    vTaskDelete(NULL);
}

