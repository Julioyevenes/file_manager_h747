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

#include "FLAC/stream_decoder.h"

/* Private types -------------------------------------------------------------*/
typedef enum
{
    FLAC_LIB_INIT = 0,
    FLAC_LIB_FILL_READBUFF,
    FLAC_LIB_FIRST_SAMPLE,
    FLAC_LIB_PROC_SAMPLES
} flac_lib_state_t;

typedef struct
{
    uint32_t                blocksize;
    uint32_t                channels;
    uint32_t                bps;
} flac_lib_frame_t;

typedef struct
{
    FLAC__uint64            total_samples;
    uint32_t                sample_rate;
    uint32_t                channels;
    uint32_t                bps;
} flac_lib_metadata_t;

typedef struct
{
    uint8_t                 play_idx;
    uint8_t                 deco_idx;
    
    uint32_t                bitrate;
    uint32_t                frame_size;

    FLAC__StreamDecoder *   h;
    
    flac_lib_frame_t        frame;
    flac_lib_metadata_t     metadata;
} flac_lib_codec_t;

/* Private constants ---------------------------------------------------------*/
#define FLAC_LIB_AUDIOBUF_SIZE          (0x9000)    /* 36864 byte */
#define FLAC_LIB_AUDIOBUF_NUM           2

#define FLAC_LIB_READBUFF_SIZE    (2 * 1024 * 1024)

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static audio_lib_buffer_t flac_buffer[FLAC_LIB_AUDIOBUF_NUM];
static audio_lib_read_state_t read_state;
static flac_lib_codec_t flac_codec;
static flac_lib_state_t flac_lib_state;

/* Private function prototypes -----------------------------------------------*/
void                                    flac_lib_process(audio_lib_handle_t * hlib);
void                                    flac_lib_seek(audio_lib_handle_t * hlib);
void                                    flac_lib_free(audio_lib_handle_t * hlib);
void                                    flac_lib_transfer_complete_cb(audio_lib_handle_t * hlib);
void                                    flac_lib_transfer_half_cb(audio_lib_handle_t * hlib);

static uint8_t                          FLAC_FillReadBuf(void *arg, void* buff, uint32_t btr, uint32_t* br);
static void                             FLAC_AddAudioBuf(audio_lib_handle_t * hlib);

