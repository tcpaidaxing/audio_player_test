#include "audio_player_process.h"
#include "typedefs.h"
#include <string.h>

#define malloc(x)   pvPortMalloc(x)
#define free(x)     vPortFree(x)

log_create_module(audio_player_proc, PRINT_LEVEL_INFO);

#define AUDIO_PLAYER_TASK_STACK_SIZE            (10240/sizeof(StackType_t))
#define AUDIO_PLAYER_MAX_WAIT_TIME              (30000/portTICK_RATE_MS)
#define AUDIO_PLAYER_MONITOR_INTERVAL           (300/portTICK_RATE_MS)
#define AUDIO_PLAYER_PROGRESS_INTERVAL          (1000/portTICK_RATE_MS)
#define AUDIO_PLAYER_WAIT_10K_TIMEOUT           (5000/portTICK_RATE_MS)
#define AUDIO_PLAYER_WAIT_HTTP_TIMEOUT          (5000/portTICK_RATE_MS)
#define AUDIO_PLAYER_WAIT_PROMPT_IDV3_SIZE      (4*1024)
#define AUDIO_PLAYER_WAIT_RESOURCE_IDV3_SIZE    (25*1024)
#define AUDIO_PLAYER_WAIT_HTTP_SIZE             (20*1024)
#define AUDIO_PLAYER_MAX_REGISTER_SIZE          (10)

static int __player_input_callback(void* param, uint8_t* buf, int size);
static int __player_seek_callback(void* param, int position);
static int __player_error_callback(void* param, int error);
static void audio_player_task(void *param);

static audio_player_proc_t*   g_registers[AUDIO_PLAYER_MAX_REGISTER_SIZE];
static bool                   g_register_initialized = false;
static audio_player_handle_t  g_last_alloc_handle = 0;
static SemaphoreHandle_t      g_common_mutex;

static void audio_player_lock(audio_player_proc_t* audio_player);
static void audio_player_unlock(audio_player_proc_t* audio_player);
static void audio_player_set_event(audio_player_proc_t* audio_player, uint32_t events);
static uint32_t audio_player_wait_event(audio_player_proc_t* audio_player, uint32_t events, uint32_t timeout);
static void audio_player_clear_event(audio_player_proc_t* audio_player, uint32_t events);

#define audio_player_common_lock()      do { xSemaphoreTake(g_common_mutex, portMAX_DELAY); } while(0)
#define audio_player_common_unlock()    do { xSemaphoreGive(g_common_mutex); } while(0)

