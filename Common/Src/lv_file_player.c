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
#include "lv_file_player.h"

#include "lvgl_port.h"

#include "wav_lib.h"
#include "mp3_lib.h"
#include "flac_lib.h"
#include "jmv_lib.h"

#include "remote_wav_lib.h"
#include "remote_mp3_lib.h"
#include "remote_flac_lib.h"
#include "remote_jmv_lib.h"

/* Private types -------------------------------------------------------------*/
/* Private constants ---------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
#define LV_FM_PLAYER_FREE(ptr)		if (ptr != NULL) \
									{ \
										free(ptr); ptr = NULL; \
									}

#define LV_FM_PLAYER_OBJ_DEL(ptr)	if (ptr != NULL) \
									{ \
										lv_obj_del(ptr); ptr = NULL; \
									}


#define LV_FM_PLAYER_TASK_DEL(ptr)	if (ptr != NULL) \
									{ \
										lv_task_del(ptr); ptr = NULL; \
									}

/* Private variables ---------------------------------------------------------*/
extern lv_task_t * autoplay_task;

lv_fm_player_format_t player_format;

audio_lib_handle_t hlib = {.volume = 50};
audio_lib_t * audio_lib;

lv_task_t * player_process_task;
lv_task_t * player_seek_task;
lv_task_t * player_volume_task;
lv_task_t * player_local_buffer_task;
lv_task_t * player_remote_buffer_task;
lv_task_t * player_spin_task;

lv_obj_t * player_h;
lv_obj_t * volume_slider;
lv_obj_t * player_slider;
lv_obj_t * player_img;
lv_obj_t * player_ppbtn;
lv_obj_t * player_spin_h;
lv_obj_t * player_spin_lb;

lv_img_dsc_t img_dsc;

uint8_t last_volume;

static lv_style_t style_hide;
static lv_style_t style_player_cont;

/* Private function prototypes -----------------------------------------------*/
static void lv_fm_player_create(void);
static void lv_fm_player_spin_create(void);

static void lv_fm_player_delete(void);

static void lv_fm_player_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void lv_fm_player_volume_slider_event_cb(lv_obj_t * slider, lv_event_t e);
static void lv_fm_player_seek_slider_event_cb(lv_obj_t * slider, lv_event_t e);
static void lv_fm_player_img_event_cb(lv_obj_t * obj, lv_event_t e);
static void lv_fm_player_spin_event_cb(lv_obj_t * obj, lv_event_t e);

void 		lv_fm_player_process_task(lv_task_t * task);
void 		lv_fm_player_seek_task(lv_task_t * task);
void 		lv_fm_player_volume_task(lv_task_t * task);
void        lv_fm_player_local_buffer_task(lv_task_t * task);
void        lv_fm_player_remote_buffer_task(lv_task_t * task);
void        lv_fm_player_spin_task(lv_task_t * task);

audio_lib_err_t lv_fm_local_player_start(lv_fm_player_format_t format, 
                                         uint16_t device, 
                                         FIL * fp)
{
	uint32_t offset;
	
	player_format = format;

    /* Set image raw data address */
	offset = BSP_LCD_GetXSize() * BSP_LCD_GetYSize() * sizeof(lv_color_t) * 2;
    hlib.img.ptr = (uint8_t *) (LCD_FB_START_ADDRESS + offset);
    hlib.img.codec = &lvgl_img_hjpeg;
	
	hlib.active = 1;
	hlib.device = device;
	hlib.fp = fp;
	hlib.err = AUDIO_LIB_NO_ERROR;

	switch(format)
	{
		case player_wav:
			audio_lib = &wav_lib;
			break;

		case player_mp3:
			audio_lib = &mp3_lib;
			break;

		case player_flac:
			audio_lib = &flac_lib;
			break;
			
		case player_jmv:
			audio_lib = &jmv_lib;
			break;			

		default:
			audio_lib = NULL;
			f_close (hlib.fp);
			hlib.err = AUDIO_LIB_UNSUPPORTED_FORMAT;
			break;
	}

	if(audio_lib != NULL)
	{
        lv_fm_player_spin_create();
        player_local_buffer_task = lv_task_create(lv_fm_player_local_buffer_task, 1, LV_TASK_PRIO_MID, &hlib);
        player_process_task = lv_task_create(lv_fm_player_process_task, 1, LV_TASK_PRIO_MID, &hlib);
	}

	return hlib.err;
}

