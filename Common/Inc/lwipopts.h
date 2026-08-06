/**
  ******************************************************************************
  * @file    LwIP/LwIP_TFTP_Server/Inc/lwipopts.h
  * @author  MCD Application Team
  * @version V1.2.0
  * @date    30-December-2016
  * @brief   lwIP Options Configuration.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics International N.V. 
  * All rights reserved.</center></h2>
  *
  * Redistribution and use in source and binary forms, with or without 
  * modification, are permitted, provided that the following conditions are met:
  *
  * 1. Redistribution of source code must retain the above copyright notice, 
  *    this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  * 3. Neither the name of STMicroelectronics nor the names of other 
  *    contributors to this software may be used to endorse or promote products 
  *    derived from this software without specific written permission.
  * 4. This software, including modifications and/or derivative works of this 
  *    software, must execute solely and exclusively on microcontroller or
  *    microprocessor devices manufactured by or for STMicroelectronics.
  * 5. Redistribution and use of this software other than as permitted under 
  *    this license is void and will automatically terminate your rights under 
  *    this license. 
  *
  * THIS SOFTWARE IS PROVIDED BY STMICROELECTRONICS AND CONTRIBUTORS "AS IS" 
  * AND ANY EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT 
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
  * PARTICULAR PURPOSE AND NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY
  * RIGHTS ARE DISCLAIMED TO THE FULLEST EXTENT PERMITTED BY LAW. IN NO EVENT 
  * SHALL STMICROELECTRONICS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
  * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
  * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/* NO_SYS==1: Provides VERY minimal functionality. Otherwise,
   use lwIP facilities. */
#define NO_SYS                              1

/* SYS_LIGHTWEIGHT_PROT==0: disable inter-task protection (and task-vs-interrupt
   protection) for certain critical regions during buffer allocation, deallocation
   and memory allocation and deallocation. */
#define SYS_LIGHTWEIGHT_PROT                0

/*
   ------------------------------------
   ---------- Memory options ----------
   ------------------------------------
*/
/* MEM_LIBC_MALLOC==1: Use malloc/free/realloc provided by your C-library
   instead of the lwip internal allocator. Can save code size if you
   already use it. */
#define MEM_LIBC_MALLOC                     0

/* MEMP_MEM_MALLOC==1: Use mem_malloc/mem_free instead of the lwip pool allocator.
   Especially useful with MEM_LIBC_MALLOC but handle with care regarding execution
   speed (heap alloc can be much slower than pool alloc) and usage from interrupts
   (especially if your netif driver allocates PBUF_POOL pbufs for received frames
   from interrupt)!
   ATTENTION: Currently, this uses the heap for ALL pools (also for private pools,
   not only for internal pools defined in memp_std.h)! */
#define MEMP_MEM_MALLOC                     1

/* MEM_ALIGNMENT: should be set to the alignment of the CPU for which
   lwIP is compiled. 4 byte alignment -> define MEM_ALIGNMENT to 4, 2
   byte alignment -> define MEM_ALIGNMENT to 2. */
#define MEM_ALIGNMENT                       4

/* MEM_SIZE: the size of the heap memory. If the application will send
   a lot of data that needs to be copied, this should be set high. */
#define MEM_SIZE                            (256*1024)

/* Relocate the LwIP RAM heap pointer */
#define LWIP_RAM_HEAP_POINTER               (0x30004000)

/* MEMP_NUM_PBUF: the number of memp struct pbufs. If the application
   sends a lot of data out of ROM (or other static memory), this
   should be set high. */
#define MEMP_NUM_PBUF                       0
/* MEMP_NUM_UDP_PCB: the number of UDP protocol control blocks. One
   per active UDP "connection". */
#define MEMP_NUM_UDP_PCB                    3
/* MEMP_NUM_TCP_PCB: the number of simulatenously active TCP
   connections. */
#define MEMP_NUM_TCP_PCB                    32
/* MEMP_NUM_TCP_PCB_LISTEN: the number of listening TCP
   connections. */
#define MEMP_NUM_TCP_PCB_LISTEN             32
/* MEMP_NUM_TCP_SEG: the number of simultaneously queued TCP
   segments. */
