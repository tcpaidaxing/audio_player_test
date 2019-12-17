#include "audio_manager.h"
#include "audio_message_queue.h"

#ifndef DEF_LINUX_PLATFORM
#include "hal_trng.h"
#include "bsp_nau88c10.h"
#include "expression_display.h"
#include "tf_card_file_manage.h"
#include "mqtt_internal.h"
#include "pcm_trans.h"
#include "i2s_record.h"
#include "alarm_clock.h"
#include <string.h>
#include "story_common.h"
#endif

log_create_module(audio_manager, PRINT_LEVEL_INFO);

#define malloc(x)       pvPortMalloc(x)
#define free(x)         vPortFree(x)

#define AUDIO_MGR_MAX_SD_CARD_PATH  256
#define AUDIO_MGR_TASK_STACK_SIZE   (10240/sizeof(StackType_t))
#define AUDIO_MGR_QUEUE_LENGTH      5

#define AUDIO_MGR_LOCAL_VAR_UPDATE() do {\
    if(g_audio_local_max <= 0) g_audio_local_max = tf_card_audio_file_num_get();\
    if(g_audio_local_index < 0) g_audio_local_index = g_audio_local_max - 1;\
    else if(g_audio_local_index >= g_audio_local_max) g_audio_local_index = 0;\
} while(0)

typedef enum {
    AUDIO_MGR_MQTT_TYPE_NEXT = 0,
    AUDIO_MGR_MQTT_TYPE_PREV,
    AUDIO_MGR_MQTT_TYPE_PAUSE,
    AUDIO_MGR_MQTT_TYPE_RESUME,
    AUDIO_MGR_MQTT_TYPE_DONE,
    AUDIO_MGR_MQTT_TYPE_PROGRESS,
        
} audio_mgr_mqtt_type_t;

typedef enum {
    AUDIO_MGR_EVENT_NONE = 0,
    AUDIO_MGR_EVENT_PLAYER_START,
    AUDIO_MGR_EVENT_PLAYER_STOP,
    AUDIO_MGR_EVENT_PLAYER_BREAK,
    AUDIO_MGR_EVENT_PLAYER_PAUSE,
    AUDIO_MGR_EVENT_PLAYER_RESUME,
    AUDIO_MGR_EVENT_PLAYER_PROGRESS,
        
} audio_mgr_event_t;

extern int32_t mqtt_msg_send_with_timeout(char *topic, int qos, char *buf, TickType_t xTicksToWait);

static audio_msg_queue_t   g_audio_msg_queue;
static audio_player_proc_t g_prompt_play;
static audio_player_proc_t g_resource_play;
static int32_t             g_audio_local_index;
static int32_t             g_audio_local_max;
static bool                g_audio_local_pending;
static bool                g_audio_new_progress;
static bool                g_audio_auto_next;
static bool                g_auto_resume_prev;
TaskHandle_t               audio_mgr_init_task_handle;

static void audio_prompt_player_callback(void* param, audio_player_event_t event);
static void audio_resource_player_callback(void* param, audio_player_event_t event);

audio_mgr_return_t audio_mgr_init(void)
{

    g_audio_local_index   = 0;
    g_audio_local_max     = 0;
    g_audio_local_pending = false;
    g_audio_new_progress  = false;
    g_audio_auto_next     = false;
    g_auto_resume_prev    = true;

    audio_msg_queue_init(&g_audio_msg_queue, AUDIO_MGR_QUEUE_LENGTH);
    pcm_trans_init();
    
    audio_player_init(&g_prompt_play);
    audio_player_init(&g_resource_play);

    audio_player_register_callback(&g_prompt_play, audio_prompt_player_callback);
    audio_player_register_callback(&g_resource_play, audio_resource_player_callback);
    
    xTaskCreate(
        audio_mgr_task, 
        "audio_mgr_task", 
        AUDIO_MGR_TASK_STACK_SIZE, 
        NULL,
        TASK_PRIORITY_HIGH,
        &audio_mgr_init_task_handle);
    
    return AUDIO_MGR_SUCCESS;
}

#ifdef DEF_LINUX_PLATFORM
audio_mgr_return_t audio_mgr_deinit(void)
{
	audio_player_deinit(&g_prompt_play);
    audio_player_deinit(&g_resource_play);
	pcm_trans_deinit();
	audio_msg_queue_deinit(&g_audio_msg_queue);
	
    return AUDIO_MGR_SUCCESS;
}
#endif

