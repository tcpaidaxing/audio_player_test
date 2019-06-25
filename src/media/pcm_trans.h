#ifndef __PCM_TRANS_H
#define __PCM_TRANS_H

#include "typedefs.h"
#include "common_event.h"

typedef enum {
    PCM_TRANS_STA_IDLE = 0,
    PCM_TRANS_STA_RX_RUN,
    PCM_TRANS_STA_TX_RUN,
    PCM_TRANS_STA_RX_PAUSE,
    PCM_TRANS_STA_TX_PAUSE,
    PCM_TRANS_STA_TX_DONE,
	PCM_TRANS_STA_EXIT,

} pcm_trans_state_t;

typedef enum {
	PCM_TRANS_SUCCESS = 0,
	PCM_TRANS_ERR_SAMPLE_RATE,
	PCM_TRANS_ERR_CHANNEL,
	PCM_TRANS_ERR_HAL_I2S,

} pcm_trans_err_t;

typedef int(*p_pcm_trans_data_request_callback)(void* param, uint8_t* buf, int size);
typedef int(*p_pcm_trans_data_notify_callback)(void* param, uint8_t* buf, int size);

int pcm_trans_init(void);
int pcm_trans_start_tx(uint32_t sample_rate, uint8_t channels, bool only_init);
int pcm_trans_start_rx(uint32_t sample_rate, uint8_t channels, bool only_init);
int pcm_trans_stop_tx(void);
int pcm_trans_pause_tx(void);
int pcm_trans_resume_tx(void);
int pcm_trans_stop_rx(void);
int pcm_trans_register_data_request_callback(p_pcm_trans_data_request_callback callback, void* param);
int pcm_trans_register_data_notify_callback(p_pcm_trans_data_notify_callback callback, void* param);
void pcm_trans_set_tx_no_data(void);
int pcm_trans_wait_tx_done(void);
bool pcm_trans_is_tx_done(void);
bool pcm_trans_is_tx_pause(void);
int pcm_trans_get_cur_state(void);
int pcm_trans_config_nau8810(uint32_t sample_rate, uint8_t channels);
int pcm_trans_config_i2s(uint32_t sample_rate, uint8_t channels);

#endif