#define MEMP_NUM_TCP_SEG                    64
/* MEMP_NUM_SYS_TIMEOUT: the number of simulateously active
   timeouts. */
#define MEMP_NUM_SYS_TIMEOUT                (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 1)

/*
   ----------------------------------
   ---------- Pbuf options ----------
   ----------------------------------
*/
/* PBUF_POOL_SIZE: the number of buffers in the pbuf pool. */
#define PBUF_POOL_SIZE                      64

/* PBUF_POOL_BUFSIZE: the size of each pbuf in the pbuf pool. */
#define PBUF_POOL_BUFSIZE                   1536

/* LWIP_SUPPORT_CUSTOM_PBUF == 1: to pass directly MAC Rx buffers to the stack 
   no copy is needed */
#define LWIP_SUPPORT_CUSTOM_PBUF            1

/*
   ----------------------------------
   ---------- IPv4 options ----------
   ----------------------------------
*/
#define LWIP_IPV4                           1

/*
   ---------------------------------
   ---------- TCP options ----------
   ---------------------------------
*/
#define LWIP_TCP                            1
#define TCP_TTL                             255

/* Controls if TCP should queue segments that arrive out of
   order. Define to 0 if your device is low on memory. */
#define TCP_QUEUE_OOSEQ                     1

/* TCP Maximum segment size. */
#define TCP_MSS                             (1500 - 40)	  /* TCP_MSS = (Ethernet MTU - IP header size - TCP header size) */

/* TCP sender buffer space (bytes). */
#define TCP_SND_BUF                         (32*TCP_MSS)

/* TCP_SND_QUEUELEN: TCP sender buffer space (pbufs). This must be at least
   as much as (2 * TCP_SND_BUF/TCP_MSS) for things to work. */
#define TCP_SND_QUEUELEN                    (4* TCP_SND_BUF/TCP_MSS)

/* TCP receive window. */
#define TCP_WND                             (4*TCP_MSS)

/*
   ----------------------------------
   ---------- ICMP options ----------
   ----------------------------------
*/
#define LWIP_ICMP                           1

/*
   ----------------------------------
   ---------- DHCP options ----------
   ----------------------------------
*/
#define LWIP_DHCP                           1

/* LWIP_NETIF_HOSTNAME==1: use DHCP_OPTION_HOSTNAME with netif's hostname
   field. */
#define LWIP_NETIF_HOSTNAME                 1

/*
   ----------------------------------
   ---------- DNS options -----------
   ----------------------------------
*/
/* LWIP_DNS==1: Turn on DNS module. UDP must be available for DNS
   transport. */
#define LWIP_DNS                            1

/*
   ---------------------------------
   ---------- UDP options ----------
   ---------------------------------
*/
#define LWIP_UDP                            1
#define UDP_TTL                             255

/*
   ----------------------------------------
   ---------- Statistics options ----------
   ----------------------------------------
*/
#define LWIP_STATS                          1

/*
   -------------------------------------------
   ---------- link callback options ----------
   -------------------------------------------
*/
/* LWIP_NETIF_LINK_CALLBACK==1: Support a callback function from an interface
   whenever the link changes (i.e., link down) */
#define LWIP_NETIF_LINK_CALLBACK            1

/*
   --------------------------------------
   ---------- Checksum options ----------
   --------------------------------------
*/

/* The STM32F7xxallows computing and verifying the IP, UDP, TCP and ICMP checksums by hardware:
   - To use this feature let the following define uncommented.
   - To disable it and process by CPU comment the  the checksum. */
#define CHECKSUM_BY_HARDWARE 

