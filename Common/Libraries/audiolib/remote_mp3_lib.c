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
#include <string.h>
#include "libdef.h"
#include "stm32h747i_discovery_audio.h"

#include "mp3dec.h"

/* Private types -------------------------------------------------------------*/
typedef enum
{
    MP3_LIB_INIT = 0,
    MP3_LIB_FILL_NETBUFF,
    MP3_LIB_FILL_SAMPLES,
    MP3_LIB_PROC_SAMPLES
} remote_mp3_lib_state_t;

typedef enum
{
    MP3_LIB_DECODE_READ = 0,
    MP3_LIB_DECODE_PROC
} remote_mp3_lib_decode_state_t;

typedef struct
{
    uint8_t         frame_decoded : 1;
	uint8_t			deco_idx;	
	
	uint8_t *		read_buf;
	uint8_t *		read_ptr;

	uint32_t		bytes_left;
	uint32_t 		frame_size;
	uint32_t		no_data : 1;
	uint32_t		eof : 1;	
	
	HMP3Decoder 	h;
	MP3FrameInfo 	info;
} remote_mp3_lib_codec_t;

/* Private constants ---------------------------------------------------------*/
#define MP3_LIB_READBUF_SIZE    (0x1000)  	/* 4096 byte */
#define MP3_LIB_FRAME_SIZE      (0x1200)    /* 4608 byte */      
#define MP3_LIB_AUDIOBUF_NUM    8
#define MP3_LIB_AUDIOBUF_SIZE   (MP3_LIB_FRAME_SIZE * MP3_LIB_AUDIOBUF_NUM)

#define REMOTE_MP3_LIB_NETBUFF_SIZE  (256 * MP3_LIB_READBUF_SIZE)

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static audio_lib_buffer_t mp3_buffer[MP3_LIB_AUDIOBUF_NUM];
static audio_lib_read_state_t remote_read_state;
static remote_mp3_lib_codec_t mp3_codec;
static remote_mp3_lib_state_t mp3_lib_state;

/* Private function prototypes -----------------------------------------------*/
void 	remote_mp3_lib_process(audio_lib_handle_t * hlib);
void    remote_mp3_lib_seek(audio_lib_handle_t * hlib);
void 	remote_mp3_lib_free(audio_lib_handle_t * hlib);
void 	remote_mp3_lib_transfer_complete_cb(audio_lib_handle_t * hlib);
void 	remote_mp3_lib_transfer_half_cb(audio_lib_handle_t * hlib);

static void     MP3_DecodeFrame(audio_lib_handle_t * hlib);
static int 	    MP3_FillReadBuf(audio_lib_handle_t * hlib, uint32_t bytesAlign);
static void 	MP3_AddAudioBuf(audio_lib_handle_t * hlib);

const audio_lib_t remote_mp3_lib = 
{
	NULL,
	remote_mp3_lib_process,
	remote_mp3_lib_seek,
	remote_mp3_lib_free,
	remote_mp3_lib_transfer_complete_cb,
	remote_mp3_lib_transfer_half_cb
};