static audio_mgr_return_t audio_mgr_new_player(audio_player_info_t** pp_player_info, char *path, audio_src_flag_t src_flag)
{
    audio_player_info_t* tmp = NULL;
    audio_mgr_return_t ret = AUDIO_MGR_SUCCESS;

    if(NULL==path || strlen(path) <= 0) {
        LOG_E(audio_manager, "path is empty!");
        ret = AUDIO_MGR_ERR_PARAM;
        goto ERROR;
    }
    
    tmp = (audio_player_info_t*)malloc(sizeof(audio_player_info_t));

    if(NULL == tmp) {
        LOG_E(audio_manager, "malloc failed!");
        ret = AUDIO_MGR_ERR_MALLOC;
        goto ERROR;
    }

    memset(tmp, 0, sizeof(audio_player_info_t));
    strncpy(tmp->path, path, AUDIO_PLAYER_MAX_PATH_SIZE);

    tmp->auto_resume_prev = g_auto_resume_prev;
    tmp->uuid             = xTaskGetTickCount();

    switch(src_flag)
    {
    case AUDIO_SRC_FLAG_LOCAL:
        tmp->source = AUDIO_PLAYER_SRC_SD_CARD;
        tmp->type = AUDIO_PLAYER_TYPE_RESOURCE;
        break;
        
	case AUDIO_SRC_FLAG_PROMPT:
        tmp->source = AUDIO_PLAYER_SRC_FLASH;
        tmp->type = AUDIO_PLAYER_TYPE_PROMPT;
        break;
        
	case AUDIO_SRC_FLAG_HTTP_URL:
        tmp->source = AUDIO_PLAYER_SRC_WEB;
        tmp->type = AUDIO_PLAYER_TYPE_RESOURCE;
        break;
        
	case AUDIO_SRC_FLAG_TTS:
        tmp->source = AUDIO_PLAYER_SRC_WEB;
        tmp->type = AUDIO_PLAYER_TYPE_PROMPT;
        break;

    default:
        LOG_E(audio_manager, "src_flag error!");
        ret = AUDIO_MGR_ERR_PARAM;
        goto ERROR;
    }

    *pp_player_info = tmp;
    return AUDIO_MGR_SUCCESS;

ERROR:
    if(NULL != tmp) {
        free(tmp);
        tmp = NULL;
    }

    return ret;
}

static audio_mgr_return_t audio_mgr_send_mqtt(audio_mgr_mqtt_type_t type, void* param)
{
    audio_mgr_return_t ret = AUDIO_MGR_SUCCESS;
    int32_t mqtt_ret = 0;
    char strMsg[128] = {0};

    switch(type)
    {
    case AUDIO_MGR_MQTT_TYPE_NEXT:
        snprintf(strMsg, sizeof(strMsg), "{\"name\":\"toy\",\"deviceid\":\"%s\",\"data\":{},\"do\":\"next\"}", DEVICE_ID);
        mqtt_ret = mqtt_msg_send_with_timeout(MQTT_TOPIC_SERVER, 0, strMsg, 1000/portTICK_RATE_MS);
        break;
        
    case AUDIO_MGR_MQTT_TYPE_PREV:
        snprintf(strMsg, sizeof(strMsg), "{\"name\":\"toy\",\"deviceid\":\"%s\",\"data\":{},\"do\":\"prev\"}", DEVICE_ID);
        mqtt_ret = mqtt_msg_send_with_timeout(MQTT_TOPIC_SERVER, 0, strMsg, 1000/portTICK_RATE_MS);
        break;
        
    case AUDIO_MGR_MQTT_TYPE_PAUSE:
        snprintf(strMsg, sizeof(strMsg), "{\"name\":\"toy_dev\",\"data\":{},\"do\":\"browser_pause\"}");
        mqtt_ret = mqtt_msg_send_with_timeout(MQTT_TOPIC_BROWSER, 0, strMsg, 1000/portTICK_RATE_MS);
        break;
        
    case AUDIO_MGR_MQTT_TYPE_RESUME:
        snprintf(strMsg, sizeof(strMsg), "{\"name\":\"toy_dev\",\"data\":{},\"do\":\"browser_resume\"}");
        mqtt_ret = mqtt_msg_send_with_timeout(MQTT_TOPIC_BROWSER, 0, strMsg, 1000/portTICK_RATE_MS);
        break;

    case AUDIO_MGR_MQTT_TYPE_DONE:
        snprintf(strMsg, sizeof(strMsg), "{\"name\":\"toy\",\"deviceid\":\"%s\",\"data\":{},\"do\":\"play_done\"}", DEVICE_ID);
        mqtt_ret = mqtt_msg_send_with_timeout(MQTT_TOPIC_SERVER, 0, strMsg, 1000/portTICK_RATE_MS);
        break;
        
    case AUDIO_MGR_MQTT_TYPE_PROGRESS:
        //snprintf(strMsg, sizeof(strMsg), "{\"name\":\"toy\",\"data\":{\"process\":%.2f},\"do\":\"browser_play_process\"}", *((double*)param));
        //mqtt_ret = mqtt_msg_send_with_timeout(MQTT_TOPIC_BROWSER, 0, strMsg, 0);
        break;

    default:
        return ret;
        
    }

    if(0 != mqtt_ret) {
        LOG_E(audio_manager, "call mqtt_msg_send failed!");
        ret = AUDIO_MGR_ERR_MQTT_FAILED;
    }

    return ret;
}