audio_player_return_t audio_player_init(audio_player_proc_t* audio_player)
{
    taskENTER_CRITICAL();
    if(false == g_register_initialized) {
        memset(g_registers, 0, sizeof(g_registers));
        g_register_initialized = true;
        g_common_mutex = xSemaphoreCreateMutex();
    }
    taskEXIT_CRITICAL();

    memset(audio_player, 0, sizeof(audio_player_proc_t));

    audio_player->cur_state             = AUDIO_PLAYER_STA_IDLE;
    audio_player->event_handle          = xEventGroupCreate();
    audio_player->mutex_handle          = xSemaphoreCreateMutex();
    audio_player->audio_player_callback = NULL;

    common_buffer_init(&audio_player->http_buffer, COMMON_BUF_HTTP_NODE_SIZE, COMMON_BUF_HTTP_MAX_SIZE);
    http_download_init(&audio_player->http_proc);
    com_player_init(&audio_player->com_player);

    xTaskCreate(
        audio_player_task, 
        "audio_player_task", 
        AUDIO_PLAYER_TASK_STACK_SIZE, 
        audio_player,
        TASK_PRIORITY_HIGH,
        &audio_player->task_handle);
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

audio_player_return_t audio_player_deinit(audio_player_proc_t* audio_player)
{
    audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_EXIT);
    
    if(AUDIO_PLAYER_EVENT_NONE==audio_player_wait_event(audio_player, 
        AUDIO_PLAYER_EVENT_EXIT_DONE, AUDIO_PLAYER_MAX_WAIT_TIME))
    {
        LOG_E(audio_player_proc, "audio_player_wait_event timeout!");
    }
    
    if(NULL != audio_player->event_handle) {
        vEventGroupDelete(audio_player->event_handle);
        audio_player->event_handle = NULL;
    }

    if(NULL != audio_player->mutex_handle) {
        vSemaphoreDelete(audio_player->mutex_handle);
        audio_player->mutex_handle = NULL;
    }
	
#ifdef DEF_LINUX_PLATFORM
    pthread_join(audio_player->task_handle, NULL);
#endif

    com_player_deinit(&audio_player->com_player);
    http_download_stop(&audio_player->http_proc);
    http_download_deinit(&audio_player->http_proc);
    common_buffer_deinit(&audio_player->http_buffer);
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

audio_player_return_t audio_player_start(audio_player_proc_t* audio_player, audio_player_info_t* player_info, bool wait_finish)
{
    audio_player_handle_t player_handle;
    uint32_t begTick = xTaskGetTickCount();
    
    if(NULL==audio_player || strlen(player_info->path) <= 0) {
        return AUDIO_PLAYER_PROC_ERR_PARAM;
    }

    audio_player_common_lock();

    audio_player_stop(audio_player);
    audio_player_lock(audio_player);

    memcpy(&audio_player->player_info, player_info, sizeof(audio_player_info_t));

    player_handle = ++g_last_alloc_handle;
    
    audio_player->last_error    = AUDIO_PLAYER_PROC_SUCCESS;
    audio_player->player_handle = player_handle;

    audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_START);

    if(AUDIO_PLAYER_EVENT_NONE==audio_player_wait_event(audio_player, 
        AUDIO_PLAYER_EVENT_START_DONE, AUDIO_PLAYER_MAX_WAIT_TIME))
    {
        LOG_E(audio_player_proc, "audio_player_wait_event timeout!");
    }

    audio_player_unlock(audio_player);

    audio_player_common_unlock();

    LOG_I(audio_player_proc, "audio_player_start time: %d ms", (xTaskGetTickCount()-begTick));

    while(true == wait_finish)
    {
        if(player_handle != audio_player->player_handle) {
            return AUDIO_PLAYER_PROC_ERR_BREAK;
        }
        else if(AUDIO_PLAYER_STA_IDLE == audio_player->cur_state) {
            break;
        }

        vTaskDelay(300/portTICK_RATE_MS);
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

audio_player_return_t audio_player_stop(audio_player_proc_t* audio_player)
{
    if(NULL==audio_player) {
        return AUDIO_PLAYER_PROC_ERR_PARAM;
    }
    
    audio_player_lock(audio_player);

    audio_player->player_info.auto_resume_prev = false;
    
    if(AUDIO_PLAYER_STA_IDLE != audio_player->cur_state) {
        audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_STOP);

        if(AUDIO_PLAYER_EVENT_NONE==audio_player_wait_event(audio_player, 
            AUDIO_PLAYER_EVENT_STOP_DONE, AUDIO_PLAYER_MAX_WAIT_TIME))
        {
            LOG_E(audio_player_proc, "audio_player_wait_event timeout!");
        }
    }

    audio_player_clear_event(audio_player, AUDIO_PLAYER_EVENT_ALL);

    audio_player_unlock(audio_player);
    return AUDIO_PLAYER_PROC_SUCCESS;
}

audio_player_return_t audio_player_pause(audio_player_proc_t* audio_player)
{
    audio_player_return_t ret = AUDIO_PLAYER_PROC_SUCCESS;
    
    if(NULL==audio_player) {
        return AUDIO_PLAYER_PROC_ERR_PARAM;
    }
    
    audio_player_lock(audio_player);
    
    if(AUDIO_PLAYER_STA_PLAY == audio_player->cur_state)
    {
        audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_PAUSE);

        if(AUDIO_PLAYER_EVENT_NONE==audio_player_wait_event(audio_player, 
            AUDIO_PLAYER_EVENT_PAUSE_DONE |AUDIO_PLAYER_EVENT_STOP_DONE, AUDIO_PLAYER_MAX_WAIT_TIME))
        {
            LOG_E(audio_player_proc, "audio_player_wait_event timeout!");
        }
    }
    else
    {
        ret = AUDIO_PLAYER_PROC_ERR_NO_PLAY;
    }

    audio_player_unlock(audio_player);
    return ret;
}

