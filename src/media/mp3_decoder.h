#ifndef __MP3_DECODER_H
#define __MP3_DECODER_H

#include "typedefs.h"
#include "common_event.h"

typedef enum {
    MP3_DECODER_SUCCESS = 0,
    MP3_DECODER_ERR_MALLOC,

} mp3_decoder_return_t;

typedef enum {
    MP3_DECODER_EVENT_NONE       = 0x000000UL,
    MP3_DECODER_EVENT_ALL        = 0xFFFFFFUL,
    MP3_DECODER_EVENT_START      = 0x000001UL,
    MP3_DECODER_EVENT_RESUME     = 0x000002UL,
    MP3_DECODER_EVENT_PAUSE      = 0x000004UL,
    MP3_DECODER_EVENT_EXIT       = 0x000008UL,
    MP3_DECODER_EVENT_EXIT_DONE  = 0x000010UL,
    MP3_DECODER_EVENT_STOP       = 0x000020UL,
    MP3_DECODER_EVENT_STOP_DONE  = 0x000040UL,

} mp3_decoder_event_t;

typedef enum {
    MP3_DECODER_STA_IDLE = 0,
    MP3_DECODER_STA_RUN,
    MP3_DECODER_STA_PAUSE,
    MP3_DECODER_STA_EXIT,

} mp3_decoder_state_t;

typedef struct {
    uint32_t tag_size;
    uint32_t sample_rate;
    uint32_t bit_rate;
    uint8_t  channels;

} audio_decoder_info_t;

typedef int(*p_decoder_input_callback)(void* param, uint8_t* buf, int size);
typedef int(*p_decoder_error_callback)(void* param, int error);
typedef int(*p_decoder_seek_callback)(void* param, int position);
typedef int(*p_decoder_output_callback)(void* param, audio_decoder_info_t* decoder_info, uint8_t* buf, int size);

typedef struct {
    TaskHandle_t        task_handle;
    EventGroupHandle_t  event_handle;
    
    mp3_decoder_state_t cur_state;
    bool                input_done;
    bool                output_done;
    
    p_decoder_input_callback    input_callback;
    void*                       input_param;
    p_decoder_error_callback    error_callback;
    void*                       error_param;
    p_decoder_output_callback   output_callback;
    void*                       output_param;

} mp3_decoder_t;

mp3_decoder_return_t mp3_decoder_init(mp3_decoder_t* mp3_decoder);
mp3_decoder_return_t mp3_decoder_deinit(mp3_decoder_t* mp3_decoder);
mp3_decoder_return_t mp3_decoder_start(mp3_decoder_t* mp3_decoder);
mp3_decoder_return_t mp3_decoder_stop(mp3_decoder_t* mp3_decoder);
mp3_decoder_return_t mp3_decoder_pause(mp3_decoder_t* mp3_decoder, bool from_isr);
mp3_decoder_return_t mp3_decoder_resume(mp3_decoder_t* mp3_decoder, bool from_isr);
mp3_decoder_return_t mp3_decoder_register_input_callback(mp3_decoder_t* mp3_decoder, p_decoder_input_callback callback, void* param);
mp3_decoder_return_t mp3_decoder_register_error_callback(mp3_decoder_t* mp3_decoder, p_decoder_error_callback callback, void* param);
mp3_decoder_return_t mp3_decoder_register_output_callback(mp3_decoder_t* mp3_decoder, p_decoder_output_callback callback, void* param);

void mp3_decoder_set_input_done(mp3_decoder_t* mp3_decoder);
bool mp3_decoder_is_output_done(mp3_decoder_t* mp3_decoder);
bool mp3_decoder_is_pause(mp3_decoder_t* mp3_decoder);

#endif
