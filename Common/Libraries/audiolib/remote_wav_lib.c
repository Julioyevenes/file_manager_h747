/**
  ******************************************************************************
  * @file    ${file_name} 
  * @author  ${user}
  * @version 
  * @date    ${date}
  * @brief   
  ******************************************************************************
  * @attention
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "libdef.h"
#include "stm32h747i_discovery_audio.h"

/* Private types -------------------------------------------------------------*/
typedef enum
{
    WAV_LIB_READ_HEADER = 0,
    WAV_LIB_FILL_NETBUFF,
    WAV_LIB_PROC_SAMPLES
} remote_wav_lib_state_t;

typedef struct
{
	uint32_t ChunkID;       /* 0 */
	uint32_t FileSize;      /* 4 */
	uint32_t FileFormat;    /* 8 */
	uint32_t SubChunk1ID;   /* 12 */
	uint32_t SubChunk1Size; /* 16 */
	uint16_t AudioFormat;   /* 20 */
	uint16_t NbrChannels;   /* 22 */
	uint32_t SampleRate;    /* 24 */

	uint32_t ByteRate;      /* 28 */
	uint16_t BlockAlign;    /* 32 */
	uint16_t BitPerSample;  /* 34 */
	uint32_t SubChunk2ID;   /* 36 */
	uint32_t SubChunk2Size; /* 40 */
} remote_wav_lib_info_t;

/* Private constants ---------------------------------------------------------*/
#define REMOTE_WAV_LIB_AUDIOBUF_SIZE (36 * 1024)
#define REMOTE_WAV_LIB_NETBUFF_SIZE  (2 * 1024 * 1024)

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static audio_lib_buffer_t audio_buffer;
static audio_lib_read_state_t remote_read_state;
static remote_wav_lib_info_t wav_info;
static remote_wav_lib_state_t wav_lib_state;

/* Private function prototypes -----------------------------------------------*/
void remote_wav_lib_process(audio_lib_handle_t * hlib);
void remote_wav_lib_seek(audio_lib_handle_t * hlib);
void remote_wav_lib_free(audio_lib_handle_t * hlib);
void remote_wav_lib_transfer_complete_cb(audio_lib_handle_t * hlib);
void remote_wav_lib_transfer_half_cb(audio_lib_handle_t * hlib);

static uint32_t WAV_ReadFrame(audio_lib_handle_t * hlib, uint32_t offset);

const audio_lib_t remote_wav_lib = 
{
	NULL,
	remote_wav_lib_process,
	remote_wav_lib_seek,
	remote_wav_lib_free,
	remote_wav_lib_transfer_complete_cb,
	remote_wav_lib_transfer_half_cb
};

void remote_wav_lib_process(audio_lib_handle_t * hlib)
{
    static uint32_t bytesread;
    
    remote_wav_lib_info_t * info = hlib->prv_data;
    
    lwftp_session_t *s = hlib->remote.lwftp_session;
    
    switch (wav_lib_state)
    {
        case WAV_LIB_READ_HEADER:
            switch (remote_read_state)
            {
                case AUDIO_LIB_READ_REQ:
                    hlib->remote.buffer.size = REMOTE_WAV_LIB_NETBUFF_SIZE;
                    hlib->remote.buffer.ptr = (uint8_t *) malloc(hlib->remote.buffer.size);
                    if(!hlib->remote.buffer.ptr)
                    {
                        hlib->err = AUDIO_LIB_MEMORY_ERROR;
                        return;
                    }                     
                    
                    hlib->prv_data = &wav_info;
                    hlib->buffer = &audio_buffer;
                    hlib->buffer->size = REMOTE_WAV_LIB_AUDIOBUF_SIZE;
                    hlib->buffer->ptr = (uint8_t *) malloc(hlib->buffer->size);
                    if(!hlib->buffer->ptr)
                    {
                        hlib->err = AUDIO_LIB_MEMORY_ERROR;
                        return;
                    }
                    
                    LWFTP_FileRead(hlib, &wav_info, sizeof(wav_info), &bytesread);
                    if (hlib->err) 
                        return;

                    remote_read_state = AUDIO_LIB_READ_WAIT;
                    break;
                    
                case AUDIO_LIB_READ_WAIT:
                    if (s->control_pcb == NULL)
                    {
                        if(bytesread != sizeof(wav_info))
                        {
                            hlib->err = AUDIO_LIB_READ_ERROR;
                            return;
                        }
                        
                        hlib->time.total_time = hlib->remote.fsize / wav_info.ByteRate;
                        
                        wav_lib_state = WAV_LIB_FILL_NETBUFF;
                        remote_read_state = AUDIO_LIB_READ_REQ;
                    }
                    break;
            }           
            break;
            
        case WAV_LIB_FILL_NETBUFF:
            switch (remote_read_state)
            {
                case AUDIO_LIB_READ_REQ:
                    hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;
                    
                    hlib->remote.play_cur = hlib->remote.fptr;
                    hlib->remote.read_state = AUDIO_LIB_READ_REQ;
                    
                    hlib->remote.buffer.rptr = 0;
                    hlib->remote.buffer.state = AUDIO_LIB_BUFFER_OFFSET_NONE;
                    hlib->remote.buffer.last_state = AUDIO_LIB_BUFFER_OFFSET_NONE;                    

                    LWFTP_FileRead(hlib, hlib->remote.buffer.ptr, hlib->remote.buffer.size, &bytesread);
                    if (hlib->err)
                        return;

                    remote_read_state = AUDIO_LIB_READ_WAIT;                    
                    break;
                    
                case AUDIO_LIB_READ_WAIT:
                    if (s->control_pcb == NULL)
                    {
                        if(!bytesread)
                        {
                            hlib->err = AUDIO_LIB_READ_ERROR;
                            return;
                        }
                        hlib->remote.buffer.empty = 0;
                        
                        memcpy(hlib->buffer->ptr, hlib->remote.buffer.ptr, hlib->buffer->size);
                        hlib->remote.buffer.rptr += hlib->buffer->size;
                        hlib->remote.play_cur += hlib->buffer->size;
                        
                        if(BSP_AUDIO_OUT_Init(hlib->device, hlib->volume, wav_info.SampleRate) != 0)
                        {
                            hlib->err = AUDIO_LIB_HARDWARE_ERROR;
                            return;
                        }                            
                        
                        hlib->playback_state = AUDIO_LIB_STATE_PLAY;
                        BSP_AUDIO_OUT_Play((uint16_t *) hlib->buffer->ptr, hlib->buffer->size);
                        
                        wav_lib_state = WAV_LIB_PROC_SAMPLES;
                        remote_read_state = AUDIO_LIB_READ_REQ;                        
                    }
                    break;
            }
            break;
            
        case WAV_LIB_PROC_SAMPLES:
            switch(hlib->playback_state)
            {
                case AUDIO_LIB_STATE_PLAY:
                    hlib->time.elapsed_time = hlib->remote.play_cur / info->ByteRate;
                    
                    if(hlib->time.elapsed_time >= hlib->time.total_time)
                    {
                        hlib->playback_state = AUDIO_LIB_STATE_STOP;
                    }
                    
                    if(hlib->buffer->state == AUDIO_LIB_BUFFER_OFFSET_HALF)
                    {
                        hlib->remote.play_cur += WAV_ReadFrame(hlib, 0);
                        
                        hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;                        
                    }
                    
                    if(hlib->buffer->state == AUDIO_LIB_BUFFER_OFFSET_FULL)
                    {
                        hlib->remote.play_cur += WAV_ReadFrame(hlib, hlib->buffer->size / 2);
                        
                        hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;                        
                    }
                    break;
                    
                case AUDIO_LIB_STATE_STOP:
                    BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
                    hlib->active = 0;
                    break;

                case AUDIO_LIB_STATE_PAUSE:
                    BSP_AUDIO_OUT_Pause();
                    hlib->playback_state = AUDIO_LIB_STATE_WAIT;
                    break;

                case AUDIO_LIB_STATE_RESUME:
                    BSP_AUDIO_OUT_Resume();
                    hlib->playback_state = AUDIO_LIB_STATE_PLAY;
                    break;
                
                case AUDIO_LIB_STATE_WAIT:
                case AUDIO_LIB_STATE_IDLE:
                case AUDIO_LIB_STATE_INIT:
                default:
                    break;
            }
            break;
    }
}

