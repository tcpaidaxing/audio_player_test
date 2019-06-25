#ifndef __AUDIO_MANAGER_H
#define __AUDIO_MANAGER_H

#include "audio_player_process.h"

typedef enum {
	AUDIO_SRC_FLAG_INVALID = 0,
	AUDIO_SRC_FLAG_LOCAL,
	AUDIO_SRC_FLAG_PROMPT,
	AUDIO_SRC_FLAG_HTTP_URL,
	AUDIO_SRC_FLAG_TTS,
	AUDIO_SRC_FLAG_TOTAL
	
} audio_src_flag_t;

typedef enum {
    AUDIO_MGR_SUCCESS = 0,
    AUDIO_MGR_ERR_MALLOC,
    AUDIO_MGR_ERR_PARAM,
    AUDIO_MGR_ERR_MQTT_FAILED,
    AUDIO_MGR_ERR_SEND_FAILED,
    AUDIO_MGR_ERR_RECV_NULL,
    AUDIO_MGR_ERR_PLAYER_START,
    AUDIO_MGR_ERR_PLAYER_PATH,
    AUDIO_MGR_ERR_RECORD_DO,

} audio_mgr_return_t;

#define player_play_start(path, src_flag, play_handle)  audio_mgr_player_start(path, src_flag, 0)
#define player_play_start_wait_finish(path, src_flag)   audio_mgr_player_start_wait_finish(path, src_flag)
#define player_play_stop()                              audio_mgr_player_stop()
#define current_player_is_pause()                       audio_mgr_player_is_pause()
#define current_player_is_play()                        audio_mgr_player_is_play()
#define current_player_is_stop()                        audio_mgr_player_is_stop()
#define current_player_is_local()                       audio_mgr_player_is_local()
#define current_player_is_web()                         audio_mgr_player_is_web()

audio_mgr_return_t audio_mgr_init(void);
audio_mgr_return_t audio_mgr_player_start(char *path, audio_src_flag_t src_flag, uint32_t wait_start_timeout);
audio_mgr_return_t audio_mgr_player_start_wait_finish(char *path, audio_src_flag_t src_flag);
audio_mgr_return_t audio_mgr_player_stop(void);
audio_mgr_return_t audio_mgr_player_break(void);
audio_mgr_return_t audio_mgr_player_pause(bool from_key);
audio_mgr_return_t audio_mgr_player_resume(bool from_key);
audio_mgr_return_t audio_mgr_player_toggle(bool from_key);
audio_mgr_return_t audio_mgr_player_start_local(void);
audio_mgr_return_t audio_mgr_player_start_next(bool from_key);
audio_mgr_return_t audio_mgr_player_start_prev(void);
int32_t get_random_number(uint32_t *p_random_num, uint32_t base_number);
bool audio_mgr_player_is_pause(void);
bool audio_mgr_player_is_play(void);
bool audio_mgr_player_is_stop(void);
bool audio_mgr_player_is_local(void);
bool audio_mgr_player_is_web(void);
bool audio_mgr_player_all_stop(void);
bool audio_mgr_player_any_play(void);
void audio_mgr_player_stop_prompt(void);
void audio_mgr_set_local_pending(bool pending);
bool audio_mgr_get_local_pending(void);
void audio_mgr_player_set_audio_next(bool auto_next);
void audio_mgr_set_auto_resume_prev(bool auto_resume_prev);
void audio_mgr_task(void* param);

#endif