audio_lib_err_t lv_fm_remote_player_start(lv_fm_player_format_t format, 
                                          uint16_t device,
                                          uint32_t fsize, 
                                          const char *user, const char *pass, 
                                          const char *name, const char *path,
                                          uint8_t *ip_addr, uint16_t port,
                                          lwftp_session_t * s)
{
    uint32_t offset;
    
    player_format = format;

    /* Set image raw data address */
    offset = BSP_LCD_GetXSize() * BSP_LCD_GetYSize() * sizeof(lv_color_t) * 2;
    hlib.img.ptr = (uint8_t *) (LCD_FB_START_ADDRESS + offset);
    hlib.img.codec = &lvgl_img_hjpeg;
    
    hlib.active = 1;
    hlib.device = device;
    hlib.err = AUDIO_LIB_NO_ERROR;
    
    hlib.remote.fsize = fsize;
    strcpy(hlib.remote.user, user);
    strcpy(hlib.remote.pass, pass);
    strcpy(hlib.remote.name, name);
    strcpy(hlib.remote.path, path);
    
    memcpy(hlib.remote.ip_addr, ip_addr, sizeof(hlib.remote.ip_addr));
    hlib.remote.port = port;
    
    hlib.remote.lwftp_session = s;
    
    switch(format)
    {
        case player_wav:
            audio_lib = &remote_wav_lib;
            break;          

        case player_mp3:
            audio_lib = &remote_mp3_lib;
            break;

        case player_flac:
            audio_lib = &remote_flac_lib;
            break;            
            
        case player_jmv:
            audio_lib = &remote_jmv_lib;
            break;            
            
        default:
            audio_lib = NULL;
            hlib.err = AUDIO_LIB_UNSUPPORTED_FORMAT;
            break;        
    }
    
    if(audio_lib != NULL)
    {
        lv_fm_player_spin_create();
        player_remote_buffer_task = lv_task_create(lv_fm_player_remote_buffer_task, 1, LV_TASK_PRIO_MID, &hlib);
        player_process_task = lv_task_create(lv_fm_player_process_task, 1, LV_TASK_PRIO_MID, &hlib);       
    }
    
    return hlib.err;
}