audio_player_return_t audio_player_resume(audio_player_proc_t* audio_player)
{
    audio_player_return_t ret = AUDIO_PLAYER_PROC_SUCCESS;
    
    if(NULL==audio_player) {
        return AUDIO_PLAYER_PROC_ERR_PARAM;
    }
    
    audio_player_lock(audio_player);
    
    if( AUDIO_PLAYER_STA_PAUSE == audio_player->cur_state ||
        AUDIO_PLAYER_STA_BREAK == audio_player->cur_state )
    {
        audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_RESUME);
    }
    else
    {
        ret = AUDIO_PLAYER_PROC_ERR_NO_PAUSE;
    }

    audio_player_unlock(audio_player);
    return ret;
}

audio_player_return_t audio_player_break(audio_player_proc_t* audio_player)
{
    audio_player_return_t ret = AUDIO_PLAYER_PROC_SUCCESS;
    
    if(NULL==audio_player) {
        return AUDIO_PLAYER_PROC_ERR_PARAM;
    }
    
    audio_player_lock(audio_player);
    
    if(AUDIO_PLAYER_STA_PLAY == audio_player->cur_state)
    {
        audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_BREAK);

        if(AUDIO_PLAYER_EVENT_NONE==audio_player_wait_event(audio_player, 
            AUDIO_PLAYER_EVENT_BREAK_DONE |AUDIO_PLAYER_EVENT_STOP_DONE, AUDIO_PLAYER_MAX_WAIT_TIME))
        {
            LOG_E(audio_player_proc, "audio_player_wait_event timeout!");
        }
    }
    else
    {
        ret = AUDIO_PLAYER_PROC_ERR_NO_PLAY;
    }

    audio_player_unlock(audio_player);
    return ret;
}

