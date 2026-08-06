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
#ifndef __STM32H747I_DISCOVERY_H
#define __STM32H747I_DISCOVERY_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Exported types ------------------------------------------------------------*/
typedef enum 
{  
  LED0 = 0,
  LED1,
  LED2,
  LED3
} Led_TypeDef;

typedef enum 
{  
  BTN0 = 0
} Button_TypeDef;

typedef enum 
{  
  BUTTON_MODE_GPIO = 0,
  BUTTON_MODE_EXTI = 1
} ButtonMode_TypeDef;

/* Exported constants --------------------------------------------------------*/
/**
 * @brief User leds
 */
#define LEDn										            4

#define USER_LED0_PIN                				GPIO_PIN_12
#define USER_LED0_GPIO_PORT          				GPIOI
#define USER_LED0_GPIO_CLK_ENABLE()  				__HAL_RCC_GPIOI_CLK_ENABLE()
#define USER_LED0_GPIO_CLK_DISABLE() 				__HAL_RCC_GPIOI_CLK_DISABLE()

#define USER_LED1_PIN                				GPIO_PIN_13
#define USER_LED1_GPIO_PORT          				GPIOI
#define USER_LED1_GPIO_CLK_ENABLE()  				__HAL_RCC_GPIOI_CLK_ENABLE()
#define USER_LED1_GPIO_CLK_DISABLE() 				__HAL_RCC_GPIOI_CLK_DISABLE()

#define USER_LED2_PIN                				GPIO_PIN_14
#define USER_LED2_GPIO_PORT          				GPIOI
#define USER_LED2_GPIO_CLK_ENABLE()  				__HAL_RCC_GPIOI_CLK_ENABLE()
#define USER_LED2_GPIO_CLK_DISABLE() 				__HAL_RCC_GPIOI_CLK_DISABLE()

#define USER_LED3_PIN                				GPIO_PIN_15
#define USER_LED3_GPIO_PORT          				GPIOI
#define USER_LED3_GPIO_CLK_ENABLE()  				__HAL_RCC_GPIOI_CLK_ENABLE()
#define USER_LED3_GPIO_CLK_DISABLE() 				__HAL_RCC_GPIOI_CLK_DISABLE()

/**
 * @brief User push-buttons
 */
#define BTNn										            1

#define USER_BTN0_PIN                				GPIO_PIN_13
#define USER_BTN0_GPIO_PORT          				GPIOC
#define USER_BTN0_GPIO_CLK_ENABLE()  				__HAL_RCC_GPIOC_CLK_ENABLE()
#define USER_BTN0_GPIO_CLK_DISABLE() 				__HAL_RCC_GPIOC_CLK_DISABLE()
#define USER_BTN0_EXTI_IRQn          				EXTI15_10_IRQn

/**
 * @brief I2C4
 */
#define BSP_I2C_TIMING 								              0xa0802d2e

#define DISCOVERY_I2Cx                            	I2C4
#define DISCOVERY_I2Cx_CLK_ENABLE()               	__HAL_RCC_I2C4_CLK_ENABLE()
#define DISCOVERY_I2Cx_SCL_SDA_GPIO_CLK_ENABLE()  	__HAL_RCC_GPIOD_CLK_ENABLE()
#define DISCOVERY_I2Cx_SCL_SDA_AF                 	GPIO_AF4_I2C4
#define DISCOVERY_I2Cx_SCL_SDA_GPIO_PORT          	GPIOD
#define DISCOVERY_I2Cx_SCL_PIN                    	GPIO_PIN_12
#define DISCOVERY_I2Cx_SDA_PIN                    	GPIO_PIN_13

#define DISCOVERY_I2Cx_FORCE_RESET()              	__HAL_RCC_I2C4_FORCE_RESET()
#define DISCOVERY_I2Cx_RELEASE_RESET()            	__HAL_RCC_I2C4_RELEASE_RESET()

#define DISCOVERY_I2Cx_EV_IRQn                    	I2C4_EV_IRQn
#define DISCOVERY_I2Cx_ER_IRQn                    	I2C4_ER_IRQn

