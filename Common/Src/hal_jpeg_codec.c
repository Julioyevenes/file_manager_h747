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
#include "hal_jpeg_codec.h"
#include "diskio.h"

/* Private types -------------------------------------------------------------*/
/* Private constants ---------------------------------------------------------*/
#define JPEG_CODEC_SIZE_IN  4096
#define JPEG_CODEC_SIZE_OUT (768 * 64)

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static uint8_t jpeg_inbuff[JPEG_CODEC_SIZE_IN * 2];
static uint8_t jpeg_outbuff[JPEG_CODEC_SIZE_OUT * 2];
static jpeg_codec_handle_t * hjpegcodec;

/* Private function prototypes -----------------------------------------------*/

jpeg_codec_err_t jpeg_decoder_init(jpeg_codec_handle_t * hcodec, 
                                   JPEG_HandleTypeDef * hjpeg,
                                   void * arg,
                                   uint8_t (* read_cb) (void *arg, void* buff, uint32_t btr, uint32_t* br), 
                                   uint8_t * src_addr, uint8_t * dst_addr)
{
    hjpegcodec = hcodec;

    if(hcodec->mode != hcodec->last_mode)
    {
        if(hcodec->mode == JPEG_CODEC_MODE_IT)
        {
            /* Init The JPEG Look Up Tables used for YCbCr to RGB conversion */
            JPEG_InitColorTables();
        }

        /* Init the HAL JPEG driver */
        hjpeg->Instance = JPEG;
        HAL_JPEG_DeInit(hjpeg);
        HAL_JPEG_Init(hjpeg);

        /* Save jpeg codec mode */
        hcodec->last_mode = hcodec->mode;
    }

    hcodec->frame_addr = dst_addr;
    hcodec->read_cb = read_cb;
    hcodec->hjpeg = hjpeg;
    hcodec->arg = arg;
    
    if(src_addr != NULL) /* User jpeg source buffer */
    {
        hcodec->in_buf[0].ptr = src_addr;
    }
    else /* Internal jpeg source buffer */
    {
        hcodec->in_buf[0].ptr = (uint8_t *) &jpeg_inbuff;
        hcodec->in_buf[1].ptr = ((uint8_t *) &jpeg_inbuff) + JPEG_CODEC_SIZE_IN;
    }

    if(hcodec->mode == JPEG_CODEC_MODE_IT)
    {
        hcodec->out_buf[0].ptr = (uint8_t *) &jpeg_outbuff;
        hcodec->out_buf[1].ptr = ((uint8_t *) &jpeg_outbuff) + JPEG_CODEC_SIZE_OUT;
    }

    return JPEG_CODEC_NO_ERROR;
}

jpeg_codec_err_t jpeg_decoder_start(jpeg_codec_handle_t * hcodec, uint32_t src_size)
{
    uint32_t i;

    hcodec->in_read_idx = hcodec->in_write_idx = 0;
    hcodec->out_read_idx = hcodec->out_write_idx = 0;
    hcodec->in_pause = hcodec->out_pause = 0;
    hcodec->mcu_total = hcodec->mcu_value = 0;
    hcodec->frame_ptr = hcodec->frame_addr;

    for(i = 0; i < 2; i++)
    {
        hcodec->in_buf[i].full = hcodec->in_buf[i].size = 0;

        if(hcodec->mode == JPEG_CODEC_MODE_IT)
        {
            hcodec->out_buf[i].full = hcodec->out_buf[i].size = 0;
        }
    }

    hcodec->state = JPEG_CODEC_STATE_IDLE;

    /* Read from JPG file and fill input buffers */
    if(hcodec->in_buf[1].ptr == NULL) /* User jpeg source buffer */
    {       
        if(hcodec->read_cb (hcodec->arg,
                            hcodec->in_buf[0].ptr,
                            src_size,
                            &hcodec->in_buf[0].size) == FR_OK)
        {
            hcodec->in_buf[0].full = 1;
        }
        else
        {
            return JPEG_CODEC_READ_ERROR;
        }
    }
    else /* Internal jpeg source buffer */
    {
        for(i = 0; i < 2; i++)
        {
            if(hcodec->read_cb (hcodec->arg,
                                hcodec->in_buf[i].ptr,
                                JPEG_CODEC_SIZE_IN,
                                &hcodec->in_buf[i].size) == FR_OK)
            {
                hcodec->in_buf[i].full = 1;
            }
            else
            {
                return JPEG_CODEC_READ_ERROR;
            }
        }
    }

    if(hcodec->mode == JPEG_CODEC_MODE_IT)
    {
        /* Start JPEG decoding with IT method */
        HAL_JPEG_Decode_IT(hcodec->hjpeg,
                           hcodec->in_buf[0].ptr,
                           hcodec->in_buf[0].size,
                           hcodec->out_buf[0].ptr,
                           JPEG_CODEC_SIZE_OUT);
    }
    else
    {
        /* Start JPEG decoding with DMA method */
        HAL_JPEG_Decode_DMA(hcodec->hjpeg,
                            hcodec->in_buf[0].ptr,
                            hcodec->in_buf[0].size,
                            hcodec->frame_ptr,
                            JPEG_CODEC_SIZE_OUT);
    }

    hcodec->state = JPEG_CODEC_STATE_INFO;
    return JPEG_CODEC_NO_ERROR;
}