audio_player_return_t audio_player_register_callback(audio_player_proc_t* audio_player, p_audio_player_callback callback)
{
    audio_player->audio_player_callback = callback;
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static void audio_player_lock(audio_player_proc_t* audio_player)
{
    if(xQueueGetMutexHolder(audio_player->mutex_handle)==xTaskGetCurrentTaskHandle())
        LOG_E(audio_player_proc, "============== audio_player->mutex_handle: deadlock ===============\n");

    xSemaphoreTake(audio_player->mutex_handle, portMAX_DELAY);
}

static void audio_player_unlock(audio_player_proc_t* audio_player)
{
    xSemaphoreGive(audio_player->mutex_handle);
}

static void audio_player_set_event(audio_player_proc_t* audio_player, uint32_t events)
{
    if(NULL==audio_player->event_handle)
        return;
    
    xEventGroupSetBits(audio_player->event_handle, events);
}

static uint32_t audio_player_wait_event(audio_player_proc_t* audio_player, uint32_t events, uint32_t timeout)
{
    if(NULL==audio_player->event_handle)
        return 0;
    
    return xEventGroupWaitBits(audio_player->event_handle, events, pdTRUE, pdFALSE, timeout);
}

static void audio_player_clear_event(audio_player_proc_t* audio_player, uint32_t events)
{
    if(NULL==audio_player->event_handle)
        return;
    
    xEventGroupClearBits(audio_player->event_handle, events);
}

static audio_player_return_t __audio_player_register(audio_player_proc_t* curr_player)
{
    int i;
    taskENTER_CRITICAL();
    
    for(i = 0; i < AUDIO_PLAYER_MAX_REGISTER_SIZE; i++) {
        if(NULL == g_registers[i] || curr_player == g_registers[i]) {
            g_registers[i] = curr_player;
            break;
        }
    }
    
    taskEXIT_CRITICAL();
    if(i >= AUDIO_PLAYER_MAX_REGISTER_SIZE)
        return AUDIO_PLAYER_PROC_ERR_OVERFLOW;
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static audio_player_return_t __audio_player_unregister(audio_player_proc_t* curr_player)
{
    int i;
    taskENTER_CRITICAL();
    
    for(i = 0; i < AUDIO_PLAYER_MAX_REGISTER_SIZE; i++) {
        if(curr_player == g_registers[i]) {
            g_registers[i] = NULL;
            break;
        }
    }
    
    taskEXIT_CRITICAL();
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static audio_player_return_t __audio_player_before_start(audio_player_proc_t* curr_player)
{
    int i;
    
    for(i = 0; i < AUDIO_PLAYER_MAX_REGISTER_SIZE; i++)
    {
        if(NULL == g_registers[i] || curr_player == g_registers[i])
            continue;

        if(AUDIO_PLAYER_STA_PLAY == g_registers[i]->cur_state)
        {
            audio_player_break(g_registers[i]);
        }
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static audio_player_return_t __audio_player_after_stop(audio_player_proc_t* curr_player)
{
    int i;
    
    for(i = 0; i < AUDIO_PLAYER_MAX_REGISTER_SIZE; i++)
    {
        if(NULL == g_registers[i] || curr_player == g_registers[i])
            continue;

        if( true == curr_player->player_info.auto_resume_prev && 
            AUDIO_PLAYER_STA_BREAK == g_registers[i]->cur_state )
        {
            audio_player_resume(g_registers[i]);
            break;
        }
    }

    for(i = 0; i < AUDIO_PLAYER_MAX_REGISTER_SIZE; i++) {
        if(NULL != g_registers[i])
            break;
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static int __player_input_callback(void* param, uint8_t* buf, int size)
{
    audio_player_proc_t* audio_player = (audio_player_proc_t*)param;

    uint32_t read_len = 0;
    bool check_read_pos = false;
    bool input_done = false;

    if(AUDIO_PLAYER_SRC_WEB == audio_player->player_info.source)
    {
        if(true==http_download_is_finish(&audio_player->http_proc))
        {
            if(common_buffer_get_count(&audio_player->http_buffer) <= 0)
                input_done = true;
        }
        else if(audio_player->total_length <= 0 || common_buffer_get_count(&audio_player->http_buffer) < size)
        {
            http_download_proc_return_t http_ret;
            uint32_t wait_size, wait_timeout;

            if(audio_player->total_length <= 0) {
                wait_size    = (AUDIO_PLAYER_TYPE_PROMPT==audio_player->player_info.type) ?AUDIO_PLAYER_WAIT_PROMPT_IDV3_SIZE :AUDIO_PLAYER_WAIT_RESOURCE_IDV3_SIZE;
                wait_timeout = wait_size *AUDIO_PLAYER_WAIT_10K_TIMEOUT /10240 + AUDIO_PLAYER_WAIT_HTTP_TIMEOUT;
            }
            else {
                wait_size    = AUDIO_PLAYER_WAIT_HTTP_SIZE;
                wait_timeout = wait_size *AUDIO_PLAYER_WAIT_10K_TIMEOUT /10240;
            }
            
            LOG_I(audio_player_proc, "[%d] wait http download, read_pos:%d, size:%d, wait_size:%d", audio_player->player_handle, audio_player->read_pos, size, wait_size);
            
            http_ret = http_download_wait_buffer(&audio_player->http_proc, wait_size, wait_timeout);

            if(HTTP_DOWNLOAD_PROC_ERR_DOWNLOAD_PAUSE == http_ret) {
                audio_player->last_error = AUDIO_PLAYER_PROC_SUCCESS;
                goto END;
            }
            else if(HTTP_DOWNLOAD_PROC_SUCCESS != http_ret) {
                audio_player->last_error = AUDIO_PLAYER_PROC_ERR_WEB_TIMEOUT;
                goto END;
            }
        }

        read_len = size;
        common_buffer_pop(&audio_player->http_buffer, buf, &read_len);

        if(audio_player->total_length < audio_player->http_proc.total_length)
            audio_player->total_length = audio_player->http_proc.total_length;
    }
#if 0
    else if(AUDIO_PLAYER_SRC_FLASH == audio_player->player_info.source)
    {
        uint8_t* buffer = NULL;

        if(size + audio_player->read_pos > audio_player->total_length) {
            read_len = audio_player->total_length - audio_player->read_pos;
        }
        else {
            read_len = size;
        }
        
        if(0 != prompt_audio_data_get_by_offset(&buffer, audio_player->prompt_offset+audio_player->read_pos, read_len)) {
            LOG_E(audio_player_proc, "[%d] fail to read %s!", audio_player->player_handle, audio_player->player_info.path);

            if(NULL != buffer)
                vPortFree(buffer);

            audio_player->last_error = AUDIO_PLAYER_PROC_ERR_READ_FILE;
            goto END;
        }

        memcpy(buf, buffer, read_len);
        if(NULL != buffer)
            vPortFree(buffer);

        check_read_pos = true;
    }
#endif
    else if(AUDIO_PLAYER_SRC_SD_CARD == audio_player->player_info.source)
    {
        UINT bytes_read = 0;
        
        if(FR_OK != f_read(&audio_player->file_handle, buf, size, &bytes_read)) {
            LOG_E(audio_player_proc, "[%d] fail to read %s!", audio_player->player_handle, audio_player->player_info.path);
            audio_player->last_error = AUDIO_PLAYER_PROC_ERR_READ_FILE;
            goto END;
        }

        read_len = bytes_read;
        check_read_pos = true;
    }

    audio_player->read_pos += read_len;

    if(true == check_read_pos && audio_player->read_pos >= audio_player->total_length) {
        input_done = true;
    }

    if(true == input_done) {
        com_player_set_done(&audio_player->com_player, false);
        LOG_I(audio_player_proc, "[%d] input buffer done!", audio_player->player_handle);
    }

END:
    if(AUDIO_PLAYER_PROC_SUCCESS != audio_player->last_error) {
        com_player_set_done(&audio_player->com_player, true);
        return 0;
    }
    
    return read_len;
}

static int __player_seek_callback(void* param, int position)
{
    return 0;
}

static int __player_error_callback(void* param, int error)
{
    audio_player_proc_t* audio_player = (audio_player_proc_t*)param;

    audio_player->last_error = AUDIO_PLAYER_PROC_ERR_AUDIO_TYPE;
    com_player_set_done(&audio_player->com_player, true);
    
    return 0;
}

static audio_player_return_t audio_player_proc_open_file(audio_player_proc_t* audio_player)
{
    bool check_total_length = false;
    
    if(true == audio_player->file_open_flag) {
        return AUDIO_PLAYER_PROC_SUCCESS;
    }
    else {
        audio_player->file_open_flag = true;
    }
        
    if(AUDIO_PLAYER_SRC_WEB == audio_player->player_info.source)
    {
        bool range_enable = (AUDIO_PLAYER_TYPE_PROMPT==audio_player->player_info.type) ?false :true;
        
        if(HTTP_DOWNLOAD_PROC_SUCCESS != http_download_start(&audio_player->http_proc, &audio_player->http_buffer, audio_player->player_info.path, range_enable)) {
            LOG_E(audio_player_proc, "[%d] fail to open %s!", audio_player->player_handle, audio_player->player_info.path);
            return AUDIO_PLAYER_PROC_ERR_OPEN_FILE;
        }
    }
#if 0
    else if(AUDIO_PLAYER_SRC_FLASH == audio_player->player_info.source)
    {
        if(0 != prompt_addr_info_get(audio_player->player_info.path, &audio_player->prompt_offset, &audio_player->total_length)) {
            LOG_E(audio_player_proc, "[%d] fail to open %s!", audio_player->player_handle, audio_player->player_info.path);
            return AUDIO_PLAYER_PROC_ERR_OPEN_FILE;
        }

        check_total_length = true;
    }
#endif
    else if(AUDIO_PLAYER_SRC_SD_CARD == audio_player->player_info.source)
    {
        if(FR_OK != f_open(&audio_player->file_handle, _T(audio_player->player_info.path), FA_OPEN_EXISTING |FA_WRITE |FA_READ)) {
            LOG_E(audio_player_proc, "[%d] fail to open %s!", audio_player->player_handle, audio_player->player_info.path);
            return AUDIO_PLAYER_PROC_ERR_OPEN_FILE;
        }

        if(FR_OK != f_lseek(&audio_player->file_handle, (FSIZE_t)audio_player->read_pos)) {
            LOG_E(audio_player_proc, "[%d] fail to read %s!", audio_player->player_handle, audio_player->player_info.path);
            return AUDIO_PLAYER_PROC_ERR_READ_FILE;
        }

        audio_player->total_length = (uint32_t)f_size(&audio_player->file_handle);
        check_total_length = true;
    }

    if(true == check_total_length && audio_player->total_length <= 0) {
        LOG_E(audio_player_proc, "[%d] %s is empty!", audio_player->player_handle, audio_player->player_info.path);
        return AUDIO_PLAYER_PROC_ERR_EMPTY_FILE;
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static audio_player_return_t audio_player_proc_close_file(audio_player_proc_t* audio_player)
{
    if(false == audio_player->file_open_flag) {
        return AUDIO_PLAYER_PROC_SUCCESS;
    }
    
    if(AUDIO_PLAYER_SRC_WEB == audio_player->player_info.source)
    {
    }
    else if(AUDIO_PLAYER_SRC_FLASH == audio_player->player_info.source)
    {
        audio_player->file_open_flag = false;
    }
    else if(AUDIO_PLAYER_SRC_SD_CARD == audio_player->player_info.source)
    {
        audio_player->file_open_flag = false;
        
        if(FR_OK != f_close(&audio_player->file_handle)) {
            LOG_E(audio_player_proc, "[%d] fail to close %s!", audio_player->player_handle, audio_player->player_info.path);
            return AUDIO_PLAYER_PROC_ERR_CLOSE_FILE;
        }
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static audio_player_return_t audio_player_proc_start(audio_player_proc_t* audio_player)
{
    audio_player_return_t ret;

    audio_player->total_length          = 0;
    audio_player->read_pos              = 0;
    audio_player->file_open_flag        = false;
    audio_player->progress_monitor_tick = 0;

    LOG_I(audio_player_proc, "[%d] player_path: %s", audio_player->player_handle, audio_player->player_info.path);
    
    ret = audio_player_proc_open_file(audio_player);
    if(AUDIO_PLAYER_PROC_SUCCESS != ret) {
        return ret;
    }

    __audio_player_before_start(audio_player);
    __audio_player_register(audio_player);

    if(COM_PLAYER_SUCCESS != com_player_start(
        &audio_player->com_player, 
        __player_input_callback,
        __player_error_callback,
        __player_seek_callback,
        audio_player))
    {
        LOG_E(audio_player_proc, "[%d] fail to call play!", audio_player->player_handle);
        return AUDIO_PLAYER_PROC_ERR_PLAYER_START;
    }
	
	if(NULL != audio_player->audio_player_callback) {
        audio_player->audio_player_callback(audio_player, AUDIO_PLAYER_EVENT_START);
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static audio_player_return_t audio_player_proc_stop(audio_player_proc_t* audio_player)
{
    com_player_stop(&audio_player->com_player);
    
    audio_player_proc_close_file(audio_player);

    http_download_stop(&audio_player->http_proc);
    common_buffer_clear(&audio_player->http_buffer);

    __audio_player_unregister(audio_player);
    __audio_player_after_stop(audio_player);

    if(NULL != audio_player->audio_player_callback) {
        audio_player->audio_player_callback(audio_player, AUDIO_PLAYER_EVENT_STOP);
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static audio_player_return_t audio_player_proc_pause(audio_player_proc_t* audio_player)
{
    com_player_pause(&audio_player->com_player);

    audio_player_proc_close_file(audio_player);
    http_download_pause(&audio_player->http_proc);

    if(NULL != audio_player->audio_player_callback) {
        audio_player->audio_player_callback(audio_player, AUDIO_PLAYER_EVENT_PAUSE);
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static audio_player_return_t audio_player_proc_resume(audio_player_proc_t* audio_player)
{
    http_download_resume(&audio_player->http_proc);
    audio_player_proc_open_file(audio_player);

    com_player_resume(&audio_player->com_player);

    if(NULL != audio_player->audio_player_callback) {
        audio_player->audio_player_callback(audio_player, AUDIO_PLAYER_EVENT_RESUME);
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

static audio_player_return_t audio_player_proc_play_monitor(audio_player_proc_t* audio_player)
{
    if((xTaskGetTickCount() - audio_player->progress_monitor_tick) > AUDIO_PLAYER_PROGRESS_INTERVAL)
    {
        int cur, all;
        if(true == com_player_get_progress(&audio_player->com_player, audio_player->total_length, &cur, &all)) {
            LOG_I(audio_player_proc, "[%d] progress: %02d:%02d/%02d:%02d", audio_player->player_handle, cur/60, cur%60, all/60, all%60);
        }
        else {
            LOG_E(audio_player_proc, "[%d] com_player_get_progress failed", audio_player->player_handle);
        }

        audio_player->progress_monitor_tick = xTaskGetTickCount();
    }

    if(true == com_player_is_done(&audio_player->com_player)) {
        LOG_I(audio_player_proc, "[%d] all end!", audio_player->player_handle);
        return AUDIO_PLAYER_PROC_ALL_END;
    }
    else if(true == com_player_auto_resume(&audio_player->com_player)) {
        LOG_I(audio_player_proc, "[%d] auto resume com_player", audio_player->player_handle);
    }
    
    return AUDIO_PLAYER_PROC_SUCCESS;
}

void audio_player_task(void *param)
{
    audio_player_proc_t* audio_player = (audio_player_proc_t*)param;
    audio_player_return_t ret = AUDIO_PLAYER_PROC_SUCCESS;
    uint32_t events;
    bool running = true;

    while(true==running)
	{
        ret = AUDIO_PLAYER_PROC_SUCCESS;
        
        switch(audio_player->cur_state)
		{
		case AUDIO_PLAYER_STA_IDLE:
            LOG_I(audio_player_proc, "[%d] last_error: %d", audio_player->player_handle, audio_player->last_error);
            
            audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_STOP_DONE);
            audio_player_clear_event(audio_player, AUDIO_PLAYER_EVENT_START_DONE);
        
            events = audio_player_wait_event(audio_player, AUDIO_PLAYER_EVENT_START |AUDIO_PLAYER_EVENT_EXIT, portMAX_DELAY);

            audio_player_clear_event(audio_player, AUDIO_PLAYER_EVENT_STOP_DONE);

            if(AUDIO_PLAYER_EVENT_EXIT & events) {
                running = false;
                LOG_I(audio_player_proc, "[%d] STA_IDLE --> STA_EXIT", audio_player->player_handle);
            
                audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_EXIT_DONE);
            }
            else if(AUDIO_PLAYER_EVENT_START & events) {
                audio_player->cur_state = AUDIO_PLAYER_STA_START;
                LOG_I(audio_player_proc, "[%d] STA_IDLE --> STA_START", audio_player->player_handle);
            }
        
			break;

        case AUDIO_PLAYER_STA_START:
            ret = audio_player_proc_start(audio_player);
        
            if(AUDIO_PLAYER_PROC_SUCCESS == ret) {
                audio_player->cur_state = AUDIO_PLAYER_STA_PLAY;
                LOG_I(audio_player_proc, "[%d] STA_START --> STA_PLAY", audio_player->player_handle);
            }
            else {
                audio_player->cur_state = AUDIO_PLAYER_STA_STOP;
                LOG_I(audio_player_proc, "[%d] STA_START --> STA_STOP", audio_player->player_handle);
            }

            audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_START_DONE);
			break;
			
		case AUDIO_PLAYER_STA_STOP:
            audio_player_proc_stop(audio_player);
            
            audio_player->cur_state = AUDIO_PLAYER_STA_IDLE;
            LOG_I(audio_player_proc, "[%d] STA_STOP --> STA_IDLE", audio_player->player_handle);
			break;
			
		case AUDIO_PLAYER_STA_PAUSE:
            events = AUDIO_PLAYER_EVENT_STOP |AUDIO_PLAYER_EVENT_RESUME;
            events = audio_player_wait_event(audio_player, events, portMAX_DELAY);

            if(AUDIO_PLAYER_EVENT_STOP & events) {
                audio_player->cur_state = AUDIO_PLAYER_STA_STOP;
                LOG_I(audio_player_proc, "[%d] STA_PAUSE --> STA_STOP", audio_player->player_handle);
            }
            else if(AUDIO_PLAYER_EVENT_RESUME & events)
            {
                ret = audio_player_proc_resume(audio_player);

                if(AUDIO_PLAYER_PROC_SUCCESS == ret) {
                    audio_player->cur_state = AUDIO_PLAYER_STA_PLAY;
                    LOG_I(audio_player_proc, "[%d] STA_PAUSE --> STA_PLAY", audio_player->player_handle);
                }
                else {
                    audio_player->cur_state = AUDIO_PLAYER_STA_STOP;
                    LOG_I(audio_player_proc, "[%d] STA_PAUSE --> STA_STOP", audio_player->player_handle);
                }
            }
			break;

        case AUDIO_PLAYER_STA_BREAK:
            events = AUDIO_PLAYER_EVENT_STOP |AUDIO_PLAYER_EVENT_RESUME;
            events = audio_player_wait_event(audio_player, events, portMAX_DELAY);
            
            if(AUDIO_PLAYER_EVENT_STOP & events) {
                audio_player->cur_state = AUDIO_PLAYER_STA_STOP;
                LOG_I(audio_player_proc, "[%d] STA_BREAK --> STA_STOP", audio_player->player_handle);
            }
            else if(AUDIO_PLAYER_EVENT_RESUME & events)
            {
                ret = audio_player_proc_resume(audio_player);

                if(AUDIO_PLAYER_PROC_SUCCESS == ret) {
                    audio_player->cur_state = AUDIO_PLAYER_STA_PLAY;
                    LOG_I(audio_player_proc, "[%d] STA_BREAK --> STA_PLAY", audio_player->player_handle);
                }
                else {
                    audio_player->cur_state = AUDIO_PLAYER_STA_STOP;
                    LOG_I(audio_player_proc, "[%d] STA_BREAK --> STA_STOP", audio_player->player_handle);
                }
            }
			break;

        case AUDIO_PLAYER_STA_PLAY:
            events = 
                AUDIO_PLAYER_EVENT_STOP |
                AUDIO_PLAYER_EVENT_RESUME |
                AUDIO_PLAYER_EVENT_PAUSE |
                AUDIO_PLAYER_EVENT_BREAK;
            
            events = audio_player_wait_event(audio_player, events, AUDIO_PLAYER_MONITOR_INTERVAL);

            if(AUDIO_PLAYER_EVENT_STOP & events) {
                audio_player->cur_state = AUDIO_PLAYER_STA_STOP;
                LOG_I(audio_player_proc, "[%d] STA_PLAY --> STA_STOP", audio_player->player_handle);
            }
            else if(AUDIO_PLAYER_EVENT_PAUSE & events) {
                ret = audio_player_proc_pause(audio_player);

                if(AUDIO_PLAYER_PROC_SUCCESS == ret) {
                    audio_player->cur_state = AUDIO_PLAYER_STA_PAUSE;
                    LOG_I(audio_player_proc, "[%d] STA_PLAY --> STA_PAUSE", audio_player->player_handle);
                }

                audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_PAUSE_DONE);
            }
            else if(AUDIO_PLAYER_EVENT_BREAK & events) {
                ret = audio_player_proc_pause(audio_player);

                if(AUDIO_PLAYER_PROC_SUCCESS == ret) {
                    audio_player->cur_state = AUDIO_PLAYER_STA_BREAK;
                    LOG_I(audio_player_proc, "[%d] STA_PLAY --> STA_BREAK", audio_player->player_handle);
                }

                audio_player_set_event(audio_player, AUDIO_PLAYER_EVENT_BREAK_DONE);
            }

            if(AUDIO_PLAYER_PROC_SUCCESS == ret)
                ret = audio_player_proc_play_monitor(audio_player);

            if(AUDIO_PLAYER_PROC_SUCCESS != ret && AUDIO_PLAYER_PROC_SUCCESS == audio_player->last_error)
                audio_player->last_error = ret;

            if(AUDIO_PLAYER_PROC_SUCCESS != audio_player->last_error) {
                audio_player->cur_state = AUDIO_PLAYER_STA_STOP;
                LOG_I(audio_player_proc, "[%d] STA_PLAY --> STA_STOP", audio_player->player_handle);
            }
            
            break;
        }

        if(AUDIO_PLAYER_PROC_SUCCESS != ret && AUDIO_PLAYER_PROC_SUCCESS == audio_player->last_error)
            audio_player->last_error = ret;
    }

    vTaskDelete(NULL);
}

