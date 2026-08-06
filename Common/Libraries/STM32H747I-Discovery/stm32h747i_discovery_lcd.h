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
#ifndef __STM32H747I_DISCOVERY_LCD_H
#define __STM32H747I_DISCOVERY_LCD_H

#ifdef __cplusplus
 extern "C" {
#endif 

/* Includes ------------------------------------------------------------------*/
#include "stm32h747i_discovery.h"

/* Include OTM8009A LCD Driver IC driver code */
#include "../Components/otm8009a/otm8009a.h"

/* Include ADV7533 HDMI Driver IC driver code */
#include "../Components/adv7533/adv7533.h"

/* Include SDRAM Driver */
#include "stm32h747i_discovery_sdram.h"

/* Exported types ------------------------------------------------------------*/
/**
 *  @brief  Possible values of Display Init Option
 */
typedef enum
{
  LCD_RGB565_800_480 = 0,
  LCD_RGB565_480_800,
  LCD_RGB888_800_480,
  LCD_RGB888_480_800,
  HDMI_RGB888_720_480,
  HDMI_RGB888_720_576
} LCD_InitOptionTypeDef;

/* Exported constants --------------------------------------------------------*/
#define LTDC_MAX_LAYER_NUMBER         ((uint32_t) 2)
#define LTDC_ACTIVE_LAYER_BACKGROUND  ((uint32_t) 0)
#define LTDC_ACTIVE_LAYER_FOREGROUND  ((uint32_t) 1)
#define LTDC_DEFAULT_ACTIVE_LAYER     LTDC_ACTIVE_LAYER_FOREGROUND

#define LCD_LayerCfgTypeDef           LTDC_LayerCfgTypeDef

/** 
  * @brief  LCD FB_StartAddress  
  */
#define LCD_FB_START_ADDRESS          ((uint32_t) 0xD0000000)

/** 
  * @brief  LCD status structure definition  
  */     
#define LCD_OK                 		    ((uint8_t) 0x00)
#define LCD_ERROR              		    ((uint8_t) 0x01)
#define LCD_TIMEOUT            		    ((uint8_t) 0x02)

/**
 *  @brief  Possible values of
 *  pixel data format (ie color coding) transmitted on DSI Data lane in DSI packets
 */
#define LCD_DSI_PIXEL_DATA_FMT_RBG888  DSI_RGB888 /*  DSI packet pixel format chosen is RGB888 : 24 bpp */
#define LCD_DSI_PIXEL_DATA_FMT_RBG565  DSI_RGB565 /*  DSI packet pixel format chosen is RGB565 : 16 bpp */

/** 
  * @brief  LCD Display OTM8009A DSI Virtual Channel  ID 
  */ 
#define LCD_OTM8009A_ID               ((uint32_t) 0)

/** 
  * @brief  HDMI ADV7533 DSI Virtual Channel  ID  
  */    
#define HDMI_ADV7533_ID               ((uint32_t) 0) 

/** 
  * @brief  HDMI Foramt   
  */   
#define HDMI_FORMAT_720_480           ((uint8_t) 0x00) /* 720_480 format choice of HDMI display */
#define HDMI_FORMAT_720_576           ((uint8_t) 0x01) /* 720_576 format choice of HDMI display */

/* Exported macro ------------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions --------------------------------------------------------*/
uint8_t  BSP_LCD_Init(void);
uint8_t  BSP_LCD_InitEx(LCD_InitOptionTypeDef option);
uint8_t  BSP_LCD_InitLCD(void);
uint8_t  BSP_LCD_InitHDMI(void);
uint32_t BSP_LCD_GetXSize(void);
uint32_t BSP_LCD_GetYSize(void);
void     BSP_LCD_SetXSize(uint32_t imageWidthPixels);
void     BSP_LCD_SetYSize(uint32_t imageHeightPixels);

void     BSP_LCD_LayerDefaultInit(uint16_t LayerIndex, uint32_t Address);
void     BSP_LCD_LayerRgb565Init(uint16_t LayerIndex, uint32_t Address);
void     BSP_LCD_SetTransparency(uint32_t LayerIndex, uint8_t Transparency);
void     BSP_LCD_SetLayerAddress(uint32_t LayerIndex, uint32_t Address);
void     BSP_LCD_SetColorKeying(uint32_t LayerIndex, uint32_t RGBValue);
void     BSP_LCD_ResetColorKeying(uint32_t LayerIndex);
void     BSP_LCD_SetLayerWindow(uint16_t LayerIndex, uint16_t Xpos, uint16_t Ypos, uint16_t Width, uint16_t Height);
void     BSP_LCD_SelectLayer(uint32_t LayerIndex);
void     BSP_LCD_SetLayerVisible(uint32_t LayerIndex, FunctionalState State);

void     BSP_LCD_Reset(void);
void     BSP_LCD_DisplayOn(void);
void     BSP_LCD_DisplayOff(void);
void     BSP_LCD_SetBrightness(uint8_t BrightnessValue);

void     BSP_LCD_MspInit(void);
void     BSP_LCD_MspDeInit(void);

#ifdef __cplusplus
}
#endif

#endif /* __STM32H747I_DISCOVERY_LCD_H */