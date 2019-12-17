TARGET  := audio_player
SRC_DIR := ../../src
OBJ_DIR := objs
BIN_DIR := bin
#CC      := aarch64-rockchip-linux-gnu-gcc
CC      := gcc
CFLAGS := -Wall -g

INCS += -I.
INCS += -I$(SRC_DIR)
INCS += -I$(SRC_DIR)/com
INCS += -I$(SRC_DIR)/media
INCS += -I$(SRC_DIR)/network
INCS += -I$(SRC_DIR)/virtual

SRCS += com/typedefs.c
SRCS += com/common_event.c

SRCS += media/id3tag.c
SRCS += media/pcm_trans.c
SRCS += media/ring_buffer.c
SRCS += media/mp3_decoder.c
SRCS += media/common_player.c
SRCS += media/audio_player_process.c
SRCS += media/audio_message_queue.c
SRCS += media/audio_manager.c

SRCS += network/common_buffer.c
SRCS += network/httpclient.c
SRCS += network/http_download_process.c

SRCS += ./main.c
SRCS += ./auth_do.c
OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))

$(OBJ_DIR)/com/%.o: $(SRC_DIR)/com/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< $(DEPS) -o $@ $(INCS)
	
$(OBJ_DIR)/media/%.o: $(SRC_DIR)/media/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< $(DEPS) -o $@ $(INCS)
	
$(OBJ_DIR)/network/%.o: $(SRC_DIR)/network/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< $(DEPS) -o $@ $(INCS)
	
$(OBJ_DIR)/virtual/%.o: $(SRC_DIR)/virtual/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< $(DEPS) -o $@ $(INCS)
	
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< $(DEPS) -o $@ $(INCS)

all: $(OBJS)
	@mkdir -p $(BIN_DIR)
	@$(CC) $(CFLAGS) -o $(BIN_DIR)/$(TARGET) $(OBJS) -lasound -lpthread -lmad

.PHONY: all clean

clean:
	@rm -rf objs
