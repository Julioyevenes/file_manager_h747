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
#include "ftps.h"

#include "ff.h"
#include "lwip/tcp.h"
#include "lwip/mem.h"
#include "lv_file_manager.h"

#include <stdarg.h>

/* Private types -------------------------------------------------------------*/
typedef enum ftps_state_t
{
	FTPS_IDLE,
	FTPS_PASS,
	FTPS_LIST,
	FTPS_RNFR,
	FTPS_RETR,
	FTPS_STOR,
	FTPS_QUIT
} ftps_state_t;

typedef struct ftps_buf_t
{
    u8_t *addr;
    u32_t wptr, rptr;    
} ftps_buf_t;

typedef struct ftps_data_t
{
    u8_t conn;
    
    FIL *fp;
    DIR *dp;
    
    ftps_buf_t buf;
    struct ftps_ctrl_t *ctrl;
    struct tcp_pcb *ctrl_pcb;
    struct tcp_pcb *listen_pcb;

    struct ftps_data_t *next;
} ftps_data_t;

typedef struct ftps_ctrl_t
{
    char frn[256], cwd[256];
    u8_t pasv;
    u32_t offset, timeout;    
    
    ftps_state_t state;
    ftps_buf_t buf;
    
    u16_t data_port;
    struct ip4_addr data_ip;
    struct tcp_pcb *data_pcb;
    struct ftps_data_t *data;
    
    struct tcp_pcb *pcb;
    struct ftps_ctrl_t *next;    
} ftps_ctrl_t;

typedef struct ftps_cmd_t
{
	char *cmd;
	void (* func) (const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
} ftps_cmd_t;

/* Private constants ---------------------------------------------------------*/
#define FTPS_CRED_SIZE 20
#define FTPS_CTRL_PORT 21
#define FTPS_CTRL_BUFSIZE 2048
#define FTPS_CTRL_TIMEOUT 60
#define FTPS_DATA_BUFSIZE (32 * 1024)
#define FTPS_DATA_MAXSIZE (6 * 5840)
#define FTPS_PORT_START 4096
#define FTPS_PORT_MAX 32768

static const char *month_table[12] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
};

/* Private macro -------------------------------------------------------------*/
#define FTPS_DEBUG(...) lv_fm_printf(__VA_ARGS__);

#define FTPS_malloc     malloc
#define FTPS_free       free

#define FTPS_FREE(ptr)          \
if (ptr != NULL) {              \
    FTPS_free(ptr); ptr = NULL; \
}

#define FTPS_FCLS(ptr)                          \
if (ptr != NULL) {                              \
    f_close(ptr); FTPS_free(ptr); ptr = NULL;   \
}

#define FTPS_DCLS(ptr)                              \
if (ptr != NULL) {                                  \
    f_closedir(ptr); FTPS_free(ptr); ptr = NULL;    \
}

#define FTPS_LL_ADD(list, e)    \
do {                            \
    e->next = list;             \
    list = e;                   \
} while (0)
    
#define FTPS_LL_DEL(list, e)                                    \
do {                                                            \
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
} while (0)
    
#define FTPS_LL_FIND(list, e)                               \
({                                                          \
    bool res = false;                                       \
    typeof(list) _tmp;                                      \
    for (_tmp = list; _tmp != NULL; _tmp = _tmp->next) {    \
        if (_tmp == e) {                                    \
            res = true;                                     \
            break;                                          \
        }                                                   \
    }                                                       \
    res;                                                    \
})    

/* Private variables ---------------------------------------------------------*/
u8_t cur_year;
u32_t ctrl_cnt = 0;
u32_t data_cnt = 0;
ftps_ctrl_t *ctrl_conns;
ftps_data_t *data_conns;

char ftps_user[FTPS_CRED_SIZE] = "admin";
char ftps_pass[FTPS_CRED_SIZE] = "1234";
char ftps_lfn_buffer[256];

/* Private function prototypes -----------------------------------------------*/
static void ftps_ctrl_send(ftps_ctrl_t *h, struct tcp_pcb *pcb, char *fmt, ...);
static void ftps_ctrl_close(ftps_ctrl_t *h, struct tcp_pcb *pcb);
static err_t ftps_data_open(ftps_ctrl_t *h, struct tcp_pcb *pcb);
static void ftps_data_close(ftps_data_t *h, struct tcp_pcb *pcb);

static void ftps_write(ftps_buf_t *b, struct tcp_pcb *pcb);
static void ftps_dir_read(ftps_data_t *h, struct tcp_pcb *pcb);
static void ftps_file_send(ftps_data_t *h, struct tcp_pcb *pcb);

static err_t ftps_ctrl_accept_cb(void *arg, struct tcp_pcb *pcb, err_t err);
static err_t ftps_ctrl_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len);
static err_t ftps_ctrl_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static err_t ftps_ctrl_poll_cb(void *arg, struct tcp_pcb *pcb);
static void ftps_ctrl_err_cb(void *arg, err_t err);

static err_t ftps_data_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err);
static err_t ftps_data_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len);
static err_t ftps_data_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static void ftps_data_err_cb(void *arg, err_t err);

