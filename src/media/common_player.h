#ifndef __COMMMON_PLAYER_H
#define __COMMMON_PLAYER_H

#include "mp3_decoder.h"
#include "ring_buffer.h"
#include "pcm_trans.h"

typedef enum {
    COM_PLAYER_SUCCESS = 0,
    COM_PLAYER_ERR_PARAM,
    COM_PLAYER_ERR_PCM_TRANS,
    COM_PLAYER_ERR_EXIT,
    COM_PLAYER_ERR_TIMEOUT,
    
} com_player_return_t;

typedef enum {
    COM_PLAYER_EVENT_NONE     = 0x000000UL,
    COM_PLAYER_EVENT_ALL      = 0xFFFFFFUL,
    COM_PLAYER_EVENT_DECODER  = 0x000001UL,
    COM_PLAYER_EVENT_EXIT     = 0x000002UL,
    
} com_player_event_t;

typedef enum {
    COM_PLAYER_TYPE_MP3 = 0,
    COM_PLAYER_TYPE_M4A,

} com_player_type_t;

typedef struct {
    EventGroupHandle_t    event_handle;
    com_player_type_t     decoder_type;
    mp3_decoder_t         mp3_decoder;
    ring_buffer_t         output_buffer;
    audio_decoder_info_t  decoder_info;
    bool                  wait_decoder;
    bool                  input_done;
    uint32_t              pcm_played;
    bool                  enable_pcm_or_decoder;
    
} com_player_t;

com_player_return_t com_player_init(com_player_t* com_player);
com_player_return_t com_player_deinit(com_player_t* com_player);
com_player_return_t com_player_start(
    com_player_t* com_player, 
    p_decoder_input_callback input_callback,
    p_decoder_error_callback error_callback,
    p_decoder_seek_callback seek_callback,
    void* callback_param);

com_player_return_t com_player_stop(com_player_t* com_player);
com_player_return_t com_player_pause(com_player_t* com_player);
com_player_return_t com_player_resume(com_player_t* com_player);

void com_player_set_done(com_player_t* com_player, bool error_occur);
bool com_player_is_done(com_player_t* com_player);
bool com_player_auto_resume(com_player_t* com_player);
bool com_player_get_progress(com_player_t* com_player, int total_length, int* cur_time, int* all_time);


#endif