static void audio_mgr_player_prompt_wait(audio_player_info_t* player_info)
{
    while(AUDIO_PLAYER_STA_IDLE != g_prompt_play.cur_state && 
        AUDIO_PLAYER_SRC_FLASH == g_prompt_play.player_info.source &&
        AUDIO_PLAYER_TYPE_PROMPT == g_prompt_play.player_info.type &&
        AUDIO_PLAYER_SRC_FLASH == player_info->source &&
        AUDIO_PLAYER_TYPE_PROMPT == player_info->type)
    {
        vTaskDelay(500/portTICK_RATE_MS);
    }
}

audio_mgr_return_t audio_mgr_player_start(char *path, audio_src_flag_t src_flag, uint32_t wait_start_timeout)
{
    audio_mgr_return_t ret = AUDIO_MGR_SUCCESS;
    audio_player_info_t* player_info = NULL;
    audio_msg_item_t msg;

    if(true == is_rec_do()) {
        LOG_E(audio_manager, "record do!");
        return AUDIO_MGR_ERR_RECORD_DO;
    }
    
    ret = audio_mgr_new_player(&player_info, path, src_flag);
    if(AUDIO_MGR_SUCCESS != ret) {
        return ret;
    }

    if(AUDIO_PLAYER_TYPE_RESOURCE == player_info->type) {
        audio_mgr_player_set_audio_next(true);
    }

    audio_mgr_player_prompt_wait(player_info);

    msg.event = AUDIO_MGR_EVENT_PLAYER_START;
    msg.data  = player_info;

    ret = audio_msg_queue_send(&g_audio_msg_queue, &msg, 0);
    if(AUDIO_MGR_SUCCESS != ret) {
        return ret;
    }

    if(wait_start_timeout > 0)
    {
        uint32_t begTick = xTaskGetTickCount();

        while((xTaskGetTickCount() - begTick) < wait_start_timeout)
        {
            if( AUDIO_PLAYER_TYPE_RESOURCE == player_info->type && 
                AUDIO_PLAYER_STA_PLAY == g_resource_play.cur_state &&
                player_info->uuid == g_resource_play.player_info.uuid)
            {
                break;
            }

            if( AUDIO_PLAYER_TYPE_PROMPT == player_info->type && 
                AUDIO_PLAYER_STA_PLAY == g_prompt_play.cur_state &&
                player_info->uuid == g_prompt_play.player_info.uuid)
            {
                break;
            }

            vTaskDelay(100/portTICK_RATE_MS);
        }
    }

    return ret;
}

audio_mgr_return_t audio_mgr_player_start_wait_finish(char *path, audio_src_flag_t src_flag)
{
    audio_mgr_return_t ret = AUDIO_MGR_SUCCESS;
    audio_player_info_t* player_info = NULL;

    if(true == is_rec_do()) {
        LOG_E(audio_manager, "record do!");
        return AUDIO_MGR_ERR_RECORD_DO;
    }

    ret = audio_mgr_new_player(&player_info, path, src_flag);
    if(AUDIO_MGR_SUCCESS != ret) {
        return ret;
    }

    if(AUDIO_PLAYER_TYPE_PROMPT==player_info->type)
    {
        audio_mgr_player_prompt_wait(player_info);

        if(AUDIO_PLAYER_PROC_SUCCESS != audio_player_start(&g_prompt_play, player_info, true)) {
            ret = AUDIO_MGR_ERR_PLAYER_START;
        }
    }
    else if(AUDIO_PLAYER_TYPE_RESOURCE==player_info->type)
    {
        if(AUDIO_PLAYER_PROC_SUCCESS != audio_player_start(&g_resource_play, player_info, true)) {
            ret = AUDIO_MGR_ERR_PLAYER_START;
        }
    }

    free(player_info);

    return ret;
}