void remote_mp3_lib_process(audio_lib_handle_t * hlib)
{
    static uint32_t bytesread;
    
    uint8_t * ptr, i;
    
	remote_mp3_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);
    lwftp_session_t *s = hlib->remote.lwftp_session;

    switch (mp3_lib_state)
    {
        case MP3_LIB_INIT:
            /* Allocate net buffer */
            hlib->remote.buffer.size = REMOTE_MP3_LIB_NETBUFF_SIZE;
            hlib->remote.buffer.ptr = (uint8_t *) malloc(hlib->remote.buffer.size);
            if(!hlib->remote.buffer.ptr)
            {
                hlib->err = AUDIO_LIB_MEMORY_ERROR;
                return;
            }        
        
            /* Register variables */
            hlib->prv_data = &mp3_codec;
            hlib->buffer = &mp3_buffer[0];
            
            mp3_codec.h = MP3InitDecoder();
            if(mp3_codec.h == 0)
            {
                hlib->err = AUDIO_LIB_MEMORY_ERROR;
                return;
            }

            mp3_codec.read_ptr = mp3_codec.read_buf = (uint8_t *) malloc(MP3_LIB_READBUF_SIZE);
            if(mp3_codec.read_ptr == 0)
            {
                hlib->err = AUDIO_LIB_MEMORY_ERROR;
                return;
            }

            ptr = (uint8_t *) malloc(MP3_LIB_AUDIOBUF_SIZE);
            if(ptr == 0)
            {
                hlib->err = AUDIO_LIB_MEMORY_ERROR;
                return;
            }

            for(i = 0; i < MP3_LIB_AUDIOBUF_NUM; i++)
            {
                mp3_buffer[i].ptr = ptr + (i * MP3_LIB_FRAME_SIZE);
                mp3_buffer[i].empty = 1;
                mp3_buffer[i].size = 0;
            }

            mp3_lib_state = MP3_LIB_FILL_NETBUFF;
            break;
            
        case MP3_LIB_FILL_NETBUFF:
            switch (remote_read_state)
            {
                case AUDIO_LIB_READ_REQ:
                    /* Init variables */
                    mp3_codec.bytes_left = 0;
                    mp3_codec.no_data = 0;
                    mp3_codec.eof = 0;
                    mp3_codec.frame_size = 0;
                    mp3_codec.frame_decoded = 0;
                    
                    /* Decode and playback from audio buf 0 */
                    mp3_codec.deco_idx = 0;
                    mp3_buffer[0].empty = mp3_buffer[1].empty = 1;                    
                    
                    hlib->remote.play_cur = hlib->remote.fptr;
                    hlib->remote.read_state = AUDIO_LIB_READ_REQ;
                    
                    hlib->remote.buffer.rptr = 0;
                    hlib->remote.buffer.state = AUDIO_LIB_BUFFER_OFFSET_NONE;
                    hlib->remote.buffer.last_state = AUDIO_LIB_BUFFER_OFFSET_NONE;                    

                    LWFTP_FileRead(hlib, hlib->remote.buffer.ptr, hlib->remote.buffer.size, &bytesread);
                    if (hlib->err) return;

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

                        mp3_lib_state = MP3_LIB_FILL_SAMPLES;
                        remote_read_state = AUDIO_LIB_READ_REQ;
                    }
            }
            break;
            
        case MP3_LIB_FILL_SAMPLES:
            MP3_DecodeFrame(hlib);
            if (codec->frame_decoded)
            {
                codec->frame_decoded = 0;
                
                mp3_codec.frame_size = (mp3_codec.info.bitsPerSample / 8) * mp3_codec.info.outputSamps;
                MP3_AddAudioBuf (hlib);

                if (codec->deco_idx == 0)
                {
                    hlib->time.total_time = (hlib->remote.fsize * 8) / ((mp3_codec.info.bitrate / 1000) * 1024);
                    
                    /* Init hardware */
                    if(BSP_AUDIO_OUT_Init(hlib->device, hlib->volume, mp3_codec.info.samprate) != 0)
                    {
                        hlib->err = AUDIO_LIB_HARDWARE_ERROR;
                        return;
                    }

                    /* Start hardware */
                    audio_lib_buffer_t * buf = &(hlib->buffer[0]);
                    hlib->playback_state = AUDIO_LIB_STATE_PLAY;
                    BSP_AUDIO_OUT_Play((uint16_t *) buf->ptr, buf->size * MP3_LIB_AUDIOBUF_NUM);

                    mp3_lib_state = MP3_LIB_PROC_SAMPLES;
                }
            }
            else if (codec->no_data)
            {
                hlib->err = AUDIO_LIB_READ_ERROR;
                return;
            }
            break;
            
        case MP3_LIB_PROC_SAMPLES:
            switch(hlib->playback_state)
            {
                case AUDIO_LIB_STATE_PLAY:
                    hlib->time.elapsed_time = (hlib->remote.play_cur * 8) / ((codec->info.bitrate / 1000) * 1024);
                    
                    if(hlib->time.elapsed_time >= hlib->time.total_time)
                    {
                        hlib->playback_state = AUDIO_LIB_STATE_STOP;
                    }                    
                    
                    /* wait for DMA transfer */
                    if(pbuf->empty == 1)
                    {
                        /* Decoder one frame */
                        MP3_DecodeFrame(hlib);
                        if (codec->frame_decoded)
                        {
                            codec->frame_decoded = 0;
                            codec->frame_size = (codec->info.bitsPerSample / 8) * codec->info.outputSamps;
                            MP3_AddAudioBuf (hlib);                            
                        }
                    }

                    if(codec->no_data == 1)
                    {
                        hlib->playback_state = AUDIO_LIB_STATE_STOP;
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

void remote_mp3_lib_seek(audio_lib_handle_t * hlib)
{
    remote_mp3_lib_codec_t * codec = hlib->prv_data;
    
    BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
    mp3_lib_state = MP3_LIB_FILL_NETBUFF;

    hlib->remote.fptr = (hlib->seek_pos * hlib->remote.fsize) / 100;

    hlib->time.elapsed_time = (hlib->remote.fptr * 8) / ((codec->info.bitrate / 1000) * 1024);    
}

void remote_mp3_lib_free(audio_lib_handle_t * hlib)
{
	remote_mp3_lib_codec_t * codec;
	
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
        
        codec = hlib->prv_data;
        if (codec)
        {
            MP3FreeDecoder(codec->h);
            codec->h = NULL;
            
            if (codec->read_buf)
            {
                free(codec->read_buf);
                codec->read_ptr = codec->read_buf = NULL;
            }
        }        
    }
    
	mp3_lib_state = MP3_LIB_INIT;
	remote_read_state = AUDIO_LIB_READ_REQ;    
}

void remote_mp3_lib_transfer_complete_cb(audio_lib_handle_t * hlib)
{
    uint8_t i;
    
    if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
    {
        for(i = MP3_LIB_AUDIOBUF_NUM / 2; i < MP3_LIB_AUDIOBUF_NUM; i++) 
        {
            /* Data in buffer has been sent out */
            hlib->buffer[i].empty = 1;
            hlib->buffer[i].size = -1;
        }       
    }
}

void remote_mp3_lib_transfer_half_cb(audio_lib_handle_t * hlib)
{
    uint8_t i;
    
    if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
    {
        for(i = 0; i < MP3_LIB_AUDIOBUF_NUM / 2; i++) 
        {
            /* Data in buffer has been sent out */
            hlib->buffer[i].empty = 1;
            hlib->buffer[i].size = -1;
        }       
    }   
}

/**
 * @brief  Decode a frame.
 */
static void MP3_DecodeFrame(audio_lib_handle_t * hlib)
{
    static remote_mp3_lib_decode_state_t decode_state;
    
	remote_mp3_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);
	uint8_t word_align;
	int nRead, offset, err;

	word_align = 0;
	nRead = 0;
	offset = 0;
    
    switch (decode_state)
    {
        case MP3_LIB_DECODE_READ:
            /* somewhat arbitrary trigger to refill buffer - should always be enough for a full frame */
            if (codec->bytes_left < 2 * MAINBUF_SIZE) {
                /* Align to 4 bytes */
                word_align = (4 - (codec->bytes_left & 3)) & 3;

                /* Fill read buffer */
                nRead = MP3_FillReadBuf(hlib, word_align);
                hlib->remote.play_cur += nRead;

                codec->bytes_left += nRead;
                codec->read_ptr = codec->read_buf + word_align;
                if (nRead == 0) {
                    break;
                }
            }
            
            decode_state = MP3_LIB_DECODE_PROC;
            break;
            
        case MP3_LIB_DECODE_PROC:
            /* find start of next MP3 frame - assume EOF if no sync found */
            offset = MP3FindSyncWord(codec->read_ptr, codec->bytes_left);
            if (offset < 0) {
                codec->read_ptr = codec->read_buf;
                codec->bytes_left = 0;
                
                decode_state = MP3_LIB_DECODE_READ;
                break;
            }
            codec->read_ptr += offset;
            codec->bytes_left -= offset;
            
            //simple check for valid header
            if (((*(codec->read_ptr + 1) & 24) == 8) || ((*(codec->read_ptr + 1) & 6) != 2) || ((*(codec->read_ptr + 2) & 240) == 240) || ((*(codec->read_ptr + 2) & 12) == 12)
                    || ((*(codec->read_ptr + 3) & 3) == 2)) {
                codec->read_ptr += 1; //header not valid, try next one
                codec->bytes_left -= 1;
                
                decode_state = MP3_LIB_DECODE_READ;
                break;
            }            
            
            err = MP3Decode(codec->h, &codec->read_ptr, (int *) &codec->bytes_left, (short *) pbuf->ptr, 0);
            if (err == -6) {
                codec->read_ptr += 1;
                codec->bytes_left -= 1;
                
                decode_state = MP3_LIB_DECODE_READ;
                break;
            }

            if (err) {
                /* error occurred */
                switch (err) {
                    case ERR_MP3_INDATA_UNDERFLOW:
                        /* do nothing - next call to decode will provide more inData */
                        break;
                    case ERR_MP3_MAINDATA_UNDERFLOW:
                        /* do nothing - next call to decode will provide more mainData */
                        break;
                    case ERR_MP3_INVALID_HUFFCODES:
                        /* do nothing */
                        break;
                    case ERR_MP3_FREE_BITRATE_SYNC:
                    default:
                        codec->no_data = 1;
                        break;
                }
            } else {
                /* no error */
                MP3GetLastFrameInfo(codec->h, &(codec->info));
                codec->frame_decoded = 1;
            }

            decode_state = MP3_LIB_DECODE_READ;
            break;
    }
}