#define I2Cx_TIMEOUT_MAX    						            0x1000

/**
  * @brief TouchScreen
  */
#define TS_I2C_ADDRESS                              0x70

/**
  * @brief AUDIO
  */
#define AUDIO_WM8994_I2C_ADDRESS                    0x34
#define AUDIO_ADV7533_I2C_ADDRESS                   0x7A

/**
  * @brief USB OTG HS Over Current signal
  */
#define OTG_HS_OVER_CURRENT_PIN                     GPIO_PIN_1
#define OTG_HS_OVER_CURRENT_PORT                    GPIOJ
#define OTG_HS_OVER_CURRENT_PORT_CLK_ENABLE()       __HAL_RCC_GPIOJ_CLK_ENABLE()

/**
  * @brief SD-detect signal
  */
#define SD_DETECT_PIN                        		    GPIO_PIN_8
#define SD_DETECT_GPIO_PORT                  		    GPIOI
#define SD_DETECT_GPIO_CLK_ENABLE()          		    __HAL_RCC_GPIOI_CLK_ENABLE()
#define SD_DETECT_GPIO_CLK_DISABLE()         		    __HAL_RCC_GPIOI_CLK_DISABLE()
#define SD_DETECT_EXTI_IRQn                  		    EXTI9_5_IRQn

/**
  * @brief Touch screen interrupt signal
  */
#define TS_INT_PIN                                  GPIO_PIN_7
#define TS_INT_GPIO_PORT                            GPIOK
#define TS_INT_GPIO_CLK_ENABLE()                    __HAL_RCC_GPIOK_CLK_ENABLE()
#define TS_INT_GPIO_CLK_DISABLE()                   __HAL_RCC_GPIOK_CLK_DISABLE()
#define TS_INT_EXTI_IRQn                            EXTI9_5_IRQn

/* Exported macro ------------------------------------------------------------*/
/**
 * @brief User leds
 */
#define LEDx_GPIO_CLK_ENABLE(__INDEX__) 		do{	if((__INDEX__) == 0) USER_LED0_GPIO_CLK_ENABLE(); else \
                                                if((__INDEX__) == 1) USER_LED1_GPIO_CLK_ENABLE(); else \
                                                if((__INDEX__) == 2) USER_LED2_GPIO_CLK_ENABLE(); else \
                                                if((__INDEX__) == 3) USER_LED3_GPIO_CLK_ENABLE(); \
                                              }while(0)

#define LEDx_GPIO_CLK_DISABLE(__INDEX__) 		do{	if((__INDEX__) == 0) USER_LED0_GPIO_CLK_DISABLE(); else \
                                                if((__INDEX__) == 1) USER_LED1_GPIO_CLK_DISABLE(); else \
                                                if((__INDEX__) == 2) USER_LED2_GPIO_CLK_DISABLE(); else \
                                                if((__INDEX__) == 3) USER_LED3_GPIO_CLK_DISABLE(); \
                                              }while(0)
                                              
/**
 * @brief User push-buttons
 */
#define BUTTONx_GPIO_CLK_ENABLE()           USER_BTN0_GPIO_CLK_ENABLE()
#define BUTTONx_GPIO_CLK_DISABLE()          USER_BTN0_GPIO_CLK_DISABLE()
 
/* Exported variables --------------------------------------------------------*/
extern I2C_HandleTypeDef    I2cHandle;

/* Exported functions --------------------------------------------------------*/
void      BSP_LED_Init(Led_TypeDef Led);
void      BSP_LED_DeInit(Led_TypeDef Led);
void      BSP_LED_On(Led_TypeDef Led);
void      BSP_LED_Off(Led_TypeDef Led);
void      BSP_LED_Toggle(Led_TypeDef Led);

void      BSP_PB_Init(Button_TypeDef Button, ButtonMode_TypeDef Mode);
void      BSP_PB_DeInit(Button_TypeDef Button);
uint32_t  BSP_PB_GetState(Button_TypeDef Button);

#ifdef __cplusplus
}
#endif

#endif /* __STM32H747I_DISCOVERY_H */