audio_mgr_return_t audio_mgr_player_stop(void)
{
    audio_msg_item_t msg = {AUDIO_MGR_EVENT_PLAYER_STOP, NULL};

    audio_mgr_player_set_audio_next(false);
    
    return audio_msg_queue_send(&g_audio_msg_queue, &msg, 0);
}

audio_mgr_return_t audio_mgr_player_break(void)
{
#if 0
    audio_msg_item_t msg = {AUDIO_MGR_EVENT_PLAYER_BREAK, NULL};
    return audio_msg_queue_send(&g_audio_msg_queue, &msg, 0);
#else
    audio_player_stop(&g_prompt_play);
    audio_player_break(&g_resource_play);
    return AUDIO_MGR_SUCCESS;
#endif
}

audio_mgr_return_t audio_mgr_player_pause(bool from_key)
{
    audio_msg_item_t msg = {AUDIO_MGR_EVENT_PLAYER_PAUSE, NULL};

    if(true == from_key && false == audio_mgr_player_is_local()) {
        audio_mgr_send_mqtt(AUDIO_MGR_MQTT_TYPE_PAUSE, NULL);
    }
    
    return audio_msg_queue_send(&g_audio_msg_queue, &msg, 0);
}

audio_mgr_return_t audio_mgr_player_resume(bool from_key)
{
    audio_msg_item_t msg = {AUDIO_MGR_EVENT_PLAYER_RESUME, NULL};

    if(true == from_key && false == audio_mgr_player_is_local()) {
        audio_mgr_send_mqtt(AUDIO_MGR_MQTT_TYPE_RESUME, NULL);
    }
    
    return audio_msg_queue_send(&g_audio_msg_queue, &msg, 0);
}

audio_mgr_return_t audio_mgr_player_toggle(bool from_key)
{
    if(true == audio_mgr_player_is_play()) {
        return audio_mgr_player_pause(from_key);
    }
    else if(true == audio_mgr_player_is_pause()) {
        return audio_mgr_player_resume(from_key);
    }

    return AUDIO_MGR_SUCCESS;
}

static audio_mgr_return_t audio_mgr_player_start_local_inner(void)
{
    char path[AUDIO_MGR_MAX_SD_CARD_PATH];
    memset(path, 0, AUDIO_MGR_MAX_SD_CARD_PATH);
    
    AUDIO_MGR_LOCAL_VAR_UPDATE();

    if(0 != tf_card_file_path_get(g_audio_local_index, path)) {
        LOG_E(audio_manager, "[ERR] tf_card_file_path_get(%d)\n", g_audio_local_index);
        return AUDIO_MGR_ERR_PLAYER_PATH;
    }

    return audio_mgr_player_start(path, AUDIO_SRC_FLAG_LOCAL, 0);
}

audio_mgr_return_t audio_mgr_player_start_local(void)
{
    audio_mgr_player_set_audio_next(true);

    if(audio_mgr_player_is_web()) {
        audio_mgr_send_mqtt(AUDIO_MGR_MQTT_TYPE_PAUSE, NULL);
    }
    
    g_audio_local_max = tf_card_audio_file_num_get();
    return audio_mgr_player_start_local_inner();
}

audio_mgr_return_t audio_mgr_player_start_next(bool from_key)
{
    bool play_local = (false==audio_mgr_player_is_local() && WIFI_CONNECTED==g_wifi_connected_status) ?false :true;

    if(false == play_local)
    {
        if(AUDIO_MGR_SUCCESS != audio_mgr_send_mqtt((from_key==true) ?AUDIO_MGR_MQTT_TYPE_NEXT :AUDIO_MGR_MQTT_TYPE_DONE, NULL)) {
            play_local = true;
        }
    }
    
    if(true == play_local)
    {
        g_audio_local_index++;
        return audio_mgr_player_start_local_inner();
    }

    return AUDIO_MGR_SUCCESS;
}

