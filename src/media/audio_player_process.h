#ifndef __AUDIO_PLAYER_PROCESS_H
#define __AUDIO_PLAYER_PROCESS_H

#include "typedefs.h"
#include "common_player.h"
#include "http_download_process.h"

#define AUDIO_PLAYER_MAX_PATH_SIZE  2048

typedef enum {
    AUDIO_PLAYER_SRC_WEB = 0,
    AUDIO_PLAYER_SRC_FLASH,
    AUDIO_PLAYER_SRC_SD_CARD,

} audio_player_source_t;

typedef enum {
    AUDIO_PLAYER_TYPE_RESOURCE = 0,
    AUDIO_PLAYER_TYPE_PROMPT,

} audio_player_type_t;

typedef enum {
    AUDIO_PLAYER_STA_IDLE = 0,
    AUDIO_PLAYER_STA_START,
    AUDIO_PLAYER_STA_PLAY,
    AUDIO_PLAYER_STA_BREAK,
    AUDIO_PLAYER_STA_PAUSE,
    AUDIO_PLAYER_STA_STOP,

} audio_player_state_t;

typedef enum {
    AUDIO_PLAYER_EVENT_NONE          = 0x000000UL,
    AUDIO_PLAYER_EVENT_ALL           = 0xFFFFFFUL,
    AUDIO_PLAYER_EVENT_START         = 0x000001UL,
    AUDIO_PLAYER_EVENT_STOP          = 0x000002UL,
    AUDIO_PLAYER_EVENT_PAUSE         = 0x000004UL,
    AUDIO_PLAYER_EVENT_RESUME        = 0x000008UL,
    AUDIO_PLAYER_EVENT_BREAK         = 0x000010UL,
    AUDIO_PLAYER_EVENT_EXIT          = 0x000020UL,
    AUDIO_PLAYER_EVENT_EXIT_DONE     = 0x000040UL,
    AUDIO_PLAYER_EVENT_STOP_DONE     = 0x000080UL,
    AUDIO_PLAYER_EVENT_START_DONE    = 0x000100UL,
    AUDIO_PLAYER_EVENT_PAUSE_DONE    = 0x000200UL,
    AUDIO_PLAYER_EVENT_BREAK_DONE    = 0x000400UL,
    AUDIO_PLAYER_EVENT_PROGRESS      = 0x000800UL,
    
} audio_player_event_t;

typedef enum {
    AUDIO_PLAYER_PROC_SUCCESS = 0,
    AUDIO_PLAYER_PROC_ALL_END,
    AUDIO_PLAYER_PROC_ERR_PARAM,
    AUDIO_PLAYER_PROC_ERR_OVERFLOW,
    AUDIO_PLAYER_PROC_ERR_PLAYER_START,
    AUDIO_PLAYER_PROC_ERR_OPEN_FILE,
    AUDIO_PLAYER_PROC_ERR_CLOSE_FILE,
    AUDIO_PLAYER_PROC_ERR_EMPTY_FILE,
    AUDIO_PLAYER_PROC_ERR_READ_FILE,
    AUDIO_PLAYER_PROC_ERR_AUDIO_TYPE,
    AUDIO_PLAYER_PROC_ERR_WEB_TIMEOUT,
    AUDIO_PLAYER_PROC_ERR_NO_PAUSE,
    AUDIO_PLAYER_PROC_ERR_NO_PLAY,
    AUDIO_PLAYER_PROC_ERR_BREAK,
    
} audio_player_return_t;

typedef uint32_t audio_player_handle_t;
typedef void (*p_audio_player_callback)(void* param, audio_player_event_t event);

typedef struct {
    char                       path[AUDIO_PLAYER_MAX_PATH_SIZE];
    audio_player_source_t      source;
    audio_player_type_t        type;
    uint32_t                   uuid;
    bool                       auto_resume_prev;

} audio_player_info_t;

typedef struct audio_player_proc_s {
    audio_player_state_t            cur_state;
    TaskHandle_t                    task_handle;
    EventGroupHandle_t              event_handle;
    SemaphoreHandle_t               mutex_handle;
    audio_player_handle_t           player_handle;
    audio_player_info_t             player_info;
    com_player_t                    com_player;
    audio_player_return_t           last_error;
    FIL                             file_handle;
    uint32_t                        total_length;
    uint32_t                        prompt_offset;
    uint32_t                        read_pos;
    bool                            file_open_flag;
    uint32_t                        progress_monitor_tick;
    common_buffer_t                 http_buffer;
    http_download_proc_t            http_proc;
    p_audio_player_callback         audio_player_callback;
    
} audio_player_proc_t;

audio_player_return_t audio_player_init(audio_player_proc_t* audio_player);
audio_player_return_t audio_player_deinit(audio_player_proc_t* audio_player);
audio_player_return_t audio_player_start(audio_player_proc_t* audio_player, audio_player_info_t* player_info, bool wait_finish);
audio_player_return_t audio_player_stop(audio_player_proc_t* audio_player);
audio_player_return_t audio_player_pause(audio_player_proc_t* audio_player);
audio_player_return_t audio_player_resume(audio_player_proc_t* audio_player);
audio_player_return_t audio_player_break(audio_player_proc_t* audio_player);
audio_player_return_t audio_player_register_callback(audio_player_proc_t* audio_player, p_audio_player_callback callback);

#endif

