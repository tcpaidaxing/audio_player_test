#include "common_player.h"
#include <string.h>

#define malloc(x)   pvPortMalloc(x)
#define free(x)     vPortFree(x)

#define COM_PLAYER_OUTPUT_SIZE      (32*1024)
#define COM_PLAYER_START_TIMEOUT    (15000/portTICK_RATE_MS)

static int __decoder_output_callback(void* param, audio_decoder_info_t* decoder_info, uint8_t* buf, int size);
static int __pcm_trans_data_request_callback(void* param, uint8_t* buf, int size);

com_player_return_t com_player_init(com_player_t* com_player)
{
    memset(com_player, 0, sizeof(com_player_t));

    com_player->event_handle = xEventGroupCreate();
	
    ring_buffer_init(&com_player->output_buffer, COM_PLAYER_OUTPUT_SIZE);
    //mp3_decoder_init(&com_player->mp3_decoder);
    
    return COM_PLAYER_SUCCESS;
}

com_player_return_t com_player_deinit(com_player_t* com_player)
{
    if(NULL != com_player->event_handle) {
        vEventGroupDelete(com_player->event_handle);
        com_player->event_handle = NULL;
    }

	//mp3_decoder_deinit(&com_player->mp3_decoder);
    ring_buffer_deinit(&com_player->output_buffer);
    
    return COM_PLAYER_SUCCESS;
}

static void com_player_set_event(com_player_t* com_player, uint32_t events)
{
    if(NULL==com_player->event_handle)
        return;
    
    xEventGroupSetBits(com_player->event_handle, events);
}

static uint32_t com_player_wait_event(com_player_t* com_player, uint32_t events, uint32_t timeout)
{
    if(NULL==com_player->event_handle)
        return 0;
    
    return xEventGroupWaitBits(com_player->event_handle, events, pdTRUE, pdFALSE, timeout);
}

static void com_player_clear_event(com_player_t* com_player, uint32_t events)
{
    if(NULL==com_player->event_handle)
        return;
    
    xEventGroupClearBits(com_player->event_handle, events);
}

com_player_return_t com_player_start(
    com_player_t* com_player, 
    p_decoder_input_callback input_callback,
    p_decoder_error_callback error_callback,
    p_decoder_seek_callback seek_callback,
    void* callback_param)
{
    uint32_t events;
    
    com_player->input_done            = false;
    com_player->wait_decoder          = true;
    com_player->pcm_played            = 0;
    com_player->enable_pcm_or_decoder = true;

    com_player->decoder_type = COM_PLAYER_TYPE_MP3;

    memset(&com_player->decoder_info, 0, sizeof(audio_decoder_info_t));

    if(COM_PLAYER_TYPE_MP3 == com_player->decoder_type) {
        mp3_decoder_init(&com_player->mp3_decoder);
        mp3_decoder_register_input_callback(&com_player->mp3_decoder, input_callback, callback_param);
        mp3_decoder_register_error_callback(&com_player->mp3_decoder, error_callback, callback_param);
        mp3_decoder_register_output_callback(&com_player->mp3_decoder, __decoder_output_callback, com_player);
        mp3_decoder_start(&com_player->mp3_decoder);
    }

    events = COM_PLAYER_EVENT_DECODER |COM_PLAYER_EVENT_EXIT;
    events = com_player_wait_event(com_player, events, COM_PLAYER_START_TIMEOUT);

    if(COM_PLAYER_EVENT_DECODER & events) {
        pcm_trans_register_data_request_callback(__pcm_trans_data_request_callback, com_player);
        
        if(PCM_TRANS_SUCCESS != pcm_trans_start_tx(com_player->decoder_info.sample_rate, com_player->decoder_info.channels, false))
            return COM_PLAYER_ERR_PCM_TRANS;
    }
    else if(COM_PLAYER_EVENT_EXIT & events) {
        return COM_PLAYER_ERR_EXIT;
    }
    else {
        return COM_PLAYER_ERR_TIMEOUT;
    }
    
    com_player->wait_decoder = false;
    
    return COM_PLAYER_SUCCESS;
}

