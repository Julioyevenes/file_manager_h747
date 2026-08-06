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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __LIBDEF_H
#define __LIBDEF_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "ff.h"
#include "lwftp.h"

/* Exported types ------------------------------------------------------------*/
typedef enum
{
	AUDIO_LIB_NO_ERROR 				= 0,
	AUDIO_LIB_READ_ERROR 			= -1,
	AUDIO_LIB_MEMORY_ERROR 			= -5,
	AUDIO_LIB_UNSUPPORTED_FORMAT 	= -7,
	AUDIO_LIB_HARDWARE_ERROR 		= -8,
	AUDIO_LIB_CONNECTION_ERROR      = -9
} audio_lib_err_t;

typedef enum
{
	AUDIO_LIB_STATE_IDLE = 0,
	AUDIO_LIB_STATE_WAIT,    
	AUDIO_LIB_STATE_INIT,    
	AUDIO_LIB_STATE_PLAY,
	AUDIO_LIB_STATE_PRERECORD,
	AUDIO_LIB_STATE_RECORD,  
	AUDIO_LIB_STATE_NEXT,  
	AUDIO_LIB_STATE_PREVIOUS,
	AUDIO_LIB_STATE_FORWARD,   
	AUDIO_LIB_STATE_BACKWARD,
	AUDIO_LIB_STATE_STOP,   
	AUDIO_LIB_STATE_PAUSE,
	AUDIO_LIB_STATE_RESUME,
	AUDIO_LIB_STATE_VOLUME_UP,
	AUDIO_LIB_STATE_VOLUME_DOWN,
	AUDIO_LIB_STATE_ERROR,  
} audio_lib_playback_state_t;

typedef enum
{
	AUDIO_LIB_BUFFER_OFFSET_NONE = 0,
	AUDIO_LIB_BUFFER_OFFSET_HALF,
	AUDIO_LIB_BUFFER_OFFSET_FULL,
} audio_lib_buffer_state_t;

typedef enum
{
    AUDIO_LIB_READ_REQ = 0,
    AUDIO_LIB_READ_WAIT
} audio_lib_read_state_t;

typedef struct
{
	uint32_t 					elapsed_time;
	uint32_t 					total_time;
} audio_lib_time_t;

typedef struct
{
	uint8_t 					empty : 1;
	uint8_t *					ptr;

	uint32_t					size;
	uint32_t                    rptr;
	
	audio_lib_buffer_state_t 	state;
	audio_lib_buffer_state_t    last_state;
} audio_lib_buffer_t;

typedef struct
{
    uint8_t                     invalidate : 1;
    uint8_t *                   ptr;

    uint16_t                    width;
    uint16_t                    height;

    void *                      codec;
} audio_lib_img_t;

typedef struct
{
    void * ptr;
    uint32_t btr;
    uint32_t br;
    
    uint32_t play_cur;
    
    audio_lib_buffer_t buffer;
    audio_lib_read_state_t read_state;    
} audio_lib_local_t;

typedef struct
{
    void * ptr;
    uint32_t btr;
    uint32_t * br;
    
    uint32_t fptr, fsize;
    
    uint32_t play_cur;
    
    char user[20], pass[20];
    char name[256], path[256];
    uint8_t ip_addr[4];
    uint16_t port;
    
    lwftp_session_t * lwftp_session;
    audio_lib_buffer_t buffer;
    audio_lib_read_state_t read_state;    
} audio_lib_remote_t;

typedef struct
{
	uint8_t 					active : 1;
	uint8_t 					volume, seek_pos;
	
	uint16_t                    device;
	
	void *						prv_data;
	
	FIL * 						fp;

	audio_lib_err_t				err;
	audio_lib_buffer_t *		buffer;
	audio_lib_playback_state_t 	playback_state;
	audio_lib_time_t			time;
	audio_lib_img_t             img;
	audio_lib_local_t           local;
	audio_lib_remote_t          remote;
} audio_lib_handle_t;

typedef struct
{
	void (* lib_start)					(audio_lib_handle_t *);
	void (* lib_process)				(audio_lib_handle_t *);
	void (* lib_seek)                   (audio_lib_handle_t *);
	void (* lib_free)					(audio_lib_handle_t *);
	void (* lib_transfer_complete_cb)	(audio_lib_handle_t *);
	void (* lib_transfer_half_cb)		(audio_lib_handle_t *);	
} audio_lib_t;

/* Exported constants --------------------------------------------------------*/
#define AUDIO_LIB_MIN_READSIZE  (64 * 1024)
#define AUDIO_FIFO_SIZE         (80 * 1024)
#define JPEG_FRAME_MAX_SIZE     (192 * 1024)

/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
extern uint8_t audio_fifo[AUDIO_FIFO_SIZE];
extern uint8_t jpeg_frame_buff[JPEG_FRAME_MAX_SIZE];

extern volatile uint32_t fifo_write_ptr;
extern volatile uint32_t fifo_read_ptr;
extern volatile uint32_t fifo_bytes_available;

/* Exported functions --------------------------------------------------------*/
void LWFTP_FileRead(audio_lib_handle_t * hlib, void* buff, uint32_t btr, uint32_t* br);

#ifdef __cplusplus
}
#endif

#endif /* __LIBDEF_H */