#ifdef CHECKSUM_BY_HARDWARE
  /* CHECKSUM_GEN_IP==0: Generate checksums by hardware for outgoing IP packets.*/
  #define CHECKSUM_GEN_IP                   0
  /* CHECKSUM_GEN_UDP==0: Generate checksums by hardware for outgoing UDP packets.*/
  #define CHECKSUM_GEN_UDP                  0
  /* CHECKSUM_GEN_TCP==0: Generate checksums by hardware for outgoing TCP packets.*/
  #define CHECKSUM_GEN_TCP                  0 
  /* CHECKSUM_CHECK_IP==0: Check checksums by hardware for incoming IP packets.*/
  #define CHECKSUM_CHECK_IP                 0
  /* CHECKSUM_CHECK_UDP==0: Check checksums by hardware for incoming UDP packets.*/
  #define CHECKSUM_CHECK_UDP                0
  /* CHECKSUM_CHECK_TCP==0: Check checksums by hardware for incoming TCP packets.*/
  #define CHECKSUM_CHECK_TCP                0
  /* CHECKSUM_CHECK_ICMP==0: Check checksums by hardware for incoming ICMP packets.*/
  #define CHECKSUM_GEN_ICMP                 0
#else
  /* CHECKSUM_GEN_IP==1: Generate checksums in software for outgoing IP packets.*/
  #define CHECKSUM_GEN_IP                   1
  /* CHECKSUM_GEN_UDP==1: Generate checksums in software for outgoing UDP packets.*/
  #define CHECKSUM_GEN_UDP                  1
  /* CHECKSUM_GEN_TCP==1: Generate checksums in software for outgoing TCP packets.*/
  #define CHECKSUM_GEN_TCP                  1
  /* CHECKSUM_CHECK_IP==1: Check checksums in software for incoming IP packets.*/
  #define CHECKSUM_CHECK_IP                 1
  /* CHECKSUM_CHECK_UDP==1: Check checksums in software for incoming UDP packets.*/
  #define CHECKSUM_CHECK_UDP                1
  /* CHECKSUM_CHECK_TCP==1: Check checksums in software for incoming TCP packets.*/
  #define CHECKSUM_CHECK_TCP                1
  /* CHECKSUM_CHECK_ICMP==1: Check checksums by hardware for incoming ICMP packets.*/
  #define CHECKSUM_GEN_ICMP                 1
#endif

/*
   ----------------------------------------------
   ---------- Sequential layer options ----------
   ----------------------------------------------
*/
/* LWIP_NETCONN==1: Enable Netconn API (require to use api_lib.c) */
#define LWIP_NETCONN                        0

/*
   ------------------------------------
   ---------- Socket options ----------
   ------------------------------------
*/
/* LWIP_SOCKET==1: Enable Socket API (require to use sockets.c) */
#define LWIP_SOCKET                         0

/*
   ------------------------------------
   ---------- SNTP options ----------
   ------------------------------------
*/
/* SNTP macro to change system time in seconds
   Define SNTP_SET_SYSTEM_TIME_US(sec, us) to set the time in microseconds
   instead of this one if you need the additional precision. Alternatively,
   define SNTP_SET_SYSTEM_TIME_NTP(sec, frac) in order to work with native
   NTP timestamps instead. */
#include "rtc.h"   
#define SNTP_SET_SYSTEM_TIME(sec)           RTC_CalendarConfig(sec)

/* Set this to 1 to support DNS names (or IP address strings) to set sntp servers
   One server address/name can be defined as default if SNTP_SERVER_DNS == 1:
   \#define SNTP_SERVER_ADDRESS "pool.ntp.org" */
#define SNTP_SERVER_DNS                     1

/*
   -----------------------------------
   ---------- Debug options ----------
   -----------------------------------
*/
/* Platform specific diagnostic output.\n
   Note the default implementation pulls in printf, which may
   in turn pull in a lot of standard libary code. In resource-constrained 
   systems, this should be defined to something less resource-consuming. */
#include "lv_file_manager.h"
#define LWIP_PLATFORM_DIAG(x)               do {lv_fm_printf x;} while(0)

#define LWIP_DEBUG                          1
/*#define NETIF_DEBUG   LWIP_DBG_ON*/
/*#define DHCP_DEBUG    LWIP_DBG_ON*/
/*#define UDP_DEBUG     LWIP_DBG_ON*/
/*#define TCP_DEBUG     LWIP_DBG_ON*/
#define MEMP_DEBUG                          LWIP_DBG_ON
#define MEM_DEBUG                           LWIP_DBG_ON
/*#define ICMP_DEBUG    LWIP_DBG_ON*/


#endif /* __LWIPOPTS_H__ */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
