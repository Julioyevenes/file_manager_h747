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
#include "hal_jpeg_codec.h"

/* Private types -------------------------------------------------------------*/
typedef enum
{
    JMV_LIB_READ_HEADER = 0,
    JMV_LIB_READ_FRAMEVECT,
    JMV_LIB_FILL_NETBUFF,
    JMV_LIB_FIRST_FRAME,
    JMV_LIB_PROC_SAMPLES
} remote_jmv_lib_state_t;

typedef enum
{
    JMV_LIB_DECODE_INIT = 0,
    JMV_LIB_DECODE_WAIT
} remote_jmv_lib_decode_state_t;

typedef struct
{
 	/* video */
 	uint16_t frame_width;
 	uint16_t frame_height;
 	uint8_t  frame_bytedepth;
 	uint8_t  frame_rate;
 	uint8_t  frame_jpeg;
 	uint32_t frame_nb;
 	uint32_t frame_vect_size;
 	uint32_t frame_maxsize;

 	/* audio */
 	uint8_t  audio_numchannels;
 	uint8_t  audio_bytedepth;
 	uint16_t audio_samplerate;
	uint32_t audio_byterate;
	uint32_t audio_totalsize;

 	/* padding */
 	uint8_t  pad[481];
} __attribute__ ((packed)) remote_jmv_lib_header_t;

typedef struct
{
    /* video */
    uint32_t * vect_addr;
    uint32_t vect_size;
} remote_jmv_lib_frame_t;

typedef struct
{
	remote_jmv_lib_header_t header;
	remote_jmv_lib_frame_t  frame;	
} remote_jmv_lib_metadata_t;

typedef struct
{
	uint8_t *  jpeg_buff_ptr;
	
	uint32_t   frame_pos;
	uint32_t   fifo_pos;
	uint32_t   video_pos;
	uint32_t   jpeg_buff_size;
	uint32_t   expected_play_cur;

	JPEG_ConfTypeDef   	jpeginfo;
	jpeg_codec_handle_t hjpegcodec;
	
	remote_jmv_lib_metadata_t metadata;
} remote_jmv_lib_codec_t;

/* Private constants ---------------------------------------------------------*/
#define REMOTE_JMV_LIB_NETBUFF_SIZE (8 * 1024 * 1024)
#define REMOTE_JMV_FRAME_TIMEOUT    100U
#define JPEG_INTERRUPT_MASK         ((uint32_t)0x0000007EU)

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static audio_lib_buffer_t jmv_buffer;
static audio_lib_read_state_t remote_read_state;
static remote_jmv_lib_codec_t jmv_codec;
static remote_jmv_lib_state_t jmv_lib_state;

/* Private function prototypes -----------------------------------------------*/
void remote_jmv_lib_process(audio_lib_handle_t * hlib);
void remote_jmv_lib_seek(audio_lib_handle_t * hlib);
void remote_jmv_lib_free(audio_lib_handle_t * hlib);
void remote_jmv_lib_transfer_complete_cb(audio_lib_handle_t * hlib);
void remote_jmv_lib_transfer_half_cb(audio_lib_handle_t * hlib);

static void     JMV_DecodeFrame(audio_lib_handle_t * hlib);
static void     JMV_FillAudioFIFO(audio_lib_handle_t * hlib);
static uint8_t  JMV_FillReadBuf(void *arg, void* buff, uint32_t btr, uint32_t* br);

const audio_lib_t remote_jmv_lib = 
{
	NULL,
	remote_jmv_lib_process,
	remote_jmv_lib_seek,
	remote_jmv_lib_free,
	remote_jmv_lib_transfer_complete_cb,
	remote_jmv_lib_transfer_half_cb
};

