#include "pcm_trans.h"
#include <alsa/asoundlib.h>

typedef enum {
    PCM_TRANS_EVENT_NONE            = 0x000000UL,
    PCM_TRANS_EVENT_ALL             = 0xFFFFFFUL,
    PCM_TRANS_EVENT_TX_RESUME       = 0x000001UL,
    PCM_TRANS_EVENT_TX_PAUSE        = 0x000002UL,
    PCM_TRANS_EVENT_TX_STOP         = 0x000004UL,
    PCM_TRANS_EVENT_TX_STOP_DONE    = 0x000008UL,

} pcm_trans_event_t;

#define I2S_TX_BUFFER_SIZE          4096
#define I2S_RX_BUFFER_SIZE          1024

#define I2S_DATA_REQUEST_SIZE       (I2S_TX_BUFFER_SIZE*sizeof(uint32_t))
#define I2S_DATA_NOTIFY_SIZE        (I2S_RX_BUFFER_SIZE*sizeof(uint32_t))

static uint8_t i2s_data_request_buffer[I2S_DATA_REQUEST_SIZE];
static uint8_t i2s_data_notify_buffer[I2S_DATA_NOTIFY_SIZE];

static EventGroupHandle_t event_handle;
static uint8_t i2s_tx_channels;
static uint8_t i2s_rx_channels;
static uint32_t i2s_tx_sample_rate;

static bool pcm_trans_tx_no_data;
static pcm_trans_state_t pcm_trans_cur_state;
static p_pcm_trans_data_request_callback pcm_trans_data_request_callback;
static p_pcm_trans_data_notify_callback pcm_trans_data_notify_callback;
static void* pcm_trans_data_request_param;
static void* pcm_trans_data_notify_param;

static pthread_t       pcm_thread;

static void* pcm_trans_task(void* param);
static uint32_t pcm_trans_wait_event(uint32_t events, uint32_t timeout);
static void pcm_trans_set_event(uint32_t events);
static void pcm_trans_clear_event(uint32_t events);

int pcm_trans_init(void)
{
    pcm_trans_data_request_callback = NULL;
    pcm_trans_data_notify_callback = NULL;
    pcm_trans_data_request_param = NULL;
    pcm_trans_data_notify_param = NULL;
    
    event_handle = xEventGroupCreate();
    i2s_tx_channels = 1;
    i2s_rx_channels = 1;
    pcm_trans_cur_state = PCM_TRANS_STA_EXIT;
	
    return PCM_TRANS_SUCCESS;
}

int pcm_trans_deinit(void)
{
    vEventGroupDelete(event_handle);
    
    return PCM_TRANS_SUCCESS;
}

int pcm_trans_start_tx(uint32_t sample_rate, uint8_t channels, bool only_init)
{
    pcm_trans_cur_state = PCM_TRANS_STA_IDLE;
    i2s_tx_channels = channels;
    i2s_tx_sample_rate = sample_rate;
    pcm_trans_tx_no_data = false;

    pcm_trans_clear_event(PCM_TRANS_EVENT_ALL);
    pthread_create(&pcm_thread, NULL, pcm_trans_task, NULL);

    if(false == only_init) {
        pcm_trans_resume_tx();
    }

    LOG_I(pcm_trans, "pcm_trans_start_tx sample_rate:%d, channels:%d", sample_rate, channels);
    
    return PCM_TRANS_SUCCESS;
}

int pcm_trans_stop_tx(void)
{
    if(PCM_TRANS_STA_EXIT != pcm_trans_cur_state) {
        pcm_trans_set_event(PCM_TRANS_EVENT_TX_STOP);
        pcm_trans_wait_event(PCM_TRANS_EVENT_TX_STOP_DONE, portMAX_DELAY);
        LOG_I(pcm_trans, "pcm_trans_stop_tx");

        pthread_join(pcm_thread, NULL);
    }
    
    return PCM_TRANS_SUCCESS;
}

int pcm_trans_pause_tx(void)
{
    if(PCM_TRANS_STA_TX_RUN == pcm_trans_cur_state) {
        pcm_trans_set_event(PCM_TRANS_EVENT_TX_PAUSE);
        LOG_I(pcm_trans, "pcm_trans_pause_tx");
    }
    
    return PCM_TRANS_SUCCESS;
}