void remote_wav_lib_seek(audio_lib_handle_t * hlib)
{
    remote_wav_lib_info_t * info = hlib->prv_data;
    uint32_t payload_size, nbuffers, frame_pos;
    
    BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
    wav_lib_state = WAV_LIB_FILL_NETBUFF;    

    payload_size = hlib->remote.fsize - sizeof(wav_info);
    nbuffers = payload_size / hlib->buffer->size;
    frame_pos = ((uint64_t)hlib->seek_pos * nbuffers) / 100;

    hlib->remote.fptr = sizeof(wav_info) + frame_pos * hlib->buffer->size;

    hlib->time.elapsed_time = hlib->remote.fptr / info->ByteRate;
}

void remote_wav_lib_free(audio_lib_handle_t * hlib)
{
    if (hlib)
    {        
        if (hlib->buffer && \
            hlib->buffer->ptr)
        {
            free(hlib->buffer->ptr);
            hlib->buffer->ptr = NULL;
        }
        
        if (hlib->remote.buffer.ptr)
        {
            free(hlib->remote.buffer.ptr);
            hlib->remote.buffer.ptr = NULL;
        }        
	
        hlib->remote.fptr = 0;
    }
	
	wav_lib_state = WAV_LIB_READ_HEADER;
	remote_read_state = AUDIO_LIB_READ_REQ;
}

void remote_wav_lib_transfer_complete_cb(audio_lib_handle_t * hlib)
{
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_FULL;
	}    
}

void remote_wav_lib_transfer_half_cb(audio_lib_handle_t * hlib)
{
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
		hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_HALF;
	}    
}

static uint32_t WAV_ReadFrame(audio_lib_handle_t * hlib, uint32_t offset)
{
    uint32_t bytesread, btr;
    
    btr = hlib->buffer->size / 2;
    if ((hlib->remote.play_cur + btr) >= hlib->remote.fptr)
    {
        hlib->remote.buffer.empty = 1;        
        return 0;
    }    
    
    if ((hlib->remote.buffer.rptr + btr) < hlib->remote.buffer.size)
    {
        memcpy(hlib->buffer->ptr + offset, 
               hlib->remote.buffer.ptr + hlib->remote.buffer.rptr, 
               btr);

        bytesread = btr;
        hlib->remote.buffer.rptr += btr;
    }
    else
    {
        memcpy(hlib->buffer->ptr + offset, 
               hlib->remote.buffer.ptr + hlib->remote.buffer.rptr, 
               hlib->remote.buffer.size - hlib->remote.buffer.rptr);

        bytesread = hlib->remote.buffer.size - hlib->remote.buffer.rptr;
        hlib->remote.buffer.rptr = 0;
        
        memcpy(hlib->buffer->ptr + offset + bytesread, 
               hlib->remote.buffer.ptr + hlib->remote.buffer.rptr, 
               btr - bytesread);
        
        hlib->remote.buffer.rptr += btr - bytesread;
        bytesread = btr;        
    }
    
    return bytesread;
}
