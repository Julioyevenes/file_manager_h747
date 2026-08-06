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
#include "lv_file_manager.h"
#include "lv_file_player.h"
#include "lv_file_loader.h"

#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include "usbh_diskio.h"

#include "usbh_core.h"
#include "usbh_msc.h"
#include "usbh_hid.h"
#include "usbh_hub.h"

#include "lwip/stats.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "app_ethernet.h"
#include "ethernetif.h"
#include "ftps.h"
#include "lwftp.h"

#include <stdlib.h>
#include <malloc.h>

#include "lwip/apps/sntp.h"
#include "rtc.h"

/* Private types ------------------------------------------------------------*/
typedef enum
{
	other = 0,
	bmp,			/* Bitmap image */
	jpeg,			/* JPEG image */
 	gif,			/* GIF image */	
	wav,			/* WAV audio */
	mp3,			/* MP3 audio */
	flac,			/* FLAC audio */
 	jmv,			/* JMV video */
 	bin             /* BIN executable */
} lv_fm_format_t;

typedef enum
{
	LV_FM_NO_ERROR 				= 0,
	LV_FM_READ_ERROR 			= -1,
	LV_FM_WRITE_ERROR 			= -2,
	LV_FM_DELETE_ERROR 			= -3,
	LV_FM_FORMAT_ERROR 			= -4,
	LV_FM_MEMORY_ERROR 			= -5,
	LV_FM_FILE_ALREADY_EXISTS 	= -6,
	LV_FM_UNSUPPORTED_FORMAT 	= -7,
	LV_FM_AUDIO_DEVICE_ERROR    = -8,
    LV_FM_CONNECTION_ERROR      = -9	
} lv_fm_err_t;

typedef enum
{
    LV_FM_MAIN_IDLE = 0,
    LV_FM_MAIN_COPY,
    LV_FM_MAIN_PASTE,
    LV_FM_MAIN_DELETE,
    LV_FM_MAIN_RENAME,
    LV_FM_MAIN_NEWFOLDER,
    LV_FM_MAIN_FORMAT,
    LV_FM_MAIN_UPLOAD,
    LV_FM_MAIN_DOWNLOAD
} lv_fm_main_state_t;

typedef enum
{
    LV_FM_FILE_TASK_INIT = 0,
    LV_FM_FILE_TASK_PROC,
    LV_FM_FILE_TASK_FILE
} lv_fm_file_task_state_t;

typedef enum
{
    LV_FM_LIST_TASK_IDLE = 0,
    LV_FM_LIST_TASK_CLEAN,    
    LV_FM_LIST_TASK_ALLOC
} lv_fm_list_task_state_t;

typedef enum
{
	LV_FM_NO_MEDIA = 0,
	LV_FM_MEDIA_SD,
	LV_FM_MEDIA_USB
} lv_fm_media_hardware_t;

typedef struct lv_fm_obj_t
{
    char                    name[256];
    char                    user[20];
    char                    pass[20];

    uint8_t                 folder : 1;
    uint8_t                 volume : 1;
    uint8_t                 ip_addr[4];
    
    uint16_t                port;

    uint32_t                size, free;
    uint32_t                idx, time, date;

    lv_fm_format_t          format;
    
    struct lv_fm_obj_t *    next;
} lv_fm_obj_t;

typedef struct
{
	lv_fm_media_hardware_t	media_hard;

	Diskio_drvTypeDef *		drv;

	uint8_t 				media_present : 1;
	uint8_t 				valid : 1;
	uint8_t					lun;
	
	FATFS 					fat_fs;
	
	USBH_HandleTypeDef *	husb;

	char					path[4];
} lv_fm_media_t;

typedef struct lv_fm_op_t
{
    char name[256], user[20], pass[20];             
    char src_path[256], dst_path[256];
    char src_vol[4], dst_vol[4];

    uint8_t folder : 1;
    uint8_t ip_addr[4];
    
    uint16_t port;
    
    uint32_t size;

    struct lv_fm_op_t * parent_ll;
    struct lv_fm_op_t * child_ll;
    struct lv_fm_op_t * next;
} lv_fm_op_t;

typedef struct
{
    uint8_t *addr;
    uint8_t *wptr, *rptr;    
    uint32_t size;    
} lv_fm_buffer_t;

typedef struct
{
    FRESULT                     fr;

    FIL                         src;
    FIL                         dst;
    
    char                        remote_path[1024];
    char                        old_name[256], new_name[256];

    uint8_t                     flag_cut : 1;
    uint8_t                     list_vol : 1;

    uint32_t                    count;
    uint32_t                    total;
    uint32_t                    list_nobj;
    
    lv_obj_t *                  active_list;
    lv_fm_obj_t                 active_site;
    lv_fm_obj_t *               autoplay_site;
    
    lv_fm_op_t *                cur_op;

    lv_fm_buffer_t              buffer;
    lv_fm_main_state_t          main_state;
    lv_fm_file_task_state_t     file_task_state;
    lv_fm_list_task_state_t     list_task_state;
    lv_fm_err_t                 err;
    
    lwftp_session_t             lwftp_session;
} lv_fm_task_data_t;

/* Private constants --------------------------------------------------------*/
#define LV_FM_MAX_VOLUMES 	5
#define LV_FM_LOG_BUFSIZE   1024
#define LV_FM_OP_BUFSIZE    (32 * 1024)
#define LV_FM_OP_MAXSIZE    (24 * 1460)
#define LV_FM_SMALL_FONT    &lv_font_montserrat_16

const char * str_vol_opt[] = {	LV_SYMBOL_DRIVE, "Format",
								LV_SYMBOL_CLOSE, "Cancel",
								NULL  };

const char * str_new_opt[] = {  LV_SYMBOL_DIRECTORY, "New folder",
                                LV_SYMBOL_CLOSE, "Cancel",
                                NULL  };

const char * str_file_opt[] = {	LV_SYMBOL_UPLOAD, "Upload",
                                LV_SYMBOL_COPY, "Copy",
								LV_SYMBOL_CUT, "Cut",
								LV_SYMBOL_TRASH, "Delete",
								LV_SYMBOL_EDIT, "Rename",
								LV_SYMBOL_DIRECTORY, "New folder",
								LV_SYMBOL_CLOSE, "Cancel",
								NULL };

const char * str_site_opt[] = {  LV_SYMBOL_EDIT, "Edit",
                                LV_SYMBOL_TRASH, "Remove",
                                LV_SYMBOL_CLOSE, "Cancel",
                                NULL  };

const char * str_remote_opt[] = { LV_SYMBOL_DOWNLOAD, "Download",
                                  LV_SYMBOL_TRASH, "Delete",
                                  LV_SYMBOL_EDIT, "Rename",
                                  LV_SYMBOL_DIRECTORY, "New folder",        
                                  LV_SYMBOL_CLOSE, "Cancel",
                                  NULL  };

const char * str_none_opt[] = { LV_SYMBOL_CLOSE, "Cancel",
                                NULL  };

const char * btns00[] = {"Ok", ""};
const char * btns01[] = {"Cancel", "Ok", ""};

/* Private macro ------------------------------------------------------------*/
#define LV_FM_ALLOC         malloc

#define LV_FM_FREE(ptr)		if (ptr != NULL) \
							{ \
								free(ptr); ptr = NULL; \
							}

#define LV_FM_OBJ_DEL(ptr)	if (ptr != NULL) \
							{ \
								lv_obj_del(ptr); ptr = NULL; \
							}


#define LV_FM_TASK_DEL(ptr)	if (ptr != NULL) \
							{ \
								lv_task_del(ptr); ptr = NULL; \
							}

#define LV_FM_EQ(a, b)      (a == b)

#define LV_FM_LL_ADD(list)                              \
({                                                      \
    typeof(list) e;                                     \
    e = LV_FM_ALLOC(sizeof(typeof(*list)));             \
    if (e) {                                            \
        memset(e, 0, sizeof(typeof(*list)));            \
        typeof(list) _tmp;                              \
        if (list) {                                     \
            _tmp = list;                                \
            while (_tmp->next) { _tmp = _tmp->next; }   \
            _tmp->next = e;                             \
        } else {                                        \
            list = e;                                   \
        }                                               \
    }                                                   \
    e;                                                  \
})

#define LV_FM_LL_GET(list)  \
({                          \
    typeof(list) _tmp;      \
    if (list) {             \
        _tmp = list;        \
        list = list->next;  \
        _tmp->next = NULL;  \
    } else {                \
        _tmp = NULL;        \
    }                       \
    _tmp;                   \
})

#define LV_FM_LL_COUNT(list, cnt)                           \
do {                                                        \
    typeof(list) _tmp;                                      \
    for (_tmp = list; _tmp != NULL; _tmp = _tmp->next) {    \
        cnt++;                                              \
    }                                                       \
} while (0)

#define LV_FM_LL_SEARCH(list, e, field, arg, cmp)       \
do {                                                    \
    for (e = list; e != NULL; e = e->next) {            \
        if (cmp(arg, e->field)) break;                  \
    }                                                   \
} while (0)

#define LV_FM_LL_CLEAN(list)                                    \
do {                                                            \
    if (list) {                                                 \
        typeof(list) _tmp, _arg;                                \
        for (_tmp = list; _tmp != NULL; _tmp = _tmp->next) {    \
            _arg = _tmp;                                        \
            LV_FM_FREE(_arg);                                   \
        }                                                       \
        list->next = NULL;                                      \
        list = NULL;                                            \
    }                                                           \
} while (0)

#define LV_FM_LL_DEL(list ,e)                                   \
do {                                                            \
    typeof(e) _arg;                                             \
    if (list == e) {                                            \
        list = list->next;                                      \
    } else {                                                    \
        typeof(list) _tmp;                                      \
        for (_tmp = list; _tmp != NULL; _tmp = _tmp->next) {    \
            if (_tmp->next == e) {                              \
                _tmp->next = e->next;                           \
                break;                                          \
            }                                                   \
        }                                                       \
    }                                                           \
    e->next = NULL;                                             \
    _arg = e;                                                   \
    LV_FM_FREE(_arg);                                           \
} while (0)

#define LV_FM_LL_COPY(dst, src) memcpy(dst, src, sizeof(typeof(*dst)) - sizeof(typeof(dst)))

#define LV_FM_TREE_CLEAN(tree)              \
do {                                        \
    typeof(tree) _e, _tmp;                  \
    _e = tree;                              \
    while (_e != NULL) {                    \
        if (_e->child_ll == NULL) {         \
            _tmp = _e;                      \
            LV_FM_FREE(_tmp);               \
        }                                   \
        if (_e->child_ll != NULL) {         \
            _e = _e->child_ll;              \
        } else if (_e->next != NULL) {      \
            _e = _e->next;                  \
        } else if (_e->parent_ll != NULL) { \
            _e = _e->parent_ll;             \
            _e->child_ll = NULL;            \
        } else {                            \
            _e = NULL;                      \
        }                                   \
    }                                       \
    tree = NULL;                            \
} while (0)

/* Private variables --------------------------------------------------------*/
extern lv_fm_player_format_t player_format;
extern USBH_HandleTypeDef hUSBH;
extern USBH_HandleTypeDef * pusb_hid;
extern char __heap_start asm("__heap_start");
extern char __heap_limit asm("__heap_limit");
extern u32_t ctrl_cnt;
extern u32_t data_cnt;
extern app_netif_t app_netif;
extern lv_indev_t * enc_indev;
extern lv_indev_t * ptr_indev;
extern lv_indev_t * ts_indev;

char lfn_buffer[256];
uint8_t op_buffer[LV_FM_OP_MAXSIZE];
uint8_t disp_shutdown_en = 0;
uint8_t autoplay_en = 1;
uint32_t disp_timeout = 15000U;
uint16_t audiodevice = OUTPUT_DEVICE_HEADPHONE;

lv_fm_obj_t * fm_obj_list;
lv_fm_obj_t * fm_sobj_list;
lv_fm_obj_t * fm_remote_obj_list;
lv_fm_obj_t * fm_remote_site_list;
lv_fm_op_t * fm_op_list;
lv_fm_media_t fm_media[LV_FM_MAX_VOLUMES];
lv_fm_task_data_t fm_task_data;

lv_task_t * media_task;
lv_task_t * list_task;
lv_task_t * spin_task;
lv_task_t * op_task;
lv_task_t * file_task;
lv_task_t * bar_task;
lv_task_t * mbox_task;
lv_task_t * net_task;
lv_task_t * disp_task;
lv_task_t * stats_task;
lv_task_t * autoplay_task;

static lv_group_t * g;
static lv_obj_t * tv;
static lv_obj_t * t1;
static lv_obj_t * t2;
static lv_obj_t * t3;
static lv_obj_t * t4;
static lv_obj_t * list_local;
static lv_obj_t * list_remote;
static lv_obj_t * list_options;
static lv_obj_t * h;
static lv_obj_t * h_spin;
static lv_obj_t * bar;
static lv_obj_t * dd_audiodevice;
static lv_obj_t * dd_timeout;
static lv_obj_t * mbox_question;
static lv_obj_t * mbox_err;
static lv_obj_t * ta_fn_local;
static lv_obj_t * ta_fn_remote;
static lv_obj_t * ta_ip_settings;
static lv_obj_t * ta_user_settings;
static lv_obj_t * ta_pass_settings;
static lv_obj_t * ta_name_remote;
static lv_obj_t * ta_ip_remote;
static lv_obj_t * ta_port_remote;
static lv_obj_t * ta_user_remote;
static lv_obj_t * ta_pass_remote;
static lv_obj_t * kb;
static lv_obj_t * img;
static lv_obj_t * cb_dhcp;
static lv_obj_t * cb_dispsh;
static lv_obj_t * cb_autoplay;
static lv_obj_t * cpu_bar;
static lv_obj_t * lram_bar;
static lv_obj_t * hram_bar;
static lv_obj_t * lb_conn;
static lv_obj_t * lb_time;
static lv_obj_t * ta_log;
static lv_obj_t * spin;
static lv_obj_t * lb_spin;
static lv_style_t style_side_title;
static lv_style_t style_small_font;

/* Private function prototypes ----------------------------------------------*/
static void 			lv_fm_local_tab_create(lv_obj_t * parent);
static void             lv_fm_remote_tab_create(lv_obj_t * parent);
static void             lv_fm_settings_tab_create(lv_obj_t * parent);
static void             lv_fm_stats_tab_create(lv_obj_t * parent);
static lv_obj_t *       lv_fm_list_create(lv_obj_t * parent, 
                                          lv_coord_t x, lv_coord_t y, 
                                          lv_coord_t w, lv_coord_t h, 
                                          lv_align_t align, 
                                          const char ** str, 
                                          lv_event_cb_t event_cb);
static lv_obj_t * 		lv_fm_mbox_create(lv_obj_t * parent, 
                  		                  const char * txt, const char ** str, 
                  		                  lv_event_cb_t event_cb);
static lv_obj_t *       lv_fm_remote_cont_create(lv_obj_t * parent, lv_fm_obj_t * obj);

static void             lv_fm_list_local_event_cb(lv_obj_t * obj, lv_event_t e);
static void 			lv_fm_list_local_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void             lv_fm_list_remote_event_cb(lv_obj_t * obj, lv_event_t e);
static void             lv_fm_list_remote_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void             lv_fm_cont_remote_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_list_options_local_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void             lv_fm_list_options_remote_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_mbox_question_local_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void             lv_fm_mbox_question_remote_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_copying_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_mbox_err_btn_event_cb(lv_obj_t * btn, lv_event_t e);
static void 			lv_fm_img_event_cb(lv_obj_t * obj, lv_event_t e);
static void 			lv_fm_kb_event_cb(lv_obj_t * _kb, lv_event_t e);
static void             lv_fm_dropdown_event_cb(lv_obj_t * obj, lv_event_t e);
static void             lv_fm_checkbox_event_cb(lv_obj_t * obj, lv_event_t e);
static void             lv_fm_textarea_event_cb(lv_obj_t * obj, lv_event_t e);
static void             lv_fm_spin_event_cb(lv_obj_t * obj, lv_event_t e);

static void 			_lv_fm_err(lv_fm_err_t err);
static uint8_t 			_lv_fm_media_detect(lv_fm_media_t * m);
static void 			_lv_fm_media_check(lv_fm_media_t * m);
static void             _lv_fm_dir_read_start(lv_fm_task_data_t * td);
static void             _lv_fm_list_add_obj_btn(lv_obj_t * list, lv_fm_obj_t * obj, lv_event_cb_t event_cb);
static void             _lv_fm_list_add_site_btn(lv_obj_t * list, lv_fm_obj_t * obj, lv_event_cb_t event_cb);
static lv_fm_format_t 	_lv_fm_get_ext(const char *fname);
static void             _lv_fm_list_btns_checkable(lv_obj_t * list, bool enable);
static void             _lv_fm_list_btns_hidden(lv_obj_t * list, 
                                                const char **txt, 
                                                bool enable);
static bool             _lv_fm_check_obj_type(lv_obj_t * obj, const char *type_str);
lv_obj_t *              _lv_fm_get_child(const lv_obj_t * parent, const char *type_str);
static void             _lv_fm_str_to_ip(const char * pstr, uint8_t * ip);
static uint8_t          _lv_fm_month_to_int(const char *str);
static uint8_t          _lv_fm_is_parent(lv_obj_t * obj, lv_obj_t * parent);

static void 		    _lv_fm_obj_ops_start(lv_fm_task_data_t * td, 
            		                         lv_obj_t * par, 
            		                         lv_task_cb_t task_xcb);
static uint8_t          _lv_fm_local_folder_scan(lv_fm_op_t ** op_par,
                                                 uint32_t * total_cnt,
                                                 uint64_t * total_sz);
static uint8_t          _lv_fm_remote_folder_scan(lwftp_session_t * s,
                                                  lv_fm_op_t ** op_par,
                                                  uint32_t * total_cnt,
                                                  uint64_t * total_sz);
static lv_fm_err_t      _lv_fm_unlink(const char *path,
                                      const char *name,
                                      bool folder);
static lv_fm_err_t      _lv_fm_frename(const char *src_path,
                                       const char *dst_path,
                                       const char *name,
                                       bool folder);
static lv_fm_err_t      _lv_fm_fopen(FIL *fptr,
                                     const char *path,
                                     const char *name,
                                     uint8_t mode);
static lv_fm_err_t      _lv_fm_fmkdir(const char *path, const char *name);
static void             _lv_fm_futime(const char * src_path,
                                      const char * dst_path,
                                      const char * name);

void 					lv_fm_media_task(lv_task_t * task);
void                    lv_fm_list_local_task(lv_task_t * task);
void                    lv_fm_list_remote_task(lv_task_t * task);
void                    lv_fm_spin_task(lv_task_t * task);
void                    lv_fm_local_obj_ops_task(lv_task_t * task);
void                    lv_fm_remote_obj_ops_task(lv_task_t * task);
void 					lv_fm_local_file_task(lv_task_t * task);
void                    lv_fm_remote_file_task(lv_task_t * task);
void 					lv_fm_bar_task(lv_task_t * task);
void 					lv_fm_mbox_task(lv_task_t * task);
void                    lv_fm_net_task(lv_task_t * task);
void                    lv_fm_disp_task(lv_task_t * task);
void                    lv_fm_stats_task(lv_task_t * task);
void                    lv_fm_list_local_autoplay_task(lv_task_t * task);
void                    lv_fm_list_remote_autoplay_task(lv_task_t * task);

static lv_fm_err_t      lv_lwftp_connect(lwftp_session_t * s, 
                                         void (*cb)(void*, int), 
                                         uint8_t *ip_addr,
                                         uint16_t port, 
                                         char *name,
                                         char *user,
                                         char *pass,
                                         char *path);

static void             lv_lwftp_MKD_cb(void *arg, int result);
static void             lv_lwftp_RMD_cb(void *arg, int result);
static void             lv_lwftp_DELE_cb(void *arg, int result);
static void             lv_lwftp_RNFR_cb(void *arg, int result);
static void             lv_lwftp_CWD_cb(void *arg, int result);
static void             lv_lwftp_LIST_cb(void *arg, int result);
static void             lv_lwftp_STOR_cb(void *arg, int result);
static void             lv_lwftp_RETR_cb(void *arg, int result);
static void             lv_lwftp_QUIT_cb(void *arg, int result);
static uint             lv_lwftp_dir_read_cb(void *arg, const char* ptr, uint len);
static uint             lv_lwftp_file_read_cb(void *arg, const char** pptr, uint maxlen);
static uint             lv_lwftp_file_write_cb(void *arg, const char* ptr, uint len);