static FLAC__StreamDecoderReadStatus    flac_lib_read_cb(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamDecoderWriteStatus   flac_lib_write_cb(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data);
static FLAC__StreamDecoderSeekStatus    flac_lib_seek_cb(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderTellStatus    flac_lib_tell_cb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderLengthStatus  flac_lib_length_cb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data);
static FLAC__bool                       flac_lib_eof_cb(const FLAC__StreamDecoder *decoder, void *client_data);
static void                             flac_lib_metadata_cb(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
static void                             flac_lib_error_cb(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

const audio_lib_t flac_lib = 
{
    NULL,
    flac_lib_process,
    flac_lib_seek,
    flac_lib_free,
    flac_lib_transfer_complete_cb,
    flac_lib_transfer_half_cb
};

void flac_lib_process(audio_lib_handle_t * hlib)
{
    static uint32_t bytesread;
    
    uint8_t * ptr;
    
    flac_lib_codec_t * codec = hlib->prv_data;
    audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);    
    
    switch (flac_lib_state)
    {
        case FLAC_LIB_INIT:
            /* Allocate net buffer */
            hlib->local.buffer.size = FLAC_LIB_READBUFF_SIZE;
            hlib->local.buffer.ptr = (uint8_t *) malloc(hlib->local.buffer.size);
            if(!hlib->local.buffer.ptr)
            {
                hlib->err = AUDIO_LIB_MEMORY_ERROR;
                return;
            }
            
            /* Register variables */
            hlib->prv_data = &flac_codec;
            hlib->buffer = &flac_buffer[0];

            flac_codec.h = FLAC__stream_decoder_new();
            if(flac_codec.h == 0)
            {
                hlib->err = AUDIO_LIB_MEMORY_ERROR;
                return;
            }
            
            FLAC__stream_decoder_init_stream(flac_codec.h, 
                                             flac_lib_read_cb,
                                             flac_lib_seek_cb,
                                             flac_lib_tell_cb,
                                             flac_lib_length_cb,
                                             flac_lib_eof_cb,
                                             flac_lib_write_cb, 
                                             flac_lib_metadata_cb,
                                             flac_lib_error_cb,
                                             hlib);

            ptr = (uint8_t *) malloc(FLAC_LIB_AUDIOBUF_SIZE);
            if(ptr == 0)
            {
                hlib->err = AUDIO_LIB_MEMORY_ERROR;
                return;
            }

            /* Init first half pcm buffer */
            flac_buffer[0].ptr = ptr;            

            flac_lib_state = FLAC_LIB_FILL_READBUFF;
            break;
            
        case FLAC_LIB_FILL_READBUFF:
            switch (read_state)
            {
                case AUDIO_LIB_READ_REQ:
                    /* Decode and playback from audio buf 0 */
                    flac_codec.deco_idx = flac_codec.play_idx = 0;
                    flac_buffer[0].empty = flac_buffer[1].empty = 1;                
                
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

                        flac_lib_state = FLAC_LIB_FIRST_SAMPLE;
                        read_state = AUDIO_LIB_READ_REQ;                        
                    }                
                    break;
            }
            break;
            
        case FLAC_LIB_FIRST_SAMPLE:
            /* Decode the first metadata block */
            if (FLAC__stream_decoder_process_until_end_of_metadata(flac_codec.h) == true)
            {
                /* Then decode the first audio frame */
                FLAC__stream_decoder_process_single(flac_codec.h);

                flac_codec.bitrate = (uint32_t) (((float) hlib->fp->fsize / flac_codec.metadata.total_samples) * flac_codec.metadata.sample_rate * 8);
                flac_codec.frame_size = flac_codec.frame.blocksize * flac_codec.frame.channels * (flac_codec.frame.bps / 8);

                /* Init second half pcm buffer */
                flac_buffer[1].ptr = flac_buffer[0].ptr + flac_codec.frame_size;

                /* Then decode the second audio frame */
                FLAC__stream_decoder_process_single(flac_codec.h);

                hlib->time.total_time = (hlib->fp->fsize * 8) / flac_codec.bitrate;
            }
            else
            {
                hlib->err = AUDIO_LIB_READ_ERROR;
                return;
            }

            /* Init hardware */
            if(BSP_AUDIO_OUT_Init(hlib->device, hlib->volume, flac_codec.metadata.sample_rate) != 0)
            {
                hlib->err = AUDIO_LIB_HARDWARE_ERROR;
                return;
            }

            /* Start hardware */
            audio_lib_buffer_t * buf = &(hlib->buffer[0]);
            hlib->playback_state = AUDIO_LIB_STATE_PLAY;
            BSP_AUDIO_OUT_Play((uint16_t *) buf->ptr, flac_codec.frame_size * 2);

            flac_lib_state = FLAC_LIB_PROC_SAMPLES;
            break;
            
        case FLAC_LIB_PROC_SAMPLES:
            switch(hlib->playback_state)
            {
                case AUDIO_LIB_STATE_PLAY:
                    hlib->time.elapsed_time = (hlib->local.play_cur * 8) / codec->bitrate;
                    
                    if(hlib->time.elapsed_time >= hlib->time.total_time)
                    {
                        hlib->playback_state = AUDIO_LIB_STATE_STOP;
                    }

                    /* wait for DMA transfer */
                    if(pbuf->empty == 1)
                    {
                        /* Decoder one frame */
                        if (FLAC__stream_decoder_process_single(codec->h) != true)
                        {
                            hlib->err = AUDIO_LIB_READ_ERROR;
                            return;
                        }
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

void flac_lib_seek(audio_lib_handle_t * hlib)
{
    flac_lib_codec_t * codec = hlib->prv_data;
    uint32_t ofs;

    BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
    flac_lib_state = FLAC_LIB_FILL_READBUFF;

    ofs = ((uint64_t)hlib->seek_pos * hlib->fp->fsize) / 100;

    f_lseek(hlib->fp, ofs);
    
    hlib->time.elapsed_time = ((uint64_t)hlib->fp->fptr * 8) / codec->bitrate;   
}

void flac_lib_free(audio_lib_handle_t * hlib)
{
    flac_lib_codec_t * codec;

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
    
        codec = hlib->prv_data;
        if (codec && \
            codec->h)
        {
            FLAC__stream_decoder_delete(codec->h);
            codec->h = NULL;
        }        
    }
    
    flac_lib_state = FLAC_LIB_INIT;
    read_state = AUDIO_LIB_READ_REQ;    
}

void flac_lib_transfer_complete_cb(audio_lib_handle_t * hlib)
{
    flac_lib_codec_t * codec = hlib->prv_data;
    audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->play_idx]);
        
    if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
    {
        /* Data in buffer[codec->play_idx] has been sent out */
        pbuf->empty = 1;
        pbuf->size = -1;

        /* Send the data in next audio buffer */
        codec->play_idx++;
        if (codec->play_idx == FLAC_LIB_AUDIOBUF_NUM)
            codec->play_idx = 0;

        if (pbuf->empty == 1) {
            /* If empty==1, it means read file+decoder is slower than playback
             (it will cause noise) or playback is over (it is ok). */;
        }
    }    
}

void flac_lib_transfer_half_cb(audio_lib_handle_t * hlib)
{
    flac_lib_codec_t * codec = hlib->prv_data;
    audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->play_idx]);
        
    if(hlib->playback_state == AUDIO_LIB_STATE_PLAY)
    {
        /* Data in buffer[codec->play_idx] has been sent out */
        pbuf->empty = 1;
        pbuf->size = -1;

        /* Send the data in next audio buffer */
        codec->play_idx++;
        if (codec->play_idx == FLAC_LIB_AUDIOBUF_NUM)
            codec->play_idx = 0;

        if (pbuf->empty == 1) {
            /* If empty==1, it means read file+decoder is slower than playback
             (it will cause noise) or playback is over (it is ok). */;
        }
    }    
}