audio_mgr_return_t audio_mgr_player_start_prev(void)
{
    bool play_local = (false==audio_mgr_player_is_local() && WIFI_CONNECTED==g_wifi_connected_status) ?false :true;

    if(false == play_local)
    {
        if(AUDIO_MGR_SUCCESS != audio_mgr_send_mqtt(AUDIO_MGR_MQTT_TYPE_PREV, NULL)) {
            play_local = true;
        }
    }
    
    if(true == play_local)
    {
        g_audio_local_index--;
        return audio_mgr_player_start_local_inner();
    }

    return AUDIO_MGR_SUCCESS;
}

#ifndef DEF_LINUX_PLATFORM
int32_t get_random_number(uint32_t *p_random_num, uint32_t base_number)
{
	hal_trng_status_t ret = HAL_TRNG_STATUS_OK;
	uint32_t trng_random_number = 0;

	if ((NULL == p_random_num) || (0 == base_number))
	{
		return -1;
	}

	/* Initializes the TRNG source clock. */
	hal_trng_init();

	/* Gets the random number. */
	ret = hal_trng_get_generated_random_number(&trng_random_number);
	if(HAL_TRNG_STATUS_OK != ret)
	{
		LOG_E(audio_player, "Call hal_trng_get_generated_random_number failed! ret = %d.\n", ret);

		return -1;
	}

	/* Deinitializes the TRNG. */
	hal_trng_deinit();

	*p_random_num = trng_random_number % base_number;

	LOG_I(audio_player, "trng_random_number = %u, random_num = %u.\n", trng_random_number, *p_random_num);

	return 0;
}
#endif

bool audio_mgr_player_is_pause(void)
{
    return (AUDIO_PLAYER_STA_PAUSE == g_resource_play.cur_state) ?true :false;
}

bool audio_mgr_player_is_play(void)
{
    return (AUDIO_PLAYER_STA_PLAY == g_resource_play.cur_state) ?true :false;
}

bool audio_mgr_player_is_stop(void)
{
    return (AUDIO_PLAYER_STA_IDLE == g_resource_play.cur_state) ?true :false;
}

bool audio_mgr_player_is_local(void)
{
    return (AUDIO_PLAYER_SRC_SD_CARD == g_resource_play.player_info.source) ?true :false;
}

bool audio_mgr_player_is_web(void)
{
    return (AUDIO_PLAYER_SRC_WEB == g_resource_play.player_info.source) ?true :false;
}

bool audio_mgr_player_all_stop(void)
{
    if( AUDIO_PLAYER_STA_IDLE == g_resource_play.cur_state &&
        AUDIO_PLAYER_STA_IDLE == g_prompt_play.cur_state )
    {
        return true;
    }
    
    return false;
}

bool audio_mgr_player_any_play(void)
{
    if(AUDIO_PLAYER_STA_PLAY == g_resource_play.cur_state) {
        return true;
    }

    if(AUDIO_PLAYER_STA_PLAY == g_prompt_play.cur_state)
    {
        int i;
        const char* work_prompts[] = {
            ALARM_REMIND_AUDIO,
        };

        if(AUDIO_PLAYER_SRC_WEB == g_prompt_play.player_info.source)
        {
            return true;
        }
        else
        {
            for(i = 0; i < sizeof(work_prompts)/sizeof(char*); i++) {
                if(0 == strncmp(g_prompt_play.player_info.path, work_prompts[i], strlen(work_prompts[i]))) {
                    return true;
                }
            }
        }
    }
    
    return false;
}


void audio_mgr_player_stop_prompt(void)
{
    audio_player_stop(&g_prompt_play);
}

void audio_mgr_set_local_pending(bool pending)
{
    g_audio_local_pending = pending;
}

bool audio_mgr_get_local_pending(void)
{
    return g_audio_local_pending;
}

void audio_mgr_player_set_audio_next(bool auto_next)
{
    g_audio_auto_next = auto_next;
}

void audio_mgr_set_auto_resume_prev(bool auto_resume_prev)
{
    g_auto_resume_prev = auto_resume_prev;
}

static audio_mgr_return_t audio_mgr_player_error_handler(audio_player_return_t error)
{
    switch(error)
    {
    case AUDIO_PLAYER_PROC_SUCCESS:
    case AUDIO_PLAYER_PROC_ALL_END:
        break;

    case AUDIO_PLAYER_PROC_ERR_WEB_TIMEOUT:
        audio_mgr_player_start("NwBad.mp3", AUDIO_SRC_FLAG_PROMPT, 0);
        break;

    default:
        audio_mgr_player_start("StyNoSpt.mp3", AUDIO_SRC_FLAG_PROMPT, 0);
        break;
    }
    
    return AUDIO_MGR_SUCCESS;
}