void remote_jmv_lib_process(audio_lib_handle_t * hlib)
{
    static remote_jmv_lib_decode_state_t decode_state;
    static uint32_t bytesread;
    static uint32_t decode_start_tick = 0;
    
    JPEG_HandleTypeDef * hjpeg = (JPEG_HandleTypeDef *) hlib->img.codec;
    lwftp_session_t *s = hlib->remote.lwftp_session;
    
    switch (jmv_lib_state)
    {
        case JMV_LIB_READ_HEADER:
            switch (remote_read_state)
            {
                case AUDIO_LIB_READ_REQ:
                    /* Allocate net buffer */
                    hlib->remote.buffer.size = REMOTE_JMV_LIB_NETBUFF_SIZE;
                    hlib->remote.buffer.ptr = (uint8_t *) malloc(hlib->remote.buffer.size);
                    if(!hlib->remote.buffer.ptr)
                    {
                        hlib->err = AUDIO_LIB_MEMORY_ERROR;
                        return;
                    }
                    
                    /* Read file header */
                    LWFTP_FileRead(hlib, &jmv_codec.metadata.header, sizeof(remote_jmv_lib_header_t), &bytesread);
                    if (hlib->err) 
                        return;                    
                    
                    remote_read_state = AUDIO_LIB_READ_WAIT;
                    break;
                    
                case AUDIO_LIB_READ_WAIT:
                    if (s->control_pcb == NULL)
                    {
                        if(bytesread != sizeof(remote_jmv_lib_header_t))
                        {
                            hlib->err = AUDIO_LIB_READ_ERROR;
                            return;
                        }

                        if(jmv_codec.metadata.header.frame_jpeg == 0)
                        {
                            hlib->err = AUDIO_LIB_UNSUPPORTED_FORMAT;
                            return;		
                        }                       
                        
                        jmv_lib_state = JMV_LIB_READ_FRAMEVECT;
                        remote_read_state = AUDIO_LIB_READ_REQ;
                    }                
                    break;
            }        
            break;
            
        case JMV_LIB_READ_FRAMEVECT:
            switch (remote_read_state)
            {
                case AUDIO_LIB_READ_REQ:
                    /* Allocate memory for the frame size vector */
                    jmv_codec.metadata.frame.vect_size = jmv_codec.metadata.header.frame_vect_size;
                    jmv_codec.metadata.frame.vect_addr = (uint32_t *) malloc(jmv_codec.metadata.frame.vect_size);
                    if(!jmv_codec.metadata.frame.vect_addr)
                    {
                        hlib->err = AUDIO_LIB_MEMORY_ERROR;
                        return;
                    }

                    /* Load the frame size vector */
                    LWFTP_FileRead(hlib, jmv_codec.metadata.frame.vect_addr, jmv_codec.metadata.frame.vect_size, &bytesread);
                    if (hlib->err) 
                        return;                     
                
                    remote_read_state = AUDIO_LIB_READ_WAIT;
                    break;
                    
                case AUDIO_LIB_READ_WAIT:
                    if (s->control_pcb == NULL)
                    {
                        if(bytesread != jmv_codec.metadata.frame.vect_size)
                        {
                            hlib->err = AUDIO_LIB_READ_ERROR;
                            return;
                        }
                        
                        /* Allocate audio buffer */	
                        hlib->prv_data = &jmv_codec;
                        hlib->buffer = &jmv_buffer;
                        hlib->buffer->size = 2 * jmv_codec.metadata.header.audio_byterate / jmv_codec.metadata.header.frame_rate;
                        hlib->buffer->ptr = (uint8_t *) malloc(hlib->buffer->size);
                        if(!hlib->buffer->ptr)
                        {
                            hlib->err = AUDIO_LIB_MEMORY_ERROR;
                            return;
                        }
                        
                        hlib->time.total_time = jmv_codec.metadata.header.audio_totalsize / jmv_codec.metadata.header.audio_byterate;
                        
                        /* Use fast internal AXI SRAM buffer */
                        jmv_codec.jpeg_buff_size = jmv_codec.metadata.header.frame_maxsize;

                        if(jmv_codec.jpeg_buff_size > JPEG_FRAME_MAX_SIZE)
                        {
                            hlib->err = AUDIO_LIB_MEMORY_ERROR;
                            return;
                        }
                        jmv_codec.jpeg_buff_ptr = jpeg_frame_buff;                        
                        
                        jmv_lib_state = JMV_LIB_FILL_NETBUFF;
                        remote_read_state = AUDIO_LIB_READ_REQ;                        
                    }
                    break;
            }
            break;
            
        case JMV_LIB_FILL_NETBUFF:
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

                        jmv_lib_state = JMV_LIB_FIRST_FRAME;
                        remote_read_state = AUDIO_LIB_READ_REQ;
                    }
                    break;
            }
            break;
            
        case JMV_LIB_FIRST_FRAME:
            switch (decode_state)
            {
                case JMV_LIB_DECODE_INIT:
                    if (hlib->playback_state != AUDIO_LIB_STATE_WAIT)
                    {
                        jmv_codec.fifo_pos = 0;
                        
                        /* Init JPEG codec */
                        jmv_codec.frame_pos = 0;
                        jmv_codec.video_pos = 0;
                        jmv_codec.hjpegcodec.mode = JPEG_CODEC_MODE_DMA;
                        jpeg_decoder_init(&jmv_codec.hjpegcodec,
                                          hjpeg,
                                          hlib,
                                          JMV_FillReadBuf,
                                          jmv_codec.jpeg_buff_ptr,
                                          hlib->img.ptr);
                    }                    
                    
                    __disable_irq();
                    
                    /* Init variables */
                    fifo_write_ptr = 0;
                    fifo_read_ptr = 0;
                    fifo_bytes_available = 0;
                    
                    memset(hlib->buffer->ptr, 0, hlib->buffer->size);
                    
                    __enable_irq();
                    
                    jmv_codec.expected_play_cur = hlib->remote.play_cur;
                
                    /* Fill audio buffer */
                    JMV_FillAudioFIFO(hlib);
                    JMV_FillAudioFIFO(hlib);
                    
                    /* Copy fifo to dma buffer */
                    for(uint32_t i=0; i < hlib->buffer->size; i++) 
                    {
                        hlib->buffer->ptr[i] = audio_fifo[fifo_read_ptr];
                        fifo_read_ptr = (fifo_read_ptr + 1) % AUDIO_FIFO_SIZE;
                    }
                    fifo_bytes_available -= hlib->buffer->size;
                                      
                    /* JPEG decoding with DMA */
                    JMV_DecodeFrame(hlib);
                    decode_start_tick = HAL_GetTick();
                    
                    decode_state = JMV_LIB_DECODE_WAIT;
                    break;
                
                case JMV_LIB_DECODE_WAIT:
                    if (jmv_codec.hjpegcodec.state == JPEG_CODEC_STATE_IDLE)
                    {
                        hlib->img.invalidate = 1;
                        
                        if (hlib->playback_state != AUDIO_LIB_STATE_WAIT)
                        {
                            /* Get JPEG Info */
                            HAL_JPEG_GetInfo(hjpeg, &jmv_codec.jpeginfo);
    
                            hlib->img.width = jmv_codec.jpeginfo.ImageWidth;
                            hlib->img.height = jmv_codec.jpeginfo.ImageHeight;
                            
                            /* Init hardware */
                            if(BSP_AUDIO_OUT_Init(hlib->device, hlib->volume, jmv_codec.metadata.header.audio_samplerate) != 0)
                            {
                                hlib->err = AUDIO_LIB_HARDWARE_ERROR;
                                return;
                            }                            
    
                            /* Start hardware */
                            hlib->playback_state = AUDIO_LIB_STATE_PLAY;
                            BSP_AUDIO_OUT_Play((uint16_t *) hlib->buffer->ptr, hlib->buffer->size);
                        }
                        
                        jmv_lib_state = JMV_LIB_PROC_SAMPLES;
                        decode_state = JMV_LIB_DECODE_INIT;
                    }
                    else if(jmv_codec.hjpegcodec.state == JPEG_CODEC_STATE_ERROR)
                    {
                        hlib->err = AUDIO_LIB_READ_ERROR;
                    }
                    else if ((HAL_GetTick() - decode_start_tick) > REMOTE_JMV_FRAME_TIMEOUT)
                    {
                        /* Disable All Interrupts */
                        __HAL_JPEG_DISABLE_IT(hjpeg, JPEG_INTERRUPT_MASK);
                        
                        /* Call abort function to clean handle */
                        HAL_JPEG_Abort(hjpeg);
                        
                        /* JPEG decoding with DMA */
                        JMV_DecodeFrame(hlib);
                        
                        if (hlib->remote.buffer.empty == 1)
                        {
                            jmv_lib_state = JMV_LIB_FILL_NETBUFF;
                            remote_read_state = AUDIO_LIB_READ_REQ;                            
                        }
                        
                        decode_start_tick = HAL_GetTick();
                    }
                    break;
            }                        
            break;            
            
        case JMV_LIB_PROC_SAMPLES:
            switch(hlib->playback_state)
            {
                case AUDIO_LIB_STATE_PLAY:
                    hlib->time.elapsed_time = jmv_codec.frame_pos / jmv_codec.metadata.header.frame_rate;
                    
                    if(jmv_codec.frame_pos >= jmv_codec.metadata.header.frame_nb)
                    {
                        hlib->playback_state = AUDIO_LIB_STATE_STOP;
                    }
                    
                    /* Fill the fifo asynchronously whenever possible */
                    if(fifo_bytes_available <= (AUDIO_FIFO_SIZE - (hlib->buffer->size / 2))) 
                    {
                        JMV_FillAudioFIFO(hlib);
                    }

                    /* Decode one jpeg frame */
                    if(jmv_codec.video_pos <= jmv_codec.frame_pos) 
                    {
                        if (jmv_codec.hjpegcodec.state == JPEG_CODEC_STATE_IDLE)
                        {
                            hlib->img.invalidate = 1;
                            
                            /* JPEG decoding with DMA */
                            JMV_DecodeFrame(hlib);
                            
                            decode_start_tick = HAL_GetTick();
                        }
                        else if ((HAL_GetTick() - decode_start_tick) > REMOTE_JMV_FRAME_TIMEOUT)
                        {
                            /* Disable All Interrupts */
                            __HAL_JPEG_DISABLE_IT(hjpeg, JPEG_INTERRUPT_MASK);
                            
                            /* Call abort function to clean handle */
                            HAL_JPEG_Abort(hjpeg);
                            
                            /* JPEG decoding with DMA */
                            JMV_DecodeFrame(hlib);
                            
                            decode_start_tick = HAL_GetTick();
                        }                        
                    }
                    
                    if(jmv_codec.hjpegcodec.state == JPEG_CODEC_STATE_ERROR)
                    {
                        hlib->err = AUDIO_LIB_READ_ERROR;
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

void remote_jmv_lib_seek(audio_lib_handle_t * hlib)
{
    uint32_t frame_pos, ofs, idx;
    remote_jmv_lib_codec_t * codec = hlib->prv_data;
    JPEG_HandleTypeDef * hjpeg = (JPEG_HandleTypeDef *) hlib->img.codec;
    
    if (remote_read_state == AUDIO_LIB_READ_WAIT)
        return;

    frame_pos = (hlib->seek_pos * codec->metadata.header.frame_nb) / 100;

    /* Compute file offset */
    ofs = sizeof(remote_jmv_lib_header_t) + codec->metadata.header.frame_vect_size;
    for(idx = 0; idx < frame_pos; idx++)
    {
        ofs += (hlib->buffer->size / 2) + codec->metadata.frame.vect_addr[idx];
    }
    
    /* Refresh variables accordingly */
    codec->frame_pos = frame_pos;
    codec->video_pos = frame_pos;
    codec->fifo_pos  = frame_pos;    
    
    hlib->time.elapsed_time = codec->frame_pos / codec->metadata.header.frame_rate;

    /* Disable All Interrupts */
    __HAL_JPEG_DISABLE_IT(hjpeg, JPEG_INTERRUPT_MASK);    
    
    /* Call abort function to clean handle */
    HAL_JPEG_Abort(hjpeg);

    jmv_lib_state = JMV_LIB_FILL_NETBUFF;
    
    hlib->remote.fptr = ofs;
}

void remote_jmv_lib_free(audio_lib_handle_t * hlib)
{
	remote_jmv_lib_codec_t * codec;
	JPEG_HandleTypeDef * hjpeg;

    if (hlib)
    {
        hjpeg = (JPEG_HandleTypeDef *) hlib->img.codec;
        /* Disable All Interrupts */
        __HAL_JPEG_DISABLE_IT(hjpeg, JPEG_INTERRUPT_MASK);        
        /* Call abort function to clean handle */
        HAL_JPEG_Abort(hjpeg);        
        
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
            if (codec->metadata.frame.vect_addr)
            {
                free(codec->metadata.frame.vect_addr);
                codec->metadata.frame.vect_addr = NULL;
            }
            
            if (codec->jpeg_buff_ptr)
            {
                codec->jpeg_buff_ptr = NULL;
            }        
        }        
    }
    
	jmv_lib_state = JMV_LIB_READ_HEADER;
	remote_read_state = AUDIO_LIB_READ_REQ;    
}

void remote_jmv_lib_transfer_complete_cb(audio_lib_handle_t * hlib)
{
    remote_jmv_lib_codec_t * codec = hlib->prv_data;
    uint32_t btr = hlib->buffer->size / 2;
    uint8_t * dst = hlib->buffer->ptr + btr;    
    
    if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
    {
        if (fifo_bytes_available >= btr) 
        {
            for(uint32_t i = 0; i < btr; i++) 
            {
                dst[i] = audio_fifo[fifo_read_ptr];
                fifo_read_ptr = (fifo_read_ptr + 1) % AUDIO_FIFO_SIZE;
            }
            fifo_bytes_available -= btr;
        } 
        else 
        {
            memset(dst, 0, btr);
        }
        codec->frame_pos++;
    }    
}

void remote_jmv_lib_transfer_half_cb(audio_lib_handle_t * hlib)
{
    remote_jmv_lib_codec_t * codec = hlib->prv_data;
    uint32_t btr = hlib->buffer->size / 2;
    uint8_t * dst = hlib->buffer->ptr;    
    
	if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
	{
	    if (fifo_bytes_available >= btr) 
	    {
            for(uint32_t i = 0; i < btr; i++) 
            {
                dst[i] = audio_fifo[fifo_read_ptr];
                fifo_read_ptr = (fifo_read_ptr + 1) % AUDIO_FIFO_SIZE;
            }
            fifo_bytes_available -= btr;
        } 
	    else 
	    {
            memset(dst, 0, btr);
        }
        codec->frame_pos++;
	}    
}

static void JMV_DecodeFrame(audio_lib_handle_t * hlib)
{
    remote_jmv_lib_codec_t * codec = hlib->prv_data;
    JPEG_HandleTypeDef * hjpeg = (JPEG_HandleTypeDef *) hlib->img.codec;
    
    uint32_t audio_pkt_size = hlib->buffer->size / 2;
    uint32_t skip_size, jpeg_size, aligned_size, diff;
    
    /* Correcting pointers mismatch if previous jpg frame abort had occur */
    if (hlib->remote.play_cur != codec->expected_play_cur)
    {
        if (codec->expected_play_cur > hlib->remote.play_cur) {
            diff = codec->expected_play_cur - hlib->remote.play_cur;
            hlib->remote.buffer.rptr = (hlib->remote.buffer.rptr + diff) % hlib->remote.buffer.size;
        } else {
            diff = hlib->remote.play_cur - codec->expected_play_cur;
            hlib->remote.buffer.rptr = (hlib->remote.buffer.size + hlib->remote.buffer.rptr - diff) % hlib->remote.buffer.size;
        }
        hlib->remote.play_cur = codec->expected_play_cur;
    }    

    /* Advance the network buffer by discarding old frames if the CPU lagged */
    while (codec->video_pos < codec->frame_pos) {
        skip_size = audio_pkt_size + codec->metadata.frame.vect_addr[codec->video_pos];
        
        if ((hlib->remote.play_cur + skip_size) >= hlib->remote.fptr)
        {
            hlib->remote.buffer.empty = 1;
            return;
        }

        hlib->remote.buffer.rptr = (hlib->remote.buffer.rptr + skip_size) % hlib->remote.buffer.size;
        hlib->remote.play_cur += skip_size;
        codec->expected_play_cur += skip_size;
        codec->video_pos++;
    }    

    /* Ensure that audio + JPEG is available in the network buffer */
    jpeg_size = codec->metadata.frame.vect_addr[codec->video_pos];
    if ((hlib->remote.play_cur + audio_pkt_size + jpeg_size) >= hlib->remote.fptr) 
    {
        hlib->remote.buffer.empty = 1;
        return;
    }
    
    hlib->remote.buffer.rptr = (hlib->remote.buffer.rptr + audio_pkt_size) % hlib->remote.buffer.size;
    hlib->remote.play_cur += audio_pkt_size;
    
    /* Save pointers expected state after jpg decoding */
    codec->expected_play_cur = hlib->remote.play_cur + jpeg_size;

    /* Disable All Interrupts */
    __HAL_JPEG_DISABLE_IT(hjpeg, JPEG_INTERRUPT_MASK);      
  
    /* Call abort function to clean handle */
    HAL_JPEG_Abort(hjpeg);    
    
    /* JPEG decoding with DMA */
    aligned_size = (jpeg_size + 31) & ~31;
    jpeg_decoder_start(&codec->hjpegcodec, aligned_size);
    
    codec->video_pos++;
}

static void JMV_FillAudioFIFO(audio_lib_handle_t * hlib)
{
    remote_jmv_lib_codec_t * codec = hlib->prv_data;
    uint32_t audio_pkt_size = hlib->buffer->size / 2;
    uint32_t i, bytesread = 0, offset_bytes = 0;
    uint32_t orig_rptr, orig_play_cur;
    uint32_t free_space, bytesread2;
    
    if ((AUDIO_FIFO_SIZE - fifo_bytes_available) < audio_pkt_size) return;
    
    /* Calculate distance on the network (Skipping previous JPEGs) */
    for(i = codec->video_pos; i < codec->fifo_pos; i++) 
    {
        offset_bytes += audio_pkt_size + codec->metadata.frame.vect_addr[i];
    }
    
    /* Check if the audio we want has already been downloaded */
    if ((hlib->remote.play_cur + offset_bytes + audio_pkt_size) >= hlib->remote.fptr) return;
    
    /* Save network pointers (Rewind) */
    orig_rptr = hlib->remote.buffer.rptr;
    orig_play_cur = hlib->remote.play_cur;
    
    /* Skip to desired audio block */
    hlib->remote.buffer.rptr = (orig_rptr + offset_bytes) % hlib->remote.buffer.size;
    hlib->remote.play_cur = orig_play_cur + offset_bytes;
    
    /* Write to fifo */
    free_space = AUDIO_FIFO_SIZE - fifo_write_ptr;
    if (audio_pkt_size <= free_space) {
        JMV_FillReadBuf(hlib, &audio_fifo[fifo_write_ptr], audio_pkt_size, &bytesread);
        fifo_write_ptr = (fifo_write_ptr + bytesread) % AUDIO_FIFO_SIZE;
    } else {
        JMV_FillReadBuf(hlib, &audio_fifo[fifo_write_ptr], free_space, &bytesread);
        JMV_FillReadBuf(hlib, &audio_fifo[0], audio_pkt_size - free_space, &bytesread2);
        fifo_write_ptr = bytesread2;
        bytesread += bytesread2;
    }
    
    __disable_irq();
    fifo_bytes_available += bytesread;
    __enable_irq();
    
    /* Restore network pointers */
    hlib->remote.buffer.rptr = orig_rptr;
    hlib->remote.play_cur = orig_play_cur;

    codec->fifo_pos++;    
}

static uint8_t JMV_FillReadBuf(void *arg, void* buff, uint32_t btr, uint32_t* br)
{
    audio_lib_handle_t * hlib = (audio_lib_handle_t *) arg;
    
    if ((hlib->remote.buffer.rptr + btr) < hlib->remote.buffer.size)
    {
        memcpy(buff, 
               hlib->remote.buffer.ptr + hlib->remote.buffer.rptr, 
               btr);

        *br = btr;
        hlib->remote.buffer.rptr += btr;
    }
    else
    {
        memcpy(buff, 
               hlib->remote.buffer.ptr + hlib->remote.buffer.rptr, 
               hlib->remote.buffer.size - hlib->remote.buffer.rptr);

        *br = hlib->remote.buffer.size - hlib->remote.buffer.rptr;
        hlib->remote.buffer.rptr = 0;
        
        memcpy(buff + *br, 
               hlib->remote.buffer.ptr + hlib->remote.buffer.rptr, 
               btr - *br);
        
        hlib->remote.buffer.rptr += btr - *br;
        *br = btr;
    }
    
    hlib->remote.play_cur += btr;

    return 0;
}