static void lv_fm_player_create(void)
{
    lv_obj_t * img, * cont;
    
    lv_style_init(&style_hide);
    lv_style_set_bg_opa(&style_hide, LV_STATE_DEFAULT, 0);
    
    lv_style_init(&style_player_cont);
    lv_style_set_bg_color(&style_player_cont, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_border_width(&style_player_cont, LV_STATE_DEFAULT, 0);
    lv_style_set_radius(&style_player_cont, LV_STATE_DEFAULT, 0);
    
    player_h = lv_cont_create(lv_scr_act(), NULL);
    lv_cont_set_layout(player_h, LV_LAYOUT_OFF);
    lv_obj_set_click(player_h, true);
    lv_obj_set_size(player_h, BSP_LCD_GetXSize(), BSP_LCD_GetYSize());
    lv_obj_add_style(player_h, LV_CONT_PART_MAIN, &style_player_cont);
    lv_obj_set_event_cb(player_h, lv_fm_player_img_event_cb);

    if(player_format == player_jmv)
    {
        img_dsc.header.cf = LV_IMG_CF_USER_ENCODED_0;
        img_dsc.header.css = 0x2;
        img_dsc.header.w = hlib.img.width;
        img_dsc.header.h = hlib.img.height;
        img_dsc.data_size = hlib.img.width * hlib.img.height * sizeof(lv_color_t);
        img_dsc.data = hlib.img.ptr;

        player_img = lv_img_create(player_h, NULL);
        lv_img_set_src(player_img, &img_dsc);
        lv_obj_set_click(player_img, true);
        lv_obj_align(player_img, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_event_cb(player_img, lv_fm_player_img_event_cb);
        
        cont = player_img;
    }
    else
    {
        cont = player_h;
    }

    player_ppbtn = lv_btn_create(cont, NULL);
    img = lv_img_create(player_ppbtn, NULL);
    lv_img_set_src(img, LV_SYMBOL_PAUSE);
    lv_btn_set_fit2(player_ppbtn, LV_FIT_TIGHT, LV_FIT_TIGHT);
    lv_obj_align(player_ppbtn, cont, LV_ALIGN_CENTER, 0, 0);    
    lv_obj_set_event_cb(player_ppbtn, lv_fm_player_btn_event_cb);
    
    if (hlib.device == OUTPUT_DEVICE_HEADPHONE)
    {
        volume_slider = lv_slider_create(cont, NULL);
        lv_slider_set_range(volume_slider, 0, 80);
        lv_slider_set_value(volume_slider, hlib.volume, LV_ANIM_OFF);
        lv_obj_set_width(volume_slider, LV_DPI / 10); 
        lv_obj_set_height(volume_slider, lv_obj_get_height_fit(cont) - LV_DPX(100));
        lv_obj_align(volume_slider, cont, LV_ALIGN_IN_RIGHT_MID, - LV_DPX(10), 0);    
        lv_obj_set_event_cb(volume_slider, lv_fm_player_volume_slider_event_cb);
    }

    player_slider = lv_slider_create(cont, NULL);
    lv_slider_set_range(player_slider, 0, 100);
    lv_obj_set_width(player_slider, lv_obj_get_width_fit(cont));
    lv_obj_set_height(player_slider, LV_DPI / 10);    
    lv_obj_align(player_slider, cont, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
    lv_obj_set_event_cb(player_slider, lv_fm_player_seek_slider_event_cb);
    lv_obj_add_style(player_slider, LV_SLIDER_PART_KNOB, &style_hide);
    
    if(player_format == player_jmv)
    {
        lv_obj_set_hidden(player_ppbtn, true);        
        lv_obj_set_hidden(player_slider, true);
        
        if (hlib.device == OUTPUT_DEVICE_HEADPHONE)
        {
            lv_obj_set_hidden(volume_slider, true);
        }
    }
}

static void lv_fm_player_spin_create(void)
{
    lv_obj_t * spin;
    
    if (player_spin_h) return;
    
    player_spin_h = lv_cont_create(lv_scr_act(), NULL);
    lv_cont_set_layout(player_spin_h, LV_LAYOUT_PRETTY_MID);
    lv_cont_set_fit2(player_spin_h, LV_FIT_TIGHT, LV_FIT_TIGHT);
    lv_obj_set_event_cb(player_spin_h, lv_fm_player_spin_event_cb);
    lv_obj_set_click(player_spin_h, true);

    spin = lv_spinner_create(player_spin_h, NULL);
    lv_obj_set_size(spin, LV_DPI / 2, LV_DPI / 2);
    lv_obj_set_style_local_line_width(spin, LV_SPINNER_PART_BG, LV_STATE_DEFAULT, LV_DPI / 10);
    lv_obj_set_style_local_line_width(spin, LV_SPINNER_PART_INDIC, LV_STATE_DEFAULT, LV_DPI / 10);
    lv_obj_set_event_cb(spin, lv_fm_player_spin_event_cb);
    lv_obj_set_click(spin, true);
    
    player_spin_lb = lv_label_create(player_spin_h, NULL);
    lv_label_set_text(player_spin_lb, "");
    lv_obj_set_event_cb(player_spin_lb, lv_fm_player_spin_event_cb);
    lv_obj_set_click(player_spin_lb, true);
    
    lv_obj_align(player_spin_h, NULL, LV_ALIGN_CENTER, 0, 0);
    
    player_spin_task = lv_task_create(lv_fm_player_spin_task, 100, LV_TASK_PRIO_MID, &hlib);
}

static void lv_fm_player_delete(void)
{
    lwftp_session_t *s = hlib.remote.lwftp_session;
    
    if (s != NULL && \
        s->control_state == LWFTP_XFERING)
    {
        lwftp_abort(s);
    }
    
    if (hlib.playback_state == AUDIO_LIB_STATE_PLAY)
    {
        BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);        
    }
    hlib.playback_state = AUDIO_LIB_STATE_IDLE;
    
    if (hlib.fp)
    {
        f_close (hlib.fp);
    }
    
    audio_lib->lib_free(&hlib);
    
    hlib.active = 0;
    hlib.prv_data = NULL;
    hlib.fp = NULL;
    hlib.buffer = NULL;

    LV_FM_PLAYER_TASK_DEL(player_volume_task)
    LV_FM_PLAYER_TASK_DEL(player_seek_task)
    LV_FM_PLAYER_TASK_DEL(player_process_task)
    LV_FM_PLAYER_TASK_DEL(player_local_buffer_task)
    LV_FM_PLAYER_TASK_DEL(player_remote_buffer_task)
    LV_FM_PLAYER_TASK_DEL(player_spin_task)
    LV_FM_PLAYER_OBJ_DEL(player_spin_h)
    LV_FM_PLAYER_OBJ_DEL(player_h)
    player_img = NULL;
}

static void lv_fm_player_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
    char str[4];
    lv_obj_t * img;
    lv_img_ext_t * ext;
    
    if (e == LV_EVENT_CLICKED)
    {
        img = lv_obj_get_child(btn, NULL);
        ext = lv_obj_get_ext_attr(img);
        strcpy(str, ext->src);
        
        if (strcmp(str, LV_SYMBOL_PLAY) == 0)
        {
            hlib.playback_state = AUDIO_LIB_STATE_RESUME;
            
            lv_img_set_src(img, LV_SYMBOL_PAUSE);
            if(player_format == player_jmv)
            {
                lv_obj_set_hidden(player_ppbtn, true);
                lv_obj_set_hidden(player_slider, true);
                
                if (hlib.device == OUTPUT_DEVICE_HEADPHONE)
                {
                    lv_obj_set_hidden(volume_slider, true);
                }
            }
        }
        else if (strcmp(str, LV_SYMBOL_PAUSE) == 0)
        {
            hlib.playback_state = AUDIO_LIB_STATE_PAUSE;
            
            lv_img_set_src(img, LV_SYMBOL_PLAY);
        }       
    }
}

static void lv_fm_player_volume_slider_event_cb(lv_obj_t * slider, lv_event_t e)
{
	if(e == LV_EVENT_VALUE_CHANGED)
	{
		hlib.volume = lv_slider_get_value(slider);
	}
}

static void lv_fm_player_seek_slider_event_cb(lv_obj_t * slider, lv_event_t e)
{
    lwftp_session_t *s = hlib.remote.lwftp_session;
    
    if (s != NULL && \
        s->control_state == LWFTP_XFERING)
    {
        return;
    }
    
    if (e == LV_EVENT_RELEASED)
    {
        lv_fm_player_spin_create();
        hlib.seek_pos = (uint8_t) lv_slider_get_value(slider);
        audio_lib->lib_seek(&hlib);
    }
}

static void lv_fm_player_img_event_cb(lv_obj_t * obj, lv_event_t e)
{
    static lv_event_t last_e = _LV_EVENT_LAST;
    
    lv_obj_t * img;
  
    switch(hlib.playback_state)
    {
        case AUDIO_LIB_STATE_PLAY:
            if (e == LV_EVENT_LONG_PRESSED || \
                e == LV_EVENT_RIGHT_CLICKED)
            {
                hlib.playback_state = AUDIO_LIB_STATE_PAUSE;  
              
                img = lv_obj_get_child(player_ppbtn, NULL);
                lv_img_set_src(img, LV_SYMBOL_PLAY);
                if(player_format == player_jmv)
                {
                    lv_obj_set_hidden(player_ppbtn, false);
                    lv_obj_set_hidden(player_slider, false);
                    
                    if (hlib.device == OUTPUT_DEVICE_HEADPHONE)
                    {
                        lv_obj_set_hidden(volume_slider, false);
                    }
                }
                
                last_e = e;
            }
            break;
            
        case AUDIO_LIB_STATE_WAIT:
            if (e == LV_EVENT_CLICKED)
            {
                if (last_e != LV_EVENT_LONG_PRESSED)
                {
                    LV_FM_PLAYER_TASK_DEL(autoplay_task)

                    hlib.playback_state = AUDIO_LIB_STATE_STOP;
                }
                    
                last_e = _LV_EVENT_LAST;
            }
            break;
            
        default:
            break;
    }
}

static void lv_fm_player_spin_event_cb(lv_obj_t * obj, lv_event_t e)
{
    if (e == LV_EVENT_CLICKED)
    {
        LV_FM_PLAYER_TASK_DEL(autoplay_task)
                
        hlib.active = 0;
    }
}

void lv_fm_player_process_task(lv_task_t * task)
{
    static uint8_t ui_created;
    static lv_point_t last_pos = {-1, -1};    
    
    lv_indev_t * indev;
    lv_point_t current_pos;
    audio_lib_handle_t * hlib = task->user_data;
    lwftp_session_t *s = hlib->remote.lwftp_session;

    audio_lib->lib_process(hlib);
    
    if (ui_created == 1)
    {
        if (hlib->remote.buffer.ptr)
        {
            if (s != NULL && \
                s->control_pcb == NULL)
            {
                LV_FM_PLAYER_TASK_DEL(player_spin_task)
                LV_FM_PLAYER_OBJ_DEL(player_spin_h)
            }    
        }
        else if (hlib->local.buffer.ptr)
        {
            if (hlib->local.br >= hlib->local.btr || \
                (hlib->fp != NULL && \
                 hlib->fp->fptr == hlib->fp->fsize))
            {
                LV_FM_PLAYER_TASK_DEL(player_spin_task)
                LV_FM_PLAYER_OBJ_DEL(player_spin_h)
            }        
        }
    }
    
    if (hlib->playback_state == AUDIO_LIB_STATE_PLAY && \
        ui_created == 0)
    {
        lv_fm_player_create();
        player_seek_task = lv_task_create(lv_fm_player_seek_task, 100, LV_TASK_PRIO_MID, hlib);
        
        if (hlib->device == OUTPUT_DEVICE_HEADPHONE)
        {
            player_volume_task = lv_task_create(lv_fm_player_volume_task, 100, LV_TASK_PRIO_MID, hlib);
        }
        
        ui_created = 1;
    }

    if(player_img)
    {
        if(hlib->img.invalidate)
        {
            hlib->img.invalidate = 0;
            lv_obj_invalidate(player_img);
        }
        
        if(hlib->playback_state == AUDIO_LIB_STATE_WAIT)
        {
            indev = lv_indev_get_next(NULL);
            while(indev != NULL)
            {
                if(indev->driver.type == LV_INDEV_TYPE_POINTER)
                {
                    lv_indev_get_point(indev, &current_pos);
                    if(current_pos.x != last_pos.x || current_pos.y != last_pos.y) 
                    {
                        last_pos = current_pos;
                        lv_obj_invalidate(player_img);
                    }
                }
                
                indev = lv_indev_get_next(indev);
            }
        }
    }

    if(hlib->err != AUDIO_LIB_NO_ERROR || \
       hlib->active == 0)
    {
        lv_fm_player_delete();
        ui_created = 0;
        
        return;
    }
}

void lv_fm_player_seek_task(lv_task_t * task)
{
  static char buf[64];
  audio_lib_handle_t * hlib = task->user_data;
  uint32_t elapsed_time = (uint32_t) hlib->time.elapsed_time;
  uint32_t total_time = (uint32_t) hlib->time.total_time;
  int16_t x = (int16_t) ((elapsed_time * 100) / total_time);
  
  lv_snprintf(buf, sizeof(buf), "%02d:%02d", elapsed_time / 60, elapsed_time % 60);
  lv_obj_set_style_local_value_str(player_slider, LV_SLIDER_PART_BG, LV_STATE_DEFAULT, buf);
  
  if(player_slider->state != 0x12)
  {
    lv_slider_set_value(player_slider, x, LV_ANIM_OFF);
  }
}

void lv_fm_player_volume_task(lv_task_t * task)
{
	audio_lib_handle_t * hlib = task->user_data;

	if(hlib->volume != last_volume)
	{
		BSP_AUDIO_OUT_SetVolume(hlib->volume);

		last_volume = hlib->volume;
	}
}

void lv_fm_player_local_buffer_task(lv_task_t * task)
{
    static uint32_t bytesread;
    
    audio_lib_handle_t * hlib = task->user_data;
    
    if (hlib->playback_state == AUDIO_LIB_STATE_IDLE || \
        hlib->fp->fptr == hlib->fp->fsize)
    {
        return;
    }

    if (hlib->local.buffer.empty && \
        hlib->playback_state == AUDIO_LIB_STATE_PLAY)
    {
        lv_fm_player_spin_create();
        hlib->playback_state = AUDIO_LIB_STATE_PAUSE;
    }    
    
    if ( hlib->local.buffer.rptr >= hlib->local.buffer.size / 2 && \
         (hlib->local.buffer.last_state == AUDIO_LIB_BUFFER_OFFSET_NONE || \
          hlib->local.buffer.last_state == AUDIO_LIB_BUFFER_OFFSET_FULL) )
    {
        hlib->local.buffer.state = AUDIO_LIB_BUFFER_OFFSET_HALF;
    }
    else if ( hlib->local.buffer.rptr < hlib->local.buffer.size / 2 && \
              hlib->local.buffer.last_state == AUDIO_LIB_BUFFER_OFFSET_HALF )
    {
        hlib->local.buffer.state = AUDIO_LIB_BUFFER_OFFSET_FULL;
    }    

    switch(hlib->local.read_state)
    {
        case AUDIO_LIB_READ_REQ:
            switch (hlib->local.buffer.state)
            {
                case AUDIO_LIB_BUFFER_OFFSET_HALF:
                    hlib->local.ptr = hlib->local.buffer.ptr;
                    hlib->local.btr = hlib->local.buffer.size / 2;
                    hlib->local.br = 0;
                    
                    hlib->local.read_state = AUDIO_LIB_READ_WAIT;
                    hlib->local.buffer.last_state = AUDIO_LIB_BUFFER_OFFSET_HALF;                
                    break;
                    
                case AUDIO_LIB_BUFFER_OFFSET_FULL:
                    hlib->local.ptr = hlib->local.buffer.ptr + hlib->local.buffer.size / 2;
                    hlib->local.btr = hlib->local.buffer.size / 2;
                    hlib->local.br = 0;
                    
                    hlib->local.read_state = AUDIO_LIB_READ_WAIT;
                    hlib->local.buffer.last_state = AUDIO_LIB_BUFFER_OFFSET_FULL;                
                    break;
                    
                default:
                    break;        
            }
            break;
            
        case AUDIO_LIB_READ_WAIT:
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
                    hlib->playback_state = AUDIO_LIB_STATE_STOP;
                    return;
                }
                hlib->local.buffer.state = AUDIO_LIB_BUFFER_OFFSET_NONE;
                hlib->local.read_state = AUDIO_LIB_READ_REQ;

                if (hlib->local.buffer.empty)
                {
                    LV_FM_PLAYER_TASK_DEL(player_spin_task)
                    LV_FM_PLAYER_OBJ_DEL(player_spin_h)
                    hlib->local.buffer.empty = 0;
                    hlib->playback_state = AUDIO_LIB_STATE_RESUME;
                }                
            }
            break;
    }    
}