static void audio_prompt_player_callback(void* param, audio_player_event_t event)
{
    audio_player_proc_t* audio_player = (audio_player_proc_t*)param;
        
    switch(event)
    {
    case AUDIO_PLAYER_EVENT_STOP:
        if( AUDIO_PLAYER_PROC_ALL_END != audio_player->last_error && 
            AUDIO_PLAYER_SRC_FLASH != audio_player->player_info.source )
        {
            audio_mgr_player_error_handler(audio_player->last_error);
        }
        break;
    }
}

static void audio_resource_player_callback(void* param, audio_player_event_t event)
{
    audio_player_proc_t* audio_player = (audio_player_proc_t*)param;
    //audio_msg_item_t msg;
        
    switch(event)
    {
    case AUDIO_PLAYER_EVENT_STOP:
        if(AUDIO_PLAYER_PROC_ALL_END == audio_player->last_error)
        {
            if(true == g_audio_auto_next) {
                audio_mgr_player_start_next(false);
            }
        }
        else
        {
            audio_mgr_player_error_handler(audio_player->last_error);
        }
        break;

    case AUDIO_PLAYER_EVENT_PROGRESS:
        /*if( false == g_audio_new_progress && 
            AUDIO_PLAYER_SRC_WEB == audio_player->player_info.source)
        {
            g_audio_new_progress = true;
            
            msg.event = AUDIO_MGR_EVENT_PLAYER_PROGRESS;
            msg.data  = (double*)malloc(sizeof(double));

            if(NULL != msg.data) {
                *((double*)msg.data) = audio_player->player_progress;
                audio_msg_queue_send(&g_audio_msg_queue, &msg, 0);
            }
        }*/
        break;
    }
}

void audio_mgr_task(void* param)
{
    audio_player_info_t* player_info = NULL;
    audio_msg_item_t msg;
    
    while(1)
    {
        if(0 != audio_msg_queue_recv(&g_audio_msg_queue, &msg, portMAX_DELAY))
            continue;

        LOG_I(audio_manager, "event:%d, data:%p", msg.event, msg.data);

        switch(msg.event)
        {
        case AUDIO_MGR_EVENT_PLAYER_START:
            player_info = (audio_player_info_t*)msg.data;
            
            if(AUDIO_PLAYER_TYPE_PROMPT==player_info->type) {
                audio_player_start(&g_prompt_play, player_info, false);
            }
            else if(AUDIO_PLAYER_TYPE_RESOURCE==player_info->type) {
                audio_player_start(&g_resource_play, player_info, false);
                change_display_expression(EYE_DISPLAY_BLINK);
            }
            break;
            
        case AUDIO_MGR_EVENT_PLAYER_STOP:
            audio_player_stop(&g_prompt_play);
            audio_player_stop(&g_resource_play);
            break;
            
        case AUDIO_MGR_EVENT_PLAYER_BREAK:
            audio_player_stop(&g_prompt_play);
            audio_player_break(&g_resource_play);
            break;
        
        case AUDIO_MGR_EVENT_PLAYER_PAUSE:
            audio_player_stop(&g_prompt_play);
            
            if(AUDIO_PLAYER_PROC_SUCCESS == audio_player_pause(&g_resource_play)) {
                change_display_expression(EYE_DISPLAY_STATIC);
            }
            break;
            
        case AUDIO_MGR_EVENT_PLAYER_RESUME:
            audio_player_stop(&g_prompt_play);
            
            if(AUDIO_PLAYER_PROC_SUCCESS == audio_player_resume(&g_resource_play))
            {
                if(WIFI_UNCONNECTED == g_wifi_connected_status) {
			        change_display_expression(EYE_DISPLAY_CLOSE);
                }
                else {
                    change_display_expression(EYE_DISPLAY_BLINK);
                }
            }
            break;

        case AUDIO_MGR_EVENT_PLAYER_PROGRESS:
            if(NULL != msg.data) {
                audio_mgr_send_mqtt(AUDIO_MGR_MQTT_TYPE_PROGRESS, msg.data);
                g_audio_new_progress = false;
            }
            break;
        }

        if(NULL != msg.data) {
            free(msg.data);
            msg.data = NULL;
        }
    }
}