com_player_return_t com_player_stop(com_player_t* com_player)
{
    com_player->enable_pcm_or_decoder = false;
    
    pcm_trans_register_data_request_callback(NULL, NULL);
    pcm_trans_stop_tx();
    
    if(COM_PLAYER_TYPE_MP3 == com_player->decoder_type) {
        mp3_decoder_register_input_callback(&com_player->mp3_decoder, NULL, NULL);
        mp3_decoder_register_output_callback(&com_player->mp3_decoder, NULL, NULL);
        mp3_decoder_stop(&com_player->mp3_decoder);
        mp3_decoder_deinit(&com_player->mp3_decoder);
    }

    ring_buffer_clear(&com_player->output_buffer, false);
    com_player_clear_event(com_player, COM_PLAYER_EVENT_ALL);
    
    return COM_PLAYER_SUCCESS;
}

com_player_return_t com_player_pause(com_player_t* com_player)
{
    com_player->enable_pcm_or_decoder = false;
    
    pcm_trans_register_data_request_callback(NULL, NULL);
    pcm_trans_stop_tx();

    if(COM_PLAYER_TYPE_MP3 == com_player->decoder_type) {
        mp3_decoder_pause(&com_player->mp3_decoder, false);
    }
    
    return COM_PLAYER_SUCCESS;
}

com_player_return_t com_player_resume(com_player_t* com_player)
{
    com_player->enable_pcm_or_decoder = true;

    pcm_trans_register_data_request_callback(__pcm_trans_data_request_callback, com_player);
    
    if(PCM_TRANS_SUCCESS != pcm_trans_start_tx(com_player->decoder_info.sample_rate, com_player->decoder_info.channels, false)) {
         return COM_PLAYER_ERR_PCM_TRANS;
    }
    
    return COM_PLAYER_SUCCESS;
}

void com_player_set_done(com_player_t* com_player, bool error_occur)
{
    if(COM_PLAYER_TYPE_MP3 == com_player->decoder_type) {
        mp3_decoder_set_input_done(&com_player->mp3_decoder);
    }

    if(true == error_occur) {
        com_player_set_event(com_player, COM_PLAYER_EVENT_EXIT);
    }
}

bool com_player_is_done(com_player_t* com_player)
{
    return pcm_trans_is_tx_done();
}

bool com_player_auto_resume(com_player_t* com_player)
{
    if(true == pcm_trans_is_tx_pause() && true == mp3_decoder_is_pause(&com_player->mp3_decoder)) {
        pcm_trans_resume_tx();
        return true;
    }

    return false;
}

bool com_player_get_progress(com_player_t* com_player, int total_length, int* cur_time, int* all_time)
{
    audio_decoder_info_t* info = &com_player->decoder_info;
    
    if(total_length <= 0 || info->bit_rate <= 0 || info->sample_rate <= 0 || info->channels <= 0) {
        return false;
    }

    *cur_time = com_player->pcm_played /info->channels /2 /info->sample_rate;
    *all_time = (total_length - info->tag_size) *8 /info->bit_rate;
    
    return true;
}

static int __decoder_output_callback(void* param, audio_decoder_info_t* decoder_info, uint8_t* buf, int size)
{
    com_player_t* com_player = (com_player_t*)param;
    uint32_t count = ring_buffer_push(&com_player->output_buffer, buf, size, false);

    if(true == com_player->wait_decoder) {
        memcpy(&com_player->decoder_info, decoder_info, sizeof(audio_decoder_info_t));
        com_player_set_event(com_player, COM_PLAYER_EVENT_DECODER);
    }
    else if(true == com_player->enable_pcm_or_decoder) {
        pcm_trans_resume_tx();
    }

    return count;
}

static int __pcm_trans_data_request_callback(void* param, uint8_t* buf, int size)
{
    com_player_t* com_player = (com_player_t*)param;
    uint32_t count = ring_buffer_get_count(&com_player->output_buffer);
    uint32_t free_count = ring_buffer_get_free_count(&com_player->output_buffer);

    bool output_done = false;

    if(COM_PLAYER_TYPE_MP3 == com_player->decoder_type) {
        output_done = mp3_decoder_is_output_done(&com_player->mp3_decoder);
    }

    if(false == output_done && true == com_player->enable_pcm_or_decoder && free_count > count)
    {
        if(COM_PLAYER_TYPE_MP3 == com_player->decoder_type) {
            mp3_decoder_resume(&com_player->mp3_decoder, true);
        }
    }
    
    if(true == output_done && count <= 0) {
        pcm_trans_set_tx_no_data();
        return 0;
    }

    count = ring_buffer_pop(&com_player->output_buffer, buf, size, true);
    com_player->pcm_played += count;

    return count;
}

