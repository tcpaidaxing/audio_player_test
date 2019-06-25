#include "audio_player_process.h"
#include "audio_manager.h"

typedef struct {
    com_player_t    com_player;
    FILE*           file_handle;
    uint32_t        total_length;
    uint32_t        read_pos;
    
} audio_player_t;

static int com_input_callback(void* param, uint8_t* buf, int size)
{
    audio_player_t* audio_player = (audio_player_t*)param;
    int bytes_read = 0;
	
	bytes_read = fread(buf, 1, size, audio_player->file_handle);
	if(bytes_read < 0) {
        LOG_E(common, "fail to read file!");
        com_player_set_done(&audio_player->com_player, true);
        return 0;
    }

    audio_player->read_pos += bytes_read;

    if(audio_player->read_pos >= audio_player->total_length) {
        com_player_set_done(&audio_player->com_player, false);
        LOG_I(common, "input buffer done!");
    }
    
    return bytes_read;
}

static int com_seek_callback(void* param, int position)
{
    audio_player_t* audio_player = (audio_player_t*)param;
	
	//LOG_I(common, "position:%d", position);
    
    return fseek(audio_player->file_handle, position, SEEK_SET);
}

static int com_error_callback(void* param, int error)
{
    audio_player_t* audio_player = (audio_player_t*)param;

    LOG_E(common, "error:0x%X", error);
    com_player_set_done(&audio_player->com_player, true);
    
    return 0;
}

static void com_player_test(void)
{
	static audio_player_t audio_player;
    int cur, all;
    
    memset(&audio_player, 0, sizeof(audio_player_t));
	
	//audio_player.file_handle = fopen("/home/look/Files/www/music/StartUp.mp3", "rb");
	audio_player.file_handle = fopen("pfzl.mp3", "rb");
	fseek(audio_player.file_handle, 0, SEEK_END);
	audio_player.total_length = ftell(audio_player.file_handle);
	fseek(audio_player.file_handle, 0, SEEK_SET);
    
	pcm_trans_init();
    com_player_init(&audio_player.com_player);
	
	if(COM_PLAYER_SUCCESS != com_player_start(
		&audio_player.com_player, 
		com_input_callback, 
		com_error_callback, 
		com_seek_callback, 
		&audio_player))
	{
		LOG_E(common, "com_player_start failed");
	}
	else
	{
		while(true)
		{
			if(true == com_player_get_progress(&audio_player.com_player, audio_player.total_length, &cur, &all))
				LOG_I(common, "progress: %02d:%02d/%02d:%02d", cur/60, cur%60, all/60, all%60);
			
			if(true == com_player_is_done(&audio_player.com_player))
				break;
			
			usleep(1000*1000);
		}
	}

    com_player_stop(&audio_player.com_player);
	com_player_deinit(&audio_player.com_player);
    fclose(audio_player.file_handle);
}

static void http_download_test(void)
{
	common_buffer_t http_buffer;
	http_download_proc_t http_proc;
	
	uint32_t len;
	FILE* file_writer;
	char* file_name = "http://47.98.36.22/432506345.mp3";
	char buf[512];
	
	for(len=strlen(file_name); len>0; len--) {
        if(file_name[len-1]=='/')
            break;
    }

    sprintf(buf, "/home/look/Files/www/music/%s", &file_name[len]);
    file_writer = fopen(buf, "wb");

	common_buffer_init(&http_buffer, COMMON_BUF_HTTP_NODE_SIZE, COMMON_BUF_HTTP_MAX_SIZE);
	http_download_init(&http_proc);
	http_download_start(&http_proc, &http_buffer, file_name, true);
			
	while(1)
    {
        len = common_buffer_get_count(&http_buffer);
        
        if(len > 0) {
            len = sizeof(buf);
            common_buffer_pop(&http_buffer, buf, &len);
			fwrite(buf, 1, len, file_writer);
        }
        else if(true==http_download_is_stopped(&http_proc)) {
            break;
        }
        else {
            vTaskDelay(200/portTICK_RATE_MS);
        }
    }		
			
	http_download_stop(&http_proc);
	http_download_deinit(&http_proc);
	common_buffer_deinit(&http_buffer);
	
	fclose(file_writer);
}

static void audio_player_test(void)
{
	static audio_player_proc_t resource_player;
    static audio_player_proc_t prompt_player;
    
    static audio_player_info_t resource_player_info = { "http://47.98.36.22/432506345.mp3", AUDIO_PLAYER_SRC_WEB, AUDIO_PLAYER_TYPE_RESOURCE, 0, true };
	//static audio_player_info_t resource_player_info = { "/home/look/Files/www/music/test.mp3", AUDIO_PLAYER_SRC_SD_CARD, AUDIO_PLAYER_TYPE_RESOURCE, 0, true };
    static audio_player_info_t prompt_player_info   = { "pfzl.mp3", AUDIO_PLAYER_SRC_SD_CARD, AUDIO_PLAYER_TYPE_PROMPT, 0, true };

    pcm_trans_init();
    audio_player_init(&resource_player);
    audio_player_init(&prompt_player);

    audio_player_start(&resource_player, &resource_player_info, false);vTaskDelay(10000);
    //audio_player_start(&prompt_player, &prompt_player_info, false);
    
    while(1)
    {
		if( AUDIO_PLAYER_STA_IDLE == resource_player.cur_state && 
			AUDIO_PLAYER_STA_IDLE == prompt_player.cur_state)
		{
			break;
		}
		
        vTaskDelay(1000);
    }
	
	audio_player_deinit(&resource_player);
	audio_player_deinit(&prompt_player);
	pcm_trans_deinit();
}

int main(int argc, char* argv[])
{
	//com_player_test();
	audio_player_test();
	
    return 0;
}