void lv_fm_init(void)
{  
  fm_media[0].media_hard = LV_FM_MEDIA_SD;
  fm_media[0].drv = (Diskio_drvTypeDef *) &SD_Driver;
  FATFS_LinkDriver(fm_media[0].drv, &(fm_media[0].path[0]));
  
  hUSBH.address = 0xFF;
  hUSBH.Pipes   = USBH_malloc(sizeof(uint32_t) * USBH_MAX_PIPES_NBR);
  
  USBH_Init(&hUSBH, 0, 0);
  USBH_RegisterClass(&hUSBH, &USBH_msc);
  USBH_RegisterClass(&hUSBH, &HID_Class);
  USBH_RegisterClass(&hUSBH, &HUB_Class);
  USBH_Start(&hUSBH);
  
  RTC_Config();
  
  lwip_init();
  Netif_Config(&app_netif);
  ftps_init();
  
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "pool.ntp.org");
  sntp_init();  
  
  g = lv_group_create();
  lv_indev_set_group(enc_indev, g);
  
  tv = lv_tabview_create(lv_scr_act(), NULL);
  lv_obj_set_style_local_pad_left(tv, LV_TABVIEW_PART_TAB_BG, LV_STATE_DEFAULT, LV_HOR_RES / 4);
  
  lb_time = lv_label_create(lv_scr_act(), NULL);
  lv_obj_set_pos(lb_time, LV_DPX(20), LV_DPX(20));
  lv_label_set_text(lb_time, "\0");
  
  t1 = lv_tabview_add_tab(tv, "Local");
  t2 = lv_tabview_add_tab(tv, "Remote");
  t3 = lv_tabview_add_tab(tv, "Settings");
  t4 = lv_tabview_add_tab(tv, "Stats");

  lv_style_init(&style_side_title);
  lv_style_set_value_align(&style_side_title, LV_STATE_DEFAULT, LV_ALIGN_OUT_TOP_LEFT);
  
  lv_style_init(&style_small_font);
  lv_style_set_text_font(&style_small_font, LV_STATE_DEFAULT, LV_FM_SMALL_FONT);  

  lv_fm_local_tab_create(t1);
  lv_fm_remote_tab_create(t2);
  lv_fm_settings_tab_create(t3);
  lv_fm_stats_tab_create(t4);
}

void lv_fm_non_task_process(void)
{
    HID_HandleTypeDef * HID_Handle;
	USBH_HandleTypeDef * pusb = HUB_Process(&hUSBH);
	lv_fm_media_t * m = (lv_fm_media_t *) &fm_media;
	app_netif_t * net = &app_netif;
	uint8_t idx;

	if(pusb->gState == HOST_CLASS)
	{
        if(USBH_GetActiveClass(pusb) == USB_HID_CLASS)
        {
            HID_Handle = (HID_HandleTypeDef *) pusb->pActiveClass->pData;
            if(HID_Handle->state != HID_INIT)
            {
                if(USBH_HID_GetDeviceType(pusb) == HID_MOUSE)
                {
                    if(pusb_hid == NULL)
                    {
                        pusb_hid = pusb;
                        pusb_hid->valid = 1;
                    }
                }
            }
        }
        else if(USBH_GetActiveClass(pusb) == USB_MSC_CLASS)
        {
            idx = pusb->address > 1 ? pusb->address - 1 : pusb->address;

            if(m[idx].husb == NULL)
            {
                m[idx].media_hard = LV_FM_MEDIA_USB;
                m[idx].drv = (Diskio_drvTypeDef *) &USBH_Driver;
                m[idx].husb = pusb;
                m[idx].husb->valid = 1;

                FATFS_LinkDriverEx(m[idx].drv, &(m[idx].path[0]), 0, m[idx].husb);
            }
        }
	}

	for(idx = 1; idx < LV_FM_MAX_VOLUMES; idx++)
	{
		if(m[idx].valid == 0 && \
		   m[idx].husb != NULL && \
		   m[idx].husb->valid == 0)
		{
			FATFS_UnLinkDriver(&(m[idx].path[0]));

			memset(&m[idx], 0, sizeof(lv_fm_media_t));
		}
	}

	if(pusb_hid->valid == 0)
	{
		pusb_hid = NULL;
	}
	
	ethernetif_input(net->gnetif);
	sys_check_timeouts();
#if LWIP_NETIF_LINK_CALLBACK
  Ethernet_Link_Periodic_Handle(net);
#endif
  if (net->use_dhcp)
  {
    DHCP_Periodic_Handle(net);
  }   
}

int lv_fm_printf(const char *format, ...)
{
  char s[LV_FM_LOG_BUFSIZE];
  static char buf[LV_FM_LOG_BUFSIZE];
  va_list arg;
  int done, len;
  static int pos;

  va_start (arg, format);
  done = vsprintf (s, format, arg);
  va_end (arg);
  
  len = strlen(s);
  if ((LV_FM_LOG_BUFSIZE - pos) < len)
  {
      pos = 0;
      strcpy(&buf[pos], s);
  }
  else
  {
      strcpy(&buf[pos], s);
      pos += len;
  }
  
  lv_textarea_set_text(ta_log, (const char *)buf);

  return done;   
}

