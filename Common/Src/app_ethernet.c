/**
  ******************************************************************************
  * @file    LwIP/LwIP_HTTP_Server_Raw/Src/app_ethernet.c 
  * @author  MCD Application Team
  * @brief   Ethernet specefic module
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
/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"
#include "lwip/opt.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"
#include "app_ethernet.h"
#include "ethernetif.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define MAX_DHCP_TRIES  4

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
struct netif gnetif;

uint32_t EthernetLinkTimer;
uint32_t DHCPfineTimer = 0;
__IO uint8_t DHCP_state = DHCP_OFF;

app_netif_t app_netif =
{
    /* use_dhcp */
    1,
    
    /* link_up */
    0,
    
    /* link_down */
    0,    
    
    /* ip_addr */
    {192,168,0,10},
    
    /* mk_addr */
    {255,255,255,0},
    
    /* gw_addr */
    {192,168,0,1},
    
    /* gnetif */
    &gnetif
};

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
static void ethernet_link_status_updated(struct netif *netif);

void Netif_Config(app_netif_t * net)
{
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;

  if (net->use_dhcp)
  {
    ip_addr_set_zero_ip4(&ipaddr);
    ip_addr_set_zero_ip4(&netmask);
    ip_addr_set_zero_ip4(&gw);
  }
  else
  {
    IP_ADDR4(&ipaddr,net->ip_addr[0],net->ip_addr[1],net->ip_addr[2],net->ip_addr[3]);
    IP_ADDR4(&netmask,net->mk_addr[0],net->mk_addr[1],net->mk_addr[2],net->mk_addr[3]);
    IP_ADDR4(&gw,net->gw_addr[0],net->gw_addr[1],net->gw_addr[2],net->gw_addr[3]);
  }

  /* add the network interface */
  netif_add(net->gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);

  /*  Registers the default network interface */
  netif_set_default(net->gnetif);

  ethernet_link_status_updated(net->gnetif);

#if LWIP_NETIF_LINK_CALLBACK
  netif_set_link_callback(net->gnetif, ethernet_link_status_updated);
#endif
}

/**
  * @brief  This function notify user about link status changement.
  * @param  netif: the network interface
  * @retval None
  */
void ethernet_link_status_updated(struct netif *netif)
{
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;
  app_netif_t * net = &app_netif;
  
  if(netif_is_link_up(net->gnetif))
  {    
    if (net->use_dhcp)
    {
        /* Update DHCP state machine */
        DHCP_state = DHCP_START;
    }
    else
    {        
        IP_ADDR4(&ipaddr,net->ip_addr[0],net->ip_addr[1],net->ip_addr[2],net->ip_addr[3]);
        IP_ADDR4(&netmask,net->mk_addr[0],net->mk_addr[1],net->mk_addr[2],net->mk_addr[3]);
        IP_ADDR4(&gw,net->gw_addr[0],net->gw_addr[1],net->gw_addr[2],net->gw_addr[3]);    
        netif_set_addr(net->gnetif, &ipaddr , &netmask, &gw);
    }     
    
    /* When the netif is fully configured this function must be called.*/
    netif_set_up(net->gnetif);     
  }
  else
  {
    if (net->use_dhcp)
    {
        /* Update DHCP state machine */
        DHCP_state = DHCP_LINK_DOWN;
    }
    
    /*  When the netif link is down this function must be called.*/
    netif_set_down(net->gnetif);

    net->link_down = 1;   
  }
}

#if LWIP_NETIF_LINK_CALLBACK
/**
  * @brief  Ethernet Link periodic check
  * @param  netif
  * @retval None
  */
void Ethernet_Link_Periodic_Handle(app_netif_t * net)
{
  /* Ethernet Link every 100ms */
  if (HAL_GetTick() - EthernetLinkTimer >= 1000)
  {
    EthernetLinkTimer = HAL_GetTick();
    ethernet_link_check_state(net->gnetif);
  }
}
#endif

/**
  * @brief  DHCP_Process_Handle
  * @param  None
  * @retval None
  */
void DHCP_Process(app_netif_t * net)
{
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gw;
  struct dhcp *dhcp;
  
  switch (DHCP_state)
  {
    case DHCP_START:
    {
      ip_addr_set_zero_ip4(&(net->gnetif)->ip_addr);
      ip_addr_set_zero_ip4(&(net->gnetif)->netmask);
      ip_addr_set_zero_ip4(&(net->gnetif)->gw);
      DHCP_state = DHCP_WAIT_ADDRESS;
      dhcp_start(net->gnetif);
    }
    break;
    
  case DHCP_WAIT_ADDRESS:
    {
      if (dhcp_supplied_address(net->gnetif)) 
      {
        DHCP_state = DHCP_ADDRESS_ASSIGNED;
        
        net->link_up = 1;
      }
      else
      {
        dhcp = (struct dhcp *)netif_get_client_data(net->gnetif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP);
    
        /* DHCP timeout */
        if (dhcp->tries > MAX_DHCP_TRIES)
        {
          DHCP_state = DHCP_TIMEOUT;
          
          /* Stop DHCP */
          dhcp_stop(net->gnetif);
          
          /* Static address used */
          IP_ADDR4(&ipaddr,net->ip_addr[0],net->ip_addr[1],net->ip_addr[2],net->ip_addr[3]);
          IP_ADDR4(&netmask,net->mk_addr[0],net->mk_addr[1],net->mk_addr[2],net->mk_addr[3]);
          IP_ADDR4(&gw,net->gw_addr[0],net->gw_addr[1],net->gw_addr[2],net->gw_addr[3]);
          netif_set_addr(net->gnetif, &ipaddr, &netmask, &gw);
        }
      }
    }
    break;
  case DHCP_LINK_DOWN:
    {
      /* Stop DHCP */
      dhcp_stop(net->gnetif);
      DHCP_state = DHCP_OFF; 
    }
    break;
  default: break;
  }
}

/**
  * @brief  DHCP periodic check
  * @param  localtime the current LocalTime value
  * @retval None
  */
void DHCP_Periodic_Handle(app_netif_t * net)
{  
  /* Fine DHCP periodic process every 500ms */
  if (HAL_GetTick() - DHCPfineTimer >= DHCP_FINE_TIMER_MSECS)
  {
    DHCPfineTimer =  HAL_GetTick();
    /* process DHCP state machine */
    DHCP_Process(net);
  }
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
