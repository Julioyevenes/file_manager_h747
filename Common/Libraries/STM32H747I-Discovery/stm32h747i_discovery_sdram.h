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
#ifndef __STM32H747I_DISCOVERY_SDRAM_H
#define __STM32H747I_DISCOVERY_SDRAM_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "stm32h747i_discovery.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/**
  * @brief  SDRAM status structure definition
  */
#define   SDRAM_OK         			  ((uint8_t)0x00)
#define   SDRAM_ERROR      			  ((uint8_t)0x01)
  
/**
  * @brief  FMC SDRAM Bank address
  */   
#define SDRAM_DEVICE_ADDR         ((uint32_t)0xD0000000)
#define SDRAM_DEVICE_SIZE         ((uint32_t)0x2000000)  /* SDRAM device size in Bytes */
  
/**
  * @brief  FMC SDRAM Memory Width
  */  
/* #define SDRAM_MEMORY_WIDTH   	FMC_SDRAM_MEM_BUS_WIDTH_8 */
/* #define SDRAM_MEMORY_WIDTH     FMC_SDRAM_MEM_BUS_WIDTH_16 */
#define SDRAM_MEMORY_WIDTH      	FMC_SDRAM_MEM_BUS_WIDTH_32

/**
  * @brief  FMC SDRAM CAS Latency
  */  
/* #define SDRAM_CAS_LATENCY    	FMC_SDRAM_CAS_LATENCY_2 */
#define SDRAM_CAS_LATENCY       	FMC_SDRAM_CAS_LATENCY_3 

/**
  * @brief  FMC SDRAM Memory clock period
  */  
#define SDCLOCK_PERIOD          	FMC_SDRAM_CLOCK_PERIOD_2    /* Default configuration used with LCD */
/* #define SDCLOCK_PERIOD       	FMC_SDRAM_CLOCK_PERIOD_3 */

/**
  * @brief  FMC SDRAM Memory Read Burst feature
  */  
/* #define SDRAM_READBURST        FMC_SDRAM_RBURST_DISABLE */    /* Default configuration used with LCD */
#define SDRAM_READBURST      	    FMC_SDRAM_RBURST_ENABLE

/**
  * @brief  FMC SDRAM Bank Remap
  */    
/* #define SDRAM_BANK_REMAP */

/* Set the refresh rate counter */
/* (15.62 us x Freq) - 20 */
#define REFRESH_COUNT           	((uint32_t)0x0603)   /* SDRAM refresh counter */
#define SDRAM_TIMEOUT           	((uint32_t)0xFFFF)

/* DMA definitions for SDRAM DMA transfer */
#define SDRAM_MDMAx_CLK_ENABLE                    __HAL_RCC_MDMA_CLK_ENABLE
#define SDRAM_MDMAx_CLK_DISABLE                   __HAL_RCC_MDMA_CLK_DISABLE
#define SDRAM_MDMAx_CHANNEL                       MDMA_Channel0
#define SDRAM_MDMAx_IRQn                          MDMA_IRQn
#define SDRAM_MDMA_IRQHandler                     MDMA_IRQHandler

/**
  * @brief  FMC SDRAM Mode definition register defines
  */
#define SDRAM_MODEREG_BURST_LENGTH_1              ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2              ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4              ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8              ((uint16_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL       ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED      ((uint16_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2               ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3               ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD     ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED  ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE      ((uint16_t)0x0200)

/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions --------------------------------------------------------*/
uint8_t BSP_SDRAM_Init(void);
void    BSP_SDRAM_Initialization_sequence(uint32_t RefreshCount);
uint8_t BSP_SDRAM_ReadData(uint32_t uwStartAddress, uint32_t* pData, uint32_t uwDataSize);
uint8_t BSP_SDRAM_ReadData_DMA(uint32_t uwStartAddress, uint32_t* pData, uint32_t uwDataSize);
uint8_t BSP_SDRAM_WriteData(uint32_t uwStartAddress, uint32_t* pData, uint32_t uwDataSize);
uint8_t BSP_SDRAM_WriteData_DMA(uint32_t uwStartAddress, uint32_t* pData, uint32_t uwDataSize);
uint8_t BSP_SDRAM_Sendcmd(FMC_SDRAM_CommandTypeDef *SdramCmd);
void    BSP_SDRAM_IRQHandler(void);

void    BSP_SDRAM_MspInit(SDRAM_HandleTypeDef  *hsdram, void *Params);
void    BSP_SDRAM_MspDeInit(SDRAM_HandleTypeDef  *hsdram, void *Params);

#ifdef __cplusplus
}
#endif

#endif /* __STM32H747I_DISCOVERY_SDRAM_H */