/**
 * @brief  Read from netbuff.
 */
static uint8_t FLAC_FillReadBuf(void *arg, void* buff, uint32_t btr, uint32_t* br)
{
    audio_lib_handle_t * hlib = (audio_lib_handle_t *) arg;
    
    if ((hlib->local.play_cur + btr) >= hlib->fp->fptr)
    {
        hlib->local.buffer.empty = 1;
        *br = 0;
        return 0;
    }    
    
    if ((hlib->local.buffer.rptr + btr) < hlib->local.buffer.size)
    {
        memcpy(buff, 
               hlib->local.buffer.ptr + hlib->local.buffer.rptr, 
               btr);

        *br = btr;
        hlib->local.buffer.rptr += btr;
    }
    else
    {
        memcpy(buff, 
               hlib->local.buffer.ptr + hlib->local.buffer.rptr, 
               hlib->local.buffer.size - hlib->local.buffer.rptr);

        *br = hlib->local.buffer.size - hlib->local.buffer.rptr;
        hlib->local.buffer.rptr = 0;
        
        memcpy(buff + *br, 
               hlib->local.buffer.ptr + hlib->local.buffer.rptr, 
               btr - *br);
        
        hlib->local.buffer.rptr += btr - *br;
        *br = btr;
    }
    
    hlib->local.play_cur += btr;

    return 0;    
}

/**
 * @brief  Add an PCM frame to audio buf after decoding.
 */
static void FLAC_AddAudioBuf(audio_lib_handle_t * hlib)
{
    flac_lib_codec_t * codec = hlib->prv_data;
    audio_lib_buffer_t * pbuf = &(hlib->buffer[codec->deco_idx]);
    
    /* Mark the status to not-empty which means it is available to playback. */
    pbuf->empty = 0;
    pbuf->size = codec->frame_size;

    /* Point to the next buffer */
    codec->deco_idx ++;
    if (codec->deco_idx == FLAC_LIB_AUDIOBUF_NUM)
        codec->deco_idx = 0;
}

/**
 * @brief  Read data callback. Called when decoder needs more input data.
 */