void lv_fm_player_remote_buffer_task(lv_task_t * task)
{
    static uint32_t bytesread;
    
    audio_lib_handle_t * hlib = task->user_data;
    lwftp_session_t *s = hlib->remote.lwftp_session;
    
    if (hlib->playback_state == AUDIO_LIB_STATE_IDLE || \
        hlib->remote.fptr == hlib->remote.fsize)
    {
        return;
    }
    
    if (hlib->remote.buffer.empty && \
        hlib->playback_state == AUDIO_LIB_STATE_PLAY)
    {
        lv_fm_player_spin_create();
        hlib->playback_state = AUDIO_LIB_STATE_PAUSE;
    }    
    
    if ( hlib->remote.buffer.rptr >= hlib->remote.buffer.size / 2 && \
         (hlib->remote.buffer.last_state == AUDIO_LIB_BUFFER_OFFSET_NONE || \
          hlib->remote.buffer.last_state == AUDIO_LIB_BUFFER_OFFSET_FULL) )
    {
        hlib->remote.buffer.state = AUDIO_LIB_BUFFER_OFFSET_HALF;
    }
    else if ( hlib->remote.buffer.rptr < hlib->remote.buffer.size / 2 && \
              hlib->remote.buffer.last_state == AUDIO_LIB_BUFFER_OFFSET_HALF )
    {
        hlib->remote.buffer.state = AUDIO_LIB_BUFFER_OFFSET_FULL;
    }
    
    switch(hlib->remote.read_state)
    {
        case AUDIO_LIB_READ_REQ:
            switch (hlib->remote.buffer.state)
            {                                
                case AUDIO_LIB_BUFFER_OFFSET_HALF:
                    LWFTP_FileRead(hlib, 
                                   hlib->remote.buffer.ptr, 
                                   hlib->remote.buffer.size / 2, 
                                   &bytesread);
                    if (hlib->err)
                    {
                        return;
                    }
                    hlib->remote.read_state = AUDIO_LIB_READ_WAIT;
                    hlib->remote.buffer.last_state = AUDIO_LIB_BUFFER_OFFSET_HALF;
                    break;
                
                case AUDIO_LIB_BUFFER_OFFSET_FULL:
                    LWFTP_FileRead(hlib, 
                                   hlib->remote.buffer.ptr + (hlib->remote.buffer.size / 2), 
                                   hlib->remote.buffer.size / 2, 
                                   &bytesread);
                    if (hlib->err)
                    {
                        return;
                    }
                    hlib->remote.read_state = AUDIO_LIB_READ_WAIT;
                    hlib->remote.buffer.last_state = AUDIO_LIB_BUFFER_OFFSET_FULL;
                    break;
                
                default:
                    break;
            }        
            break;
            
        case AUDIO_LIB_READ_WAIT:
            if (s->control_pcb == NULL)
            {
                if((bytesread < hlib->remote.buffer.size / 2 && \
                    hlib->remote.fptr < hlib->remote.fsize) || \
                    !bytesread)
                {
                    hlib->playback_state = AUDIO_LIB_STATE_STOP;
                    return;
                }                    
                hlib->remote.buffer.state = AUDIO_LIB_BUFFER_OFFSET_NONE;
                hlib->remote.read_state = AUDIO_LIB_READ_REQ;
                
                if (hlib->remote.buffer.empty)
                {
                    LV_FM_PLAYER_TASK_DEL(player_spin_task)
                    LV_FM_PLAYER_OBJ_DEL(player_spin_h)
                    hlib->remote.buffer.empty = 0;
                    hlib->playback_state = AUDIO_LIB_STATE_RESUME;
                }
            }        
            break;
    }
}

void lv_fm_player_spin_task(lv_task_t * task)
{
    static char buf[64];
    
    uint32_t value;
    uint32_t total;
    int16_t x = 0;    
    
    audio_lib_handle_t * hlib = task->user_data;
    
    if (hlib->remote.buffer.ptr)
    {
        value = *(hlib->remote.br) / 1024;
        total = hlib->remote.btr / 1024;        
    }
    else if (hlib->local.buffer.ptr)
    {
        value = hlib->local.br / 1024;
        total = hlib->local.btr / 1024;        
    }
    
    if (total > 0) 
    {
        x = (int16_t) ((value * 100) / total);
    }
    lv_snprintf(buf, sizeof(buf), "Loading %d", x);
    lv_label_set_text(player_spin_lb, buf);    
}

void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
	if(audio_lib->lib_transfer_complete_cb != NULL)
	{
		audio_lib->lib_transfer_complete_cb(&hlib);
	}
}

void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
	if(audio_lib->lib_transfer_half_cb != NULL)
	{
		audio_lib->lib_transfer_half_cb(&hlib);
	}
}
