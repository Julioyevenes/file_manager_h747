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
#ifndef __STM32H747I_DISCOVERY_TS_H
#define __STM32H747I_DISCOVERY_TS_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "stm32h747i_discovery.h"

/* Include TouchScreen component driver */
#include "../Components/ft6x06/ft6x06.h"

/* Exported types ------------------------------------------------------------*/
typedef struct
{
  uint16_t TouchDetected;
  uint16_t X;
  uint16_t Y;
  uint16_t Z;
}TS_StateTypeDef;

typedef enum 
{
  TS_OK               = 0x00,
  TS_ERROR            = 0x01,
  TS_TIMEOUT          = 0x02,
  TS_DEVICE_NOT_FOUND = 0x03
}TS_StatusTypeDef;

/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
extern TS_DrvTypeDef *ts_drv;

/* Exported functions --------------------------------------------------------*/
uint8_t BSP_TS_Init(uint16_t x_size, uint16_t y_size);
uint8_t BSP_TS_GetState(TS_StateTypeDef *TsState);
uint8_t BSP_TS_ITConfig(void);

#ifdef __cplusplus
}
#endif

#endif /* __STM32H747I_DISCOVERY_TS_H */