static void cmd_user(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_pass(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_quit(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_abrt(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_type(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_syst(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_mode(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_pwd(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_noop(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_port(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_list(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_cdup(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_cwd(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_retr(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_stor(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_appe(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_dele(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_rnfr(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_rnto(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_mkd(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_rest(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);
static void cmd_pasv(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h);

ftps_cmd_t ftps_cmd[] =
{
	{"USER", cmd_user},
	{"PASS", cmd_pass},
	{"QUIT", cmd_quit},
	{"ABOR", cmd_abrt},
	{"TYPE", cmd_type},
	{"SYST", cmd_syst},
	{"MODE", cmd_mode},
	{"PWD", cmd_pwd},
	{"NOOP", cmd_noop},
	{"PORT", cmd_port},
	{"LIST", cmd_list},
    {"CDUP", cmd_cdup},	
	{"CWD", cmd_cwd},
	{"RETR", cmd_retr},
	{"STOR", cmd_stor},
	{"APPE", cmd_appe},
	{"DELE", cmd_dele},
	{"RMD", cmd_dele},
    {"RNFR", cmd_rnfr},
    {"RNTO", cmd_rnto},
    {"MKD", cmd_mkd},
    {"REST", cmd_rest},
    {"PASV", cmd_pasv},
	{NULL, NULL}
};

void ftps_init(void)
{
	struct tcp_pcb *pcb;
	
	pcb = tcp_new();
	tcp_bind(pcb, IP_ADDR_ANY, FTPS_CTRL_PORT);
	pcb = tcp_listen(pcb);
	tcp_accept(pcb, ftps_ctrl_accept_cb);	
}

void ftps_user_set(const char *str)
{
	strcpy(ftps_user, str);
}

void ftps_pass_set(const char *str)
{
	strcpy(ftps_pass, str);
}

void ftps_user_get(char *str)
{
	strcpy(str, ftps_user);
}

void ftps_pass_get(char *str)
{
	strcpy(str, ftps_pass);
}

__weak void ftps_connect_cb(const ip_addr_t ip)
{
}

__weak void ftps_disconnect_cb(const ip_addr_t ip)
{
}

static void ftps_ctrl_send(ftps_ctrl_t *h, struct tcp_pcb *pcb, char *fmt, ...)
{
	va_list arg;
	
	va_start(arg, fmt);
	vsprintf((char *)h->buf.addr, fmt, arg);
	va_end(arg);
	
	strcat((char *)h->buf.addr, "\r\n");
	h->buf.wptr = strlen((char *)h->buf.addr);
	h->buf.rptr = 0;
	
	FTPS_DEBUG("[ftps_ctrl_send] response: %s", h->buf.addr);
	
	ftps_write(&h->buf, pcb);
}

static void ftps_ctrl_close(ftps_ctrl_t *h, struct tcp_pcb *pcb)
{
    u32_t tmp_ctrl_cnt = 0;
    ftps_ctrl_t *tmp_ctrl;
    
    if (pcb)
    {
        for (tmp_ctrl = ctrl_conns; tmp_ctrl != NULL; tmp_ctrl = tmp_ctrl->next)
        {
            if (tmp_ctrl->pcb->remote_ip.addr == pcb->remote_ip.addr)
            {
                tmp_ctrl_cnt++;
            }
        }
        if (tmp_ctrl_cnt < 2)
        {
            ftps_disconnect_cb(pcb->remote_ip);
        }
    }    
    
    if (h && FTPS_LL_FIND(ctrl_conns, h))
    {
        FTPS_LL_DEL(ctrl_conns, h);
        ctrl_cnt--;
        
        if (h->data)
        {
            ftps_data_close(h->data, h->data_pcb);
        }
        
        FTPS_FREE(h->buf.addr)
        h->buf.wptr = h->buf.rptr = 0;

        FTPS_FREE(h);
    }
    
    tcp_arg(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    tcp_err(pcb, NULL);
    tcp_close(pcb);    
}

static err_t ftps_data_open(ftps_ctrl_t *h, struct tcp_pcb *pcb)
{
    ip_addr_t ip;
    
    if (h->pasv)
    {
        return ERR_OK;
    }
    
    h->data = FTPS_malloc(sizeof(ftps_data_t));
    if (!h->data)
    {
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return ERR_MEM;        
    }    
    memset(h->data, 0, sizeof(ftps_data_t));
    h->data->ctrl = h;
    h->data->ctrl_pcb = pcb;
    
    h->data->buf.addr = FTPS_malloc(FTPS_DATA_MAXSIZE);
    if (!h->data->buf.addr)
    {
        FTPS_FREE(h->data)
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return ERR_MEM;        
    }    
    h->data->buf.wptr = h->data->buf.rptr = 0;
    
    h->data_pcb = tcp_new();
    if (!h->data_pcb)
    {
        FTPS_FREE(h->data->buf.addr)        
        FTPS_FREE(h->data)        
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return ERR_MEM;        
    }
    tcp_arg(h->data_pcb, h->data);
    IP_SET_TYPE_VAL(ip, IPADDR_TYPE_V4);
    ip4_addr_copy(*ip_2_ip4(&ip), h->data_ip);
    tcp_connect(h->data_pcb, &ip, h->data_port, ftps_data_connected_cb);
    
    FTPS_LL_ADD(data_conns, h->data);
    data_cnt++;
    
    return ERR_OK;
}

static void ftps_data_close(ftps_data_t *h, struct tcp_pcb *pcb)
{
    if (h && FTPS_LL_FIND(data_conns, h))
    {
        FTPS_LL_DEL(data_conns, h);
        data_cnt--;
        
        FTPS_FCLS(h->fp)    
        FTPS_DCLS(h->dp)    
        FTPS_FREE(h->buf.addr)
        h->buf.wptr = h->buf.rptr = 0;
        
        if (h->ctrl)
        {
            h->ctrl->data = NULL;
            h->ctrl->data_pcb = NULL;
            h->ctrl->state = FTPS_IDLE;
            h->ctrl->offset = 0;
        }
        
        if (h->listen_pcb)
        {
            tcp_arg(h->listen_pcb, NULL);
            tcp_accept(h->listen_pcb, NULL);
            tcp_close(h->listen_pcb);            
        }
        
        FTPS_FREE(h)        
    }
    
    tcp_arg(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    tcp_err(pcb, NULL);
    tcp_close(pcb);    
}

static void ftps_write(ftps_buf_t *b, struct tcp_pcb *pcb)
{
    err_t err;
    u32_t len;
    
    if ((b->wptr - b->rptr) > 0)
    {
        if (tcp_sndbuf(pcb) < (b->wptr - b->rptr))
        {
            len = tcp_sndbuf(pcb);
        }
        else
        {
            len = b->wptr - b->rptr;
        }
        
        err = tcp_write(pcb, b->addr + b->rptr, len, 1);
        if (err != ERR_OK) {
            FTPS_DEBUG("[ftps_write] error writing!\n");
            return;
        }

        b->rptr += len;
    }
}

static void ftps_dir_read(ftps_data_t *h, struct tcp_pcb *pcb)
{
    char fname[256];
    FILINFO fi;
    FRESULT fr;
    ftps_ctrl_t *ctrl;
    struct tcp_pcb *ctrl_pcb;
    
    fi.lfname = (TCHAR *) &ftps_lfn_buffer[0];
    fi.lfsize = 256;
    
    fr = f_readdir(h->dp, &fi);
    if (fr == FR_OK && fi.fname[0] != 0)
    {
        if(h->dp->lfn_idx != 0xFFFF) 
        {
            strcpy(fname, fi.lfname);
        } 
        else 
        {
            strcpy(fname, fi.fname);
        }        
        
        fr = f_stat(fname, &fi);
        if (fr == FR_OK && fi.fname[0] != '.')
        {
            u8_t year, month, day, hour, min;
            
            year = (fi.fdate >> 9) & 0x7f;
            month = (fi.fdate >> 5) & 0xf;
            if (!month)
            {
              month = 1;
            }
            day = fi.fdate & 0x1f;
            if (!day)
            {
              day = 1;
            }
            
            hour = (fi.ftime >> 11) & 0x1f;
            min = (fi.ftime >> 5) & 0x3f;
            
            if (year == cur_year)
            {
              h->buf.wptr = sprintf((char *)h->buf.addr, 
                                    "-rw-rw-rw- 1 user ftp %11lu %s %02i %02i:%02i %s\r\n", 
                                    fi.fsize, month_table[month - 1], day, hour, min, fname);
            }
            else
            {
              h->buf.wptr = sprintf((char *)h->buf.addr, 
                                    "-rw-rw-rw- 1 user ftp %11lu %s %02i %5i %s\r\n", 
                                    fi.fsize, month_table[month - 1], day, year + 1980, fname);              
            }
            
            h->buf.rptr = 0;
            if (fi.fattrib & AM_DIR)
            {
                h->buf.addr[0] = 'd';
            }
            
            ftps_write(&h->buf, pcb);
        }        
    }
    else
    {
        if ((h->buf.wptr - h->buf.rptr) > 0)
        {
            ftps_write(&h->buf, pcb);
            return;
        }        
        
        ctrl = h->ctrl;
        ctrl_pcb = h->ctrl_pcb;
        ftps_data_close(h, pcb);
        
        ctrl->state = FTPS_IDLE;
        
        ftps_ctrl_send(ctrl, ctrl_pcb, "226 Closing data connection."); 
    }
}

static void ftps_file_send(ftps_data_t *h, struct tcp_pcb *pcb)
{
    u32_t br;
    ftps_ctrl_t *ctrl;
    struct tcp_pcb *ctrl_pcb;    
    
    if (!h->conn)
    {
        return;
    }
    
    if (h->fp)
    {
        if (h->buf.wptr == h->buf.rptr)
        {
            f_read (h->fp, h->buf.addr, FTPS_DATA_BUFSIZE, (UINT*)&br);
            if (!br)
            {
                FTPS_FCLS(h->fp)
                h->ctrl->offset = 0;
                return;
            }        
            h->buf.wptr = br;
            h->buf.rptr = 0;
        }
        
        ftps_write(&h->buf, pcb);
    }
    else
    {
        if ((h->buf.wptr - h->buf.rptr) > 0)
        {
            ftps_write(&h->buf, pcb);
            return;
        }        
        
        ctrl = h->ctrl;
        ctrl_pcb = h->ctrl_pcb;
        ftps_data_close(h, pcb);
        
        ctrl->state = FTPS_IDLE;
        
        ftps_ctrl_send(ctrl, ctrl_pcb, "226 Closing data connection.");        
    }
}

static err_t ftps_ctrl_accept_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    ftps_ctrl_t *h = NULL;
    err_t ret_err;

    if (!pcb)
    {
        ret_err = ERR_ARG;
        goto error;
    }

    h = FTPS_malloc(sizeof(ftps_ctrl_t));
    if (!h)
    {
        ret_err = ERR_MEM;
        goto error;        
    }
    memset(h, 0, sizeof(ftps_ctrl_t)); 

    h->buf.addr = FTPS_malloc(FTPS_CTRL_BUFSIZE);   
    if (!h->buf.addr)
    {
        ret_err = ERR_MEM;
        goto error;        
    }
    h->buf.wptr = h->buf.rptr = 0;
    h->state = FTPS_IDLE;
    h->pcb = pcb;

    if (err != ERR_OK)
    {
        ret_err = err;
        goto error;
    }

    FTPS_LL_ADD(ctrl_conns, h);
    ctrl_cnt++;

    tcp_arg(pcb, h);
    tcp_sent(pcb, ftps_ctrl_sent_cb);
    tcp_recv(pcb, ftps_ctrl_recv_cb);
    tcp_poll(pcb, ftps_ctrl_poll_cb, 1);
    tcp_err(pcb, ftps_ctrl_err_cb);

    ftps_ctrl_send(h, pcb, "220 lwIP FTP Server ready.");

    return ERR_OK;
    
error:
    FTPS_DEBUG("[ftps_ctrl_accept_cb] %s (%i)\n", lwip_strerr(ret_err), ret_err);
    
    ftps_ctrl_close(h, pcb);
    
    return ret_err;
}

static err_t ftps_ctrl_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    ftps_ctrl_t *h = arg;
    err_t ret_err;
    
    if (!pcb)
    {
        ret_err = ERR_ARG;
        goto error;
    }

    if (!h)
    {
        ret_err = ERR_ABRT;
        goto error;
    }

    if (!FTPS_LL_FIND(ctrl_conns, h))
    {
        ret_err = ERR_ABRT;
        goto error;
    }

    if (h->buf.rptr == h->buf.wptr)
    {
        if (h->state == FTPS_QUIT)
        {
            ftps_ctrl_close(h, pcb);
        }
    }
    
    if (pcb->state <= ESTABLISHED)
    {
        ftps_write(&h->buf, pcb);
    }

    return ERR_OK;
    
error:
    FTPS_DEBUG("[ftps_ctrl_sent_cb] %s (%i)\n", lwip_strerr(ret_err), ret_err);
    
    if (ret_err == ERR_ABRT)
    {
        tcp_abort(pcb);
    }
    
    return ret_err;
}

static err_t ftps_ctrl_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    ftps_ctrl_t *h = arg;
    err_t ret_err;
    
    char *str, *ptr, cmd[5];
    ftps_cmd_t *pcmd;
    struct pbuf *q;    
    
    if (!pcb)
    {
        ret_err = ERR_ARG;
        goto error;
    }

    if (!h)
    {
        ret_err = ERR_ABRT;
        goto error;
    }

    if (!FTPS_LL_FIND(ctrl_conns, h))
    {
        ret_err = ERR_ABRT;
        goto error;
    }    
    
    if (err != ERR_OK)
    {
        ret_err = err;
        goto error;
    }
    
    if (p)
    {
        tcp_recved(pcb, p->tot_len);
        
        str = FTPS_malloc(p->tot_len + 1);
        if (str)
        {
            ptr = str;
            
            for (q = p; q != NULL; q = q->next) 
            {
                memmove(ptr, q->payload, q->len);
                ptr += q->len;
            }
            *ptr = '\0';

            ptr = &str[strlen(str) - 1];
            while (((*ptr == '\r') || (*ptr == '\n')) && ptr >= str)
            {
                *ptr-- = '\0';
            }

            FTPS_DEBUG("[ftps_ctrl_recv_cb] query: %s\n", str);

            strncpy(cmd, str, 4);
            for (ptr = cmd; isalpha(*ptr) && ptr < &cmd[4]; ptr++)
            {
                *ptr = toupper(*ptr);
            }
            *ptr = '\0';

            for (pcmd = ftps_cmd; pcmd->cmd != NULL; pcmd++) 
            {
                if (!strcmp(pcmd->cmd, cmd))
                {
                    break;
                }
            }

            if (strlen(str) < (strlen(cmd) + 1))
            {
                ptr = "";
            }
            else
            {
                ptr = &str[strlen(cmd) + 1];
            }

            if (pcmd->func)
            {
                pcmd->func(ptr, pcb, h);
            }
            else
            {
                ftps_ctrl_send(h, pcb, "502 Command not implemented.");
            }

            FTPS_free(str);
        }        
        
        pbuf_free(p);
    }
    else
    {
        ftps_ctrl_close(h, pcb);
    }

    return ERR_OK;

error:
    FTPS_DEBUG("[ftps_ctrl_recv_cb] %s (%i)\n", lwip_strerr(ret_err), ret_err);
    
    if (p && ret_err != ERR_ABRT)
    {
        pbuf_free(p);
    }
    
    if (ret_err == ERR_ABRT)
    {
        tcp_abort(pcb);
    }    
    
    return ret_err;    
}

static err_t ftps_ctrl_poll_cb(void *arg, struct tcp_pcb *pcb)
{
    ftps_ctrl_t *h = arg;
    err_t ret_err;
    
    if (!pcb)
    {
        ret_err = ERR_ARG;
        goto error;
    }

    if (!h)
    {
        ret_err = ERR_ABRT;
        goto error;
    }
    
    if (!FTPS_LL_FIND(ctrl_conns, h))
    {
        ret_err = ERR_ABRT;
        goto error;
    }

    if (h->data)
    {
        h->timeout = 0;
        
        if (h->data->conn)
        {
            switch (h->state) 
            {
                case FTPS_LIST:
                    ftps_dir_read(h->data, h->data_pcb);
                    break;
                case FTPS_RETR:
                    ftps_file_send(h->data, h->data_pcb);
                    break;
                default:
                    break;
            }
        }
    }
    else if (h->timeout >= FTPS_CTRL_TIMEOUT)
    {
        ftps_ctrl_close(h, pcb);
    }    
    
    h->timeout++;    

    return ERR_OK;
    
error:
    FTPS_DEBUG("[ftps_ctrl_poll_cb] %s (%i)\n", lwip_strerr(ret_err), ret_err);
    
    if (ret_err == ERR_ABRT)
    {
        tcp_abort(pcb);
    }    
    
    return ret_err;    
}

static void ftps_ctrl_err_cb(void *arg, err_t err)
{
    ftps_ctrl_t *h = arg;
    
    FTPS_DEBUG("[ftps_ctrl_err_cb] %s (%i)\n", lwip_strerr(err), err);
    
    if (!h)
    {
        return;
    }

    if (!FTPS_LL_FIND(ctrl_conns, h))
    {
        return;
    }

    FTPS_LL_DEL(ctrl_conns, h);
    ctrl_cnt--; 
    
    if (h->data)
    {
        ftps_data_close(h->data, h->data_pcb);
    }
    
    FTPS_FREE(h->buf.addr)
    h->buf.wptr = h->buf.rptr = 0;

    FTPS_FREE(h);    
}

static err_t ftps_data_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    ftps_data_t *h = arg;
    err_t ret_err;
    
    if (!pcb)
    {
        ret_err = ERR_ARG;
        goto error;
    }

    if (!h)
    {
        ret_err = ERR_ABRT;
        goto error;
    }

    if (!FTPS_LL_FIND(data_conns, h))
    {
        ret_err = ERR_ABRT;
        goto error;
    }
    
    if (err != ERR_OK)
    {
        ret_err = err;
        goto error;
    }    

    h->ctrl->data_pcb = pcb;
    h->conn = 1;
    
    tcp_sent(pcb, ftps_data_sent_cb);
    tcp_recv(pcb, ftps_data_recv_cb);
    tcp_err(pcb, ftps_data_err_cb);
    
    switch (h->ctrl->state) 
    {
        case FTPS_LIST:
            ftps_dir_read(h, pcb);
            break;
        case FTPS_RETR:
            ftps_file_send(h, pcb);
            break;
        default:
            break;
    }    
    
    return ERR_OK;
    
error:
    FTPS_DEBUG("[ftps_data_connected_cb] %s (%i)\n", lwip_strerr(ret_err), ret_err);
    
    ftps_data_close(h, pcb);
    
    if (ret_err == ERR_ABRT)
    {
        tcp_abort(pcb);
    }    
    
    return ret_err;    
}

static err_t ftps_data_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    ftps_data_t *h = arg;
    err_t ret_err;
    
    if (!pcb)
    {
        ret_err = ERR_ARG;
        goto error;
    }

    if (!h)
    {
        ret_err = ERR_ABRT;
        goto error;
    }
    
    if (!FTPS_LL_FIND(data_conns, h))
    {
        ret_err = ERR_ABRT;
        goto error;
    }

    switch (h->ctrl->state) 
    {
        case FTPS_LIST:
            ftps_dir_read(h, pcb);
            break;
        case FTPS_RETR:
            ftps_file_send(h, pcb);
            break;
        default:
            break;
    }    

    return ERR_OK;
    
error:
    FTPS_DEBUG("[ftps_data_sent_cb] %s (%i)\n", lwip_strerr(ret_err), ret_err);
    
    if (ret_err == ERR_ABRT)
    {
        tcp_abort(pcb);
    }    
    
    return ret_err;    
}

static err_t ftps_data_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    ftps_data_t *h = arg;
    err_t ret_err;
    
    ftps_ctrl_t *ctrl;
    struct tcp_pcb *ctrl_pcb;    
    struct pbuf *q;
    static u8_t *p_buf;
    u16_t tot_len = 0;
    u32_t bw;    
    
    if (!pcb)
    {
        ret_err = ERR_ARG;
        goto error;
    }

    if (!h)
    {
        ret_err = ERR_ABRT;
        goto error;
    }

    if (!FTPS_LL_FIND(data_conns, h))
    {
        ret_err = ERR_ABRT;
        goto error;
    }    
    
    if (err != ERR_OK)
    {
        ret_err = err;
        goto error;
    }
    
    if (p)
    {
        if (h->buf.wptr == 0)
        {
            p_buf = &h->buf.addr[0];
        }
        
        for (q = p; q != NULL; q = q->next) 
        {       
            memcpy(p_buf, q->payload, q->len);
            p_buf += q->len;
            h->buf.wptr += q->len;
            tot_len += q->len;

            if (h->buf.wptr >= FTPS_DATA_BUFSIZE) 
            {
                f_write (h->fp, h->buf.addr, h->buf.wptr, (UINT*)&bw);
                p_buf = &h->buf.addr[0];
                h->buf.wptr = 0;
            }
        }   

        tcp_recved(pcb, tot_len);        
        pbuf_free(p);
    }
    else
    {
        if (h->buf.wptr > 0) 
        {
            f_write (h->fp, h->buf.addr, h->buf.wptr, (UINT*)&bw);
            p_buf = &h->buf.addr[0];
            h->buf.wptr = 0;
        }
        
        FTPS_FCLS(h->fp)
        h->ctrl->offset = 0;
        
        ctrl = h->ctrl;
        ctrl_pcb = h->ctrl_pcb;
        ftps_data_close(h, pcb);
        
        ctrl->state = FTPS_IDLE;
        
        ftps_ctrl_send(ctrl, ctrl_pcb, "226 Closing data connection.");        
    }

    return ERR_OK;

error:
    FTPS_DEBUG("[ftps_data_recv_cb] %s (%i)\n", lwip_strerr(ret_err), ret_err);
    
    if (p && ret_err != ERR_ABRT)
    {
        pbuf_free(p);
    }
    
    if (ret_err == ERR_ABRT)
    {
        tcp_abort(pcb);
    }    
    
    return ret_err;    
}

static void ftps_data_err_cb(void *arg, err_t err)
{
    ftps_data_t *h = arg;
    
    FTPS_DEBUG("[ftps_data_err_cb] %s (%i)\n", lwip_strerr(err), err);
    
    if (!h)
    {
        return;
    }

    if (!FTPS_LL_FIND(data_conns, h))
    {
        return;
    }

    FTPS_LL_DEL(data_conns, h);
    data_cnt--;
    
    FTPS_FCLS(h->fp)    
    FTPS_DCLS(h->dp)    
    FTPS_FREE(h->buf.addr)
    h->buf.wptr = h->buf.rptr = 0;
    
    if (h->ctrl)
    {
        h->ctrl->data = NULL;
        h->ctrl->data_pcb = NULL;
        h->ctrl->state = FTPS_IDLE;
        h->ctrl->offset = 0;
    }
    
    if (h->listen_pcb)
    {
        tcp_arg(h->listen_pcb, NULL);
        tcp_accept(h->listen_pcb, NULL);
        tcp_close(h->listen_pcb);            
    }
    
    FTPS_FREE(h)    
}

static void cmd_user(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    if (strcmp(arg, ftps_user) != 0) 
	{
		ftps_ctrl_send(h, pcb, "530 Not logged in.");
        h->state = FTPS_QUIT;
    } 
	else 
	{
		ftps_ctrl_send(h, pcb, "331 User name okay, need password.");
        h->state = FTPS_PASS;
    }	
}

static void cmd_pass(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    if (strcmp(arg, ftps_pass) != 0) 
	{
		ftps_ctrl_send(h, pcb, "530 Not logged in.");
        h->state = FTPS_QUIT;
    } 
	else 
	{
        ftps_connect_cb(pcb->remote_ip);
		ftps_ctrl_send(h, pcb, "230 User logged in, proceed.");
        h->state = FTPS_IDLE;
    }	
}

static void cmd_quit(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
	ftps_ctrl_send(h, pcb, "221 Goodbye.");
	h->state = FTPS_QUIT;	
}

static void cmd_abrt(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    if (h->data)
    {
        ftps_data_close(h->data, h->data_pcb);
        ftps_ctrl_send(h, pcb, "426 Connection closed; transfer aborted.");
    }
    else
    {
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
    }
}

static void cmd_type(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    if(strcmp(arg, "A") != 0 && strcmp(arg, "I") != 0) 
    {
        ftps_ctrl_send(h, pcb, "502 Command not implemented.");
        return;
    }
    
    ftps_ctrl_send(h, pcb, "200 Command okay.");
}

static void cmd_syst(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    ftps_ctrl_send(h, pcb, "215 %s system type.", "UNIX");
}

static void cmd_mode(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    ftps_ctrl_send(h, pcb, "502 Command not implemented.");
}

static void cmd_pwd(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{    
    if (strlen(h->cwd) == 0)
    {
        if (f_getcwd(h->cwd, 256) != FR_OK) 
        {
            ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
            return;
        }
    }

    ftps_ctrl_send(h, pcb, "257 \"%s\" is current directory.", h->cwd);
}

static void cmd_noop(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    ftps_ctrl_send(h, pcb, "200 Command okay.");
}

static void cmd_port(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    u32_t nr, ph, pl, ip[4];

    nr = sscanf(arg, "%u,%u,%u,%u,%u,%u", &(ip[0]), &(ip[1]), &(ip[2]), &(ip[3]), &ph, &pl);
    if (nr != 6) 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
    } 
    else 
    {
        IP4_ADDR(&h->data_ip, ip[0], ip[1], ip[2], ip[3]);
        h->data_port = ((u16_t) ph << 8) | (u16_t) pl;
        ftps_ctrl_send(h, pcb, "200 Command okay.");
    }    
}

static void cmd_list(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    u32_t cur_time;
    DIR *dir;
    
    if (strlen(h->cwd) == 0)
    {
        if (f_getcwd(h->cwd, 256) != FR_OK) 
        {
            ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
            return;
        }
    }
    
    dir = (DIR *) FTPS_malloc(sizeof(DIR));    
    if (f_opendir(dir, h->cwd) != FR_OK) 
    {
        FTPS_FREE(dir)
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return;
    }
    
    if (ftps_data_open(h, pcb))
    {
        return;
    }
    
    cur_time = (u32_t)get_fattime();
    cur_year = (cur_time >> 25) & 0x7f;    
    
    h->data->dp = dir;
    h->state = FTPS_LIST;
    ftps_ctrl_send(h, pcb, "150 File status okay; about to open data connection.");
}

static void cmd_cdup(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    if (f_chdir("..") == FR_OK) 
        ftps_ctrl_send(h, pcb, "250 Requested file action okay, completed.");
    else
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
        
    if (f_getcwd(h->cwd, 256) != FR_OK) 
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");        
}

static void cmd_cwd(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    char vol_str[256], path_str[256];
    char * path_pstr;
    uint32_t len;

    if (f_getcwd(path_str, 256) != FR_OK)
    {
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return;
    }
    stpcpy(vol_str, path_str);
    vol_str[3] = '\0';   

    if (arg[0] == '/' && strstr(arg, vol_str) == NULL) 
    {        
        len = strlen(path_str);
        if (len > 2) path_str[2] = '\0';        
        strcat(path_str, arg);
    } 
    else 
    {
        strcpy(path_str, arg);
    }
    
    if (path_str[0] == '/')
        path_pstr = &path_str[1];
    else
        path_pstr = &path_str[0];
    
    len = strlen(path_pstr);
    if (len > 3) 
    {
        if (path_pstr[len - 1] == '/') path_pstr[len - 1] = '\0';
    }    
    
    f_chdrive (path_pstr);
    if (f_chdir(path_pstr) == FR_OK)
        ftps_ctrl_send(h, pcb, "250 Requested file action okay, completed.");
    else 
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
    
    if (f_getcwd(h->cwd, 256) != FR_OK) 
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
}

static void cmd_retr(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    char path[256];
    FILINFO fi;
    FIL *f;
    
    fi.lfname = (TCHAR *) &ftps_lfn_buffer[0];
    fi.lfsize = 256;
    
    if (f_getcwd(path, 256) != FR_OK) 
    {
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return;
    }

    if (strcmp(h->cwd, path))
    {
        f_chdrive (h->cwd);
        if (f_chdir(h->cwd) != FR_OK)
        {
            ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
            return;
        }
    }    
    
    if (f_stat(arg, &fi) != FR_OK)
    {
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
        return;
    }
    
    f = (FIL *) FTPS_malloc(sizeof(FIL));
    if (f_open (f, arg, FA_READ) != FR_OK)
    {
        FTPS_FREE(f)
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
        return;        
    }
    
    ftps_ctrl_send(h, pcb, "150 Opening BINARY mode data connection for %s (%u bytes).", 
                   arg, fi.fsize);
    
    if (h->offset > fi.fsize) 
    {
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
        return;
    } 
    else 
    {
        f_lseek(f, h->offset);
    }
    
    if (ftps_data_open(h, pcb))
    {
        FTPS_FCLS(f)
        h->offset = 0;
        return;
    }
    
    h->data->fp = f;
    h->state = FTPS_RETR;
}

static void cmd_stor(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    char path[256];
    FIL *f;
    
    if (f_getcwd(path, 256) != FR_OK) 
    {
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return;
    }

    if (strcmp(h->cwd, path))
    {
        f_chdrive (h->cwd);
        if (f_chdir(h->cwd) != FR_OK)
        {
            ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
            return;
        }
    }    
    
    f = (FIL *) FTPS_malloc(sizeof(FIL));
    if (f_open (f, arg, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        FTPS_FREE(f)
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
        return;        
    }
    
    ftps_ctrl_send(h, pcb, "150 Opening BINARY mode data connection for %s.", 
                   arg);
    
    if (ftps_data_open(h, pcb))
    {
        FTPS_FCLS(f)
        h->offset = 0;
        return;
    }
    
    h->data->fp = f;
    h->state = FTPS_STOR;    
}

static void cmd_appe(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    char path[256];
    FIL *f;
    
    if (f_getcwd(path, 256) != FR_OK) 
    {
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return;
    }

    if (strcmp(h->cwd, path))
    {
        f_chdrive (h->cwd);
        if (f_chdir(h->cwd) != FR_OK)
        {
            ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
            return;
        }
    }    
    
    f = (FIL *) FTPS_malloc(sizeof(FIL));
    if (f_open (f, arg, FA_WRITE | FA_OPEN_ALWAYS) != FR_OK)
    {
        FTPS_FREE(f)
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
        return;        
    }
    
    if (f_lseek(f, f_size(f)) != FR_OK)
    {
        FTPS_FCLS(f)
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
        return;                
    }
    
    ftps_ctrl_send(h, pcb, "150 Opening BINARY mode data connection for %s.", 
                   arg);
    
    if (ftps_data_open(h, pcb))
    {
        FTPS_FCLS(f)
        h->offset = 0;
        return;
    }
    
    h->data->fp = f;
    h->state = FTPS_STOR;    
}

static void cmd_dele(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{    
    char path_str[256];
    
    if (arg == NULL) 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    if (*arg == '\0') 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    if (arg[0] == '/')
    {
        if (f_getcwd(path_str, 256) != FR_OK)
        {
            ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
            return;
        }
        path_str[3] = '\0';        
        strcat(path_str, &arg[1]);
    }
    else
    {
        stpcpy(path_str, arg);
    }    
    
    if (f_unlink(path_str) != FR_OK) 
    {
        if (f_chdir("..") != FR_OK)
    {
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
        }
        else
        {
            if (f_unlink(path_str) != FR_OK)
                ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
            else
                ftps_ctrl_send(h, pcb, "250 Requested file action okay, completed.");
        }
    } 
    else 
    {
        ftps_ctrl_send(h, pcb, "250 Requested file action okay, completed.");
    }    
}

static void cmd_rnfr(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    if (arg == NULL) 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    if (*arg == '\0') 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    strcpy(h->frn, arg);
    
    h->state = FTPS_RNFR;
    
    ftps_ctrl_send(h, pcb, "350 Requested file action pending further information.");
}

static void cmd_rnto(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    if (h->state != FTPS_RNFR) 
    {
        ftps_ctrl_send(h, pcb, "503 Bad sequence of commands.");
        return;
    }
    
    h->state = FTPS_IDLE;
    
    if (arg == NULL) 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    if (*arg == '\0') 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    if (f_rename(h->frn, arg) != FR_OK) 
    {
        ftps_ctrl_send(h, pcb, "450 Requested file action not taken.");
    } 
    else 
    {
        ftps_ctrl_send(h, pcb, "250 Requested file action okay, completed.");
    }    
}

static void cmd_mkd(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    if (arg == NULL) 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    if (*arg == '\0') 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    if (f_mkdir(arg) != FR_OK) 
    {
        ftps_ctrl_send(h, pcb, "550 Requested action not taken.");
    } 
    else 
    {
        ftps_ctrl_send(h, pcb, "257 \"%s\" created.", arg);
    }    
}

static void cmd_rest(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{    
    if (arg == NULL) 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    if (*arg == '\0') 
    {
        ftps_ctrl_send(h, pcb, "501 Syntax error in parameters or arguments.");
        return;
    }
    
    sscanf(arg, "%u", &h->offset);
    ftps_ctrl_send(h, pcb, "350 Restarting at %s.", arg);
}

static void cmd_pasv(const char *arg, struct tcp_pcb *pcb, ftps_ctrl_t *h)
{
    err_t err;
    static u16_t port = FTPS_PORT_START;
    struct tcp_pcb *lpcb, *npcb;
    
    h->data = FTPS_malloc(sizeof(ftps_data_t));
    if (!h->data)
    {
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return;        
    }    
    memset(h->data, 0, sizeof(ftps_data_t));
    h->data->ctrl = h;
    h->data->ctrl_pcb = pcb;
    
    h->data->buf.addr = FTPS_malloc(FTPS_DATA_MAXSIZE);
    if (!h->data->buf.addr)
    {
        FTPS_FREE(h->data)
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return;        
    }    
    h->data->buf.wptr = h->data->buf.rptr = 0;
    
    npcb = tcp_new();
    if (!npcb)
    {
        FTPS_FREE(h->data->buf.addr)        
        FTPS_FREE(h->data)
        ftps_ctrl_send(h, pcb, "451 Requested action aborted: local error in processing.");
        return;        
    }
    
    do
    {
        if (port > FTPS_PORT_MAX)
        {
            port = FTPS_PORT_START;
        }
        h->data_port = port;
        port++;
        
        err = tcp_bind(npcb, (ip_addr_t*)&pcb->local_ip, h->data_port);
        if (err != ERR_OK && err != ERR_USE)
        {
            ftps_data_close(h->data, npcb);
            return;
        }
    } while(err != ERR_OK);
    
    lpcb = tcp_listen(npcb);
    if (!lpcb)
    {
        ftps_data_close(h->data, npcb);
        return;        
    }
    h->data->listen_pcb = lpcb;
    
    h->data->conn = 0;
    h->pasv = 1;
    
    tcp_arg(h->data->listen_pcb, h->data);
    tcp_accept(h->data->listen_pcb, ftps_data_connected_cb);
    
    ftps_ctrl_send(h, pcb, "227 Entering Passive Mode (%i,%i,%i,%i,%i,%i).", 
                   ip4_addr1(ip_2_ip4(&pcb->local_ip)), 
                   ip4_addr2(ip_2_ip4(&pcb->local_ip)), 
                   ip4_addr3(ip_2_ip4(&pcb->local_ip)), 
                   ip4_addr4(ip_2_ip4(&pcb->local_ip)), 
                   (h->data_port >> 8) & 0xff, 
                   (h->data_port) & 0xff);
    
    FTPS_LL_ADD(data_conns, h->data);
    data_cnt++;    
}