jpeg_codec_err_t jpeg_decoder_io(jpeg_codec_handle_t * hcodec)
{
    if(hcodec->in_buf[hcodec->in_write_idx].full == 0 && \
       hjpegcodec->in_buf[1].ptr != NULL)
    {
        if(hcodec->read_cb (hcodec->arg,
                            hcodec->in_buf[hcodec->in_write_idx].ptr,
                            JPEG_CODEC_SIZE_IN,
                            &hcodec->in_buf[hcodec->in_write_idx].size) == FR_OK)
        {
            hcodec->in_buf[hcodec->in_write_idx].full = 1;
        }
        else
        {
            return JPEG_CODEC_READ_ERROR;
        } 

        if((hcodec->in_pause == 1) && (hcodec->in_write_idx == hcodec->in_read_idx))
        {
            hcodec->in_pause = 0;
            HAL_JPEG_ConfigInputBuffer(hcodec->hjpeg,
                                       hcodec->in_buf[hcodec->in_read_idx].ptr,
                                       hcodec->in_buf[hcodec->in_read_idx].size);
            HAL_JPEG_Resume(hcodec->hjpeg, JPEG_PAUSE_RESUME_INPUT);
        } 

        hcodec->in_write_idx++;
        if(hcodec->in_write_idx >= 2)
        {
            hcodec->in_write_idx = 0;
        }      
    }

    if(hcodec->mode == JPEG_CODEC_MODE_IT)
    {
        uint32_t data_converted;

        if(hcodec->out_buf[hcodec->out_read_idx].full == 1)
        {
            hcodec->mcu_value += hcodec->color_fn(hcodec->out_buf[hcodec->out_read_idx].ptr,
                                                  hcodec->frame_addr,
                                                  hcodec->mcu_value,
                                                  hcodec->out_buf[hcodec->out_read_idx].size,
                                                  &data_converted);

            hcodec->out_buf[hcodec->out_read_idx].full = 0;
            hcodec->out_buf[hcodec->out_read_idx].size = 0;

            hcodec->out_read_idx++;
            if(hcodec->out_read_idx >= 2)
            {
                hcodec->out_read_idx = 0;
            }

            if(hcodec->mcu_value == hcodec->mcu_total)
            {
                hcodec->state = JPEG_CODEC_STATE_IDLE;
            }
        }
        else if((hcodec->out_pause == 1) && \
                (hcodec->out_buf[hcodec->out_read_idx].full == 0) &&\
                (hcodec->out_buf[hcodec->out_write_idx].full == 0))
        {
            hcodec->out_pause = 0;
            HAL_JPEG_Resume(hcodec->hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
        }
    }

    return JPEG_CODEC_NO_ERROR;
}

void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef * hjpeg, JPEG_ConfTypeDef * pInfo)
{
    if(hjpegcodec->mode == JPEG_CODEC_MODE_IT)
    {
        JPEG_GetDecodeColorConvertFunc(pInfo, &(hjpegcodec->color_fn), &(hjpegcodec->mcu_total));
    }

    hjpegcodec->state = JPEG_CODEC_STATE_IMG;
}

