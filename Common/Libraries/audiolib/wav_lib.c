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
#include "ff.h"
#include "stm32h747i_discovery_audio.h"

/* Private types -------------------------------------------------------------*/
typedef enum
{
    WAV_LIB_READ_HEADER = 0,
    WAV_LIB_FILL_READBUFF,
    WAV_LIB_PROC_SAMPLES
} wav_lib_state_t;

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
} wav_lib_info_t;

/* Private constants ---------------------------------------------------------*/
#define WAV_LIB_AUDIOBUF_SIZE   (36 * 1024)
#define WAV_LIB_READBUFF_SIZE   (2 * 1024 * 1024)

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static audio_lib_buffer_t audio_buffer;
static audio_lib_read_state_t read_state;
static wav_lib_info_t wav_info;
static wav_lib_state_t wav_lib_state;

/* Private function prototypes -----------------------------------------------*/
void wav_lib_process(audio_lib_handle_t * hlib);
void wav_lib_seek(audio_lib_handle_t * hlib);
void wav_lib_free(audio_lib_handle_t * hlib);
void wav_lib_transfer_complete_cb(audio_lib_handle_t * hlib);
void wav_lib_transfer_half_cb(audio_lib_handle_t * hlib);

static uint32_t WAV_ReadFrame(audio_lib_handle_t * hlib, uint32_t offset);

const audio_lib_t wav_lib = 
{
    NULL,
    wav_lib_process,
    wav_lib_seek,
    wav_lib_free,
    wav_lib_transfer_complete_cb,
    wav_lib_transfer_half_cb
};