static void lv_fm_local_tab_create(lv_obj_t * parent)
{
    lv_fm_task_data_t * td = &fm_task_data;
    lv_coord_t grid_h = lv_page_get_height_grid(parent, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(parent, 1, 1);

    lv_page_set_scrl_layout(parent, LV_LAYOUT_GRID);

    td->buffer.addr = &op_buffer[0];
	td->buffer.size = LV_FM_OP_BUFSIZE;
	
    list_local = lv_fm_list_create(parent, 0, 0, grid_w, grid_h, LV_ALIGN_CENTER, NULL, NULL);
    lv_obj_set_event_cb(list_local, lv_fm_list_local_event_cb);

    media_task = lv_task_create(lv_fm_media_task, 500, LV_TASK_PRIO_MID, &fm_media[0]);
    mbox_task = lv_task_create(lv_fm_mbox_task, 500, LV_TASK_PRIO_MID, &fm_task_data);
}

static void lv_fm_remote_tab_create(lv_obj_t * parent)
{
    lv_coord_t grid_h = lv_page_get_height_grid(parent, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(parent, 1, 1);

    lv_page_set_scrl_layout(parent, LV_LAYOUT_GRID);
    
    list_remote = lv_fm_list_create(parent, 0, 0, grid_w, grid_h, LV_ALIGN_CENTER, NULL, NULL);
    lv_obj_set_event_cb(list_remote, lv_fm_list_remote_event_cb);
}

static void lv_fm_settings_tab_create(lv_obj_t * parent)
{
  char iptxt[20];
  lv_obj_t * h;
  lv_coord_t grid_w = lv_page_get_width_grid(parent, 1, 1);
  lv_coord_t grid_h = lv_page_get_height_grid(parent, 2, 1);
  lv_coord_t pad = lv_obj_get_style_pad_inner(parent, LV_PAGE_PART_SCROLLABLE);
  app_netif_t * net = &app_netif;
  
  sprintf((char *)iptxt, "%d.%d.%d.%d", net->ip_addr[0], net->ip_addr[1], 
                                        net->ip_addr[2], net->ip_addr[3]);
  
  lv_page_set_scrl_layout(parent, LV_LAYOUT_PRETTY_TOP);
  
  h = lv_cont_create(parent, NULL);
  lv_obj_set_size(h, grid_w, grid_h);
  lv_cont_set_layout(h, LV_LAYOUT_GRID);
  lv_obj_add_style(h, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(h, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Network options");
  
  ta_ip_settings = lv_textarea_create(h, NULL);
  lv_obj_set_width(ta_ip_settings, grid_w/2 - pad * 2);
  lv_obj_align(ta_ip_settings, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  lv_obj_add_style(ta_ip_settings, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(ta_ip_settings, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "IP Address");    
  lv_textarea_set_text(ta_ip_settings, iptxt);
  lv_textarea_set_one_line(ta_ip_settings, true);
  lv_textarea_set_cursor_hidden(ta_ip_settings, true);
  lv_obj_set_event_cb(ta_ip_settings, lv_fm_textarea_event_cb);    

  cb_dhcp = lv_checkbox_create(h, NULL);
  lv_obj_align(cb_dhcp, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  lv_checkbox_set_text(cb_dhcp, "Enable DHCP");
  lv_obj_set_event_cb(cb_dhcp, lv_fm_checkbox_event_cb);
  
  if(net->use_dhcp)
  {
      lv_checkbox_set_state(cb_dhcp, LV_BTN_STATE_CHECKED_RELEASED);
  }
  else
  {
      lv_checkbox_set_state(cb_dhcp, LV_BTN_STATE_RELEASED);
  }
  
  ftps_user_get(iptxt);
  
  ta_user_settings = lv_textarea_create(h, NULL);
  lv_obj_set_width(ta_user_settings, grid_w/2 - pad * 2);
  lv_obj_align(ta_user_settings, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  lv_obj_add_style(ta_user_settings, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(ta_user_settings, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "FTP User");    
  lv_textarea_set_text(ta_user_settings, iptxt);
  lv_textarea_set_one_line(ta_user_settings, true);
  lv_textarea_set_cursor_hidden(ta_user_settings, true);
  lv_obj_set_event_cb(ta_user_settings, lv_fm_textarea_event_cb);    
  
  ftps_pass_get(iptxt);
  
  ta_pass_settings = lv_textarea_create(h, NULL);
  lv_obj_set_width(ta_pass_settings, grid_w/2 - pad * 2);
  lv_obj_align(ta_pass_settings, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  lv_obj_add_style(ta_pass_settings, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(ta_pass_settings, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "FTP Pass");    
  lv_textarea_set_text(ta_pass_settings, iptxt);
  lv_textarea_set_one_line(ta_pass_settings, true);
  lv_textarea_set_cursor_hidden(ta_pass_settings, true);
  lv_obj_set_event_cb(ta_pass_settings, lv_fm_textarea_event_cb);  

  h = lv_cont_create(parent, NULL);
  lv_obj_set_size(h, grid_w, grid_h);
  lv_cont_set_layout(h, LV_LAYOUT_COLUMN_LEFT);
  lv_obj_add_style(h, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(h, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Audio options");

  dd_audiodevice = lv_dropdown_create(h, NULL);
  lv_obj_set_width(dd_audiodevice, grid_w - pad * 2);
  lv_obj_align(dd_audiodevice, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  lv_obj_add_style(dd_audiodevice, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(dd_audiodevice, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Output device");
  lv_obj_set_event_cb(dd_audiodevice, lv_fm_dropdown_event_cb);
  lv_dropdown_set_options(dd_audiodevice, "Headphone jack\n" "HDMI");
  lv_dropdown_set_selected(dd_audiodevice, 0);
  
  h = lv_cont_create(parent, NULL);
  lv_obj_set_size(h, grid_w, grid_h);
  lv_cont_set_layout(h, LV_LAYOUT_GRID);
  lv_obj_add_style(h, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(h, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Display options");
  
  dd_timeout = lv_dropdown_create(h, NULL);
  lv_obj_set_width(dd_timeout, grid_w/2 - pad * 2);
  lv_obj_add_style(dd_timeout, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(dd_timeout, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Timeout");
  lv_obj_set_event_cb(dd_timeout, lv_fm_dropdown_event_cb);
  lv_dropdown_set_options(dd_timeout, "15\n" "30\n" "60");
  lv_dropdown_set_selected(dd_timeout, 0);
  
  cb_dispsh = lv_checkbox_create(h, NULL);
  lv_obj_align(cb_dispsh, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
  lv_checkbox_set_text(cb_dispsh, "Enable Display Shutdown");
  lv_obj_set_event_cb(cb_dispsh, lv_fm_checkbox_event_cb);    
  
  if(disp_shutdown_en)
  {
      lv_checkbox_set_state(cb_dispsh, LV_BTN_STATE_CHECKED_RELEASED);
  }
  else
  {
      lv_checkbox_set_state(cb_dispsh, LV_BTN_STATE_RELEASED);
  }
  
  h = lv_cont_create(parent, NULL);
  lv_obj_set_size(h, grid_w, grid_h);
  lv_cont_set_layout(h, LV_LAYOUT_COLUMN_LEFT);
  lv_obj_add_style(h, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(h, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Playback options");
  
  cb_autoplay = lv_checkbox_create(h, NULL);
  lv_checkbox_set_text(cb_autoplay, "Enable autoplay");
  lv_obj_set_event_cb(cb_autoplay, lv_fm_checkbox_event_cb);

  if(autoplay_en)
  {
      lv_checkbox_set_state(cb_autoplay, LV_BTN_STATE_CHECKED_RELEASED);
  }
  else
  {
      lv_checkbox_set_state(cb_autoplay, LV_BTN_STATE_RELEASED);
  }  
  
  net_task = lv_task_create(lv_fm_net_task, 500, LV_TASK_PRIO_MID, net);
  disp_task = lv_task_create(lv_fm_disp_task, 1, LV_TASK_PRIO_LOW, NULL);
}

static void lv_fm_stats_tab_create(lv_obj_t * parent)
{
  lv_obj_t * h;
  lv_coord_t grid_w = lv_page_get_width_grid(parent, 2, 1);
  lv_coord_t grid_h = lv_page_get_height_grid(parent, 2, 1);
  
  lv_page_set_scrl_layout(parent, LV_LAYOUT_GRID);
  
  h = lv_cont_create(parent, NULL);
  lv_obj_set_size(h, grid_w, grid_h);
  lv_cont_set_layout(h, LV_LAYOUT_PRETTY_TOP);
  lv_obj_add_style(h, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(h, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Performance");
  
  cpu_bar = lv_bar_create(h, NULL);
  lv_obj_set_width(cpu_bar, lv_obj_get_width_fit(h));
  
  lram_bar = lv_bar_create(h, NULL);
  lv_obj_set_width(lram_bar, lv_obj_get_width_fit(h));
  
  hram_bar = lv_bar_create(h, NULL);
  lv_obj_set_width(hram_bar, lv_obj_get_width_fit(h));  
  
  h = lv_cont_create(parent, NULL);
  lv_obj_set_size(h, grid_w, grid_h);
  lv_cont_set_layout(h, LV_LAYOUT_GRID);
  lv_obj_add_style(h, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(h, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "FTP Server");  
  
  lb_conn = lv_label_create(h, NULL);
  lv_label_set_text(lb_conn, "\0");
  
  grid_w = lv_page_get_width_grid(parent, 1, 1);
  ta_log = lv_textarea_create(parent, NULL);
  lv_obj_set_size(ta_log, grid_w, grid_h);
  lv_obj_add_style(ta_log, LV_CONT_PART_MAIN, &style_side_title);
  lv_obj_set_style_local_value_str(ta_log, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Log");
  lv_textarea_set_cursor_hidden(ta_log, true);
  lv_textarea_set_text(ta_log, "\0");
  
  stats_task = lv_task_create(lv_fm_stats_task, 500, LV_TASK_PRIO_LOW, NULL);
}

static lv_obj_t * lv_fm_list_create(lv_obj_t * parent, 
                                    lv_coord_t x, lv_coord_t y, 
                                    lv_coord_t w, lv_coord_t h, 
                                    lv_align_t align, 
                                    const char ** str, 
                                    lv_event_cb_t event_cb)
{
	int32_t i;
	lv_obj_t * btn;
	lv_obj_t * list = lv_list_create(parent, NULL);

	lv_obj_set_size(list, w, h);
	lv_obj_align(list, parent, align, x, y);
	lv_group_add_obj(g, list);

	for(i = 0; str != NULL && str[i] != NULL; i += 2)
	{
		btn = lv_list_add_btn(list, str[i], str[i + 1]);
		lv_obj_set_event_cb(btn, event_cb);
	}

	return list;
}

static lv_obj_t * lv_fm_mbox_create(lv_obj_t * parent, const char * txt, const char ** str, lv_event_cb_t event_cb)
{
	lv_obj_t * m = lv_msgbox_create(parent, NULL);
	lv_msgbox_set_text(m, txt);
	lv_msgbox_add_btns(m, str);
	lv_obj_set_event_cb(m, event_cb);
	lv_obj_align(m, NULL, LV_ALIGN_CENTER, 0, 0);

	return m;
}

static lv_obj_t * lv_fm_remote_cont_create(lv_obj_t * parent, lv_fm_obj_t * obj)
{
    char iptxt[20];
    lv_obj_t * h;
    lv_obj_t * btn;
    lv_obj_t * label;
    lv_coord_t grid_w = lv_page_get_width_grid(parent, 1, 1);
    lv_coord_t grid_h = lv_page_get_height_grid(parent, 1, 1);
    lv_coord_t pad = lv_obj_get_style_pad_inner(parent, LV_PAGE_PART_SCROLLABLE);    

    h = lv_cont_create(parent, NULL);
    lv_obj_add_protect(h, LV_PROTECT_POS);
    lv_obj_set_size(h, grid_w - 2 * pad, grid_h - pad);
    lv_obj_align(h, list_remote, LV_ALIGN_CENTER, 0, 0);
    lv_cont_set_layout(h, LV_LAYOUT_GRID);
    
    ta_name_remote = lv_textarea_create(h, NULL);
    lv_obj_set_width(ta_name_remote, grid_w/2 - pad * 3);
    lv_obj_align(ta_name_remote, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
    lv_obj_add_style(ta_name_remote, LV_CONT_PART_MAIN, &style_side_title);
    lv_obj_set_style_local_value_str(ta_name_remote, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Server Name");    
    lv_textarea_set_one_line(ta_name_remote, true);
    lv_textarea_set_cursor_hidden(ta_name_remote, true);
    lv_obj_set_event_cb(ta_name_remote, lv_fm_textarea_event_cb);    

    ta_ip_remote = lv_textarea_create(h, NULL);
    lv_obj_set_width(ta_ip_remote, grid_w/2 - pad * 3);
    lv_obj_align(ta_ip_remote, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
    lv_obj_add_style(ta_ip_remote, LV_CONT_PART_MAIN, &style_side_title);
    lv_obj_set_style_local_value_str(ta_ip_remote, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "IP Address");    
    lv_textarea_set_one_line(ta_ip_remote, true);
    lv_textarea_set_cursor_hidden(ta_ip_remote, true);
    lv_obj_set_event_cb(ta_ip_remote, lv_fm_textarea_event_cb);

    ta_user_remote = lv_textarea_create(h, NULL);
    lv_obj_set_width(ta_user_remote, grid_w/2 - pad * 3);
    lv_obj_align(ta_user_remote, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
    lv_obj_add_style(ta_user_remote, LV_CONT_PART_MAIN, &style_side_title);
    lv_obj_set_style_local_value_str(ta_user_remote, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "FTP User");    
    lv_textarea_set_one_line(ta_user_remote, true);
    lv_textarea_set_cursor_hidden(ta_user_remote, true);
    lv_obj_set_event_cb(ta_user_remote, lv_fm_textarea_event_cb);
    
    ta_port_remote = lv_textarea_create(h, NULL);
    lv_obj_set_width(ta_port_remote, grid_w/2 - pad * 3);
    lv_obj_align(ta_port_remote, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
    lv_obj_add_style(ta_port_remote, LV_CONT_PART_MAIN, &style_side_title);
    lv_obj_set_style_local_value_str(ta_port_remote, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Port");    
    lv_textarea_set_one_line(ta_port_remote, true);
    lv_textarea_set_cursor_hidden(ta_port_remote, true);
    lv_obj_set_event_cb(ta_port_remote, lv_fm_textarea_event_cb);    

    ta_pass_remote = lv_textarea_create(h, NULL);
    lv_obj_set_width(ta_pass_remote, grid_w/2 - pad * 3);
    lv_obj_align(ta_pass_remote, h, LV_ALIGN_IN_TOP_LEFT, 0, 0);
    lv_obj_add_style(ta_pass_remote, LV_CONT_PART_MAIN, &style_side_title);
    lv_obj_set_style_local_value_str(ta_pass_remote, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "FTP Pass");    
    lv_textarea_set_one_line(ta_pass_remote, true);
    lv_textarea_set_cursor_hidden(ta_pass_remote, true);
    lv_obj_set_event_cb(ta_pass_remote, lv_fm_textarea_event_cb);
    
    if (obj != NULL)
    {
        lv_textarea_set_text(ta_name_remote, obj->name);
        lv_textarea_set_text(ta_user_remote, obj->user);
        lv_textarea_set_text(ta_pass_remote, obj->pass);

        sprintf((char *)iptxt, "%d.%d.%d.%d", obj->ip_addr[0], obj->ip_addr[1], 
                                              obj->ip_addr[2], obj->ip_addr[3]);
        lv_textarea_set_text(ta_ip_remote, iptxt);
        
        sprintf((char *)iptxt, "%d", obj->port);
        lv_textarea_set_text(ta_port_remote, iptxt);        
    }
    else
    {
        lv_textarea_set_text(ta_name_remote, "");
        lv_textarea_set_text(ta_user_remote, "admin");
        lv_textarea_set_text(ta_pass_remote, "1234");
        lv_textarea_set_text(ta_ip_remote, "192.168.1.1");
        lv_textarea_set_text(ta_port_remote, "21");
    }
    
    lv_cont_set_layout(h, LV_LAYOUT_OFF);

    btn = lv_btn_create(h, NULL);  
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, "Ok");
    lv_obj_align(btn, h, LV_ALIGN_IN_BOTTOM_LEFT, pad, -pad);
    lv_obj_set_event_cb(btn, lv_fm_cont_remote_btn_event_cb);

    btn = lv_btn_create(h, NULL);  
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, "Cancel");
    lv_obj_align(btn, h, LV_ALIGN_IN_BOTTOM_RIGHT, - pad, -pad);
    lv_obj_set_event_cb(btn, lv_fm_cont_remote_btn_event_cb);    
  
    return h;
}

static void lv_fm_list_local_event_cb(lv_obj_t * obj, lv_event_t e)
{
    lv_coord_t grid_h = lv_page_get_height_grid(t1, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(t1, 3, 1);
    
    if (e == LV_EVENT_LONG_PRESSED || \
        e == LV_EVENT_RIGHT_CLICKED)
    {
        if (mbox_err == NULL && \
            list_options == NULL && \
            player_h == NULL && \
            player_spin_h == NULL && \
            loader_h == NULL && \
            h == NULL && \
            h_spin == NULL && \
            img == NULL && \
            fm_obj_list != NULL && \
            strcmp(fm_obj_list->name, "..") == 0)
        {
            list_options = lv_fm_list_create(lv_scr_act(), 
                                             0, 0, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, 
                                             str_new_opt, lv_fm_list_options_local_btn_event_cb);
            lv_obj_align(list_options, list_local, LV_ALIGN_IN_RIGHT_MID, 0, 0);
        }        
    }
}

static void lv_fm_list_local_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
    static lv_event_t last_e = _LV_EVENT_LAST;
    
    uint32_t btn_idx;
    lv_obj_t * label;
    lv_fm_obj_t * obj, * sobj;
    lv_btn_state_t btn_state;
    lv_fm_task_data_t * td = &fm_task_data;
    lv_coord_t grid_h = lv_page_get_height_grid(t1, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(t1, 3, 1);
        
    label = _lv_fm_get_child(btn, "lv_label");
    
    lv_group_focus_obj(list_local);
    lv_group_set_editing(g, true);
    
    if (e == LV_EVENT_FOCUSED)
    {
        lv_label_set_long_mode(label, LV_LABEL_LONG_SROLL_CIRC);
    }
    
    if (e == LV_EVENT_DEFOCUSED)
    {
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    }   

    if (e == LV_EVENT_CLICKED)
    {
        if (last_e != LV_EVENT_LONG_PRESSED)
        {
            btn_idx = lv_list_get_btn_index(list_local, btn);
            LV_FM_LL_SEARCH(fm_obj_list, obj, idx, btn_idx, LV_FM_EQ);

            if (mbox_err == NULL && \
                player_h == NULL && \
                player_spin_h == NULL && \
                loader_h == NULL && \
                h == NULL && \
                h_spin == NULL && \
                img == NULL)
            {
                if(list_options == NULL || \
                   td->main_state == LV_FM_MAIN_COPY)
                {
                    if(obj->volume == 1 && \
                       obj->folder == 1)
                    {
                        td->list_vol = 1;
                        td->active_list = list_local;
                        _lv_fm_dir_read_start(td);
                    }
                    else if(obj->volume == 1 && \
                            obj->folder == 0)
                    {
                        if (f_chdrive (obj->name) == FR_OK)
                        {
                            td->list_vol = 0;
                            td->active_list = list_local;
                            _lv_fm_dir_read_start(td);
                        }
                    }
                    else if(obj->volume == 0 && \
                            obj->folder == 1)
                    {
                        if (f_chdir (obj->name) == FR_OK)
                        {
                            td->list_vol = 0;
                            td->active_list = list_local;
                            _lv_fm_dir_read_start(td);
                        }
                    }
                    else if(obj->format == wav || \
                            obj->format == mp3 || \
                            obj->format == flac || \
                            obj->format == jmv)
                    {
                        if (list_options == NULL)
                        {
                            td->fr = f_open (&td->src, obj->name, FA_READ);
                            if (td->fr == FR_OK)
                            {
                                td->err = (lv_fm_err_t) lv_fm_local_player_start((lv_fm_player_format_t) obj->format,
                                                                                 audiodevice,
                                                                                 &td->src);
                                
                                if (autoplay_en == 1)
                                {
                                    td->autoplay_site = obj;
                                    autoplay_task = lv_task_create(lv_fm_list_local_autoplay_task, 1000, LV_TASK_PRIO_LOW, td);
                                }                                
                            }
                        }
                    }
                    else if(obj->format == jpeg)
                    {
                        if (list_options == NULL)
                        {
                            img = lv_img_create(lv_scr_act(), NULL);
                            lv_img_set_src(img, obj->name);
                            lv_obj_set_drag(img, true);
                            lv_obj_align(img, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
                            lv_obj_set_event_cb(img, lv_fm_img_event_cb);
    
                            if(lv_obj_get_width(img) == 0 || lv_obj_get_height(img) == 0)
                            {
                                LV_FM_OBJ_DEL(img)
                            }
                        }
                    }
                    else if(obj->format == bin)
                    {
                        if (list_options == NULL)
                        {
                            td->fr = f_open (&td->src, obj->name, FA_READ);
                            if (td->fr == FR_OK)
                            {
                                td->err = (lv_fm_err_t) lv_fm_loader_init(t1, &td->src);
                            }
                        }
                    }
                }
                else
                {                
                    if (lv_btn_get_checkable(btn) == false)
                    {                
                        lv_btn_set_checkable(btn, true);
                        lv_btn_set_state(btn, LV_BTN_STATE_CHECKED_RELEASED);
                    }
                    
                    btn_state = lv_btn_get_state(btn);
                    if (btn_state & LV_STATE_CHECKED)
                    {
                        sobj = LV_FM_LL_ADD(fm_sobj_list);
                        LV_FM_LL_COPY(sobj, obj);                
                    }
                    else
                    {
                        LV_FM_LL_SEARCH(fm_sobj_list, sobj, idx, btn_idx, LV_FM_EQ);
                        LV_FM_LL_DEL(fm_sobj_list, sobj);
                    }
                    
                    if (fm_sobj_list != NULL && \
                        fm_sobj_list->next != NULL && \
                        fm_sobj_list->next->next == NULL)
                    {
                        const char *txt[] = {"Rename","Format","New folder",NULL};
                        _lv_fm_list_btns_hidden(list_options, txt, true);
                    }
                    else if (fm_sobj_list != NULL && \
                             fm_sobj_list->next == NULL)
                    {
                        const char *txt[] = {"Rename","Format","New folder",NULL};
                        _lv_fm_list_btns_hidden(list_options, txt, false);                    
                    }
                    
                    if (fm_sobj_list == NULL && \
                        td->main_state == LV_FM_MAIN_IDLE)
                    {
                        LV_FM_OBJ_DEL(list_options)
                    }                
                }
            }
        }

        last_e = _LV_EVENT_LAST;
    }

    if (e == LV_EVENT_LONG_PRESSED || \
        e == LV_EVENT_RIGHT_CLICKED)
    {
        btn_idx = lv_list_get_btn_index(list_local, btn);
        LV_FM_LL_SEARCH(fm_obj_list, obj, idx, btn_idx, LV_FM_EQ);

        if (mbox_err == NULL && \
            list_options == NULL && \
            player_h == NULL && \
            player_spin_h == NULL && \
            loader_h == NULL && \
            h == NULL && \
            h_spin == NULL && \
            img == NULL && \
            strcmp(obj->name, "..") != 0)
        {           
            if(obj->volume == 1 && \
               obj->folder == 0)
            {
                list_options = lv_fm_list_create(lv_scr_act(), 
                                                 0, 0, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, 
                                                 str_vol_opt, lv_fm_list_options_local_btn_event_cb);
                lv_obj_align(list_options, list_local, LV_ALIGN_IN_RIGHT_MID, 0, 0);
            }
            else if(obj->volume == 0 && \
                    obj->folder == 1)
            {
                list_options = lv_fm_list_create(lv_scr_act(), 
                                                 0, 0, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, 
                                                 str_file_opt, lv_fm_list_options_local_btn_event_cb);
                lv_obj_align(list_options, list_local, LV_ALIGN_IN_RIGHT_MID, 0, 0);
            }
            else if(obj->volume == 0 && \
                    obj->folder == 0)
            {
                list_options = lv_fm_list_create(lv_scr_act(), 
                                                 0, 0, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, 
                                                 str_file_opt, lv_fm_list_options_local_btn_event_cb);
                lv_obj_align(list_options, list_local, LV_ALIGN_IN_RIGHT_MID, 0, 0);
            }
            
            if (fm_remote_obj_list == NULL || \
                (fm_remote_obj_list->volume == 1 && \
                 fm_remote_obj_list->folder == 0))
            {
                const char *txt[] = {"Upload",NULL};
                _lv_fm_list_btns_hidden(list_options, txt, true);    
            }
            else
            {
                const char *txt[] = {"Upload",NULL};
                _lv_fm_list_btns_hidden(list_options, txt, false);
            }            
            
            lv_btn_set_checkable(btn, true);
            if (e == LV_EVENT_RIGHT_CLICKED) 
                lv_btn_set_state(btn, LV_BTN_STATE_CHECKED_RELEASED);
            sobj = LV_FM_LL_ADD(fm_sobj_list);
            LV_FM_LL_COPY(sobj, obj);
        }
        
        last_e = e;
    }
}

static void lv_fm_list_remote_event_cb(lv_obj_t * obj, lv_event_t e)
{
    lv_coord_t grid_h = lv_page_get_height_grid(t2, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(t2, 3, 1);
    
    if (e == LV_EVENT_LONG_PRESSED || \
        e == LV_EVENT_RIGHT_CLICKED)
    {
        if (mbox_err == NULL && \
            list_options == NULL && \
            player_h == NULL && \
            player_spin_h == NULL && \
            loader_h == NULL && \
            h == NULL && \
            h_spin == NULL && \
            img == NULL)
        {
            if (fm_remote_obj_list == NULL || \
                (fm_remote_obj_list->volume == 1 && \
                 fm_remote_obj_list->folder == 0))
            {
                h = lv_fm_remote_cont_create(t2, NULL);
            }
            else
            {
                list_options = lv_fm_list_create(lv_scr_act(), 
                                                 0, 0, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, 
                                                 str_new_opt, lv_fm_list_options_remote_btn_event_cb);
                lv_obj_align(list_options, list_remote, LV_ALIGN_IN_RIGHT_MID, 0, 0);            
            }
        }        
    }
}

static void lv_fm_list_remote_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
    static lv_event_t last_e = _LV_EVENT_LAST;
    
    uint32_t btn_idx;
    lv_obj_t * label;
    lv_fm_obj_t * obj, * sobj;
    lv_btn_state_t btn_state;
    lv_fm_task_data_t * td = &fm_task_data;
    lv_coord_t grid_h = lv_page_get_height_grid(t2, 1, 1);
    lv_coord_t grid_w = lv_page_get_width_grid(t2, 3, 1);
    
    label = _lv_fm_get_child(btn, "lv_label");
    
    lv_group_focus_obj(list_remote);
    lv_group_set_editing(g, true);
    
    if (e == LV_EVENT_FOCUSED)
    {
        lv_label_set_long_mode(label, LV_LABEL_LONG_SROLL_CIRC);
    }
    
    if (e == LV_EVENT_DEFOCUSED)
    {
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    }    
    
    if (e == LV_EVENT_CLICKED)
    {
        if (last_e != LV_EVENT_LONG_PRESSED)
        {
            btn_idx = lv_list_get_btn_index(list_remote, btn);
            LV_FM_LL_SEARCH(fm_remote_obj_list, obj, idx, btn_idx, LV_FM_EQ);
            
            if (mbox_err == NULL && \
                player_h == NULL && \
                player_spin_h == NULL && \
                loader_h == NULL && \
                h == NULL && \
                h_spin == NULL && \
                img == NULL)
            {
                if(list_options == NULL || \
                   td->main_state == LV_FM_MAIN_COPY)
                {
                    if(obj->volume == 1 && \
                       obj->folder == 1)
                    {
                        td->list_vol = 1;
                        td->active_list = list_remote;
                        _lv_fm_dir_read_start(td);
                    }
                    else if(obj->volume == 1 && \
                            obj->folder == 0)
                    {
                        memset(td->remote_path, 0, sizeof(td->remote_path));
                        
                        sobj = LV_FM_LL_ADD(fm_sobj_list);
                        LV_FM_LL_COPY(sobj, obj);
                        LV_FM_LL_COPY(&td->active_site, obj);
                        
                        td->err = lv_lwftp_connect(&td->lwftp_session, 
                                                   lv_lwftp_CWD_cb, 
                                                   obj->ip_addr,
                                                   obj->port,
                                                   obj->user,
                                                   obj->pass,
                                                   NULL,
                                                   NULL);
                    }
                    else if(obj->volume == 0 && \
                            obj->folder == 1)
                    {
                        sobj = LV_FM_LL_ADD(fm_sobj_list);
                        LV_FM_LL_COPY(sobj, obj);                        
                        
                        td->err = lv_lwftp_connect(&td->lwftp_session, 
                                                   lv_lwftp_CWD_cb, 
                                                   obj->ip_addr,
                                                   obj->port,
                                                   obj->user,
                                                   obj->pass,
                                                   obj->name,
                                                   NULL);
                        if (td->err) {
                            td->list_vol = 1;
                            td->active_list = list_remote;
                            _lv_fm_dir_read_start(td);                            
                        }
                    }
                    else if(obj->format == wav || \
                            obj->format == mp3 || \
                            obj->format == flac || \
                            obj->format == jmv)
                    {
                        if (list_options == NULL)
                        {
                            td->err = (lv_fm_err_t) lv_fm_remote_player_start((lv_fm_player_format_t) obj->format,
                                                                              audiodevice,
                                                                              obj->size,
                                                                              obj->user, obj->pass,
                                                                              obj->name, td->remote_path,
                                                                              obj->ip_addr, obj->port,
                                                                              &td->lwftp_session);
                            
                            if (autoplay_en == 1)
                            {
                                td->autoplay_site = obj;
                                autoplay_task = lv_task_create(lv_fm_list_remote_autoplay_task, 1000, LV_TASK_PRIO_LOW, td);
                            }                            
                        }
                    }                    
                }
                else
                {                
                    if (lv_btn_get_checkable(btn) == false)
                    {                
                        lv_btn_set_checkable(btn, true);
                        lv_btn_set_state(btn, LV_BTN_STATE_CHECKED_RELEASED);
                    }
                    
                    btn_state = lv_btn_get_state(btn);
                    if (btn_state & LV_STATE_CHECKED)
                    {
                        sobj = LV_FM_LL_ADD(fm_sobj_list);
                        LV_FM_LL_COPY(sobj, obj);                
                    }
                    else
                    {
                        LV_FM_LL_SEARCH(fm_sobj_list, sobj, idx, btn_idx, LV_FM_EQ);
                        LV_FM_LL_DEL(fm_sobj_list, sobj);
                    }
                    
                    if (fm_sobj_list != NULL && \
                        fm_sobj_list->next != NULL && \
                        fm_sobj_list->next->next == NULL)
                    {
                        const char *txt[] = {"Edit","Remove", "Rename","New folder",NULL};
                        _lv_fm_list_btns_hidden(list_options, txt, true);
                    }
                    else if (fm_sobj_list != NULL && \
                             fm_sobj_list->next == NULL)
                    {
                        const char *txt[] = {"Edit","Remove", "Rename","New folder",NULL};
                        _lv_fm_list_btns_hidden(list_options, txt, false);                    
                    }
                    
                    if (fm_sobj_list == NULL && \
                        td->main_state == LV_FM_MAIN_IDLE)
                    {
                        LV_FM_OBJ_DEL(list_options)
                    }
                }
            }
        }
        
        last_e = _LV_EVENT_LAST;
    }
    
    if (e == LV_EVENT_LONG_PRESSED || \
        e == LV_EVENT_RIGHT_CLICKED)
    {
        btn_idx = lv_list_get_btn_index(list_remote, btn);
        LV_FM_LL_SEARCH(fm_remote_obj_list, obj, idx, btn_idx, LV_FM_EQ);        
        
        if (mbox_err == NULL && \
            list_options == NULL && \
            player_h == NULL && \
            player_spin_h == NULL && \
            loader_h == NULL && \
            h == NULL && \
            h_spin == NULL && \
            img == NULL && \
            strcmp(obj->name, "..") != 0)
        {
            if(obj->volume == 1 && \
               obj->folder == 0)
            {
                list_options = lv_fm_list_create(lv_scr_act(), 
                                                 0, 0, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, 
                                                 str_site_opt, lv_fm_list_options_remote_btn_event_cb);
                lv_obj_align(list_options, list_remote, LV_ALIGN_IN_RIGHT_MID, 0, 0);
            }
            else if(obj->volume == 0 && \
                    obj->folder == 1)
            {
                list_options = lv_fm_list_create(lv_scr_act(), 
                                                 0, 0, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, 
                                                 str_remote_opt, lv_fm_list_options_remote_btn_event_cb);
                lv_obj_align(list_options, list_remote, LV_ALIGN_IN_RIGHT_MID, 0, 0);
            }
            else if(obj->volume == 0 && \
                    obj->folder == 0)
            {
                list_options = lv_fm_list_create(lv_scr_act(), 
                                                 0, 0, grid_w, grid_h, LV_ALIGN_IN_RIGHT_MID, 
                                                 str_remote_opt, lv_fm_list_options_remote_btn_event_cb);
                lv_obj_align(list_options, list_remote, LV_ALIGN_IN_RIGHT_MID, 0, 0);
            }
            
            if (fm_obj_list == NULL || \
                (fm_obj_list->volume == 1 && \
                 fm_obj_list->folder == 0))
            {
                const char *txt[] = {"Download",NULL};
                _lv_fm_list_btns_hidden(list_options, txt, true);    
            }
            else
            {
                const char *txt[] = {"Download",NULL};
                _lv_fm_list_btns_hidden(list_options, txt, false);
            }            
            
            lv_btn_set_checkable(btn, true);
            if (e == LV_EVENT_RIGHT_CLICKED) 
                lv_btn_set_state(btn, LV_BTN_STATE_CHECKED_RELEASED);
            sobj = LV_FM_LL_ADD(fm_sobj_list);
            LV_FM_LL_COPY(sobj, obj);            
        }
        
        last_e = e;
    }    
}

static void lv_fm_cont_remote_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
    char * pstr;
    uint32_t cnt = 0;
    lv_fm_obj_t * obj, * tobj;
    lv_fm_task_data_t * td = &fm_task_data;
    
    if (e == LV_EVENT_CLICKED)
    {
        pstr = (char *) lv_list_get_btn_text(btn);
        
        if (strcmp(pstr, "Ok") == 0)
        {
            if (fm_sobj_list != NULL)
            {
                LV_FM_LL_SEARCH(fm_remote_site_list, obj, idx, fm_sobj_list->idx, LV_FM_EQ);
                for (tobj = obj->next ; tobj != NULL; tobj = tobj->next)
                    tobj->idx--;            
                LV_FM_LL_DEL(fm_remote_site_list, obj);
                
                LV_FM_LL_CLEAN(fm_sobj_list);                    
            }
            
            obj = LV_FM_LL_ADD(fm_remote_site_list);
            
            pstr = (char *) lv_textarea_get_text(ta_name_remote);
            strcpy(obj->name, pstr);            
            pstr = (char *) lv_textarea_get_text(ta_user_remote);
            strcpy(obj->user, pstr);
            pstr = (char *) lv_textarea_get_text(ta_pass_remote);
            strcpy(obj->pass, pstr);                        
            pstr = (char *) lv_textarea_get_text(ta_ip_remote);
            _lv_fm_str_to_ip(pstr, obj->ip_addr);
            pstr = (char *) lv_textarea_get_text(ta_port_remote);
            sscanf(pstr, "%d", &obj->port);
            obj->volume = 1;
            obj->folder = 0;
            
            LV_FM_LL_COUNT(fm_remote_site_list, cnt);
            obj->idx = cnt - 1;
            
            td->list_vol = 1;
            td->active_list = list_remote;
            _lv_fm_dir_read_start(&fm_task_data);
        }
        
        LV_FM_OBJ_DEL(h)
    }
}

static void lv_fm_list_options_local_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
    char * pstr;
    lv_obj_t * label;
    lv_fm_task_data_t * td = &fm_task_data;
    
    lv_group_focus_obj(list_options);
    lv_group_set_editing(g, true);

    if (e == LV_EVENT_CLICKED)
    {
        pstr = (char *) lv_list_get_btn_text(btn);

        if (strcmp(pstr, "Upload") == 0)
        {
            td->main_state = LV_FM_MAIN_UPLOAD;
            op_task = lv_task_create(lv_fm_remote_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);
        }        
        else if (strcmp(pstr, "Copy") == 0)
        {
            td->flag_cut = 0;
            td->main_state = LV_FM_MAIN_COPY;
            op_task = lv_task_create(lv_fm_local_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);

            label = lv_obj_get_child(btn, NULL);
            lv_label_set_text(label, "Paste");
            
            const char *txt[] = {"Cut","Delete","Rename","Format","New folder",NULL};
            _lv_fm_list_btns_hidden(list_options, txt, true);            
        }
        else if (strcmp(pstr, "Cut") == 0)
        {
            td->flag_cut = 1;
            td->main_state = LV_FM_MAIN_COPY;
            op_task = lv_task_create(lv_fm_local_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);

            label = lv_obj_get_child(btn, NULL);
            lv_label_set_text(label, "Paste");
            
            const char *txt[] = {"Copy","Delete","Rename","Format","New folder",NULL};
            _lv_fm_list_btns_hidden(list_options, txt, true);            
        }
        else if (strcmp(pstr, "Paste") == 0)
        {
            td->main_state = LV_FM_MAIN_PASTE;
            op_task = lv_task_create(lv_fm_local_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);
        }
        else if (strcmp(pstr, "Delete") == 0)
        {
            td->main_state = LV_FM_MAIN_DELETE;
            mbox_question = lv_fm_mbox_create(lv_scr_act(),
                                              "Confirm Delete.",
                                              btns01,
                                              lv_fm_mbox_question_local_btn_event_cb);
        }
        else if (strcmp(pstr, "Rename") == 0)
        {
            td->main_state = LV_FM_MAIN_RENAME;
            op_task = lv_task_create(lv_fm_local_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);
        }
        else if (strcmp(pstr, "Format") == 0)
        {
            td->main_state = LV_FM_MAIN_FORMAT;
            mbox_question = lv_fm_mbox_create(lv_scr_act(),
                                              "Confirm Format.",
                                              btns01,
                                              lv_fm_mbox_question_local_btn_event_cb);
        }
        else if (strcmp(pstr, "New folder") == 0)
        {
            td->main_state = LV_FM_MAIN_NEWFOLDER;
            op_task = lv_task_create(lv_fm_local_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);
        }
        else if (strcmp(pstr, "Cancel") == 0)
        {
            td->main_state = LV_FM_MAIN_IDLE;
            
            LV_FM_TREE_CLEAN(fm_op_list);
            LV_FM_LL_CLEAN(fm_sobj_list);
            
            _lv_fm_list_btns_checkable(list_local, false);

            LV_FM_OBJ_DEL(list_options)
        }
    }
}

static void lv_fm_list_options_remote_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
    char * pstr;
    lv_fm_obj_t * obj, * tobj;
    lv_fm_task_data_t * td = &fm_task_data;

    lv_group_focus_obj(list_options);
    lv_group_set_editing(g, true);

    if (e == LV_EVENT_CLICKED)
    {
        pstr = (char *) lv_list_get_btn_text(btn);
        
        if (strcmp(pstr, "Download") == 0)
        {
            td->main_state = LV_FM_MAIN_DOWNLOAD;
            op_task = lv_task_create(lv_fm_remote_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);
        }
        else if (strcmp(pstr, "Delete") == 0)
        {
            td->main_state = LV_FM_MAIN_DELETE;
            mbox_question = lv_fm_mbox_create(lv_scr_act(),
                                              "Confirm Delete.",
                                              btns01,
                                              lv_fm_mbox_question_remote_btn_event_cb);            
        }
        else if (strcmp(pstr, "Rename") == 0)
        {
            td->main_state = LV_FM_MAIN_RENAME;
            op_task = lv_task_create(lv_fm_remote_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);
        }
        else if (strcmp(pstr, "New folder") == 0)
        {
            td->main_state = LV_FM_MAIN_NEWFOLDER;
            op_task = lv_task_create(lv_fm_remote_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);
        }
        else if (strcmp(pstr, "Edit") == 0)
        {
            h = lv_fm_remote_cont_create(t2, fm_sobj_list);
            LV_FM_OBJ_DEL(list_options)            
        }
        else if (strcmp(pstr, "Remove") == 0)
        {
            LV_FM_LL_SEARCH(fm_remote_site_list, obj, idx, fm_sobj_list->idx, LV_FM_EQ);
            for (tobj = obj->next ; tobj != NULL; tobj = tobj->next)
                tobj->idx--;            
            LV_FM_LL_DEL(fm_remote_site_list, obj);
            
            LV_FM_LL_CLEAN(fm_sobj_list);
            
            td->list_vol = 1;
            td->active_list = list_remote;
            _lv_fm_dir_read_start(td);            
            
            LV_FM_OBJ_DEL(list_options)            
        }
        else if (strcmp(pstr, "Cancel") == 0)
        {
            td->main_state = LV_FM_MAIN_IDLE;
            
            LV_FM_TREE_CLEAN(fm_op_list);
            LV_FM_LL_CLEAN(fm_sobj_list);
            
            _lv_fm_list_btns_checkable(list_remote, false);

            LV_FM_OBJ_DEL(list_options)
        }
    }
}

static void lv_fm_mbox_question_local_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
	char * pstr;
	lv_fm_task_data_t * td = &fm_task_data;

	if (e == LV_EVENT_CLICKED)
	{
		pstr = (char *) lv_msgbox_get_active_btn_text(mbox_question);

		if (strcmp(pstr, "Cancel") == 0)
		{
		    td->main_state = LV_FM_MAIN_IDLE;
			LV_FM_OBJ_DEL(mbox_question)
		}
		else if (strcmp(pstr, "Ok") == 0)
		{
		    op_task = lv_task_create(lv_fm_local_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);
			LV_FM_OBJ_DEL(mbox_question)
		}
	}
}

static void lv_fm_mbox_question_remote_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
    char * pstr;
    lv_fm_task_data_t * td = &fm_task_data;

    if (e == LV_EVENT_CLICKED)
    {
        pstr = (char *) lv_msgbox_get_active_btn_text(mbox_question);

        if (strcmp(pstr, "Cancel") == 0)
        {
            td->main_state = LV_FM_MAIN_IDLE;
            LV_FM_OBJ_DEL(mbox_question)
        }
        else if (strcmp(pstr, "Ok") == 0)
        {
            op_task = lv_task_create(lv_fm_remote_obj_ops_task, 1, LV_TASK_PRIO_LOW, td);
            LV_FM_OBJ_DEL(mbox_question)
        }
    }
}

static void lv_fm_copying_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
    lv_fm_task_data_t * td = &fm_task_data;
    
	if (e == LV_EVENT_CLICKED)
	{
	    td->main_state = LV_FM_MAIN_IDLE;
	}
}

static void lv_fm_mbox_err_btn_event_cb(lv_obj_t * btn, lv_event_t e)
{
	char * pstr;

	if (e == LV_EVENT_CLICKED)
	{
		pstr = (char *) lv_msgbox_get_active_btn_text(mbox_err);

		if (strcmp(pstr, "Ok") == 0)
		{
			LV_FM_OBJ_DEL(mbox_err)
		}
	}
}

static void lv_fm_img_event_cb(lv_obj_t * obj, lv_event_t e)
{
	if (e == LV_EVENT_CLICKED || \
		e == LV_EVENT_DEFOCUSED)
	{
		LV_FM_OBJ_DEL(img)
	}
}

static void lv_fm_kb_event_cb(lv_obj_t * _kb, lv_event_t e)
{
    char * pstr, tok_str[20];
    lv_obj_t * ta;
    lv_fm_task_data_t * td = &fm_task_data;
    app_netif_t * net = &app_netif;

    lv_keyboard_def_event_cb(kb, e);

    if(e == LV_EVENT_APPLY)
    {
        ta = lv_keyboard_get_textarea(kb);
        pstr = (char *) lv_textarea_get_text(ta);

        if(ta == ta_fn_local)
        {
            if (td->main_state == LV_FM_MAIN_RENAME)
            {
                if( f_rename ((TCHAR *) fm_sobj_list->name, (TCHAR*) pstr) != FR_OK)
                {
                    td->err = LV_FM_WRITE_ERROR;
                }                
            }
            else if (td->main_state == LV_FM_MAIN_NEWFOLDER)
            {
                if( f_mkdir ((TCHAR*) pstr) != FR_OK)
                {
                    td->err = LV_FM_WRITE_ERROR;
                }                
            }
            
            LV_FM_LL_CLEAN(fm_sobj_list);
            td->main_state = LV_FM_MAIN_IDLE;
            
            td->list_vol = 0;
            td->active_list = list_local;
            _lv_fm_dir_read_start(td);            
        }
        else if (ta == ta_fn_remote)
        {
            strcpy(td->new_name, pstr);
            file_task = lv_task_create(lv_fm_remote_file_task, 1, LV_TASK_PRIO_LOW, td);            
        }
        else if(ta == ta_ip_settings)
        {
            _lv_fm_str_to_ip(pstr, net->ip_addr);
            
            sprintf((char *)tok_str, "%d.%d.%d.%d", net->ip_addr[0], net->ip_addr[1], 
                                                    net->ip_addr[2], net->ip_addr[3]);
            lv_textarea_set_text(ta, (char *)tok_str);
        }
        else if(ta == ta_user_settings)
        {
            ftps_user_set((char *)pstr);
        }
        else if(ta == ta_pass_settings)
        {
            ftps_pass_set((char *)pstr);
        }

        lv_obj_set_height(tv, LV_VER_RES);
        LV_FM_OBJ_DEL(kb)
        
        if (ta == ta_fn_local || \
            ta == ta_fn_remote || \
            ta == ta_ip_settings || \
            ta == ta_user_settings || \
            ta == ta_pass_settings)
        LV_FM_OBJ_DEL(h)
    }

    if(e == LV_EVENT_CANCEL)
    {
        ta = lv_keyboard_get_textarea(kb);
        
        if(ta == ta_ip_settings)
        {
            sprintf((char *)tok_str, "%d.%d.%d.%d", net->ip_addr[0], net->ip_addr[1], 
                                                    net->ip_addr[2], net->ip_addr[3]);
            lv_textarea_set_text(ta, (char *)tok_str); 
        }
        else if(ta == ta_user_settings)
        {
            ftps_user_get((char *)tok_str);
            lv_textarea_set_text(ta, (char *)tok_str);
        }
        else if(ta == ta_pass_settings)
        {
            ftps_pass_get((char *)tok_str);
            lv_textarea_set_text(ta, (char *)tok_str); 
        }
        
        LV_FM_TREE_CLEAN(fm_op_list);
        LV_FM_LL_CLEAN(fm_sobj_list);
        td->main_state = LV_FM_MAIN_IDLE;
        
        lv_obj_set_height(tv, LV_VER_RES);
        LV_FM_OBJ_DEL(kb)
        
        if (ta == ta_fn_local || \
            ta == ta_fn_remote || \
            ta == ta_ip_settings || \
            ta == ta_user_settings || \
            ta == ta_pass_settings)
        LV_FM_OBJ_DEL(h)
    }
}

static void lv_fm_dropdown_event_cb(lv_obj_t * obj, lv_event_t e)
{
  char str[32];
  
  if(e == LV_EVENT_VALUE_CHANGED)
  {
    lv_dropdown_get_selected_str(obj, str, sizeof(str));
    
    if (obj == dd_audiodevice)
    {
      if (strcmp(&str[0], "Headphone jack") == 0)
      {
        audiodevice = OUTPUT_DEVICE_HEADPHONE;
      }
      else if (strcmp(&str[0], "HDMI") == 0)
      {
        audiodevice = OUTPUT_DEVICE_ADV7533_HDMI;
      }     
    }
    else if (obj == dd_timeout)
    {
        disp_timeout = atoi(str) * 1000;
    }
  }
}

static void lv_fm_checkbox_event_cb(lv_obj_t * obj, lv_event_t e)
{
    app_netif_t * net = &app_netif;
    
    if(e == LV_EVENT_VALUE_CHANGED)
    {
        if(obj == cb_dhcp)
        {
            if(lv_checkbox_is_checked(obj))
            {
                net->use_dhcp = 1;
            }
            else
            {
                net->use_dhcp = 0;
            }
        }
        else if(obj == cb_dispsh)
        {
            if(lv_checkbox_is_checked(obj))
            {
                disp_shutdown_en = 1;
            }
            else
            {
                disp_shutdown_en = 0;
            }
        }
        else if(obj == cb_autoplay)
        {
            if(lv_checkbox_is_checked(obj))
            {
                autoplay_en = 1;
            }
            else
            {
                autoplay_en = 0;
            }           
        }        
    }
}

static void lv_fm_textarea_event_cb(lv_obj_t * obj, lv_event_t e)
{
    lv_obj_t * par;
    
    if(e == LV_EVENT_CLICKED)
    {
        if(kb == NULL)
        {
            if (_lv_fm_is_parent(obj, t2))
                par = t2;
            else if (_lv_fm_is_parent(obj, t3))
                par = t3;
            
            lv_obj_set_height(tv, LV_VER_RES / 2);
            kb = lv_keyboard_create(lv_scr_act(), NULL);
            lv_obj_set_event_cb(kb, lv_fm_kb_event_cb);
            lv_page_focus(par, lv_textarea_get_label(obj), LV_ANIM_ON);
            lv_keyboard_set_textarea(kb, obj);
        }       
    }
}

static void lv_fm_spin_event_cb(lv_obj_t * obj, lv_event_t e)
{
    lv_fm_task_data_t * td = &fm_task_data;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;
    
    if (e == LV_EVENT_CLICKED)
    {       
        if (s->control_pcb != NULL)
        {
            lwftp_abort(s);
        }
        else
        {
            td->list_task_state = LV_FM_LIST_TASK_IDLE;
        }
    }
}

static void _lv_fm_err(lv_fm_err_t err)
{
	mbox_err = lv_fm_mbox_create(lv_scr_act(),
							 	 "Message",
								 btns00,
								 lv_fm_mbox_err_btn_event_cb);

	switch(err)
	{
        case LV_FM_NO_ERROR:
          break;
      
		case LV_FM_READ_ERROR:
			lv_msgbox_set_text(mbox_err, "Read error.");
			break;

		case LV_FM_WRITE_ERROR:
			lv_msgbox_set_text(mbox_err, "Write error.");
			break;

		case LV_FM_DELETE_ERROR:
			lv_msgbox_set_text(mbox_err, "Delete error.");
			break;

		case LV_FM_FORMAT_ERROR:
			lv_msgbox_set_text(mbox_err, "Format error.");
			break;

		case LV_FM_MEMORY_ERROR:
			lv_msgbox_set_text(mbox_err, "Memory error.");
			break;

		case LV_FM_FILE_ALREADY_EXISTS:
			lv_msgbox_set_text(mbox_err, "File already exists.");
			break;

		case LV_FM_UNSUPPORTED_FORMAT:
			lv_msgbox_set_text(mbox_err, "Unsupported format.");
			break;

        case LV_FM_AUDIO_DEVICE_ERROR:
            lv_msgbox_set_text(mbox_err, "Could not init audio device.");
            break;			
			
        case LV_FM_CONNECTION_ERROR:
            lv_msgbox_set_text(mbox_err, "Could not connect to remote.");
            break;			
	}
}

static uint8_t _lv_fm_media_detect(lv_fm_media_t * m)
{
	switch(m->media_hard)
	{
    case LV_FM_NO_MEDIA:
      break;
      
		case LV_FM_MEDIA_SD:
			return BSP_SD_IsDetected();

		case LV_FM_MEDIA_USB:
			if(m->husb != NULL && \
			   m->husb->valid == 1 && \
			   USBH_GetActiveClass(m->husb) == USB_MSC_CLASS)
			{
				return !m->drv->disk_status(m->husb, m->lun);
			}
			else
			{
				return 0;
			}
	}

	return 0;
}

static void _lv_fm_media_check(lv_fm_media_t * m)
{
	int32_t i;
	uint8_t media_present_now;
	lv_fm_media_t * tm;
	lv_fm_task_data_t * td = &fm_task_data;
	
	for (i = 0; i < LV_FM_MAX_VOLUMES; i++)
	{
		tm = &m[i];
		media_present_now = _lv_fm_media_detect(tm);
		
		if(media_present_now != tm->media_present)
		{
			if(media_present_now)
			{
				if(tm->media_hard == LV_FM_MEDIA_SD)
				{
					NVIC_DisableIRQ(OTG_HS_IRQn);
				}

				if( tm->drv->disk_initialize(tm->lun) != STA_NOINIT )
				{
					if( f_mount(&(tm->fat_fs), (TCHAR const*)tm->path, 1) == FR_OK )
					{
						tm->media_present = 1;
						tm->valid = 1;
					}
					else
					{
						tm->media_present = 0;
					}
				}
				else
				{
					tm->media_present = 0;
				}

				if(tm->media_hard == LV_FM_MEDIA_SD)
				{
					NVIC_EnableIRQ(OTG_HS_IRQn);
				}
			}
			else
			{
				tm->media_present = 0;
				tm->valid = 0;

				f_mount(NULL, (TCHAR const*)tm->path, 1);
			}

			td->list_vol = 1;
			td->active_list = list_local;
			_lv_fm_dir_read_start(td);
		}
	}	
}

static void _lv_fm_dir_read_start(lv_fm_task_data_t * td)
{    
    if (list_task || spin_task)
        return;
        
    h_spin = lv_cont_create(lv_scr_act(), NULL);
    lv_cont_set_layout(h_spin, LV_LAYOUT_PRETTY_MID);
    lv_cont_set_fit2(h_spin, LV_FIT_TIGHT, LV_FIT_TIGHT);
    lv_obj_set_width(h_spin, lv_page_get_width_grid(t1, 1, 1));
    lv_obj_set_event_cb(h_spin, lv_fm_spin_event_cb);
    lv_obj_set_click(h_spin, true); 
    
    spin = lv_spinner_create(h_spin, NULL);
    lv_obj_set_size(spin, LV_DPI / 2, LV_DPI / 2);
    lv_obj_set_style_local_line_width(spin, LV_SPINNER_PART_BG, LV_STATE_DEFAULT, LV_DPI / 10);
    lv_obj_set_style_local_line_width(spin, LV_SPINNER_PART_INDIC, LV_STATE_DEFAULT, LV_DPI / 10);
    lv_obj_set_event_cb(spin, lv_fm_spin_event_cb);
    lv_obj_set_click(spin, true);   
    
    lb_spin = lv_label_create(h_spin, NULL);
    lv_label_set_text(lb_spin, "");
    lv_obj_set_event_cb(lb_spin, lv_fm_spin_event_cb);
    lv_obj_set_click(lb_spin, true);    
    
    lv_obj_align(h_spin, NULL, LV_ALIGN_CENTER, 0, 0);

    if (td->active_list == list_local)
        list_task = lv_task_create(lv_fm_list_local_task, 1, LV_TASK_PRIO_LOW, td);
    else if (td->active_list == list_remote)
        list_task = lv_task_create(lv_fm_list_remote_task, 1, LV_TASK_PRIO_LOW, td);
    spin_task = lv_task_create(lv_fm_spin_task, 100, LV_TASK_PRIO_LOW, td);
    
    td->list_task_state = LV_FM_LIST_TASK_CLEAN;
}

static void _lv_fm_list_add_obj_btn(lv_obj_t * list, lv_fm_obj_t * obj, lv_event_cb_t event_cb)
{
    char str[256], * pimg;
    uint8_t year, month, day;
    lv_obj_t * btn, * label, * img;
    
    if(obj->volume == 1 && \
       obj->folder == 0)
        pimg = LV_SYMBOL_DRIVE;
    else if(obj->folder == 1)
        pimg = LV_SYMBOL_DIRECTORY;
    else if(obj->format == jpeg)
        pimg = LV_SYMBOL_IMAGE;
    else if(obj->format == bmp)
        pimg = LV_SYMBOL_IMAGE;
    else if(obj->format == flac)
        pimg = LV_SYMBOL_AUDIO;
    else if(obj->format == mp3)
        pimg = LV_SYMBOL_AUDIO;
    else if(obj->format == wav)
        pimg = LV_SYMBOL_AUDIO;
    else if(obj->format == jmv)
        pimg = LV_SYMBOL_VIDEO;
    else
        pimg = LV_SYMBOL_FILE;

    btn = lv_btn_create(list, NULL);
    lv_btn_set_layout(btn, LV_LAYOUT_GRID);
    lv_btn_set_fit2(btn, LV_FIT_PARENT, LV_FIT_TIGHT);
    
    lv_obj_add_protect(btn, LV_PROTECT_PRESS_LOST);
    lv_obj_set_event_cb(btn, event_cb);
    
    lv_theme_apply(btn, LV_THEME_LIST_BTN);
    lv_page_glue_obj(btn, true);
    
    img = lv_img_create(btn, NULL);
    lv_img_set_src(img, pimg);

    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, obj->name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    
    lv_obj_set_width(label, (lv_obj_get_width(list) * 7) / 10);
    
    if(strcmp(obj->name, "..") != 0)
    {
        year = (obj->date >> 9) & 0x7f;
        month = (obj->date >> 5) & 0xf;
        if (!month) month = 1;
        day = obj->date & 0x1f;
        if (!day) day = 1;
        
        if(obj->folder == 0 && obj->volume == 0)
        {
            if (obj->size >= (1 << 30))
                lv_snprintf(str, sizeof(str), "%d GB\n%02d-%02d-%02d", 
                            obj->size >> 30, day, month, year + 1980);
            else if (obj->size >= (1 << 20))
                lv_snprintf(str, sizeof(str), "%d MB\n%02d-%02d-%02d", 
                            obj->size >> 20, day, month, year + 1980);
            else
                lv_snprintf(str, sizeof(str), "%d KB\n%02d-%02d-%02d", 
                            obj->size >> 10, day, month, year + 1980);
        }
        else if(obj->folder == 1 && obj->volume == 0)
        {
            lv_snprintf(str, sizeof(str), "-\n%02d-%02d-%02d", 
                        day, month, year + 1980);
        }
        else if(obj->folder == 0 && obj->volume == 1)
        {
            char txt_size[3], txt_free[3];
            uint32_t obj_size, obj_free;
            
            if (obj->size >= (1 << 20))
            {
                obj_size = obj->size >> 20;
                strcpy(txt_size, "GB");
            }
            else if (obj->size >= (1 << 10))
            {
                obj_size = obj->size >> 10;
                strcpy(txt_size, "MB");
            }
            else
            {
                obj_size = obj->size;
                strcpy(txt_size, "KB");
            }                 
            
            if (obj->free >= (1 << 20))
            {
                obj_free = obj->free >> 20;
                strcpy(txt_free, "GB");
            }
            else if (obj->free >= (1 << 10))
            {
                obj_free = obj->free >> 10;
                strcpy(txt_free, "MB");
            }
            else
            {
                obj_free = obj->free;
                strcpy(txt_free, "KB");
            }               
            
            lv_snprintf(str, sizeof(str), "Size: %d %s\nFree: %d %s", 
                        obj_size, txt_size, obj_free, txt_free);
        }
    }
    else
    {
        lv_snprintf(str, sizeof(str), "\n");
    }
    
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, str);
    
    lv_obj_add_style(label, LV_OBJ_PART_MAIN, &style_small_font);    
}

static void _lv_fm_list_add_site_btn(lv_obj_t * list, lv_fm_obj_t * obj, lv_event_cb_t event_cb)
{
    char str[256];
    lv_obj_t * btn, * label, * img;
    
    btn = lv_btn_create(list, NULL);
    lv_btn_set_layout(btn, LV_LAYOUT_GRID);
    lv_btn_set_fit2(btn, LV_FIT_PARENT, LV_FIT_TIGHT);
    
    lv_obj_add_protect(btn, LV_PROTECT_PRESS_LOST);
    lv_obj_set_event_cb(btn, event_cb);
    
    lv_theme_apply(btn, LV_THEME_LIST_BTN);
    lv_page_glue_obj(btn, true);
    
    img = lv_img_create(btn, NULL);
    lv_img_set_src(img, LV_SYMBOL_DRIVE);

    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, obj->name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    lv_obj_set_width(label, (lv_obj_get_width(list) * 7) / 10);

    lv_snprintf(str, sizeof(str), "%d.%d.%d.%d", obj->ip_addr[0], obj->ip_addr[1],
                                                 obj->ip_addr[2], obj->ip_addr[3]);

    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, str);
    
    lv_obj_add_style(label, LV_OBJ_PART_MAIN, &style_small_font);                 
}

static lv_fm_format_t _lv_fm_get_ext(const char *fname)
{
	lv_fm_format_t Ext;

	if     (strstr(fname, ".bmp") || strstr(fname, ".BMP"))
		Ext = bmp;
	else if(strstr(fname, ".jpg") || strstr(fname, ".JPG"))
		Ext = jpeg;
	else if(strstr(fname, ".gif") || strstr(fname, ".GIF"))
		Ext = gif;
	else if(strstr(fname, ".wav") || strstr(fname, ".WAV"))
		Ext = wav;
	else if(strstr(fname, ".mp3") || strstr(fname, ".MP3"))
		Ext = mp3;
	else if(strstr(fname, ".jmv") || strstr(fname, ".JMV"))
		Ext = jmv;
	else if(strstr(fname, ".fla") || strstr(fname, ".FLA"))
		Ext = flac;
	else if(strstr(fname, ".bin") || strstr(fname, ".BIN"))
		Ext = bin;
	else
		Ext = other;

	return(Ext);
}

static void _lv_fm_list_btns_checkable(lv_obj_t * list, bool enable)
{
    lv_obj_t * scrl, * iter;
    
    scrl = lv_page_get_scrollable(list);
    for (iter = lv_obj_get_child(scrl, NULL); 
        iter != NULL; 
        iter = lv_obj_get_child(scrl, iter))
    {
        lv_btn_set_checkable(iter, enable);
        lv_btn_set_state(iter, LV_BTN_STATE_RELEASED);
    }
}

static void _lv_fm_list_btns_hidden(lv_obj_t * list, const char **txt, bool enable)
{
    char * pstr;
    uint32_t i;
    lv_obj_t * scrl, * iter;
    
    scrl = lv_page_get_scrollable(list);
    for (iter = lv_obj_get_child(scrl, NULL); 
        iter != NULL; 
        iter = lv_obj_get_child(scrl, iter))
    {
        pstr = (char *) lv_list_get_btn_text(iter);
        for (i = 0; txt != NULL && txt[i] != NULL; i++)
        {
            if (strcmp(pstr, txt[i]) == 0)
                lv_obj_set_hidden(iter, enable);
        }
    }
}

static bool _lv_fm_check_obj_type(lv_obj_t * obj, const char *type_str)
{
    lv_obj_type_t type;

    lv_obj_get_type(obj, &type);
    uint8_t cnt;
    for(cnt = 0; cnt < LV_MAX_ANCESTOR_NUM; cnt++) {
        if(type.type[cnt] == NULL) break;
        if(!strcmp(type.type[cnt], type_str)) return true;
    }
    return false;
}

lv_obj_t * _lv_fm_get_child(const lv_obj_t * parent, const char *type_str)
{
    lv_obj_t * child = lv_obj_get_child_back(parent, NULL);
    if(child == NULL) return NULL;

    while(_lv_fm_check_obj_type(child, type_str) == false) {
        child = lv_obj_get_child_back(parent, child);
        if(child == NULL) break;
    }

    return child;
}

static void _lv_fm_str_to_ip(const char * pstr, uint8_t * ip)
{
    uint8_t idx;
    char * token, tok_str[20];    
    
    strcpy(tok_str, pstr);
    
    for(idx = 0; idx < 4; idx++)
    {
        uint8_t ret_val, n;
        char ret_ch;
        
        if(idx == 0)
        {
            token = strtok(tok_str, ".");
        }
        else
        {
            token = strtok(NULL, ".");
        }
        
        n = sscanf(token, "%d%c", (int *)&ret_val, &ret_ch);
        
        if(token != NULL && n == 1)
        {
            ip[idx] = ret_val;
        }
        else
        {
            ip[idx] = 0;
        }
    }    
}

static uint8_t _lv_fm_month_to_int(const char *str)
{
    if (strcmp(str, "Jan") == 0) return 1;
    if (strcmp(str, "Feb") == 0) return 2;
    if (strcmp(str, "Mar") == 0) return 3;
    if (strcmp(str, "Apr") == 0) return 4;
    if (strcmp(str, "May") == 0) return 5;
    if (strcmp(str, "Jun") == 0) return 6;
    if (strcmp(str, "Jul") == 0) return 7;
    if (strcmp(str, "Aug") == 0) return 8;
    if (strcmp(str, "Sep") == 0) return 9;
    if (strcmp(str, "Oct") == 0) return 10;
    if (strcmp(str, "Nov") == 0) return 11;
    if (strcmp(str, "Dec") == 0) return 12;
    
    return 0;
}

static uint8_t _lv_fm_is_parent(lv_obj_t * obj, lv_obj_t * parent)
{
    lv_obj_t * tobj;
    
    for (tobj = obj->parent; tobj != NULL; tobj = tobj->parent)
    {
        if (tobj == parent) return 1;
    }
    
    return 0;
}

static void _lv_fm_obj_ops_start(lv_fm_task_data_t * td, lv_obj_t * par, lv_task_cb_t task_xcb)
{
    if (file_task || bar_task)
        return;
    
    h = lv_cont_create(par, NULL);
    lv_cont_set_layout(h, LV_LAYOUT_PRETTY_MID);
    lv_cont_set_fit2(h, LV_FIT_NONE, LV_FIT_TIGHT);
    lv_obj_set_width(h, lv_page_get_width_grid(par, 1, 1));

    bar = lv_bar_create(h, NULL);
    lv_obj_set_width(bar, lv_obj_get_width_fit(h));

    lv_obj_t * btn = lv_btn_create(h, NULL);
    lv_obj_t * label = lv_label_create(btn, NULL);
    lv_label_set_text(label ,"Cancel");
    lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
    lv_obj_set_width(btn, lv_obj_get_width_fit(h));
    lv_obj_set_event_cb(btn, lv_fm_copying_btn_event_cb);

    lv_obj_align(h, NULL, LV_ALIGN_CENTER, 0, 0);

    file_task = lv_task_create(task_xcb, 1, LV_TASK_PRIO_LOW, td);
    bar_task = lv_task_create(lv_fm_bar_task, 100, LV_TASK_PRIO_LOW, td);
}

static uint8_t _lv_fm_local_folder_scan(lv_fm_op_t ** op_par,
                                        uint32_t * total_cnt,
                                        uint64_t * total_sz)
{
    typedef enum {SCAN_CWD = 0, SCAN_INIT, SCAN_DIR} scan_states_t;

    static scan_states_t scan_state;
    static FRESULT     fr;
    static DIR         dir;
    static FILINFO     finfo;    
    static char src_str[256], dst_str[256];
    
    lv_fm_op_t * op;
        
    switch (scan_state)
    {
        case SCAN_CWD:        
            f_chdrive ((*op_par)->src_vol);
            if( f_chdir ((*op_par)->src_path) != FR_OK )
                return SCAN_CWD;

            scan_state = SCAN_INIT;     
            break;
            
        case SCAN_INIT:
            strcpy(dst_str, (*op_par)->dst_path);
            if(dst_str[3] != 0)
                strcat(dst_str, "/");                    
            strcat(dst_str, (*op_par)->name);

            finfo.lfname = (TCHAR *) &lfn_buffer[0];
            finfo.lfsize = 256;
            
            fr = f_findfirst (&dir, &finfo, "", "*");
            if(fr == FR_OK)
                scan_state = SCAN_DIR;
            else
                scan_state = SCAN_CWD;
            break;
            
        case SCAN_DIR:
            if(fr == FR_OK)
            {
                if(finfo.fname[0] == 0)
                    break;

                if( (finfo.fattrib == AM_ARC) || (finfo.fattrib == (AM_ARC | AM_RDO)) )
                {
                    if( finfo.fname[0] == '.' )
                    {
                        fr = f_findnext(&dir, &finfo);
                        break;
                    }

                    op = LV_FM_LL_ADD((*op_par)->child_ll);
                    op->parent_ll = *op_par;
                    op->size = finfo.fsize;

                    if(dir.lfn_idx != 0xFFFF)
                        strcpy(op->name, finfo.lfname);
                    else
                        strcpy(op->name, finfo.fname);
                    strcpy(op->src_path, (*op_par)->src_path);
                    strcpy(op->dst_path, dst_str);

                    (*total_cnt)++;
                    (*total_sz) += finfo.fsize;
                }
                else if(finfo.fattrib == AM_DIR)
                {
                    if( finfo.fname[0] == '.' || (finfo.fname[0] == '.' && finfo.fname[1] == '.') )
                    {
                        fr = f_findnext(&dir, &finfo);
                        break;
                    }
                                                         
                    op = LV_FM_LL_ADD((*op_par)->child_ll);
                    op->parent_ll = *op_par;
                    op->folder = 1;
                    op->size = finfo.fsize;                    
                    
                    if(dir.lfn_idx != 0xFFFF)
                        strcpy(op->name, finfo.lfname);
                    else
                        strcpy(op->name, finfo.fname);

                    strcpy(src_str, (*op_par)->src_path);
                    if(src_str[3] != 0)
                        strcat(src_str, "/");                    
                    strcat(src_str, op->name);                        
                    strcpy(op->src_path, src_str);
                    
                    strcpy(op->dst_path, dst_str);
                    strcpy(op->src_vol, (*op_par)->src_vol);
                    strcpy(op->dst_vol, (*op_par)->dst_vol);
                    (*total_cnt)++;                    
                }
                    
                fr = f_findnext(&dir, &finfo);
            }
            else
            {
                scan_state = SCAN_CWD;
            }
            break;
    }
    
    return scan_state;
}

static uint8_t _lv_fm_remote_folder_scan(lwftp_session_t * s,
                                         lv_fm_op_t ** op_par,
                                         uint32_t * total_cnt,
                                         uint64_t * total_sz)
{
    typedef enum {SCAN_CWD = 0, SCAN_INIT, SCAN_DIR} scan_states_t;
    
    static scan_states_t scan_state;
    static char src_str[256], dst_str[256];
    
    lv_fm_op_t * op;
    lv_fm_obj_t * obj;
    lv_fm_err_t err;
    
    switch (scan_state)
    {
        case SCAN_CWD:
            err = lv_lwftp_connect(s, 
                                   lv_lwftp_CWD_cb, 
                                   (*op_par)->ip_addr,
                                   (*op_par)->port,
                                   (*op_par)->user,
                                   (*op_par)->pass,
                                   (*op_par)->name,
                                   (*op_par)->src_path);
            if (err == LV_FM_NO_ERROR)
                scan_state = SCAN_INIT;
            break;
            
        case SCAN_INIT:
            if (s->control_pcb == NULL)
            {
                strcpy(src_str, (*op_par)->src_path);
                if(src_str[3] != 0)
                    strcat(src_str, "/");
                strcat(src_str, (*op_par)->name);                
                
                strcpy(dst_str, (*op_par)->dst_path);
                if(dst_str[3] != 0)
                    strcat(dst_str, "/");
                strcat(dst_str, (*op_par)->name);
                
                scan_state = SCAN_DIR;
            }
            break;
            
        case SCAN_DIR:
            for (obj = fm_remote_obj_list->next; obj != NULL; obj = obj->next)
            {
                op = LV_FM_LL_ADD((*op_par)->child_ll);
                op->parent_ll = *op_par;
                op->folder = obj->folder;
                op->size = obj->size;                
                
                strcpy(op->name, obj->name); 
                strcpy(op->src_path, src_str);
                strcpy(op->dst_path, dst_str);
                strcpy(op->src_vol, (*op_par)->src_vol);
                strcpy(op->dst_vol, (*op_par)->dst_vol);

                strcpy(op->user, (*op_par)->user);
                strcpy(op->pass, (*op_par)->pass);
                op->port = (*op_par)->port;
                memcpy(op->ip_addr, (*op_par)->ip_addr, sizeof((*op_par)->ip_addr));
                
                (*total_cnt)++;
                (*total_sz) += obj->size;
            }
        
            scan_state = SCAN_CWD;
            break;
    }
    
    return scan_state;
}

static lv_fm_err_t _lv_fm_unlink(const char *path,
                                 const char *name,
                                 bool folder)
{
    FRESULT res;
    char vol[4];
    
    strncpy(vol, path, 3);
    vol[3] = 0;
    
    f_chdrive (vol);
    res = f_chdir (path);
    if ( res != FR_OK )
        return LV_FM_READ_ERROR;
        
    if (folder == 1)
    {
        res = f_chdir ("..");
        if ( res != FR_OK )
            return LV_FM_READ_ERROR;        
    }
        
    res = f_unlink (name);
    if( res != FR_OK )
        return LV_FM_DELETE_ERROR;

    return LV_FM_NO_ERROR;    
}

static lv_fm_err_t _lv_fm_frename(const char *src_path,
                                  const char *dst_path,
                                  const char *name,
                                  bool folder)
{
    FRESULT res;
    char temp_src[256], temp_dst[256];
    
    strcpy(temp_src, src_path);
    strcpy(temp_dst, dst_path);
    
    if (folder == 0)
    {
        if (src_path[3] == 0)
        {
            strcat(temp_src, name);
        }
        else
        {
            strcat(temp_src, "/");
            strcat(temp_src, name);
        }
    }

    if (dst_path[3] == 0)
    {
        strcat(temp_dst, name);
    }
    else
    {
        strcat(temp_dst, "/");
        strcat(temp_dst, name);
    }

    res = f_rename ((TCHAR*) temp_src, (TCHAR*) temp_dst);
    if( res != FR_OK && res != FR_EXIST )
        return LV_FM_WRITE_ERROR;

    return LV_FM_NO_ERROR;
}

static lv_fm_err_t _lv_fm_fopen(FIL *fptr,
                                const char *path,
                                const char *name,
                                uint8_t mode)
{
    char vol[4];
    
    strncpy(vol, path, 3);
    vol[3] = 0;
    
    f_chdrive (vol);
    if (f_chdir (path) != FR_OK)
        return LV_FM_READ_ERROR;
    
    if (f_open (fptr, name, mode) != FR_OK)
    {
        if (mode == FA_READ)
            return LV_FM_READ_ERROR;
        else if (mode == (FA_CREATE_NEW | FA_WRITE))
            return LV_FM_WRITE_ERROR;
    }

    return LV_FM_NO_ERROR;
}

static lv_fm_err_t _lv_fm_fmkdir(const char *path,
                                 const char *name)
{
    FRESULT res;
    char vol[4];
    
    strncpy(vol, path, 3);
    vol[3] = 0;
    
    f_chdrive (vol);
    res = f_chdir (path);
    if ( res != FR_OK )
        return LV_FM_READ_ERROR;
    
    res = f_mkdir (name);
    if( res != FR_OK && res != FR_EXIST )
        return LV_FM_WRITE_ERROR;

    return LV_FM_NO_ERROR;    
}

static void _lv_fm_futime(const char * src_path,
                          const char * dst_path,
                          const char * name)
{
    FILINFO finfo;
    uint32_t len;
    char temp_src[256], temp_dst[256];
    
    stpcpy(temp_src, src_path);
    stpcpy(temp_dst, dst_path);
    
    len = strlen(temp_src);
    if (temp_src[len] != '/')
        strcat(temp_src, "/");
    strcat(temp_src, name);

    len = strlen(temp_dst);
    if (temp_dst[len] != '/')
        strcat(temp_dst, "/");
    strcat(temp_dst, name);                        
    
    f_stat (temp_src, &finfo);
    f_utime (temp_dst, &finfo);    
}

void lv_fm_media_task(lv_task_t * task)
{
	lv_fm_media_t * m = task->user_data;

	_lv_fm_media_check(m);
}

void lv_fm_list_local_task(lv_task_t * task)
{
    static DIR dir;
    static uint32_t idx;

    FRESULT fr;
    FILINFO finfo;    
    FATFS *fs;
    
    char path[256];
    uint32_t free_clust;
    lv_fm_obj_t * obj;
    lv_fm_media_t * m;
    lv_fm_task_data_t * td = task->user_data;
    
    finfo.lfname = (TCHAR *) &lfn_buffer[0];
    finfo.lfsize = 256;    

    switch (td->list_task_state)
    {
        case LV_FM_LIST_TASK_IDLE:
            goto end;
            break;
            
        case LV_FM_LIST_TASK_CLEAN:
            LV_FM_LL_CLEAN(fm_obj_list);
            lv_group_remove_obj(list_local);
            lv_list_clean(list_local);
            
            td->list_task_state = LV_FM_LIST_TASK_ALLOC;
            break;
            
        case LV_FM_LIST_TASK_ALLOC:
            if (td->list_vol)
            {
                if (idx < LV_FM_MAX_VOLUMES)
                {
                    m = &fm_media[idx];
                    if(m->valid)
                    {
                        f_getfree(m->path, &free_clust, &fs);
                        
                        obj = LV_FM_LL_ADD(fm_obj_list);
                        obj->idx = td->list_nobj;
                        obj->volume = 1;
                        obj->folder = 0;
                        obj->size = ((fs->n_fatent - 2) * fs->csize) >> 1;
                        obj->free = (free_clust * fs->csize) >> 1;
                        strcpy(obj->name, m->path);
                        
                        _lv_fm_list_add_obj_btn(list_local, obj, lv_fm_list_local_btn_event_cb);
                        
                        td->list_nobj++;
                    }
                    
                    idx++;
                }
                else
                {
                    td->list_task_state = LV_FM_LIST_TASK_IDLE;
                }
            }
            else
            {
                if (fm_obj_list == NULL)
                {
                    f_getcwd ((TCHAR*) &path, sizeof(path));   
                    for (idx = 0; idx < LV_FM_MAX_VOLUMES; idx++)
                    {
                        m = &fm_media[idx];
                        if (strcmp(path, m->path) == 0)
                        {
                            obj = LV_FM_LL_ADD(fm_obj_list);
                            obj->idx = td->list_nobj;
                            obj->volume = 1;
                            obj->folder = 1;
                            strcpy(obj->name, "..");
                            
                            _lv_fm_list_add_obj_btn(list_local, obj, lv_fm_list_local_btn_event_cb);
                            
                            td->list_nobj++;
                        }
                    }
                }
                
                if (dir.fs == NULL)
                    fr = f_findfirst (&dir, &finfo, "", "*");
                else
                    fr = f_findnext(&dir, &finfo);

                if (fr == FR_OK)
                {
                    if(finfo.fattrib == AM_DIR)
                    {
                        if( (finfo.fname[0] == '.' && finfo.fname[1] == '\0') || \
                             finfo.fname[0] == '\0' )
                            break;
                                
                        obj = LV_FM_LL_ADD(fm_obj_list);
                        
                        if(dir.lfn_idx != 0xFFFF)
                            strcpy(obj->name, finfo.lfname);
                        else
                            strcpy(obj->name, finfo.fname);
                        
                        obj->idx = td->list_nobj;
                        obj->volume = 0;
                        obj->folder = 1;
                        obj->time = finfo.ftime;
                        obj->date = finfo.fdate;
                        
                        _lv_fm_list_add_obj_btn(list_local, obj, lv_fm_list_local_btn_event_cb);
                        
                        td->list_nobj++;
                    }
                    else if( (finfo.fattrib == AM_ARC) || (finfo.fattrib == (AM_ARC | AM_RDO)) )
                    {
                        if( (finfo.fname[0] == '.' || finfo.fname[0] == '\0') )
                            break;
                            
                        obj = LV_FM_LL_ADD(fm_obj_list);
                        
                        if(dir.lfn_idx != 0xFFFF)
                            strcpy(obj->name, finfo.lfname);
                        else
                            strcpy(obj->name, finfo.fname);
                        
                        obj->idx = td->list_nobj;
                        obj->volume = 0;
                        obj->folder = 0;
                        obj->time = finfo.ftime;
                        obj->date = finfo.fdate;
                        obj->size = finfo.fsize;
                        obj->format = _lv_fm_get_ext(finfo.fname);
                        
                        _lv_fm_list_add_obj_btn(list_local, obj, lv_fm_list_local_btn_event_cb);
                        
                        td->list_nobj++;
                    }
                }
                else
                {
                    td->list_task_state = LV_FM_LIST_TASK_IDLE;
                }                
            }
            break;
            
        default:
            break;
    }
    
    return;
    
end:
    lv_group_add_obj(g, list_local);

    td->list_nobj = 0;
    
    memset(&dir, 0, sizeof(DIR));
    idx = 0;
    
    LV_FM_OBJ_DEL(h_spin)
    LV_FM_TASK_DEL(spin_task)
    LV_FM_TASK_DEL(list_task)     
}

void lv_fm_list_remote_task(lv_task_t * task)
{
    static lv_fm_obj_t * tobj;
    
    lv_fm_obj_t * obj;
    lv_fm_task_data_t * td = task->user_data;
    lwftp_session_t *s = &td->lwftp_session;
    
    switch (td->list_task_state)
    {
        case LV_FM_LIST_TASK_IDLE:
            goto end;
            break;
            
        case LV_FM_LIST_TASK_CLEAN:
            LV_FM_LL_CLEAN(fm_remote_obj_list);
            lv_group_remove_obj(list_remote);
            lv_list_clean(list_remote);
            
            tobj = fm_remote_site_list;
            td->list_task_state = LV_FM_LIST_TASK_ALLOC;
            break;

        case LV_FM_LIST_TASK_ALLOC:
            if (td->list_vol)
            {               
                if (tobj != NULL)
                {
                    obj = LV_FM_LL_ADD(fm_remote_obj_list);
                    LV_FM_LL_COPY(obj, tobj);
                    
                    _lv_fm_list_add_site_btn(list_remote, obj, lv_fm_list_remote_btn_event_cb);
                    
                    td->list_nobj++;
                    tobj = tobj->next;
                }
                else
                {
                    td->list_task_state = LV_FM_LIST_TASK_IDLE;
                }
            }
            else
            {
                if (fm_remote_obj_list == NULL)
                {
                    obj = LV_FM_LL_ADD(fm_remote_obj_list);
                    if (fm_sobj_list != NULL)
                        LV_FM_LL_COPY(obj, fm_sobj_list);
                    else
                        LV_FM_LL_COPY(obj, &td->active_site);
                    
                    obj->idx = td->list_nobj;
                    if (strcmp(td->remote_path, "/") == 0)
                        obj->volume = 1;
                    else
                        obj->volume = 0;
                    obj->folder = 1;
                    strcpy(obj->name, "..");
                    
                    _lv_fm_list_add_obj_btn(list_remote, obj, lv_fm_list_remote_btn_event_cb);
                    
                    td->list_nobj++;
                }
                
                if (s->control_pcb == NULL)
                {
                    td->list_task_state = LV_FM_LIST_TASK_IDLE;                
                }
            }
            break;
            
        default:
            break;            
    }

    return;
    
end:
    LV_FM_LL_CLEAN(fm_sobj_list);
    lv_group_add_obj(g, list_remote);

    td->list_nobj = 0;
    
    LV_FM_OBJ_DEL(h_spin)
    LV_FM_TASK_DEL(spin_task)
    LV_FM_TASK_DEL(list_task)    
}

void lv_fm_spin_task(lv_task_t * task)
{
    static char buf[64];
    lv_fm_task_data_t * td = task->user_data;
    uint32_t n = td->list_nobj;
    
    lv_snprintf(buf, sizeof(buf), "%d elements loaded", n);
    lv_label_set_text(lb_spin, buf);
}

void lv_fm_local_obj_ops_task(lv_task_t * task)
{
    typedef enum {OP_INIT = 0, OP_READ} op_states_t;    
    
    static lv_fm_op_t * op;
    static op_states_t op_state;
    static uint32_t scan_state;
    static uint64_t total_size = 0;
    static char src_path[256], dst_path[256];    

    FATFS *fs;
        
    lv_fm_obj_t * sobj;
    uint32_t free_clust, free_size;
    lv_fm_task_data_t * td = task->user_data;

    switch(td->main_state)
    {
        case LV_FM_MAIN_IDLE:
            goto end;
            break;

        case LV_FM_MAIN_COPY:
            sobj = LV_FM_LL_GET(fm_sobj_list);
            if (sobj != NULL)
            {
                f_getcwd ((TCHAR*) src_path, sizeof(src_path));
                
                td->total++;
                total_size += sobj->size;
                
                op = LV_FM_LL_ADD(fm_op_list);                
                strcpy(op->name, sobj->name);

                strcpy(op->src_path, src_path);
                if (sobj->folder == 1)
                {
                    if(op->src_path[3] != 0)
                        strcat(op->src_path, "/");                    
                    strcat(op->src_path, sobj->name);
                }
                
                memcpy(op->src_vol, op->src_path, 3);
                op->src_vol[3] = 0;

                op->folder = sobj->folder;
                op->size = sobj->size;
                
                LV_FM_FREE(sobj);            
            }
            else
            {
                goto end;
            }
            break;
            
        case LV_FM_MAIN_PASTE:        
            switch (op_state)
            {
                case OP_INIT:
                    op = fm_op_list;
                    f_getcwd ((TCHAR*) dst_path, sizeof(dst_path));
                    
                    op_state = OP_READ;
                    break;
                case OP_READ:
                    if (op != NULL)
                    {
                        if (scan_state == 0 && \
                            op->parent_ll == NULL)
                        {                                                        
                            strcpy(op->dst_path, dst_path);
                            memcpy(op->dst_vol, op->dst_path, 3);
                            op->dst_vol[3] = 0;
                        }
                        
                        if (op->folder == 1)
                        {
                            scan_state = _lv_fm_local_folder_scan(  &op,
                                                                    &td->total,
                                                                    &total_size);                            
                        }
                        
                        if (scan_state == 0)
                        {
                            if (op->child_ll != NULL)
                            {
                                op = op->child_ll;
                            }
                            else if (op->next != NULL)
                            {
                                op = op->next;
                            }
                            else if (op->parent_ll != NULL)
                            {
                                while (op->parent_ll != NULL && \
                                       op->parent_ll->next == NULL)
                                    op = op->parent_ll;
                                if (op->parent_ll != NULL)
                                    op = op->parent_ll->next;
                                else
                                    op = NULL;
                            }
                            else
                            {
                                op = NULL;
                            }
                        }
                    }
                    else
                    {
                        if (src_path[0] != dst_path[0])
                        {
                            f_getfree(dst_path, &free_clust, &fs);
                            free_size = (free_clust * fs->csize) >> 1;
                            total_size >>= 10;
                            
                            if (total_size > free_size)
                            {
                                f_chdrive (dst_path);
                                f_chdir (dst_path);                
                                
                                td->main_state = LV_FM_MAIN_DELETE;
                                td->err = LV_FM_MEMORY_ERROR;
                            }
                        }
                        
                        td->count = 0;
                        _lv_fm_obj_ops_start(td, t1, lv_fm_local_file_task);
                        
                        goto end;
                    }
                    break;
            }
            break;
            
        case LV_FM_MAIN_DELETE:
            switch (op_state)
            {
                case OP_INIT:
                    sobj = LV_FM_LL_GET(fm_sobj_list);
                    if (sobj != NULL)
                    {
                        f_getcwd ((TCHAR*) src_path, sizeof(src_path));
                        
                        td->total++;
                        
                        op = LV_FM_LL_ADD(fm_op_list);
                        strcpy(op->name, sobj->name);
                        
                        strcpy(op->src_path, src_path);
                        if (sobj->folder == 1)
                        {
                            if(op->src_path[3] != 0)
                                strcat(op->src_path, "/");
                            strcat(op->src_path, sobj->name);
                        }
                        
                        memcpy(op->src_vol, op->src_path, 3);
                        op->src_vol[3] = 0;
                        
                        op->folder = sobj->folder;
                        op->size = sobj->size;
                        
                        LV_FM_FREE(sobj);
                    }
                    else
                    {
                        op = fm_op_list;
                        op_state = OP_READ;                        
                    }
                    break;
                    
                case OP_READ:
                    if (op != NULL)
                    {
                        if (op->folder == 1)
                        {
                            scan_state = _lv_fm_local_folder_scan(  &op,
                                                                    &td->total,
                                                                    &total_size);                            
                        }
                        
                        if (scan_state == 0)
                        {
                            if (op->child_ll != NULL)
                            {
                                op = op->child_ll;
                            }
                            else if (op->next != NULL)
                            {
                                op = op->next;
                            }
                            else if (op->parent_ll != NULL)
                            {
                                while (op->parent_ll != NULL && \
                                       op->parent_ll->next == NULL)
                                    op = op->parent_ll;
                                if (op->parent_ll != NULL)
                                    op = op->parent_ll->next;
                                else
                                    op = NULL;
                            }
                            else
                            {
                                op = NULL;
                            }
                        }                        
                    }
                    else
                    {
                        td->count = 0;
                        _lv_fm_obj_ops_start(td, t1, lv_fm_local_file_task);

                        goto end;
                    }
                    break;
            }
            break;

        case LV_FM_MAIN_RENAME:
            h = lv_cont_create(t1, NULL);
            lv_cont_set_layout(h, LV_LAYOUT_PRETTY_MID);
            lv_cont_set_fit2(h, LV_FIT_NONE, LV_FIT_TIGHT);
            lv_obj_set_width(h, lv_page_get_width_grid(t1, 1, 1));

            ta_fn_local = lv_textarea_create(h, NULL);
            lv_cont_set_fit2(ta_fn_local, LV_FIT_PARENT, LV_FIT_NONE);
            lv_textarea_set_text(ta_fn_local, fm_sobj_list->name);
            lv_textarea_set_one_line(ta_fn_local, true);
            lv_textarea_set_cursor_hidden(ta_fn_local, false);

            lv_obj_set_height(tv, LV_VER_RES / 2);
            kb = lv_keyboard_create(lv_scr_act(), NULL);
            lv_obj_set_event_cb(kb, lv_fm_kb_event_cb);
            lv_page_focus(t1, lv_textarea_get_label(ta_fn_local), LV_ANIM_ON);
            lv_keyboard_set_textarea(kb, ta_fn_local);

            goto end;
            break;

        case LV_FM_MAIN_NEWFOLDER:
            h = lv_cont_create(t1, NULL);
            lv_cont_set_layout(h, LV_LAYOUT_PRETTY_MID);
            lv_cont_set_fit2(h, LV_FIT_NONE, LV_FIT_TIGHT);
            lv_obj_set_width(h, lv_page_get_width_grid(t1, 1, 1));

            ta_fn_local = lv_textarea_create(h, NULL);
            lv_cont_set_fit2(ta_fn_local, LV_FIT_PARENT, LV_FIT_NONE);
            lv_textarea_set_text(ta_fn_local, "");
            lv_textarea_set_one_line(ta_fn_local, true);
            lv_textarea_set_cursor_hidden(ta_fn_local, false);

            lv_obj_set_height(tv, LV_VER_RES / 2);
            kb = lv_keyboard_create(lv_scr_act(), NULL);
            lv_obj_set_event_cb(kb, lv_fm_kb_event_cb);
            lv_page_focus(t1, lv_textarea_get_label(ta_fn_local), LV_ANIM_ON);
            lv_keyboard_set_textarea(kb, ta_fn_local);

            goto end;
            break;

        case LV_FM_MAIN_FORMAT:
            if( f_mkfs (fm_sobj_list->name, 0, 0) != FR_OK)
            {
                td->err = LV_FM_FORMAT_ERROR;
            }
        
            td->main_state = LV_FM_MAIN_IDLE;

            goto end;
            break;
            
        default:
            break;        
    }
    
    return;
    
end:
    total_size = 0;
    op_state = OP_INIT;
    
    if (td->main_state != LV_FM_MAIN_RENAME)
        LV_FM_LL_CLEAN(fm_sobj_list);

    _lv_fm_list_btns_checkable(list_local, false);
    
    if (td->main_state != LV_FM_MAIN_COPY)
        LV_FM_OBJ_DEL(list_options)
        
    LV_FM_TASK_DEL(op_task)    
}

void lv_fm_remote_obj_ops_task(lv_task_t * task)
{
    typedef enum {OP_INIT = 0, OP_READ} op_states_t; 
    
    static lv_fm_op_t * op;
    static op_states_t op_state;
    static uint32_t scan_state;
    static uint64_t total_size = 0;    
    static char src_path[256], dst_path[256];
    
    FATFS *fs;
    
    lv_fm_obj_t * sobj;
    uint32_t free_clust, free_size;
    lv_fm_task_data_t * td = task->user_data;

    switch(td->main_state)
    {
        case LV_FM_MAIN_IDLE:
            goto end;
            break;        
        
        case LV_FM_MAIN_UPLOAD:
            switch (op_state)
            {
                case OP_INIT:
                    sobj = LV_FM_LL_GET(fm_sobj_list);
                    if (sobj != NULL)
                    {
                        f_getcwd ((TCHAR*) src_path, sizeof(src_path));
                        
                        td->total++;
                        total_size += sobj->size;
                        
                        op = LV_FM_LL_ADD(fm_op_list);
                        op->folder = sobj->folder;
                        op->size = sobj->size;
                        
                        strcpy(op->name, sobj->name);                
                        strcpy(op->src_path, src_path);
                        if (sobj->folder == 1)
                        {
                            if(op->src_path[3] != 0)
                                strcat(op->src_path, "/");                    
                            strcat(op->src_path, sobj->name);
                        }
                        strcpy(op->dst_path, td->remote_path);
                        memcpy(op->src_vol, src_path, 3);
                        op->src_vol[3] = 0;                
                        
                        strcpy(op->user, td->active_site.user);
                        strcpy(op->pass, td->active_site.pass);
                        op->port = td->active_site.port;
                        memcpy(op->ip_addr, td->active_site.ip_addr, sizeof(td->active_site.ip_addr));
                        
                        LV_FM_FREE(sobj);
                    }
                    else
                    {
                        op = fm_op_list;
                        op_state = OP_READ;                        
                    }                
                    break;
                
                case OP_READ:
                    if (op != NULL)
                    {
                        if (scan_state == 0 && \
                            op->parent_ll != NULL)
                        {
                            strcpy(op->user, td->active_site.user);
                            strcpy(op->pass, td->active_site.pass);
                            op->port = td->active_site.port;
                            memcpy(op->ip_addr, td->active_site.ip_addr, sizeof(td->active_site.ip_addr));                            
                        }                                
                        
                        if (op->folder == 1)
                        {
                            scan_state = _lv_fm_local_folder_scan( &op,
                                                                   &td->total,
                                                                   &total_size);                            
                        }
                        
                        if (scan_state == 0)
                        {
                            if (op->child_ll != NULL)
                            {
                                op = op->child_ll;
                            }
                            else if (op->next != NULL)
                            {
                                op = op->next;
                            }
                            else if (op->parent_ll != NULL)
                            {
                                while (op->parent_ll != NULL && \
                                       op->parent_ll->next == NULL)
                                    op = op->parent_ll;
                                if (op->parent_ll != NULL)
                                    op = op->parent_ll->next;
                                else
                                    op = NULL;
                            }
                            else
                            {
                                op = NULL;
                            }
                        }                        
                    }
                    else
                    {
                        td->count = 0;
                        _lv_fm_obj_ops_start(td, t1, lv_fm_remote_file_task);

                        goto end;                        
                    }
                    break;
            }
            break;
            
        case LV_FM_MAIN_DOWNLOAD:
            switch (op_state)
            {
                case OP_INIT:
                    sobj = LV_FM_LL_GET(fm_sobj_list);
                    if (sobj != NULL)
                    {
                        f_getcwd ((TCHAR*) dst_path, sizeof(dst_path));
                        
                        td->total++;
                        total_size += sobj->size;
                        
                        op = LV_FM_LL_ADD(fm_op_list);
                        op->folder = sobj->folder;
                        op->size = sobj->size;
                        
                        strcpy(op->name, sobj->name);
                        strcpy(op->src_path, td->remote_path);
                        strcpy(op->dst_path, dst_path);
                        memcpy(op->dst_vol, dst_path, 3);
                        op->dst_vol[3] = 0;
                        
                        strcpy(op->user, sobj->user);
                        strcpy(op->pass, sobj->pass);                
                        op->port = sobj->port;
                        memcpy(op->ip_addr, sobj->ip_addr, sizeof(sobj->ip_addr));
                        
                        LV_FM_FREE(sobj);
                    }
                    else
                    {                        
                        op = fm_op_list;
                        op_state = OP_READ;                        
                    }                
                    break;
                
                case OP_READ:
                    if (op != NULL)
                    {
                        if (op->folder == 1)
                        {
                            scan_state = _lv_fm_remote_folder_scan(&td->lwftp_session,
                                                                   &op,
                                                                   &td->total,
                                                                   &total_size);                            
                        }
                        
                        if (scan_state == 0)
                        {
                            if (op->child_ll != NULL)
                            {
                                op = op->child_ll;
                            }
                            else if (op->next != NULL)
                            {
                                op = op->next;
                            }
                            else if (op->parent_ll != NULL)
                            {
                                while (op->parent_ll != NULL && \
                                       op->parent_ll->next == NULL)
                                    op = op->parent_ll;
                                if (op->parent_ll != NULL)
                                    op = op->parent_ll->next;
                                else
                                    op = NULL;
                            }
                            else
                            {
                                op = NULL;
                            }
                        }                        
                    }
                    else
                    {
                        f_getfree(dst_path, &free_clust, &fs);
                        free_size = (free_clust * fs->csize) >> 1;
                        total_size >>= 10;
                        if (total_size > free_size) 
                        {
                            LV_FM_TREE_CLEAN(fm_op_list);
                            td->err = LV_FM_MEMORY_ERROR;
                            goto end;
                        }
                        
                        td->count = 0;
                        _lv_fm_obj_ops_start(td, t2, lv_fm_remote_file_task);

                        goto end;                        
                    }
                    break;
            }        
            break;
            
        case LV_FM_MAIN_DELETE:
            switch (op_state)
            {
                case OP_INIT:
                    sobj = LV_FM_LL_GET(fm_sobj_list);
                    if (sobj != NULL)
                    {
                        td->total++;
                        total_size += sobj->size;
                        
                        op = LV_FM_LL_ADD(fm_op_list);
                        op->folder = sobj->folder;
                        op->size = sobj->size;
                        
                        strcpy(op->name, sobj->name);
                        strcpy(op->src_path, td->remote_path);
                        
                        strcpy(op->user, sobj->user);
                        strcpy(op->pass, sobj->pass);                
                        op->port = sobj->port;
                        memcpy(op->ip_addr, sobj->ip_addr, sizeof(sobj->ip_addr));
                        
                        LV_FM_FREE(sobj);
                    }
                    else
                    {                        
                        op = fm_op_list;
                        op_state = OP_READ;                        
                    }                 
                    break;
                
                case OP_READ:
                    if (op != NULL)
                    {
                        if (op->folder == 1)
                        {
                            scan_state = _lv_fm_remote_folder_scan(&td->lwftp_session,
                                                                   &op,
                                                                   &td->total,
                                                                   &total_size);                            
                        }
                        
                        if (scan_state == 0)
                        {
                            if (op->child_ll != NULL)
                            {
                                op = op->child_ll;
                            }
                            else if (op->next != NULL)
                            {
                                op = op->next;
                            }
                            else if (op->parent_ll != NULL)
                            {
                                while (op->parent_ll != NULL && \
                                       op->parent_ll->next == NULL)
                                    op = op->parent_ll;
                                if (op->parent_ll != NULL)
                                    op = op->parent_ll->next;
                                else
                                    op = NULL;
                            }
                            else
                            {
                                op = NULL;
                            }
                        }                        
                    }
                    else
                    {
                        td->count = 0;
                        _lv_fm_obj_ops_start(td, t2, lv_fm_remote_file_task);

                        goto end;                        
                    }
                    break;
            }        
            break;
            
        case LV_FM_MAIN_RENAME:
            strcpy(td->old_name, fm_sobj_list->name);
            
            h = lv_cont_create(t2, NULL);
            lv_cont_set_layout(h, LV_LAYOUT_PRETTY_MID);
            lv_cont_set_fit2(h, LV_FIT_NONE, LV_FIT_TIGHT);
            lv_obj_set_width(h, lv_page_get_width_grid(t2, 1, 1));

            ta_fn_remote = lv_textarea_create(h, NULL);
            lv_cont_set_fit2(ta_fn_remote, LV_FIT_PARENT, LV_FIT_NONE);
            lv_textarea_set_text(ta_fn_remote, fm_sobj_list->name);
            lv_textarea_set_one_line(ta_fn_remote, true);
            lv_textarea_set_cursor_hidden(ta_fn_remote, false);

            lv_obj_set_height(tv, LV_VER_RES / 2);
            kb = lv_keyboard_create(lv_scr_act(), NULL);
            lv_obj_set_event_cb(kb, lv_fm_kb_event_cb);
            lv_page_focus(t2, lv_textarea_get_label(ta_fn_remote), LV_ANIM_ON);
            lv_keyboard_set_textarea(kb, ta_fn_remote);
            
            goto end;
            break;        
        
        case LV_FM_MAIN_NEWFOLDER:
            h = lv_cont_create(t2, NULL);
            lv_cont_set_layout(h, LV_LAYOUT_PRETTY_MID);
            lv_cont_set_fit2(h, LV_FIT_NONE, LV_FIT_TIGHT);
            lv_obj_set_width(h, lv_page_get_width_grid(t2, 1, 1));

            ta_fn_remote = lv_textarea_create(h, NULL);
            lv_cont_set_fit2(ta_fn_remote, LV_FIT_PARENT, LV_FIT_NONE);
            lv_textarea_set_text(ta_fn_remote, "");
            lv_textarea_set_one_line(ta_fn_remote, true);
            lv_textarea_set_cursor_hidden(ta_fn_remote, false);

            lv_obj_set_height(tv, LV_VER_RES / 2);
            kb = lv_keyboard_create(lv_scr_act(), NULL);
            lv_obj_set_event_cb(kb, lv_fm_kb_event_cb);
            lv_page_focus(t2, lv_textarea_get_label(ta_fn_remote), LV_ANIM_ON);
            lv_keyboard_set_textarea(kb, ta_fn_remote);

            goto end;            
            break;        
        
        default:
            break;        
    }
    
    return;
    
end:
    total_size = 0;
    op_state = OP_INIT;
    
    _lv_fm_list_btns_checkable(list_local, false);
    _lv_fm_list_btns_checkable(list_remote, false);

    LV_FM_LL_CLEAN(fm_sobj_list);
    LV_FM_OBJ_DEL(list_options)    
    
    LV_FM_TASK_DEL(op_task)
}

void lv_fm_local_file_task(lv_task_t * task)
{
    static lv_fm_op_t * op, * last_op;

    uint32_t bytesread, byteswrite;
    lv_fm_op_t * op2;
    lv_fm_task_data_t * td = task->user_data;
    lv_fm_err_t err = LV_FM_NO_ERROR;

    switch(td->main_state)
    {
        case LV_FM_MAIN_IDLE:
            goto end;
            break;
            
        case LV_FM_MAIN_PASTE:
            switch (td->file_task_state)
            {
                case LV_FM_FILE_TASK_INIT:
                    op = fm_op_list;
                    
                    td->file_task_state = LV_FM_FILE_TASK_PROC;
                    break;
                    
                case LV_FM_FILE_TASK_PROC:
                    if (op != NULL)
                    {
                        last_op = op;
                        td->cur_op = op;
                        td->count++;
                        
                        if (td->flag_cut == 1)
                        {
                            if (op->src_path[0] == op->dst_path[0])
                            {
                                err = _lv_fm_frename(op->src_path, op->dst_path, op->name, op->folder);
                                if (err) goto end;
                            }
                            else if (op->folder == 1)
                            {
                                err = _lv_fm_fmkdir(op->dst_path, op->name);
                                if (err) goto end;                                
                            }
                            else
                            {
                                err = _lv_fm_fopen(&td->src, op->src_path, op->name, FA_READ);
                                if (err) goto end;
                                
                                err = _lv_fm_fopen(&td->dst, op->dst_path, op->name, FA_CREATE_NEW | FA_WRITE);
                                if (err) goto end;                                    
                                   
                                td->file_task_state = LV_FM_FILE_TASK_FILE;
                            }
                        }
                        else
                        {
                            if (op->folder == 1)
                            {
                                err = _lv_fm_fmkdir(op->dst_path, op->name);
                                if (err) goto end;                                
                            }
                            else
                            {
                                err = _lv_fm_fopen(&td->src, op->src_path, op->name, FA_READ);
                                if (err) goto end;
                                
                                err = _lv_fm_fopen(&td->dst, op->dst_path, op->name, FA_CREATE_NEW | FA_WRITE);
                                if (err) goto end;                                    
                                   
                                td->file_task_state = LV_FM_FILE_TASK_FILE;
                            }
                        }                        
                        
                        if (op->child_ll != NULL && \
                            (last_op->src_path[0] != last_op->dst_path[0] || \
                             td->flag_cut == 0))
                        {
                            op = op->child_ll;
                        }
                        else if (op->next != NULL)
                        {
                            op = op->next;
                        }
                        else if (op->parent_ll != NULL)
                        {
                            while (op->parent_ll != NULL && \
                                   op->parent_ll->next == NULL)
                                op = op->parent_ll;
                            if (op->parent_ll != NULL)
                                op = op->parent_ll->next;
                            else
                                op = NULL;
                        }
                        else
                        {
                            op = NULL;
                        }                        
                    }
                    else
                    {
                        if (td->flag_cut == 1 && \
                            last_op->src_path[0] != last_op->dst_path[0])
                        {
                            td->main_state = LV_FM_MAIN_DELETE;
                            td->file_task_state = LV_FM_FILE_TASK_INIT;
                        }
                        else
                        {
                            goto end;
                        }                            
                    }
                    break;
                    
                case LV_FM_FILE_TASK_FILE:
                    if(f_read (&td->src, td->buffer.addr, td->buffer.size, (UINT *) &bytesread) != FR_OK)
                    {
                        err = LV_FM_READ_ERROR;
                        goto end;
                    }

                    if(f_write (&td->dst, td->buffer.addr, bytesread, (UINT *) &byteswrite) != FR_OK)
                    {
                        err = LV_FM_WRITE_ERROR;
                        goto end;      
                    }

                    if (bytesread != td->buffer.size)
                    {
                        f_close (&td->src);
                        f_close (&td->dst);

                        _lv_fm_futime(last_op->src_path,
                                      last_op->dst_path,
                                      last_op->name);                        
                        
                        td->file_task_state = LV_FM_FILE_TASK_PROC;
                    }
                    break;
            }        
            break;
            
        case LV_FM_MAIN_DELETE:
            switch (td->file_task_state)
            {
                case LV_FM_FILE_TASK_INIT:
                    op = fm_op_list;
                    
                    td->file_task_state = LV_FM_FILE_TASK_PROC;
                    break;

                case LV_FM_FILE_TASK_PROC:
                    if (op != NULL)
                    {
                        if (op->child_ll == NULL)
                        {
                            td->cur_op = op;
                            td->count++;
                            
                            err = _lv_fm_unlink(op->src_path, op->name, op->folder);
                            if (err) goto end;
                            
                            op2 = op;
                            LV_FM_FREE(op2);
                        }
                        
                        if (op->child_ll != NULL)
                        {
                            op = op->child_ll;
                        }
                        else if (op->next != NULL)
                        {
                            op = op->next;
                        }
                        else if (op->parent_ll != NULL)
                        {
                            op = op->parent_ll;
                            op->child_ll = NULL;
                        }
                        else
                        {
                            op = NULL;
                        }                        
                    }
                    else
                    {
                        goto end;
                    }
                    break;
                    
                case LV_FM_FILE_TASK_FILE:
                    break;
            }
            break;

        default:
            break;        
    }
    
    return;
    
end:
    if (td->src.fs != NULL) 
        f_close (&td->src);    
    if (td->dst.fs != NULL) 
        f_close (&td->dst);
    
    if (td->main_state != LV_FM_MAIN_DELETE)
        LV_FM_TREE_CLEAN(fm_op_list);
    else
        fm_op_list = NULL;
    
    td->cur_op = NULL;
    td->flag_cut = 0;
    td->count = td->total = 0;
    td->main_state = LV_FM_MAIN_IDLE;
    td->file_task_state = LV_FM_FILE_TASK_INIT;
    td->err = err;

    LV_FM_OBJ_DEL(h)
    LV_FM_TASK_DEL(bar_task)
    LV_FM_TASK_DEL(file_task)
    
    td->list_vol = 0;
    td->active_list = list_local;
    _lv_fm_dir_read_start(td);    
}

void lv_fm_remote_file_task(lv_task_t * task)
{
    static lv_fm_op_t * op;

    uint32_t byteswrite;
    lv_fm_op_t * op2;
    lv_fm_task_data_t * td = task->user_data;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;
    lv_fm_err_t err = LV_FM_NO_ERROR;

    switch(td->main_state)
    {
        case LV_FM_MAIN_IDLE:
            goto end;
            break;

        case LV_FM_MAIN_UPLOAD:
            switch (td->file_task_state)
            {
                case LV_FM_FILE_TASK_INIT:
                    op = fm_op_list;
                    
                    td->file_task_state = LV_FM_FILE_TASK_PROC;
                    break;

                case LV_FM_FILE_TASK_PROC:
                    if (op != NULL)
                    {
                        td->cur_op = op;
                        td->count++;
                        
                        if (op->folder == 1)
                        {
                            err = lv_lwftp_connect(&td->lwftp_session, 
                                                   lv_lwftp_MKD_cb, 
                                                   op->ip_addr,
                                                   op->port,
                                                   op->user,
                                                   op->pass,
                                                   op->name,
                                                   op->dst_path);                            
                        }
                        else
                        {
                            err = _lv_fm_fopen(&td->src, op->src_path, op->name, FA_READ);
                            if (err) goto end;

                            err = lv_lwftp_connect(&td->lwftp_session, 
                                                   lv_lwftp_STOR_cb, 
                                                   op->ip_addr,
                                                   op->port,
                                                   op->user,
                                                   op->pass,
                                                   op->name,
                                                   op->dst_path);                                                        
                        }
                        
                        td->file_task_state = LV_FM_FILE_TASK_FILE;                        
                        
                        if (op->child_ll != NULL)
                        {
                            op = op->child_ll;
                        }
                        else if (op->next != NULL)
                        {
                            op = op->next;
                        }
                        else if (op->parent_ll != NULL)
                        {
                            while (op->parent_ll != NULL && \
                                   op->parent_ll->next == NULL)
                                op = op->parent_ll;
                            if (op->parent_ll != NULL)
                                op = op->parent_ll->next;
                            else
                                op = NULL;
                        }
                        else
                        {
                            op = NULL;
                        }                        
                    }
                    else
                    {
                        err = lv_lwftp_connect(&td->lwftp_session, 
                                               lv_lwftp_CWD_cb, 
                                               td->active_site.ip_addr,
                                               td->active_site.port,
                                               td->active_site.user,
                                               td->active_site.pass,
                                               "",
                                               NULL);                        
                        
                        goto end;
                    }
                    break;
                    
                case LV_FM_FILE_TASK_FILE:
                    if (s->control_pcb == NULL)
                    {
                        td->buffer.rptr = NULL;
                        f_close (&td->src);
                        
                        td->file_task_state = LV_FM_FILE_TASK_PROC;
                    }
                    break;
            }
            break;
            
        case LV_FM_MAIN_DOWNLOAD:
            switch (td->file_task_state)
            {
                case LV_FM_FILE_TASK_INIT:
                    op = fm_op_list;
                    
                    td->file_task_state = LV_FM_FILE_TASK_PROC;
                    break;

                case LV_FM_FILE_TASK_PROC:
                    if (op != NULL)
                    {
                        td->cur_op = op;
                        td->count++;
                        
                        if (op->folder == 1)
                        {
                            err = _lv_fm_fmkdir(op->dst_path, op->name);
                            if (err) goto end;
                        }
                        else
                        {
                            err = _lv_fm_fopen(&td->dst, op->dst_path, op->name, FA_CREATE_NEW | FA_WRITE);
                            if (err) goto end;

                            err = lv_lwftp_connect(&td->lwftp_session, 
                                                   lv_lwftp_RETR_cb, 
                                                   op->ip_addr,
                                                   op->port,
                                                   op->user,
                                                   op->pass,
                                                   op->name,
                                                   op->src_path);
                                                    
                            td->file_task_state = LV_FM_FILE_TASK_FILE;
                        }                        
                        
                        if (op->child_ll != NULL)
                        {
                            op = op->child_ll;
                        }
                        else if (op->next != NULL)
                        {
                            op = op->next;
                        }
                        else if (op->parent_ll != NULL)
                        {
                            while (op->parent_ll != NULL && \
                                   op->parent_ll->next == NULL)
                                op = op->parent_ll;
                            if (op->parent_ll != NULL)
                                op = op->parent_ll->next;
                            else
                                op = NULL;
                        }
                        else
                        {
                            op = NULL;
                        }                        
                    }
                    else
                    {
                        td->list_vol = 0;
                        td->active_list = list_local;
                        _lv_fm_dir_read_start(td);                        
                        
                        goto end;
                    }
                    break;
                    
                case LV_FM_FILE_TASK_FILE:
                    if (s->control_pcb == NULL)
                    {
                        if ((td->buffer.wptr - td->buffer.addr) > 0) 
                        {
                            if(f_write (&td->dst, td->buffer.addr, td->buffer.wptr - td->buffer.addr, (UINT *) &byteswrite) != FR_OK) 
                                td->err = LV_FM_WRITE_ERROR;
                            td->buffer.wptr = NULL;
                        }                        
                        f_close (&td->dst);
                        
                        td->file_task_state = LV_FM_FILE_TASK_PROC;
                    }
                    break;
            }        
            break;
            
        case LV_FM_MAIN_DELETE:
            switch (td->file_task_state)
            {
                case LV_FM_FILE_TASK_INIT:
                    op = fm_op_list;
                    
                    td->file_task_state = LV_FM_FILE_TASK_PROC;
                    break;

                case LV_FM_FILE_TASK_PROC:
                    if (op != NULL)
                    {
                        if (op->child_ll == NULL)
                        {
                            td->cur_op = op;
                            td->count++;
                            
                            if (op->folder == 1) 
                            {
                                err = lv_lwftp_connect(&td->lwftp_session, 
                                                       lv_lwftp_RMD_cb, 
                                                       op->ip_addr,
                                                       op->port,
                                                       op->user,
                                                       op->pass,
                                                       op->name,
                                                       op->src_path);                            
                            } 
                            else 
                            {
                                err = lv_lwftp_connect(&td->lwftp_session, 
                                                       lv_lwftp_DELE_cb, 
                                                       op->ip_addr,
                                                       op->port,
                                                       op->user,
                                                       op->pass,
                                                       op->name,
                                                       op->src_path);
                            }
                                                   
                            td->file_task_state = LV_FM_FILE_TASK_FILE;                            
                            
                            op2 = op;
                            LV_FM_FREE(op2);                            
                        }
                        
                        if (op->child_ll != NULL)
                        {
                            op = op->child_ll;
                        }
                        else if (op->next != NULL)
                        {
                            op = op->next;
                        }
                        else if (op->parent_ll != NULL)
                        {
                            op = op->parent_ll;
                            op->child_ll = NULL;
                        }
                        else
                        {
                            op = NULL;
                        }                        
                    }
                    else
                    {
                        err = lv_lwftp_connect(&td->lwftp_session, 
                                               lv_lwftp_CWD_cb, 
                                               td->active_site.ip_addr,
                                               td->active_site.port,
                                               td->active_site.user,
                                               td->active_site.pass,
                                               "..",
                                               NULL);                    
                        goto end;                        
                    }
                    break;
                    
                case LV_FM_FILE_TASK_FILE:
                    if (s->control_pcb == NULL) 
                        td->file_task_state = LV_FM_FILE_TASK_PROC;                
                    break;
            }
            break;
            
        case LV_FM_MAIN_RENAME:
            switch (td->file_task_state) 
            {
                case LV_FM_FILE_TASK_INIT:
                    err = lv_lwftp_connect(&td->lwftp_session, 
                                           lv_lwftp_RNFR_cb, 
                                           td->active_site.ip_addr,
                                           td->active_site.port,
                                           td->active_site.user,
                                           td->active_site.pass,
                                           td->old_name,
                                           td->remote_path);                            

                    td->file_task_state = LV_FM_FILE_TASK_FILE;                
                    break;
                    
                case LV_FM_FILE_TASK_FILE:
                    if (s->control_pcb == NULL) 
                    {
                        err = lv_lwftp_connect(&td->lwftp_session, 
                                               lv_lwftp_CWD_cb, 
                                               td->active_site.ip_addr,
                                               td->active_site.port,
                                               td->active_site.user,
                                               td->active_site.pass,
                                               "",
                                               NULL);                    
                        goto end;                        
                    }
                    break;
                    
                default:
                    break;                
            }        
            break;
            
        case LV_FM_MAIN_NEWFOLDER:
            switch (td->file_task_state) 
            {
                case LV_FM_FILE_TASK_INIT:
                    err = lv_lwftp_connect(&td->lwftp_session, 
                                           lv_lwftp_MKD_cb, 
                                           td->active_site.ip_addr,
                                           td->active_site.port,
                                           td->active_site.user,
                                           td->active_site.pass,
                                           td->new_name,
                                           td->remote_path);                            

                    td->file_task_state = LV_FM_FILE_TASK_FILE;                
                    break;
                    
                case LV_FM_FILE_TASK_FILE:
                    if (s->control_pcb == NULL) 
                    {
                        err = lv_lwftp_connect(&td->lwftp_session, 
                                               lv_lwftp_CWD_cb, 
                                               td->active_site.ip_addr,
                                               td->active_site.port,
                                               td->active_site.user,
                                               td->active_site.pass,
                                               "",
                                               NULL);                    
                        goto end;                        
                    }
                    break;
                    
                default:
                    break;                
            }        
            break;
            
        default:
            break;
    }

    return;
    
end:
    lwftp_abort(s);
    if (td->src.fs != NULL) 
        f_close (&td->src);    
    if (td->dst.fs != NULL) 
        f_close (&td->dst);
    
    if (td->main_state != LV_FM_MAIN_DELETE)
        LV_FM_TREE_CLEAN(fm_op_list);
    else
        fm_op_list = NULL;    

    td->cur_op = NULL;
    td->flag_cut = 0;
    td->count = td->total = 0;
    td->main_state = LV_FM_MAIN_IDLE;
    td->file_task_state = LV_FM_FILE_TASK_INIT;
    td->err = err;

    LV_FM_OBJ_DEL(h)
    LV_FM_TASK_DEL(bar_task)
    LV_FM_TASK_DEL(file_task)    
}

void lv_fm_bar_task(lv_task_t * task)
{
  static char buf[64];
  lv_fm_task_data_t * td = task->user_data;
  uint32_t value, total;
  uint32_t n = (uint32_t) td->count;
  uint32_t t = (uint32_t) td->total;
  int16_t x = 0;  

  if (td->main_state == LV_FM_MAIN_PASTE)
  {
    value = (uint32_t) td->src.fptr / 1024;
    total = (uint32_t) td->src.fsize / 1024;
    
    if (td->flag_cut == 1)
    {
      lv_snprintf(buf, sizeof(buf), "Moving %d  /  %d  KB     %d  /  %d", value, total, n, t);
    }
    else
    {
      lv_snprintf(buf, sizeof(buf), "Copying %d  /  %d  KB     %d  /  %d", value, total, n, t);
    }
  }
  else if (td->main_state == LV_FM_MAIN_UPLOAD)
  {
    value = (uint32_t) td->src.fptr / 1024;
    total = (uint32_t) td->src.fsize / 1024;
      
    lv_snprintf(buf, sizeof(buf), "Uploading %d  /  %d  KB     %d  /  %d", value, total, n, t);
  }
  else if (td->main_state == LV_FM_MAIN_DOWNLOAD)
  {
    value = (uint32_t) td->dst.fptr / 1024;
    if (td->cur_op)
    {
      total = (uint32_t) td->cur_op->size / 1024;
    }
    else
    {
      total = 0;
    }
      
    lv_snprintf(buf, sizeof(buf), "Downloading %d  /  %d  KB     %d  /  %d", value, total, n, t);
  }  
  else if (td->main_state == LV_FM_MAIN_DELETE)
  {
    lv_snprintf(buf, sizeof(buf), "Deleting %d  /  %d", n, t);
  }  
  lv_obj_set_style_local_value_str(bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, buf);

  if (total > 0) 
  {
    x = (int16_t) ((value * 100) / total);
  }
  lv_bar_set_value(bar, x, LV_ANIM_OFF);
}

void lv_fm_mbox_task(lv_task_t * task)
{
	lv_fm_task_data_t * td = task->user_data;

	if (td->err)
	{
		_lv_fm_err(td->err);

		td->err = LV_FM_NO_ERROR;
	}
}

void lv_fm_net_task(lv_task_t * task)
{
    char iptxt[20];
    app_netif_t * net = task->user_data;
    
    if(net->link_up)
    {
        net->link_up = 0;
        
        sprintf((char *)iptxt, "%s", ip4addr_ntoa((const ip4_addr_t *)&(net->gnetif)->ip_addr));
        lv_textarea_set_text(ta_ip_settings, iptxt);
        lv_obj_set_style_local_text_color(ta_ip_settings, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_LIME);
    }
    else if(net->link_down)
    {
        net->link_down = 0;
        
        lv_obj_set_style_local_text_color(ta_ip_settings, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    }
}

void lv_fm_disp_task(lv_task_t * task)
{
    static uint8_t disp_en = 1, last_disp_shutdown_en = 0;
    static uint32_t disp_tick = 0;

    if (last_disp_shutdown_en != disp_shutdown_en)
    {
        disp_tick = HAL_GetTick();
        last_disp_shutdown_en = disp_shutdown_en;
    }

    if (disp_shutdown_en)
    {
        if (ts_indev->proc.state == LV_INDEV_STATE_PR || \
            ptr_indev->proc.state == LV_INDEV_STATE_PR)
        {
            disp_tick = HAL_GetTick();

            if (!disp_en)
            {
                if (ts_indev->proc.state == LV_INDEV_STATE_PR)
                    lv_indev_wait_release(ts_indev);
                else if (ptr_indev->proc.state == LV_INDEV_STATE_PR)
                    lv_indev_wait_release(ptr_indev);                    
                BSP_LCD_DisplayOn();
                BSP_LCD_SetBrightness(100);
                disp_en = 1;
            }
        }
        else if (disp_en) 
        {
            if(!(player_format == player_jmv && \
               hlib.playback_state == AUDIO_LIB_STATE_PLAY) && \
               (HAL_GetTick() - disp_tick) > disp_timeout)
            {
                BSP_LCD_DisplayOff();
                BSP_LCD_SetBrightness(0);
                disp_en = 0;

                disp_tick = HAL_GetTick();
            }
        }
    }   
}

void lv_fm_stats_task(lv_task_t * task)
{
  if (hlib.playback_state != AUDIO_LIB_STATE_PLAY && \
      hlib.playback_state != AUDIO_LIB_STATE_WAIT)
  {
    RTC_TimeTypeDef stime;
    RTC_DateTypeDef sdate;    
    static char cpu_buf[64],  lram_buf[64], hram_buf[64], conn_buf[64], time_buf[64];
    uint32_t cpu = 100 - lv_task_get_idle();
    uint32_t ram_used = (uint32_t) lwip_stats.mem.used / 1024;
    uint32_t ram_total = (uint32_t) lwip_stats.mem.avail / 1024;
    int16_t ram = (int16_t) ((ram_used * 100) / ram_total);
    HAL_RTC_GetTime(&RtcHandle, &stime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&RtcHandle, &sdate, RTC_FORMAT_BIN);
    struct mallinfo ram_info = mallinfo();
    
    lv_snprintf(cpu_buf, sizeof(cpu_buf), "CPU %d %%", cpu);
    lv_obj_set_style_local_value_str(cpu_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, cpu_buf);
    lv_bar_set_value(cpu_bar, cpu, LV_ANIM_OFF);
    
    lv_snprintf(lram_buf, sizeof(lram_buf), "Lwip RAM %d  /  %d  KB", ram_used, ram_total);
    lv_obj_set_style_local_value_str(lram_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, lram_buf);
    lv_bar_set_value(lram_bar, ram, LV_ANIM_OFF);
    
    ram_used = (uint32_t) (ram_info.uordblks + ram_info.hblkhd) / 1024;
    ram_total = (uint32_t) (&__heap_limit - &__heap_start) / 1024;
    ram = (int16_t) ((ram_used * 100) / ram_total);
    
    lv_snprintf(hram_buf, sizeof(hram_buf), "Heap RAM %d  /  %d  KB", ram_used, ram_total);
    lv_obj_set_style_local_value_str(hram_bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, hram_buf);
    lv_bar_set_value(hram_bar, ram, LV_ANIM_OFF);    
    
    lv_snprintf(conn_buf, sizeof(conn_buf), "Ctrl Conns: %d\nData Conns: %d", ctrl_cnt, data_cnt);
    lv_label_set_text(lb_conn, (const char *)conn_buf);
    
    lv_snprintf(time_buf, sizeof(time_buf), "Time: %2d:%2d:%2d\nDate: %2d-%2d-%2d", 
                stime.Hours, stime.Minutes, stime.Seconds,
                sdate.Date, sdate.Month, sdate.Year);
    lv_label_set_text(lb_time, (const char *)time_buf);
  }
}

void lv_fm_list_local_autoplay_task(lv_task_t * task)
{
    lv_fm_task_data_t * td = task->user_data;
    
    if (!hlib.active)
    {
        if (td->autoplay_site->next != NULL)
        {
            td->autoplay_site = td->autoplay_site->next;
            
            if (mbox_err == NULL && \
                player_h == NULL && \
                player_spin_h == NULL && \
                loader_h == NULL && \
                h == NULL && \
                h_spin == NULL && \
                img == NULL && \
                list_options == NULL)
            {
                if(td->autoplay_site->format == wav || \
                   td->autoplay_site->format == mp3 || \
                   td->autoplay_site->format == flac || \
                   td->autoplay_site->format == jmv)
                {
                    td->fr = f_open (&td->src, td->autoplay_site->name, FA_READ);
                    if (td->fr == FR_OK)
                    {
                        td->err = (lv_fm_err_t) lv_fm_local_player_start((lv_fm_player_format_t) td->autoplay_site->format,
                                                                         audiodevice,
                                                                         &td->src);
                    }
                }
            }            
        }
        else
        {
            LV_FM_TASK_DEL(autoplay_task)
        }
    }
}

void lv_fm_list_remote_autoplay_task(lv_task_t * task)
{
    lv_fm_task_data_t * td = task->user_data;
    
    if (!hlib.active)
    {
        if (td->autoplay_site->next != NULL)
        {
            td->autoplay_site = td->autoplay_site->next;
            
            if (mbox_err == NULL && \
                player_h == NULL && \
                player_spin_h == NULL && \
                loader_h == NULL && \
                h == NULL && \
                h_spin == NULL && \
                img == NULL && \
                list_options == NULL)
            {
                if(td->autoplay_site->format == wav || \
                   td->autoplay_site->format == mp3 || \
                   td->autoplay_site->format == flac || \
                   td->autoplay_site->format == jmv)
                {
                    td->err = (lv_fm_err_t) lv_fm_remote_player_start((lv_fm_player_format_t) td->autoplay_site->format,
                                                                      audiodevice,
                                                                      td->autoplay_site->size,
                                                                      td->autoplay_site->user, td->autoplay_site->pass,
                                                                      td->autoplay_site->name, td->remote_path,
                                                                      td->autoplay_site->ip_addr, td->autoplay_site->port,
                                                                      &td->lwftp_session);
                }
            }            
        }
        else
        {
            LV_FM_TASK_DEL(autoplay_task)
        }
    }
}

static lv_fm_err_t lv_lwftp_connect(lwftp_session_t * s, 
                                    void (*cb)(void*, int), 
                                    uint8_t *ip_addr,
                                    uint16_t port,
                                    char *user,
                                    char *pass,
                                    char *name,
                                    char *path)
{
    static char remote_fullpath[1024], remote_newfullpath[1024];
    static char remote_user[256], remote_pass[256];
    
    char c = '\0';
    uint8_t len, idx;
    lv_fm_err_t err = LV_FM_NO_ERROR;
    lv_fm_task_data_t *td = &fm_task_data;
    
    stpcpy(remote_user, user);
    stpcpy(remote_pass, pass);
    
    memset(s, 0, sizeof(lwftp_session_t));
    if (cb == lv_lwftp_CWD_cb) {
        if (!name && !path) {
            strcpy(td->remote_path, "/");
            s->remote_path = td->remote_path;
        } else if (name && !path) {
            len = idx = strlen(td->remote_path);
            if (strcmp(name, "..") == 0) {
                c = td->remote_path[idx];
                while(c != '/') {
                    idx--;
                    c = td->remote_path[idx];
                }
                if (idx > 0) {
                    td->remote_path[idx] = '\0';                
                } else {
                    td->remote_path[idx + 1] = '\0';                
                }
            } else {
                if (td->remote_path[len - 1] != '/') {
                    strcat(td->remote_path, "/");
                }
                strcat(td->remote_path, name);            
            }
            s->remote_path = td->remote_path;
        } else if (name && path) {
            strcpy(remote_fullpath, path);
            len = strlen(remote_fullpath);
            if (remote_fullpath[len - 1] != '/') {
                strcat(remote_fullpath, "/");
            }
            strcat(remote_fullpath, name);
            s->remote_path = remote_fullpath;
        }
    } else if (cb == lv_lwftp_STOR_cb || \
               cb == lv_lwftp_RETR_cb || \
               cb == lv_lwftp_DELE_cb || \
               cb == lv_lwftp_RNFR_cb || \
               cb == lv_lwftp_MKD_cb || \
               cb == lv_lwftp_RMD_cb) {
        strcpy(remote_fullpath, path);
        len = strlen(remote_fullpath);
        if (remote_fullpath[len - 1] != '/') {
            strcat(remote_fullpath, "/");
        }
        strcat(remote_fullpath, name);
        s->remote_path = remote_fullpath;
    }
    if (cb == lv_lwftp_RNFR_cb) {
        strcpy(remote_newfullpath, path);
        len = strlen(remote_newfullpath);
        if (remote_newfullpath[len - 1] != '/') {
            strcat(remote_newfullpath, "/");
        }
        strcat(remote_newfullpath, td->new_name);
        s->new_path = remote_newfullpath;        
    }
    s->offset_str = "0";
    IP4_ADDR(&(s->server_ip), 
             ip_addr[0], ip_addr[1],
             ip_addr[2], ip_addr[3]);
    s->server_port = port;
    s->done_fn = cb;
    s->user = remote_user;
    s->pass = remote_pass;
    s->handle = td; 
    if (lwftp_connect(s) != LWFTP_RESULT_INPROGRESS) {
        err = LV_FM_CONNECTION_ERROR;
    }

    return err;
}

static void lv_lwftp_MKD_cb(void *arg, int result)
{
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;

    if ( result != LWFTP_RESULT_LOGGED ) {
        return lwftp_close(s);
    }

    s->done_fn = lv_lwftp_QUIT_cb;
    lwftp_newdir(s);    
}

static void lv_lwftp_RMD_cb(void *arg, int result)
{
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;

    if ( result != LWFTP_RESULT_LOGGED ) {
        return lwftp_close(s);
    }

    s->done_fn = lv_lwftp_QUIT_cb;
    lwftp_rmd(s);    
}

static void lv_lwftp_DELE_cb(void *arg, int result)
{
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;

    if ( result != LWFTP_RESULT_LOGGED ) {
        return lwftp_close(s);
    }

    s->done_fn = lv_lwftp_QUIT_cb;
    lwftp_delete(s);    
}

static void lv_lwftp_RNFR_cb(void *arg, int result)
{
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;

    if ( result != LWFTP_RESULT_LOGGED ) {
        return lwftp_close(s);
    }

    s->done_fn = lv_lwftp_QUIT_cb;
    lwftp_rename(s);
}

static void lv_lwftp_CWD_cb(void *arg, int result)
{
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;

    if ( result != LWFTP_RESULT_LOGGED ) {
        return lwftp_close(s);
    }

    s->done_fn = lv_lwftp_LIST_cb;
    lwftp_cwd(s);    
}

static void lv_lwftp_LIST_cb(void *arg, int result)
{
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;
    
    if (s->control_state != LWFTP_CWD_SENT || \
        result != LWFTP_RESULT_OK) {
        return;
    }
    
    td->list_vol = 0;
    td->active_list = list_remote;
    _lv_fm_dir_read_start(td);
    
    s->data_sink = lv_lwftp_dir_read_cb;
    s->done_fn = lv_lwftp_QUIT_cb;
    s->control_state = LWFTP_LOGGED;
    lwftp_list(s);
}

static void lv_lwftp_STOR_cb(void *arg, int result)
{
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;

    if ( result != LWFTP_RESULT_LOGGED ) {
        return lwftp_close(s);
    }

    s->data_source = lv_lwftp_file_read_cb;
    s->done_fn = lv_lwftp_QUIT_cb;
    lwftp_store(s);    
}

static void lv_lwftp_RETR_cb(void *arg, int result)
{
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;
    
    if ( result != LWFTP_RESULT_LOGGED ) {
        return lwftp_close(s);
    }

    s->data_sink = lv_lwftp_file_write_cb;
    s->done_fn = lv_lwftp_QUIT_cb;
    lwftp_retrieve(s);    
}

static void lv_lwftp_QUIT_cb(void *arg, int result)
{
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;
    
    if ( result != LWFTP_RESULT_INPROGRESS ) {
        lwftp_close(s);
    }
}

static uint lv_lwftp_dir_read_cb(void *arg, const char* ptr, uint len)
{
    /* Static buffer for reconstructing fragmented lines between TCP packets */
    static char line_buf[512];
    static uint16_t line_len = 0;
    
    char attr[12], user[64], group[64], month_str[4], name[256];
    uint8_t day, hour, min, user_nbr;
    uint32_t size, year, month_int;
    uint32_t i;
    
    lv_fm_obj_t * obj;
    lv_fm_task_data_t * td = (lv_fm_task_data_t*)arg;
    
    if (ptr == NULL || len == 0) return 0;

    for (i = 0; i < len; i++) {
        /* Ignore the carriage return */
        if (ptr[i] == '\r') {
            continue; 
        }

        /* If we find the end of the line */
        if (ptr[i] == '\n') {
            line_buf[line_len] = '\0'; /* Terminate the string securely */
            
            /* If the line is not empty, we parse it */
            if (line_len > 0) {
                /* Now we parse from line_buf, which is a safe and complete string */
                int matched = sscanf(line_buf, "%s %d %s %s %lu %s %2d %4d %[^\n]", 
                                     attr, &user_nbr, user, group, &size, 
                                     month_str, &day, &year, name);
                
                /* If the date is in hh:mm format, the previous sscanf fails in the year. */
                /* We check if the name captured a ':', which indicates time format. */
                if (matched < 9 || name[0] == ':') {
                    sscanf(line_buf, "%s %d %s %s %lu %s %2d %2d:%2d %[^\n]", 
                           attr, &user_nbr, user, group, &size, 
                           month_str, &day, &hour, &min, name);
                }

                /* Create and write the object */
                obj = LV_FM_LL_ADD(fm_remote_obj_list);
                LV_FM_LL_COPY(obj, fm_remote_obj_list);
                
                strcpy(obj->name, name);        
                obj->idx = td->list_nobj;
                obj->volume = 0;
                
                month_int = _lv_fm_month_to_int(month_str);
                obj->date = ((year - 1980) << 9) | (month_int << 5) | day;
                obj->format = _lv_fm_get_ext(name);
                
                if (attr[0] == 'd') {
                    obj->folder = 1;
                } else {
                    obj->folder = 0;
                    obj->size = size;
                }
                
                _lv_fm_list_add_obj_btn(list_remote, obj, lv_fm_list_remote_btn_event_cb);
                td->list_nobj++;
            }
            
            /* Reset the buffer for the next line */
            line_len = 0;
        } else {
            /* Accumulate characters while avoiding buffer overflow */
            if (line_len < sizeof(line_buf) - 1) {
                line_buf[line_len++] = ptr[i];
            }
        }
    }
    
    return len;
}

static uint lv_lwftp_file_read_cb(void *arg, const char** pptr, uint maxlen)
{
    static uint32_t bytesread = 0;
    
    uint32_t len = 0;
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    
    if (pptr) {
        if (td->buffer.rptr == NULL || \
            ((td->buffer.rptr - td->buffer.addr) >= td->buffer.size)) {
            f_read (&td->src, td->buffer.addr, td->buffer.size, (UINT *) &bytesread);
            td->buffer.rptr = &td->buffer.addr[0];
        }
        
        if (bytesread > 0) {
            if (bytesread < td->buffer.size) {
                if (bytesread < maxlen) {
                    len = bytesread;
                    bytesread = 0;
                } else {
                    len = maxlen;
                    bytesread -= maxlen;
                }
            } else if ((td->buffer.rptr - td->buffer.addr + maxlen) >= td->buffer.size) {
                len = td->buffer.size - (td->buffer.rptr - td->buffer.addr);
            } else {
                len = maxlen;
            }
        }
        
        *pptr = (char *) (td->buffer.rptr);
        td->buffer.rptr += len;
    }
    
    return len;
}

static uint lv_lwftp_file_write_cb(void *arg, const char* ptr, uint len)
{
    uint32_t byteswrite;
    lv_fm_task_data_t *td = (lv_fm_task_data_t*)arg;
    lwftp_session_t *s = (lwftp_session_t*) &td->lwftp_session;
    
    if (ptr && (s->control_state == LWFTP_XFERING || 
                s->control_state == LWFTP_RETR_SENT || 
                s->control_state == LWFTP_REST_SENT)) {
        if (td->buffer.wptr == NULL) {
            td->buffer.wptr = &td->buffer.addr[0];
        }
        memcpy(td->buffer.wptr, ptr, len);
        td->buffer.wptr += len;

        if ((td->buffer.wptr - td->buffer.addr) >= td->buffer.size) {
            if(f_write (&td->dst, td->buffer.addr, td->buffer.wptr - td->buffer.addr, (UINT *) &byteswrite) != FR_OK) {
                td->err = LV_FM_WRITE_ERROR;
                lwftp_close(s);
            }
            td->buffer.wptr = &td->buffer.addr[0];
        }       
    }
    
    return len;
}

DWORD get_fattime (void)
{
    RTC_TimeTypeDef stime;
    RTC_DateTypeDef sdate;  

    HAL_RTC_GetTime(&RtcHandle, &stime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&RtcHandle, &sdate, RTC_FORMAT_BIN);

    return (DWORD)(sdate.Year + 20) << 25 |
           (DWORD)sdate.Month << 21 |
           (DWORD)sdate.Date << 16 |
           (DWORD)stime.Hours << 11 |
           (DWORD)stime.Minutes << 5 |
           (DWORD)stime.Seconds >> 1;
}
