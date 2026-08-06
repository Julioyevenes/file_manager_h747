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
#ifndef __LV_FILE_PLAYER_H
#define __LV_FILE_PLAYER_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "lvgl/lvgl.h"
#include "libdef.h"
   
#include "stm32h747i_discovery_audio.h"
#include "stm32h747i_discovery_lcd.h"

/* Exported types ------------------------------------------------------------*/
typedef enum
{
	player_wav = 4,	/* WAV audio */
	player_mp3,		/* MP3 audio */
	player_flac,	/* FLAC audio */
	player_jmv		/* JMV video */
} lv_fm_player_format_t;

/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
extern lv_obj_t * player_h;
extern lv_obj_t * player_spin_h;
extern audio_lib_handle_t hlib;

/* Exported functions --------------------------------------------------------*/
audio_lib_err_t lv_fm_local_player_start(lv_fm_player_format_t format, 
                                         uint16_t device, 
                                         FIL * fp);
audio_lib_err_t lv_fm_remote_player_start(lv_fm_player_format_t format, 
                                          uint16_t device,
                                          uint32_t fsize, 
                                          const char *user, const char *pass, 
                                          const char *name, const char *path,
                                          uint8_t *ip_addr, uint16_t port,
                                          lwftp_session_t * s);

#ifdef __cplusplus
}
#endif

#endif /* __LV_FILE_PLAYER_H */