void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef * hjpeg, uint32_t NbDecodedData)
{
	if(NbDecodedData == hjpegcodec->in_buf[hjpegcodec->in_read_idx].size)
	{
		hjpegcodec->in_buf[hjpegcodec->in_read_idx].full = 0;
		hjpegcodec->in_buf[hjpegcodec->in_read_idx].size = 0;

		if(hjpegcodec->in_buf[1].ptr != NULL) /* Internal jpeg source buffer */
        {
            hjpegcodec->in_read_idx++;
            if(hjpegcodec->in_read_idx >= 2)
            {
                hjpegcodec->in_read_idx = 0;
            }
        }

		if(hjpegcodec->in_buf[hjpegcodec->in_read_idx].full == 0)
		{
			HAL_JPEG_Pause(hjpegcodec->hjpeg, JPEG_PAUSE_RESUME_INPUT);
			hjpegcodec->in_pause = 1;
		}
		else
		{
			HAL_JPEG_ConfigInputBuffer(hjpegcodec->hjpeg,
									   hjpegcodec->in_buf[hjpegcodec->in_read_idx].ptr,
									   hjpegcodec->in_buf[hjpegcodec->in_read_idx].size);
		}
	}
	else
	{
		HAL_JPEG_ConfigInputBuffer(hjpegcodec->hjpeg,
								   hjpegcodec->in_buf[hjpegcodec->in_read_idx].ptr + NbDecodedData,
								   hjpegcodec->in_buf[hjpegcodec->in_read_idx].size - NbDecodedData);
	}
}

void HAL_JPEG_DataReadyCallback (JPEG_HandleTypeDef * hjpeg, uint8_t * pDataOut, uint32_t OutDataLength)
{
    if(hjpegcodec->mode == JPEG_CODEC_MODE_IT)
    {
        hjpegcodec->out_buf[hjpegcodec->out_write_idx].full = 1;
        hjpegcodec->out_buf[hjpegcodec->out_write_idx].size = OutDataLength;

        hjpegcodec->out_write_idx++;
        if(hjpegcodec->out_write_idx >= 2)
        {
            hjpegcodec->out_write_idx = 0;
        }

        if(hjpegcodec->out_buf[hjpegcodec->out_write_idx].full != 0)
        {
            HAL_JPEG_Pause(hjpegcodec->hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
            hjpegcodec->out_pause = 1;
        }
        HAL_JPEG_ConfigOutputBuffer(hjpegcodec->hjpeg,
                                    hjpegcodec->out_buf[hjpegcodec->out_write_idx].ptr,
                                    JPEG_CODEC_SIZE_OUT);
    }
    else
    {
        hjpegcodec->frame_ptr += OutDataLength;
        HAL_JPEG_ConfigOutputBuffer(hjpegcodec->hjpeg,
                                    hjpegcodec->frame_ptr,
                                    JPEG_CODEC_SIZE_OUT);
    }
}

void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef * hjpeg)
{
    hjpegcodec->state = JPEG_CODEC_STATE_ERROR;
}

void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef * hjpeg)
{
    if(hjpegcodec->mode == JPEG_CODEC_MODE_DMA)
    {
        hjpegcodec->state = JPEG_CODEC_STATE_IDLE;
    }
}