void wav_lib_process(audio_lib_handle_t * hlib)
{
    static uint32_t bytesread;
    
    wav_lib_info_t * info = hlib->prv_data;
    
    switch (wav_lib_state)
    {
        case WAV_LIB_READ_HEADER:
            hlib->local.buffer.size = WAV_LIB_READBUFF_SIZE;
            hlib->local.buffer.ptr = (uint8_t *) malloc(hlib->local.buffer.size);
            if(!hlib->local.buffer.ptr)
            {
                hlib->err = AUDIO_LIB_MEMORY_ERROR;
                return;
            }                     
            
            hlib->prv_data = &wav_info;
            hlib->buffer = &audio_buffer;
            hlib->buffer->size = WAV_LIB_AUDIOBUF_SIZE;
            hlib->buffer->ptr = (uint8_t *) malloc(hlib->buffer->size);
            if(!hlib->buffer->ptr)
            {
                hlib->err = AUDIO_LIB_MEMORY_ERROR;
                return;
            }

            if( f_read (hlib->fp, &wav_info, sizeof(wav_info), (UINT *) &bytesread) != FR_OK )
            {
                hlib->err = AUDIO_LIB_READ_ERROR;
                return;
            }
            
            hlib->time.total_time = hlib->fp->fsize / wav_info.ByteRate;
            
            wav_lib_state = WAV_LIB_FILL_READBUFF;
            read_state = AUDIO_LIB_READ_REQ;
            break;
            
        case WAV_LIB_FILL_READBUFF:
            switch (read_state)
            {
                case AUDIO_LIB_READ_REQ:
                    hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;
                    
                    hlib->local.play_cur = hlib->fp->fptr;
                    hlib->local.read_state = AUDIO_LIB_READ_REQ;
                    
                    hlib->local.buffer.rptr = 0;
                    hlib->local.buffer.state = AUDIO_LIB_BUFFER_OFFSET_NONE;
                    hlib->local.buffer.last_state = AUDIO_LIB_BUFFER_OFFSET_NONE;

                    /* LWFTP_FileRead call */
                    hlib->local.ptr = hlib->local.buffer.ptr;
                    hlib->local.btr = hlib->local.buffer.size;
                    hlib->local.br = 0;

                    read_state = AUDIO_LIB_READ_WAIT;
                    break;
                    
                case AUDIO_LIB_READ_WAIT:
                    /* LWFTP_FileRead process */
                    if (hlib->local.br < hlib->local.btr && \
                        hlib->fp->fptr < hlib->fp->fsize)
                    {
                        if( f_read (hlib->fp, 
                                    hlib->local.ptr + hlib->local.br, 
                                    AUDIO_LIB_MIN_READSIZE, 
                                    (UINT *) &bytesread) != FR_OK )
                        {
                            hlib->err = AUDIO_LIB_READ_ERROR;
                            return;
                        }
                        
                        hlib->local.br += bytesread;
                    }
                    else
                    {
                        if(!bytesread)
                        {
                            hlib->err = AUDIO_LIB_READ_ERROR;
                            return;
                        }
                        hlib->local.buffer.empty = 0;
                        
                        memcpy(hlib->buffer->ptr, hlib->local.buffer.ptr, hlib->buffer->size);
                        hlib->local.buffer.rptr += hlib->buffer->size;
                        hlib->local.play_cur += hlib->buffer->size;
                        
                        if(BSP_AUDIO_OUT_Init(hlib->device, hlib->volume, wav_info.SampleRate) != 0)
                        {
                            hlib->err = AUDIO_LIB_HARDWARE_ERROR;
                            return;
                        }                            
                        
                        hlib->playback_state = AUDIO_LIB_STATE_PLAY;
                        BSP_AUDIO_OUT_Play((uint16_t *) hlib->buffer->ptr, hlib->buffer->size);
                        
                        wav_lib_state = WAV_LIB_PROC_SAMPLES;
                        read_state = AUDIO_LIB_READ_REQ;                 
                    }                
                    break;
            }
            break;
            
        case WAV_LIB_PROC_SAMPLES:
            switch(hlib->playback_state)
            {
                case AUDIO_LIB_STATE_PLAY:
                    hlib->time.elapsed_time = hlib->local.play_cur / info->ByteRate;
                    
                    if(hlib->time.elapsed_time >= hlib->time.total_time)
                    {
                        hlib->playback_state = AUDIO_LIB_STATE_STOP;
                    }
                    
                    if(hlib->buffer->state == AUDIO_LIB_BUFFER_OFFSET_HALF)
                    {
                        hlib->local.play_cur += WAV_ReadFrame(hlib, 0);
                        
                        hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_NONE;                        
                    }
                    
                    if(hlib->buffer->state == AUDIO_LIB_BUFFER_OFFSET_FULL)
                    {
                        hlib->local.play_cur += WAV_ReadFrame(hlib, hlib->buffer->size / 2);
                        
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

void wav_lib_seek(audio_lib_handle_t * hlib)
{
    wav_lib_info_t * info = hlib->prv_data;
    uint32_t ofs, payload_size, nbuffers, frame_pos;
    
    BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
    wav_lib_state = WAV_LIB_FILL_READBUFF;    

    payload_size = hlib->fp->fsize - sizeof(wav_info);
    nbuffers = payload_size / hlib->buffer->size;
    frame_pos = ((uint64_t)hlib->seek_pos * nbuffers) / 100;

    ofs = sizeof(wav_info) + frame_pos * hlib->buffer->size;
    
    f_lseek(hlib->fp, ofs);

    hlib->time.elapsed_time = hlib->fp->fptr / info->ByteRate;    
}

void wav_lib_free(audio_lib_handle_t * hlib)
{
    if (hlib)
    {        
        if (hlib->buffer && \
            hlib->buffer->ptr)
        {
            free(hlib->buffer->ptr);
            hlib->buffer->ptr = NULL;
        }
        
        if (hlib->local.buffer.ptr)
        {
            free(hlib->local.buffer.ptr);
            hlib->local.buffer.ptr = NULL;
        }        
    }
    
    wav_lib_state = WAV_LIB_READ_HEADER;
    read_state = AUDIO_LIB_READ_REQ;    
}

void wav_lib_transfer_complete_cb(audio_lib_handle_t * hlib)
{
    if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
    {
        hlib->buffer->state = AUDIO_LIB_BUFFER_OFFSET_FULL;
    }    
}

void wav_lib_transfer_half_cb(audio_lib_handle_t * hlib)
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
    if ((hlib->local.play_cur + btr) >= hlib->fp->fptr)
    {
        hlib->local.buffer.empty = 1;        
        return 0;
    }
    
    if ((hlib->local.buffer.rptr + btr) < hlib->local.buffer.size)
    {
        memcpy(hlib->buffer->ptr + offset, 
               hlib->local.buffer.ptr + hlib->local.buffer.rptr, 
               btr);

        bytesread = btr;
        hlib->local.buffer.rptr += btr;
    }
    else
    {
        memcpy(hlib->buffer->ptr + offset, 
               hlib->local.buffer.ptr + hlib->local.buffer.rptr, 
               hlib->local.buffer.size - hlib->local.buffer.rptr);

        bytesread = hlib->local.buffer.size - hlib->local.buffer.rptr;
        hlib->local.buffer.rptr = 0;
        
        memcpy(hlib->buffer->ptr + offset + bytesread, 
               hlib->local.buffer.ptr + hlib->local.buffer.rptr, 
               btr - bytesread);
        
        hlib->local.buffer.rptr += btr - bytesread;
        bytesread = btr;        
    }
    
    return bytesread;
}