/**
 * @brief  Read data from MP3 file and fill in the Read Buffer.
 */
static int MP3_FillReadBuf(audio_lib_handle_t * hlib, uint32_t bytesAlign)
{
    remote_mp3_lib_codec_t * codec = hlib->prv_data;
    uint8_t * buff;
    uint32_t btr, br;

    /* Check if readbuff is empty */
    buff = (uint8_t *) (codec->read_buf + codec->bytes_left + bytesAlign);
    btr = MP3_LIB_READBUF_SIZE - codec->bytes_left - bytesAlign;
    if ((hlib->remote.play_cur + btr) >= hlib->fp->fptr)
    {
        hlib->remote.buffer.empty = 1;        
        return 0;
    }
    
    /* Move the left bytes from the end to the front */
    memmove(codec->read_buf + bytesAlign, codec->read_ptr, codec->bytes_left);    
    
    /* Copy from readbuff */
    if ((hlib->remote.buffer.rptr + btr) < hlib->remote.buffer.size)
    {
        memcpy(buff, 
               hlib->remote.buffer.ptr + hlib->remote.buffer.rptr, 
               btr);

        br = btr;
        hlib->remote.buffer.rptr += btr;
    }
    else
    {
        memcpy(buff, 
               hlib->remote.buffer.ptr + hlib->remote.buffer.rptr, 
               hlib->remote.buffer.size - hlib->remote.buffer.rptr);

        br = hlib->remote.buffer.size - hlib->remote.buffer.rptr;
        hlib->remote.buffer.rptr = 0;
        
        memcpy(buff + br, 
               hlib->remote.buffer.ptr + hlib->remote.buffer.rptr, 
               btr - br);
        
        hlib->remote.buffer.rptr += btr - br;
        br = btr;
    }
    
    return br;
}

/**
 * @brief  Add an PCM frame to audio buf after decoding.
 */
static void MP3_AddAudioBuf(audio_lib_handle_t * hlib)
{
	remote_mp3_lib_codec_t * codec = hlib->prv_data;
	audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);
	
	/* Mark the status to not-empty which means it is available to playback. */
	pbuf->empty = 0;
	pbuf->size = codec->frame_size;

	/* Point to the next buffer */
	codec->deco_idx ++;
	if (codec->deco_idx == MP3_LIB_AUDIOBUF_NUM)
		codec->deco_idx = 0;
}