static FLAC__StreamDecoderReadStatus flac_lib_read_cb(const FLAC__StreamDecoder* decoder, FLAC__byte buffer[], size_t* bytes, void* client_data)
{
    audio_lib_handle_t * hlib = client_data;
    uint32_t bytesread;
    
    if (*bytes > 0) 
    {
        // read data directly into buffer
        FLAC_FillReadBuf (hlib, buffer, *bytes * sizeof(FLAC__byte), &bytesread);
        *bytes = bytesread / sizeof(FLAC__byte);
        if (*bytes == 0) {
            // read error -> abort
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        else {
            // OK, continue decoding
            return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
        }
    }
    else {
        // decoder called but didn't want ay bytes -> abort
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }   
}

/**
 * @brief  Write callback. Called when decoder has decoded a single audio frame.
 */
static FLAC__StreamDecoderWriteStatus flac_lib_write_cb(const FLAC__StreamDecoder* decoder, const FLAC__Frame* frame, 
                                              const FLAC__int32* const buffer[], void* client_data)
{
    audio_lib_handle_t * hlib = client_data;
    flac_lib_codec_t * codec = hlib->prv_data;
    uint32_t * ptr = (uint32_t *) hlib->buffer[codec->deco_idx].ptr;
    uint32_t sample;

    if(codec->metadata.total_samples == 0) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    if(codec->metadata.channels != 2 || codec->metadata.bps != 16) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    codec->frame.blocksize = frame->header.blocksize;
    codec->frame.channels = frame->header.channels;
    codec->frame.bps = frame->header.bits_per_sample;

    for (sample = 0; sample < codec->frame.blocksize; sample++) {
        ptr[sample] = (buffer[0][sample] << 16) | (buffer[1][sample] & 0xFFFF);
    }
    FLAC_AddAudioBuf (hlib);

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

/**
 * @brief  Seek callback. Called when decoder needs to seek the stream.
 */
static FLAC__StreamDecoderSeekStatus flac_lib_seek_cb(const FLAC__StreamDecoder* decoder, FLAC__uint64 absolute_byte_offset, void* client_data)
{
    return FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;    
}

/**
 * @brief  Tell callback. Called when decoder wants to know current position of stream.
 */
static FLAC__StreamDecoderTellStatus flac_lib_tell_cb(const FLAC__StreamDecoder* decoder, FLAC__uint64* absolute_byte_offset, void* client_data)
{
    audio_lib_handle_t * hlib = client_data;
    
    if (hlib->local.play_cur < 0) {
        // seek failed
        return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
    }
    else {
        // update offset
        *absolute_byte_offset = (FLAC__uint64) hlib->local.play_cur;
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }   
}

/**
 * @brief  Length callback. Called when decoder wants total length of stream.
 */
static FLAC__StreamDecoderLengthStatus flac_lib_length_cb(const FLAC__StreamDecoder* decoder, FLAC__uint64* stream_length, void* client_data)
{
    audio_lib_handle_t * hlib = client_data;
    
    if (hlib->fp->fsize == 0) {
        // failed
        return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
    }
    else {
        // pass on length
        *stream_length = (FLAC__uint64) hlib->fp->fsize;
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }   
}

/**
 * @brief  EOF callback. Called when decoder wants to know if end of stream is reached.
 */
static FLAC__bool flac_lib_eof_cb(const FLAC__StreamDecoder* decoder, void* client_data)
{
    audio_lib_handle_t * hlib = client_data;
    
    return (hlib->local.play_cur == hlib->fp->fsize);
}

/**
 * @brief  Metadata callback. Called when decoder has decoded metadata.
 */
static void flac_lib_metadata_cb(const FLAC__StreamDecoder* decoder, const FLAC__StreamMetadata* metadata, void* client_data)
{
    audio_lib_handle_t * hlib = client_data;
    flac_lib_codec_t * codec = hlib->prv_data;
    
    if(metadata->type == FLAC__METADATA_TYPE_STREAMINFO)
    {
        /* save for later */
        codec->metadata.total_samples = metadata->data.stream_info.total_samples;
        codec->metadata.sample_rate = metadata->data.stream_info.sample_rate;
        codec->metadata.channels = metadata->data.stream_info.channels;
        codec->metadata.bps = metadata->data.stream_info.bits_per_sample;       
    }
}

/**
 * @brief  Error callback. Called when error occured during decoding.
 */
static void flac_lib_error_cb(const FLAC__StreamDecoder* decoder, FLAC__StreamDecoderErrorStatus status, void* client_data)
{
}
