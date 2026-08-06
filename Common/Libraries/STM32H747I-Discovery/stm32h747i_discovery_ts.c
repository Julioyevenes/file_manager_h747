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
#include "stm32h747i_discovery_ts.h"

/* Private types -------------------------------------------------------------*/
/* Private constants ---------------------------------------------------------*/
#define TS_SWAP_NONE                    ((uint8_t) 0x01)
#define TS_SWAP_X                       ((uint8_t) 0x02)
#define TS_SWAP_Y                       ((uint8_t) 0x04)
#define TS_SWAP_XY                      ((uint8_t) 0x08)

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static uint8_t ts_orientation;

TS_DrvTypeDef *ts_drv;

/* Private function prototypes -----------------------------------------------*/

/**
  * @brief  Initializes and configures the touch screen functionalities and
  *         configures all necessary hardware resources (GPIOs, I2C, clocks..).
  * @param  ts_SizeX : Maximum X size of the TS area on LCD
  * @param  ts_SizeY : Maximum Y size of the TS area on LCD
  * @retval TS_OK if all initializations are OK. Other value if error.
  */
uint8_t BSP_TS_Init(uint16_t x_size, uint16_t y_size)
{
  uint8_t ts_status = TS_OK;

  /* Initialize the communication channel to sensor (I2C) if necessary */
  /* that is initialization is done only once after a power up         */  
  ft6x06_ts_drv.Init(TS_I2C_ADDRESS);

  if(ft6x06_ts_drv.ReadID(TS_I2C_ADDRESS) == FT6206_ID_VALUE)
  {
    /* Found FT6206 : Initialize the TS driver structure */
    ts_drv = &ft6x06_ts_drv;

    /* Get LCD chosen orientation */
    if(x_size < y_size)
    {
      ts_orientation = TS_SWAP_NONE;                
    }
    else
    {
      ts_orientation = TS_SWAP_XY | TS_SWAP_Y;                 
    }

    /* Software reset the TouchScreen */
    ts_drv->Reset(TS_I2C_ADDRESS);

    /* Calibrate, Configure and Start the TouchScreen driver */
    ts_drv->Start(TS_I2C_ADDRESS);    
  }
  else
  {
    ts_status = TS_DEVICE_NOT_FOUND;
  }
  
  return ts_status;
}

/**
  * @brief  Returns status and positions of the touch screen.
  * @param  TsState: Pointer to touch screen current state structure
  * @retval TS_OK if all initializations are OK. Other value if error.
  */
uint8_t BSP_TS_GetState(TS_StateTypeDef *TsState)
{
  uint8_t ts_status = TS_OK;
  uint16_t tmp, x_raw, y_raw, x_diff, y_diff;
  static uint32_t x, y;
  
  TsState->TouchDetected = ts_drv->DetectTouch(TS_I2C_ADDRESS);
  if(TsState->TouchDetected)
  {
    /* Get each touch coordinates */
    ts_drv->GetXY(TS_I2C_ADDRESS, &(x_raw), &(y_raw));

    if(ts_orientation & TS_SWAP_XY)
    {
      tmp = x_raw;
      x_raw = y_raw; 
      y_raw = tmp;
    }
    
    if(ts_orientation & TS_SWAP_X)
    {
      x_raw = FT_6206_MAX_WIDTH - 1 - x_raw;
    }

    if(ts_orientation & TS_SWAP_Y)
    {
      y_raw = FT_6206_MAX_HEIGHT - 1 - y_raw;
    }
          
    x_diff = x_raw > x? (x_raw - x): (x - x_raw);
    y_diff = y_raw > y? (y_raw - y): (y - y_raw);

    if ((x_diff + y_diff) > 5)
    {
      x = x_raw;
      y = y_raw;
    }

    TsState->X = x;
    TsState->Y = y;
  }
  
  return ts_status;
}

/**
  * @brief  Configures and enables the touch screen interrupts.
  * @retval TS_OK if all initializations are OK. Other value if error.
  */
uint8_t BSP_TS_ITConfig(void)
{
  uint8_t ts_status = TS_OK;
  
  GPIO_InitTypeDef GPIO_InitStructure;
  
  /* Enable INT GPIO clock */
  TS_INT_GPIO_CLK_ENABLE();
  
  /* Configure GPIO PIN to detect Interrupt */
  GPIO_InitStructure.Pin = TS_INT_PIN;
  GPIO_InitStructure.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStructure.Speed = GPIO_SPEED_FAST;
  GPIO_InitStructure.Pull  = GPIO_PULLUP;
  HAL_GPIO_Init(TS_INT_GPIO_PORT, &GPIO_InitStructure);
  
  /* Enable and set TS Interrupt to the lowest priority */
  HAL_NVIC_SetPriority(TS_INT_EXTI_IRQn, 0x0F, 0x00);
  HAL_NVIC_EnableIRQ(TS_INT_EXTI_IRQn);  
  
  return ts_status;
}
