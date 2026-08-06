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

/* Private types -------------------------------------------------------------*/
/* Private constants ---------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
static void LWFTP_RETR_cb(void *arg, int result);
static void LWFTP_QUIT_cb(void *arg, int result);
static uint LWFTP_BufferWrite_cb(void *arg, const char* ptr, uint len);

void LWFTP_FileRead(audio_lib_handle_t * hlib, void* buff, uint32_t btr, uint32_t* br)
{
    static char remote_fullpath[1024];
    static char remote_user[256], remote_pass[256];
    static char offset_str[256];
    
    uint8_t len;
    
    lwftp_session_t *s = hlib->remote.lwftp_session;
    
    hlib->remote.ptr = buff;
    hlib->remote.btr = btr;
    hlib->remote.br = br;
    *(hlib->remote.br) = 0;
    
    stpcpy(remote_user, hlib->remote.user);
    stpcpy(remote_pass, hlib->remote.pass);

    memset(s, 0, sizeof(lwftp_session_t));
    
    strcpy(remote_fullpath, hlib->remote.path);
    len = strlen(remote_fullpath);
    if (remote_fullpath[len - 1] != '/') 
        strcat(remote_fullpath, "/");
    strcat(remote_fullpath, hlib->remote.name);
    s->remote_path = remote_fullpath;
    sprintf(offset_str, "%u", hlib->remote.fptr);
    s->offset_str = offset_str;
    IP4_ADDR(&(s->server_ip), 
             hlib->remote.ip_addr[0], hlib->remote.ip_addr[1],
             hlib->remote.ip_addr[2], hlib->remote.ip_addr[3]);
    s->server_port = hlib->remote.port;
    s->done_fn = LWFTP_RETR_cb;
    s->user = remote_user;
    s->pass = remote_pass;
    s->handle = hlib;
    if (lwftp_connect(s) != LWFTP_RESULT_INPROGRESS) 
    {
        hlib->err = AUDIO_LIB_CONNECTION_ERROR;
    }    
}

static void LWFTP_RETR_cb(void *arg, int result)
{
    audio_lib_handle_t *hlib = (audio_lib_handle_t*)arg;
    lwftp_session_t *s = hlib->remote.lwftp_session;
    
    if ( result != LWFTP_RESULT_LOGGED ) 
    {
        return lwftp_close(s);
    }

    s->data_sink = LWFTP_BufferWrite_cb;
    s->done_fn = LWFTP_QUIT_cb;
    lwftp_retrieve(s);    
}

static void LWFTP_QUIT_cb(void *arg, int result)
{
    audio_lib_handle_t *hlib = (audio_lib_handle_t*)arg;
    lwftp_session_t *s = hlib->remote.lwftp_session;
    
    if ( result != LWFTP_RESULT_INPROGRESS ) {
        lwftp_close(s);
    }
}

static uint LWFTP_BufferWrite_cb(void *arg, const char* ptr, uint len)
{
    audio_lib_handle_t *hlib = (audio_lib_handle_t*)arg;
    lwftp_session_t *s = hlib->remote.lwftp_session;
    
    uint32_t btc = 0;

    if (ptr && hlib->active)
    {
        if (*(hlib->remote.br) >= hlib->remote.btr)
        {
            if (s->control_state == LWFTP_XFERING) 
            {
                lwftp_abort(s);
            }
        }
        else
        {
            if (*(hlib->remote.br) + len <= hlib->remote.btr)
            {
                btc = len;
            }
            else 
            {
                btc = hlib->remote.btr - *(hlib->remote.br);
            }
            
            memcpy(hlib->remote.ptr + *(hlib->remote.br), ptr, btc);
            *(hlib->remote.br) += btc;
            hlib->remote.fptr += btc;
        }
    }
    
    return btc;
}