int pcm_trans_resume_tx(void)
{
    if(PCM_TRANS_STA_TX_RUN != pcm_trans_cur_state) {
        pcm_trans_set_event(PCM_TRANS_EVENT_TX_RESUME);
        //LOG_I(pcm_trans, "pcm_trans_resume_tx");
    }
    return PCM_TRANS_SUCCESS;
}

int pcm_trans_register_data_request_callback(p_pcm_trans_data_request_callback callback, void* param)
{
    pcm_trans_data_request_callback = callback;
    pcm_trans_data_request_param = param;
    return PCM_TRANS_SUCCESS;
}

int pcm_trans_register_data_notify_callback(p_pcm_trans_data_notify_callback callback, void* param)
{
    pcm_trans_data_notify_callback = callback;
    pcm_trans_data_notify_param = param;
    return PCM_TRANS_SUCCESS;
}

void pcm_trans_set_tx_no_data(void)
{
    pcm_trans_tx_no_data = true;
}

bool pcm_trans_is_tx_pause(void)
{
    return (PCM_TRANS_STA_TX_PAUSE==pcm_trans_cur_state) ?true :false;
}

bool pcm_trans_is_tx_done(void)
{
    return (PCM_TRANS_STA_TX_DONE==pcm_trans_cur_state) ?true :false;
}

static uint32_t pcm_trans_wait_event(uint32_t events, uint32_t timeout)
{
	return xEventGroupWaitBits(event_handle, events, pdTRUE, pdFALSE, timeout);
}

static void pcm_trans_set_event(uint32_t events)
{
	xEventGroupSetBits(event_handle, events);
}

static void pcm_trans_clear_event(uint32_t events)
{
	xEventGroupClearBits(event_handle, events);
}

void* pcm_trans_task(void* param)
{
    snd_pcm_t* pcm_handle;
    uint32_t events;
    int err, input_size;
    
    if((err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        printf("Playback open error: %s\n", snd_strerror(err));
    }
    
    if((err = snd_pcm_set_params(pcm_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, i2s_tx_channels, i2s_tx_sample_rate, 1, 500000)) < 0) {
        printf("Playback open error: %s\n", snd_strerror(err));
    }

    pcm_trans_cur_state = PCM_TRANS_STA_TX_PAUSE;
    
    while(1)
    {
        events = 
            PCM_TRANS_EVENT_TX_STOP |
            PCM_TRANS_EVENT_TX_PAUSE |
            PCM_TRANS_EVENT_TX_RESUME;

        if(PCM_TRANS_STA_TX_RUN != pcm_trans_cur_state)
            events = pcm_trans_wait_event(events, portMAX_DELAY);
        else
            events = pcm_trans_wait_event(events, 0);
        
        if(PCM_TRANS_EVENT_TX_STOP & events) {
            pcm_trans_cur_state = PCM_TRANS_STA_EXIT;
            pcm_trans_set_event(PCM_TRANS_EVENT_TX_STOP_DONE);
            break;
        }
        else if(PCM_TRANS_EVENT_TX_PAUSE & events) {
            pcm_trans_cur_state = PCM_TRANS_STA_TX_PAUSE;
        }
        else if(PCM_TRANS_EVENT_TX_RESUME & events) {
            pcm_trans_cur_state = PCM_TRANS_STA_TX_RUN;
        }

        if(PCM_TRANS_STA_TX_RUN != pcm_trans_cur_state)
            continue;

        if(NULL != pcm_trans_data_request_callback) {
            input_size = I2S_DATA_REQUEST_SIZE;
            input_size = pcm_trans_data_request_callback(pcm_trans_data_request_param, i2s_data_request_buffer, input_size);
        }
        else {
            input_size = 0;
        }
            
        if(input_size > 0)
        {
            int frame_size = i2s_tx_channels*2;
            snd_pcm_sframes_t frames = snd_pcm_writei(pcm_handle, i2s_data_request_buffer, input_size/frame_size);

            if (frames < 0)
                frames = snd_pcm_recover(pcm_handle, frames, 0);
            
            if (frames < 0) {
                printf("snd_pcm_writei failed: %s\n", snd_strerror(frames));
                break;
            }
        }
        else
        {
            if(true == pcm_trans_tx_no_data) {
                snd_pcm_wait(pcm_handle, 2000);
                pcm_trans_cur_state = PCM_TRANS_STA_TX_DONE;
            }
            else {
                pcm_trans_cur_state = PCM_TRANS_STA_TX_PAUSE;
            }
            
            continue;
        }
    }

    snd_pcm_close(pcm_handle);
    snd_config_update_free_global();

    return NULL;
}