void HAL_JPEG_MspInit(JPEG_HandleTypeDef *hjpeg)
{
    /* Enable JPEG clock */
    __HAL_RCC_JPGDECEN_CLK_ENABLE();

    HAL_NVIC_SetPriority(JPEG_IRQn, 0x07, 0x0F);
    HAL_NVIC_EnableIRQ(JPEG_IRQn);

    if(hjpegcodec->mode == JPEG_CODEC_MODE_DMA)
    {
        static MDMA_HandleTypeDef   hmdmaIn;
        static MDMA_HandleTypeDef   hmdmaOut;

        /* Enable MDMA clock */
        __HAL_RCC_MDMA_CLK_ENABLE();

        /* Input MDMA */
        /* Set the parameters to be configured */
        hmdmaIn.Init.Priority           = MDMA_PRIORITY_HIGH;
        hmdmaIn.Init.Endianness         = MDMA_LITTLE_ENDIANNESS_PRESERVE;
        hmdmaIn.Init.SourceInc          = MDMA_SRC_INC_BYTE;
        hmdmaIn.Init.DestinationInc     = MDMA_DEST_INC_DISABLE;
        hmdmaIn.Init.SourceDataSize     = MDMA_SRC_DATASIZE_BYTE;
        hmdmaIn.Init.DestDataSize       = MDMA_DEST_DATASIZE_WORD;
        hmdmaIn.Init.DataAlignment      = MDMA_DATAALIGN_PACKENABLE;
        hmdmaIn.Init.SourceBurst        = MDMA_SOURCE_BURST_32BEATS;
        hmdmaIn.Init.DestBurst          = MDMA_DEST_BURST_8BEATS;
        hmdmaIn.Init.SourceBlockAddressOffset = 0;
        hmdmaIn.Init.DestBlockAddressOffset  = 0;

        /*Using JPEG Input FIFO Threshold as a trigger for the MDMA*/
        hmdmaIn.Init.Request = MDMA_REQUEST_JPEG_INFIFO_TH; /* Set the MDMA HW trigger to JPEG Input FIFO Threshold flag*/
        hmdmaIn.Init.TransferTriggerMode = MDMA_BUFFER_TRANSFER;
        hmdmaIn.Init.BufferTransferLength = 32; /*Set the MDMA buffer size to the JPEG FIFO threshold size i.e 32 bytes (8 words)*/

        hmdmaIn.Instance = MDMA_Channel2;

        /* Associate the DMA handle */
        __HAL_LINKDMA(hjpeg, hdmain, hmdmaIn);

        /* DeInitialize the DMA Stream */
        HAL_MDMA_DeInit(&hmdmaIn);
        /* Initialize the DMA stream */
        HAL_MDMA_Init(&hmdmaIn);

        /* Output MDMA */
        /* Set the parameters to be configured */
        hmdmaOut.Init.Priority        = MDMA_PRIORITY_VERY_HIGH;
        hmdmaOut.Init.Endianness      = MDMA_LITTLE_ENDIANNESS_PRESERVE;
        hmdmaOut.Init.SourceInc       = MDMA_SRC_INC_DISABLE;
        hmdmaOut.Init.DestinationInc  = MDMA_DEST_INC_BYTE;
        hmdmaOut.Init.SourceDataSize  = MDMA_SRC_DATASIZE_WORD;
        hmdmaOut.Init.DestDataSize    = MDMA_DEST_DATASIZE_BYTE;
        hmdmaOut.Init.DataAlignment   = MDMA_DATAALIGN_PACKENABLE;
        hmdmaOut.Init.SourceBurst     = MDMA_SOURCE_BURST_8BEATS;
        hmdmaOut.Init.DestBurst       = MDMA_DEST_BURST_32BEATS;
        hmdmaOut.Init.SourceBlockAddressOffset = 0;
        hmdmaOut.Init.DestBlockAddressOffset  = 0;

        /*Using JPEG Output FIFO Threshold as a trigger for the MDMA*/
        hmdmaOut.Init.Request              = MDMA_REQUEST_JPEG_OUTFIFO_TH; /* Set the MDMA HW trigger to JPEG Output FIFO Threshold flag*/
        hmdmaOut.Init.TransferTriggerMode  = MDMA_BUFFER_TRANSFER;
        hmdmaOut.Init.BufferTransferLength = 32; /*Set the MDMA buffer size to the JPEG FIFO threshold size i.e 32 bytes (8 words)*/

        hmdmaOut.Instance = MDMA_Channel1;
        /* DeInitialize the DMA Stream */
        HAL_MDMA_DeInit(&hmdmaOut);
        /* Initialize the DMA stream */
        HAL_MDMA_Init(&hmdmaOut);

        /* Associate the DMA handle */
        __HAL_LINKDMA(hjpeg, hdmaout, hmdmaOut);

        HAL_NVIC_SetPriority(MDMA_IRQn, 0x08, 0x0F);
        HAL_NVIC_EnableIRQ(MDMA_IRQn);
    }
}

void JPEG_IRQHandler(void)
{
	HAL_JPEG_IRQHandler(hjpegcodec->hjpeg);
}

void MDMA_IRQHandler()
{
	HAL_MDMA_IRQHandler(hjpegcodec->hjpeg->hdmain);
	HAL_MDMA_IRQHandler(hjpegcodec->hjpeg->hdmaout);
}
