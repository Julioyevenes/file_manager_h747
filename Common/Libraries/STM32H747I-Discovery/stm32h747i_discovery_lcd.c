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
#include "stm32h747i_discovery_lcd.h"

/* Private types -------------------------------------------------------------*/
/**
  * @brief  DSI timming params
  */
typedef struct 
{
  uint16_t      HACT;
  uint16_t      HSYNC;
  uint16_t      HBP;
  uint16_t      HFP;
  uint16_t      VACT;
  uint16_t      VSYNC;
  uint16_t      VBP;
  uint16_t      VFP;
  uint8_t       RGB_CODING;
} LCD_FormatTypeDef;

/**
  * @brief  DSI packet params
  */
typedef struct 
{
  uint16_t      NullPacketSize;
  uint16_t      NumberOfChunks;
  uint16_t      PacketSize;
} LCD_DSIPacketTypeDef;

/**
  * @brief  LTDC PLL params
  */
typedef struct
{
  uint16_t      PLLN;
  uint16_t      PLLR;
  uint32_t      PCLK;
  uint16_t      IDF;
  uint16_t      NDIV;
  uint16_t      ODF;
  uint16_t      LaneByteClock;
  uint16_t      TXEscapeCkdiv;
} LCD_PLLConfigTypeDef;

/**
 *  @brief Possible values of Display Orientation
 */
typedef enum
{
  LCD_ORIENTATION_PORTRAIT  = 0x00, /* Portrait orientation choice of LCD screen  */
  LCD_ORIENTATION_LANDSCAPE = 0x01, /* Landscape orientation choice of LCD screen */
  LCD_ORIENTATION_INVALID   = 0x02  /* Invalid orientation choice of LCD screen   */
} LCD_OrientationTypeDef;

/* Private constants ---------------------------------------------------------*/
#define LCD_DSI_ID              0x11
#define LCD_DSI_ADDRESS         TS_I2C_ADDRESS
#define LCD_DSI_ID_REG          0xA8

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
LTDC_HandleTypeDef hltdc_discovery;
DSI_HandleTypeDef hdsi_discovery;

static DSI_VidCfgTypeDef hdsivideo_handle;

static LCD_FormatTypeDef *Format;
static LCD_DSIPacketTypeDef *DSIPacket;
static LCD_PLLConfigTypeDef *PLLConfig;

static uint32_t ActiveLayer = LTDC_ACTIVE_LAYER_BACKGROUND;

/**
  * @brief  DSI timming
  */
LCD_FormatTypeDef LCD_Format[6] =
{
  /* HA HS HB HF VA VS VB VF ASPECT BPP */
  {800, 63, 120, 120, 480, 12, 12, 12, LCD_DSI_PIXEL_DATA_FMT_RBG565},
  {480, 63, 120, 120, 800, 12, 12, 12, LCD_DSI_PIXEL_DATA_FMT_RBG565},
  {800, 63, 120, 120, 480, 12, 12, 12, LCD_DSI_PIXEL_DATA_FMT_RBG888},
  {480, 63, 120, 120, 800, 12, 12, 12, LCD_DSI_PIXEL_DATA_FMT_RBG888},
  {720, 62, 60, 30, 480, 6, 19, 9, LCD_DSI_PIXEL_DATA_FMT_RBG888},
  {720, 64, 68, 12, 576, 5, 39, 5, LCD_DSI_PIXEL_DATA_FMT_RBG888}
};

/**
  * @brief  DSI packet size
  */
LCD_DSIPacketTypeDef LCD_DSIPacket[6] =
{
  /* NP NC VP */
  {0xFFF, 0, 800},
  {0xFFF, 0, 480},
  {0xFFF, 0, 800},
  {0xFFF, 0, 480},
  {0, 1, 720},
  {0, 1, 720}
};

/**
  * @brief  LTDC PLL settings
  */
LCD_PLLConfigTypeDef LCD_PLLConfig[6] =
{
  /* N DIV Pclk IDF NDIV ODF LBClk TXEscapeCkdiv */
  {384, 14, 27429, DSI_PLL_IN_DIV5, 100, DSI_PLL_OUT_DIV1, 62500, 4},
  {384, 14, 27429, DSI_PLL_IN_DIV5, 100, DSI_PLL_OUT_DIV1, 62500, 4},
  {384, 14, 27429, DSI_PLL_IN_DIV5, 100, DSI_PLL_OUT_DIV1, 62500, 4},
  {384, 14, 27429, DSI_PLL_IN_DIV5, 100, DSI_PLL_OUT_DIV1, 62500, 4},
  {325, 12, 27083, DSI_PLL_IN_DIV5, 65, DSI_PLL_OUT_DIV1, 40625, 3},
  {325, 12, 27083, DSI_PLL_IN_DIV5, 65, DSI_PLL_OUT_DIV1, 40625, 3}
};

/* Private function prototypes -----------------------------------------------*/
static uint16_t LCD_IO_GetID(void);

/**
  * @brief  Initializes the LCD Driver with default param.
  * @retval LCD state
  */
uint8_t BSP_LCD_Init(void)
{
  return BSP_LCD_InitEx(LCD_RGB888_800_480);
}

/**
  * @brief  Initializes the LCD Driver with specific option param. 
  * The ititialization is done as below:
  *     - DSI PLL ititialization
  *     - DSI ititialization
  *     - LTDC ititialization
  *     - OTM8009A LCD Display IC Driver or DSI-HDMI ADV7533 adapter device ititialization
  * @param  option: LCD init option, can be any member of LCD_InitOptionTypeDef
  * @retval LCD state
  */
uint8_t BSP_LCD_InitEx(LCD_InitOptionTypeDef option)
{
  uint16_t read_id = 0;

  /* Toggle Hardware Reset of the DSI LCD using its XRES signal (active low) */
  BSP_LCD_Reset();

  /* Check the connected device */
  read_id = LCD_IO_GetID();

  if(read_id == LCD_DSI_ID)
  {
    switch(option)
    {
      case LCD_RGB565_800_480:        
      case LCD_RGB565_480_800:       
      case LCD_RGB888_800_480:        
      case LCD_RGB888_480_800:
        Format = &(LCD_Format[option]);
        DSIPacket = &(LCD_DSIPacket[option]);
        PLLConfig = &(LCD_PLLConfig[option]);
        break;
        
      default:
        Format = &(LCD_Format[LCD_RGB888_800_480]);
        DSIPacket = &(LCD_DSIPacket[LCD_RGB888_800_480]);
        PLLConfig = &(LCD_PLLConfig[LCD_RGB888_800_480]);      
    }
    
    return BSP_LCD_InitLCD();
  }
  else if(read_id == ADV7533_ID)
  {
    switch(option)
    {
      case HDMI_RGB888_720_480:        
      case HDMI_RGB888_720_576:
        Format = &(LCD_Format[option]);
        DSIPacket = &(LCD_DSIPacket[option]);
        PLLConfig = &(LCD_PLLConfig[option]);      
        break;
        
      default:
        Format = &(LCD_Format[HDMI_RGB888_720_480]);
        DSIPacket = &(LCD_DSIPacket[HDMI_RGB888_720_480]);
        PLLConfig = &(LCD_PLLConfig[HDMI_RGB888_720_480]);      
    }

    return BSP_LCD_InitHDMI();
  }
  else
  {
    return LCD_ERROR;
  }
}

/**
  * @brief  Initializes the LCD Driver for OTM8009A LCD Display.
  * @retval LCD state
  */
uint8_t BSP_LCD_InitLCD(void)
{
  DSI_PLLInitTypeDef dsiPllInit;
  static RCC_PeriphCLKInitTypeDef PeriphClkInitStruct;

  /* Call first MSP Initialize only in case of first initialization
   * This will set IP blocks LTDC, DSI and DMA2D
   * - out of reset
   * - clocked
   * - NVIC IRQ related to IP blocks enabled
   */
  BSP_LCD_MspInit();

  /* Restore to default state DSI IP block */
  hdsi_discovery.Instance = DSI;
  HAL_DSI_DeInit(&hdsi_discovery);
  
  /* Configure the DSI PLL */
  dsiPllInit.PLLNDIV    = PLLConfig->NDIV;
  dsiPllInit.PLLIDF     = PLLConfig->IDF;
  dsiPllInit.PLLODF     = PLLConfig->ODF; 

  /* Set number of Lanes */
  hdsi_discovery.Init.NumberOfLanes = DSI_TWO_DATA_LANES;
  /* Set the TX escape clock division ratio */
  hdsi_discovery.Init.TXEscapeCkdiv = PLLConfig->TXEscapeCkdiv;
  
  /* Init the DSI */
  HAL_DSI_Init(&hdsi_discovery, &dsiPllInit);  

  /* Virtual channel used by the OTM8009A */
  hdsivideo_handle.VirtualChannelID     = LCD_OTM8009A_ID;

  /* Timing parameters for Video modes
   * Set Timing parameters of DSI depending on its chosen format */
  hdsivideo_handle.ColorCoding          = Format->RGB_CODING;
  hdsivideo_handle.VSPolarity           = DSI_VSYNC_ACTIVE_HIGH;
  hdsivideo_handle.HSPolarity           = DSI_HSYNC_ACTIVE_HIGH;
  hdsivideo_handle.DEPolarity           = DSI_DATA_ENABLE_ACTIVE_HIGH;  
  hdsivideo_handle.Mode                 = DSI_VID_MODE_BURST;
  hdsivideo_handle.NullPacketSize       = DSIPacket->NullPacketSize;
  hdsivideo_handle.NumberOfChunks       = DSIPacket->NumberOfChunks;
  hdsivideo_handle.PacketSize           = DSIPacket->PacketSize; 
  hdsivideo_handle.HorizontalSyncActive = Format->HSYNC*PLLConfig->LaneByteClock/PLLConfig->PCLK;
  hdsivideo_handle.HorizontalBackPorch  = Format->HBP*PLLConfig->LaneByteClock/PLLConfig->PCLK;
  hdsivideo_handle.HorizontalLine       = (Format->HACT + Format->HSYNC + Format->HBP + Format->HFP)*PLLConfig->LaneByteClock/PLLConfig->PCLK;
  hdsivideo_handle.VerticalSyncActive   = Format->VSYNC;
  hdsivideo_handle.VerticalBackPorch    = Format->VBP;
  hdsivideo_handle.VerticalFrontPorch   = Format->VFP;
  hdsivideo_handle.VerticalActive       = Format->VACT;

  /* Enable or disable sending LP command while streaming is active in video mode */
  hdsivideo_handle.LPCommandEnable      = DSI_LP_COMMAND_ENABLE; /* Enable sending commands in mode LP (Low Power) */

  /* Largest packet size possible to transmit in LP mode in VSA, VBP, VFP regions */
  /* Only useful when sending LP packets is allowed while streaming is active in video mode */
  hdsivideo_handle.LPLargestPacketSize          = 16;

  /* Largest packet size possible to transmit in LP mode in HFP region during VACT period */
  /* Only useful when sending LP packets is allowed while streaming is active in video mode */
  hdsivideo_handle.LPVACTLargestPacketSize      = 0;

  /* Specify for each region, if the going in LP mode is allowed */
  /* while streaming is active in video mode                     */
  hdsivideo_handle.LPHorizontalFrontPorchEnable = DSI_LP_HFP_ENABLE;
  hdsivideo_handle.LPHorizontalBackPorchEnable  = DSI_LP_HBP_ENABLE;
  hdsivideo_handle.LPVerticalActiveEnable       = DSI_LP_VACT_ENABLE;
  hdsivideo_handle.LPVerticalFrontPorchEnable   = DSI_LP_VFP_ENABLE;
  hdsivideo_handle.LPVerticalBackPorchEnable    = DSI_LP_VBP_ENABLE;
  hdsivideo_handle.LPVerticalSyncActiveEnable   = DSI_LP_VSYNC_ENABLE;

  /* Configure DSI Video mode timings with settings set above */
  HAL_DSI_ConfigVideoMode(&hdsi_discovery, &hdsivideo_handle);

  /* Restore to default state LTDC IP block */
  hltdc_discovery.Instance = LTDC;
  HAL_LTDC_DeInit(&hltdc_discovery);

  /* LTDC clock configuration */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  PeriphClkInitStruct.PLL3.PLL3M = 25;
  PeriphClkInitStruct.PLL3.PLL3N = PLLConfig->PLLN;
  PeriphClkInitStruct.PLL3.PLL3R = PLLConfig->PLLR;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

  /* Timing Configuration */    
  hltdc_discovery.Init.HorizontalSync = (Format->HSYNC - 1);
  hltdc_discovery.Init.AccumulatedHBP = (Format->HSYNC + Format->HBP - 1);
  hltdc_discovery.Init.AccumulatedActiveW = (Format->HACT + Format->HSYNC + Format->HBP - 1);
  hltdc_discovery.Init.TotalWidth = (Format->HACT + Format->HSYNC + Format->HBP + Format->HFP - 1);
  hltdc_discovery.Init.VerticalSync = (Format->VSYNC - 1);
  hltdc_discovery.Init.AccumulatedVBP = (Format->VSYNC + Format->VBP - 1);
  hltdc_discovery.Init.AccumulatedActiveH = (Format->VACT + Format->VSYNC + Format->VBP - 1);
  hltdc_discovery.Init.TotalHeigh = (Format->VACT + Format->VSYNC + Format->VBP + Format->VFP - 1);
  
  /* Initialize the LCD pixel width and pixel height */
  hltdc_discovery.LayerCfg->ImageWidth  = Format->HACT;
  hltdc_discovery.LayerCfg->ImageHeight = Format->VACT;  

  /* background value */
  hltdc_discovery.Init.Backcolor.Blue = 0x00;
  hltdc_discovery.Init.Backcolor.Green = 0x00;
  hltdc_discovery.Init.Backcolor.Red = 0x00;

  /* Polarity */
  hltdc_discovery.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  
  /* Get LTDC Configuration from DSI Configuration */
  HAL_LTDC_StructInitFromVideoConfig(&hltdc_discovery, &hdsivideo_handle);  

  /* Initialize & Start the LTDC */  
  HAL_LTDC_Init(&hltdc_discovery);

  /* Enable the DSI host and wrapper */
  HAL_DSI_Start(&hdsi_discovery);  

#if !defined(DATA_IN_ExtSDRAM) && !defined(HEAP_IN_ExtSDRAM)
  /* Initialize the SDRAM */
  BSP_SDRAM_Init();
#endif /* DATA_IN_ExtSDRAM || HEAP_IN_ExtSDRAM */

  if(Format->RGB_CODING == LCD_DSI_PIXEL_DATA_FMT_RBG565)
  {
    if(Format->HACT > Format->VACT)
    {
      OTM8009A_Init(OTM8009A_FORMAT_RBG565, LCD_ORIENTATION_LANDSCAPE);
    }
    else /* if(Format->HACT < Format->VACT) */
    {
      OTM8009A_Init(OTM8009A_FORMAT_RBG565, LCD_ORIENTATION_PORTRAIT);
    }
  }
  else /* if(Format->RGB_CODING == LCD_DSI_PIXEL_DATA_FMT_RBG888) */
  {
    if(Format->HACT > Format->VACT)
    {
      OTM8009A_Init(OTM8009A_FORMAT_RGB888, LCD_ORIENTATION_LANDSCAPE);
    }
    else /* if(Format->HACT < Format->VACT) */
    {
      OTM8009A_Init(OTM8009A_FORMAT_RGB888, LCD_ORIENTATION_PORTRAIT);
    }    
  }

  return LCD_OK;
}

/**
  * @brief  Initializes the LCD Driver for DSI-HDMI ADV7533 adapter.
  * @retval LCD state
  */
uint8_t BSP_LCD_InitHDMI(void)
{
  DSI_PLLInitTypeDef dsiPllInit;
  DSI_PHY_TimerTypeDef dsiPhyInit;
  static RCC_PeriphCLKInitTypeDef PeriphClkInitStruct;
  adv7533ConfigTypeDef adv7533_config;

  /* Initialize the ADV7533 HDMI Bridge
   * depending on configuration set in 'hdsivideo_handle'.
   */
  adv7533_config.DSI_LANES = 2;
  adv7533_config.HACT = Format->HACT;
  adv7533_config.HSYNC = Format->HSYNC;
  adv7533_config.HBP = Format->HBP;
  adv7533_config.HFP = Format->HFP;
  adv7533_config.VACT = Format->VACT;
  adv7533_config.VSYNC = Format->VSYNC;
  adv7533_config.VBP = Format->VBP;
  adv7533_config.VFP = Format->VFP;  

  ADV7533_Init();  
  ADV7533_Configure(&adv7533_config);
  ADV7533_PowerOn();

  /* Call first MSP Initialize only in case of first initialization
   * This will set IP blocks LTDC, DSI and DMA2D
   * - out of reset
   * - clocked
   * - NVIC IRQ related to IP blocks enabled
   */
  BSP_LCD_MspInit();

  /* Restore to default state DSI IP block */
  hdsi_discovery.Instance = DSI;
  HAL_DSI_DeInit(&hdsi_discovery);
  
  /* Configure the DSI PLL */
  dsiPllInit.PLLNDIV    = PLLConfig->NDIV;
  dsiPllInit.PLLIDF     = PLLConfig->IDF;
  dsiPllInit.PLLODF     = PLLConfig->ODF; 

  /* Set number of Lanes */
  hdsi_discovery.Init.NumberOfLanes = DSI_TWO_DATA_LANES;
  /* Set the TX escape clock division ratio */
  hdsi_discovery.Init.TXEscapeCkdiv = PLLConfig->TXEscapeCkdiv;

  /* Init the DSI */
  HAL_DSI_Init(&hdsi_discovery, &dsiPllInit);

  /* Configure the D-PHY Timings */
  dsiPhyInit.ClockLaneHS2LPTime = 0x14;
  dsiPhyInit.ClockLaneLP2HSTime = 0x14;
  dsiPhyInit.DataLaneHS2LPTime = 0x0A;
  dsiPhyInit.DataLaneLP2HSTime = 0x0A;
  dsiPhyInit.DataLaneMaxReadTime = 0x00;
  dsiPhyInit.StopWaitTime = 0x0;
  HAL_DSI_ConfigPhyTimer(&hdsi_discovery, &dsiPhyInit);

  /* Virutal channel used by the ADV7533 */
  hdsivideo_handle.VirtualChannelID     = HDMI_ADV7533_ID;

  /* Timing parameters for Video modes
   * Set Timing parameters of DSI depending on its chosen format */
  hdsivideo_handle.ColorCoding          = Format->RGB_CODING;
  hdsivideo_handle.LooselyPacked        = DSI_LOOSELY_PACKED_DISABLE;
  hdsivideo_handle.VSPolarity           = DSI_VSYNC_ACTIVE_LOW;
  hdsivideo_handle.HSPolarity           = DSI_HSYNC_ACTIVE_LOW;
  hdsivideo_handle.DEPolarity           = DSI_DATA_ENABLE_ACTIVE_HIGH;  
  hdsivideo_handle.Mode                 = DSI_VID_MODE_NB_PULSES;
  hdsivideo_handle.NullPacketSize       = DSIPacket->NullPacketSize;
  hdsivideo_handle.NumberOfChunks       = DSIPacket->NumberOfChunks;
  hdsivideo_handle.PacketSize           = DSIPacket->PacketSize; 
  hdsivideo_handle.HorizontalSyncActive = Format->HSYNC*PLLConfig->LaneByteClock/PLLConfig->PCLK;
  hdsivideo_handle.HorizontalBackPorch  = Format->HBP*PLLConfig->LaneByteClock/PLLConfig->PCLK;
  hdsivideo_handle.HorizontalLine       = (Format->HACT + Format->HSYNC + Format->HBP + Format->HFP)*PLLConfig->LaneByteClock/PLLConfig->PCLK;
  hdsivideo_handle.VerticalSyncActive   = Format->VSYNC;
  hdsivideo_handle.VerticalBackPorch    = Format->VBP;
  hdsivideo_handle.VerticalFrontPorch   = Format->VFP;
  hdsivideo_handle.VerticalActive       = Format->VACT;

  /* Enable or disable sending LP command while streaming is active in video mode */
  hdsivideo_handle.LPCommandEnable      = DSI_LP_COMMAND_ENABLE; /* Enable sending commands in mode LP (Low Power) */

  /* Largest packet size possible to transmit in LP mode in VSA, VBP, VFP regions */
  /* Only useful when sending LP packets is allowed while streaming is active in video mode */
  hdsivideo_handle.LPLargestPacketSize          = 4;

  /* Largest packet size possible to transmit in LP mode in HFP region during VACT period */
  /* Only useful when sending LP packets is allowed while streaming is active in video mode */
  hdsivideo_handle.LPVACTLargestPacketSize      = 4;

  /* Specify for each region, if the going in LP mode is allowed */
  /* while streaming is active in video mode                     */
  hdsivideo_handle.LPHorizontalFrontPorchEnable = DSI_LP_HFP_ENABLE;
  hdsivideo_handle.LPHorizontalBackPorchEnable  = DSI_LP_HBP_ENABLE;
  hdsivideo_handle.LPVerticalActiveEnable       = DSI_LP_VACT_ENABLE;
  hdsivideo_handle.LPVerticalFrontPorchEnable   = DSI_LP_VFP_ENABLE;
  hdsivideo_handle.LPVerticalBackPorchEnable    = DSI_LP_VBP_ENABLE;
  hdsivideo_handle.LPVerticalSyncActiveEnable   = DSI_LP_VSYNC_ENABLE;

  /* Configure DSI Video mode timings with settings set above */
  HAL_DSI_ConfigVideoMode(&hdsi_discovery, &hdsivideo_handle);

  /* Restore to default state LTDC IP block */
  hltdc_discovery.Instance = LTDC;
  HAL_LTDC_DeInit(&hltdc_discovery);

  /* LTDC clock configuration */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  PeriphClkInitStruct.PLL3.PLL3M = 25;
  PeriphClkInitStruct.PLL3.PLL3N = PLLConfig->PLLN;
  PeriphClkInitStruct.PLL3.PLL3R = PLLConfig->PLLR;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

  /* Timing Configuration */    
  hltdc_discovery.Init.HorizontalSync = (Format->HSYNC - 1);
  hltdc_discovery.Init.AccumulatedHBP = (Format->HSYNC + Format->HBP - 1);
  hltdc_discovery.Init.AccumulatedActiveW = (Format->HACT + Format->HSYNC + Format->HBP - 1);
  hltdc_discovery.Init.TotalWidth = (Format->HACT + Format->HSYNC + Format->HBP + Format->HFP - 1);
  hltdc_discovery.Init.VerticalSync = (Format->VSYNC - 1);
  hltdc_discovery.Init.AccumulatedVBP = (Format->VSYNC + Format->VBP - 1);
  hltdc_discovery.Init.AccumulatedActiveH = (Format->VACT + Format->VSYNC + Format->VBP - 1);
  hltdc_discovery.Init.TotalHeigh = (Format->VACT + Format->VSYNC + Format->VBP + Format->VFP - 1);
  
  /* Initialize the LCD pixel width and pixel height */
  hltdc_discovery.LayerCfg->ImageWidth  = Format->HACT;
  hltdc_discovery.LayerCfg->ImageHeight = Format->VACT;  

  /* background value */
  hltdc_discovery.Init.Backcolor.Blue = 0x00;
  hltdc_discovery.Init.Backcolor.Green = 0x00;
  hltdc_discovery.Init.Backcolor.Red = 0x00;

  /* Polarity */
  hltdc_discovery.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc_discovery.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc_discovery.Init.DEPolarity = LTDC_DEPOLARITY_AL;  
  hltdc_discovery.Init.PCPolarity = LTDC_PCPOLARITY_IPC;  

  /* Initialize & Start the LTDC */  
  HAL_LTDC_Init(&hltdc_discovery);

  /* Enable the DSI host and wrapper */
  HAL_DSI_Start(&hdsi_discovery);

#if !defined(DATA_IN_ExtSDRAM) && !defined(HEAP_IN_ExtSDRAM)
  /* Initialize the SDRAM */
  BSP_SDRAM_Init();
#endif /* DATA_IN_ExtSDRAM || HEAP_IN_ExtSDRAM */

  return LCD_OK;
}

/**
  * @brief  Gets the LCD X size.
  * @retval Used LCD X size
  */
uint32_t BSP_LCD_GetXSize(void)
{
  return hltdc_discovery.LayerCfg[ActiveLayer].ImageWidth;
}

/**
  * @brief  Gets the LCD Y size.
  * @retval Used LCD Y size
  */
uint32_t BSP_LCD_GetYSize(void)
{
  return hltdc_discovery.LayerCfg[ActiveLayer].ImageHeight;
}

/**
  * @brief  Set the LCD X size.
  * @param  imageWidthPixels : image width in pixels unit
  * @retval None
  */
void BSP_LCD_SetXSize(uint32_t imageWidthPixels)
{
  hltdc_discovery.LayerCfg[ActiveLayer].ImageWidth = imageWidthPixels;
}

/**
  * @brief  Set the LCD Y size.
  * @param  imageHeightPixels : image height in lines unit
  * @retval None
  */
void BSP_LCD_SetYSize(uint32_t imageHeightPixels)
{
  hltdc_discovery.LayerCfg[ActiveLayer].ImageHeight = imageHeightPixels;
}

/**
  * @brief  Initializes the LCD layer in ARGB8888 format (32 bits per pixel).
  * @param  LayerIndex: Layer foreground or background
  * @param  Address: Layer frame buffer
  * @retval None
  */
void BSP_LCD_LayerDefaultInit(uint16_t LayerIndex, uint32_t Address)
{     
  LCD_LayerCfgTypeDef  layer_cfg;

  /* Layer Init */
  layer_cfg.WindowX0 = 0;
  layer_cfg.WindowX1 = BSP_LCD_GetXSize();
  layer_cfg.WindowY0 = 0;
  layer_cfg.WindowY1 = BSP_LCD_GetYSize(); 
  layer_cfg.PixelFormat = LTDC_PIXEL_FORMAT_ARGB8888;
  layer_cfg.FBStartAdress = Address;
  layer_cfg.Alpha = 255;
  layer_cfg.Alpha0 = 0;
  layer_cfg.Backcolor.Blue = 0;
  layer_cfg.Backcolor.Green = 0;
  layer_cfg.Backcolor.Red = 0;
  layer_cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  layer_cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  layer_cfg.ImageWidth = BSP_LCD_GetXSize();
  layer_cfg.ImageHeight = BSP_LCD_GetYSize();
  
  HAL_LTDC_ConfigLayer(&hltdc_discovery, &layer_cfg, LayerIndex); 
}

/**
  * @brief  Initializes the LCD layer in RGB565 format (16 bits per pixel).
  * @param  LayerIndex: Layer foreground or background
  * @param  Address: Layer frame buffer
  * @retval None
  */
void BSP_LCD_LayerRgb565Init(uint16_t LayerIndex, uint32_t Address)
{     
  LCD_LayerCfgTypeDef  layer_cfg;

  /* Layer Init */
  layer_cfg.WindowX0 = 0;
  layer_cfg.WindowX1 = BSP_LCD_GetXSize();
  layer_cfg.WindowY0 = 0;
  layer_cfg.WindowY1 = BSP_LCD_GetYSize(); 
  layer_cfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  layer_cfg.FBStartAdress = Address;
  layer_cfg.Alpha = 255;
  layer_cfg.Alpha0 = 0;
  layer_cfg.Backcolor.Blue = 0;
  layer_cfg.Backcolor.Green = 0;
  layer_cfg.Backcolor.Red = 0;
  layer_cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  layer_cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  layer_cfg.ImageWidth = BSP_LCD_GetXSize();
  layer_cfg.ImageHeight = BSP_LCD_GetYSize();
  
  HAL_LTDC_ConfigLayer(&hltdc_discovery, &layer_cfg, LayerIndex); 
}

/**
  * @brief  Configures the transparency.
  * @param  LayerIndex: Layer foreground or background.
  * @param  Transparency: Transparency
  *           This parameter must be a number between Min_Data = 0x00 and Max_Data = 0xFF 
  * @retval None
  */
void BSP_LCD_SetTransparency(uint32_t LayerIndex, uint8_t Transparency)
{    
  HAL_LTDC_SetAlpha(&hltdc_discovery, Transparency, LayerIndex);
}

/**
  * @brief  Sets an LCD layer frame buffer address.
  * @param  LayerIndex: Layer foreground or background
  * @param  Address: New LCD frame buffer value      
  * @retval None
  */
void BSP_LCD_SetLayerAddress(uint32_t LayerIndex, uint32_t Address)
{
  HAL_LTDC_SetAddress(&hltdc_discovery, Address, LayerIndex);
}

/**
  * @brief  Configures and sets the color keying.
  * @param  LayerIndex: Layer foreground or background
  * @param  RGBValue: Color reference
  * @retval None
  */
void BSP_LCD_SetColorKeying(uint32_t LayerIndex, uint32_t RGBValue)
{  
  /* Configure and Enable the color Keying for LCD Layer */
  HAL_LTDC_ConfigColorKeying(&hltdc_discovery, RGBValue, LayerIndex);
  HAL_LTDC_EnableColorKeying(&hltdc_discovery, LayerIndex);
}

/**
  * @brief  Disables the color keying.
  * @param  LayerIndex: Layer foreground or background
  * @retval None
  */
void BSP_LCD_ResetColorKeying(uint32_t LayerIndex)
{   
  /* Disable the color Keying for LCD Layer */
  HAL_LTDC_DisableColorKeying(&hltdc_discovery, LayerIndex);
}

/**
  * @brief  Sets display window.
  * @param  LayerIndex: Layer index
  * @param  Xpos: LCD X position
  * @param  Ypos: LCD Y position
  * @param  Width: LCD window width
  * @param  Height: LCD window height  
  * @retval None
  */
void BSP_LCD_SetLayerWindow(uint16_t LayerIndex, uint16_t Xpos, uint16_t Ypos, uint16_t Width, uint16_t Height)
{
  /* Reconfigure the layer size */
  HAL_LTDC_SetWindowSize(&hltdc_discovery, Width, Height, LayerIndex);
  
  /* Reconfigure the layer position */
  HAL_LTDC_SetWindowPosition(&hltdc_discovery, Xpos, Ypos, LayerIndex); 
}

/**
  * @brief  Selects the LCD Layer.
  * @param  LayerIndex: Layer foreground or background
  * @retval None
  */
void BSP_LCD_SelectLayer(uint32_t LayerIndex)
{
  ActiveLayer = LayerIndex;
}

/**
  * @brief  Sets an LCD Layer visible
  * @param  LayerIndex: Visible Layer
  * @param  State: New state of the specified layer
  *          This parameter can be one of the following values:
  *            @arg  ENABLE
  *            @arg  DISABLE 
  * @retval None
  */
void BSP_LCD_SetLayerVisible(uint32_t LayerIndex, FunctionalState State)
{
  if(State == ENABLE)
  {
    __HAL_LTDC_LAYER_ENABLE(&hltdc_discovery, LayerIndex);
  }
  else
  {
    __HAL_LTDC_LAYER_DISABLE(&hltdc_discovery, LayerIndex);
  }
  __HAL_LTDC_RELOAD_CONFIG(&hltdc_discovery);
}

/**
  * @brief  BSP LCD Reset
  *         Hw reset the LCD DSI activating its XRES signal (active low for some time)
  *         and desactivating it later.
  */
void BSP_LCD_Reset(void)
{
  GPIO_InitTypeDef  gpio_init_structure;
  
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /* Configure the GPIO on PG3 */
  gpio_init_structure.Pin   = GPIO_PIN_3;
  gpio_init_structure.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio_init_structure.Pull  = GPIO_PULLUP;
  gpio_init_structure.Speed = GPIO_SPEED_HIGH;
  HAL_GPIO_Init(GPIOG, &gpio_init_structure);
  
  /* Activate XRES active low */
  HAL_GPIO_WritePin(GPIOG, GPIO_PIN_3, GPIO_PIN_RESET);

  /* wait 250 ms */
  HAL_Delay(250);

  /* Desactivate XRES */
  HAL_GPIO_WritePin(GPIOG, GPIO_PIN_3, GPIO_PIN_SET);
  
  /* Wait for 250 ms after releasing XRES before sending commands */
  HAL_Delay(250);
}

/**
  * @brief  Switch back on the display if was switched off by previous call of BSP_LCD_DisplayOff().
  *         Exit DSI ULPM mode if was allowed and configured in Dsi Configuration.
  */
void BSP_LCD_DisplayOn(void)
{  
  if(ADV7533_ID == adv7533_drv.ReadID(ADV7533_CEC_DSI_I2C_ADDR))
  {
    return; /* Not supported for HDMI display */
  }
  else    
  {  
    /* Send Display on DCS command to display */
    HAL_DSI_ShortWrite(&hdsi_discovery,
                       hdsivideo_handle.VirtualChannelID,
                       DSI_DCS_SHORT_PKT_WRITE_P1,
                       OTM8009A_CMD_DISPON,
                       0x00);
  }  
}

/**
  * @brief  Switch Off the display.
  *         Enter DSI ULPM mode if was allowed and configured in Dsi Configuration.
  */
void BSP_LCD_DisplayOff(void)
{ 
  if(ADV7533_ID == adv7533_drv.ReadID(ADV7533_CEC_DSI_I2C_ADDR))
  {
    return; /* Not supported for HDMI display */
  }
  else    
  {
    /* Send Display off DCS Command to display */
    HAL_DSI_ShortWrite(&hdsi_discovery,
                       hdsivideo_handle.VirtualChannelID,
                       DSI_DCS_SHORT_PKT_WRITE_P1,
                       OTM8009A_CMD_DISPOFF,
                       0x00);
  }  
}

/**
  * @brief  Set the brightness value 
  * @param  BrightnessValue: [00: Min (black), 100 Max]
  */
void BSP_LCD_SetBrightness(uint8_t BrightnessValue)
{  
  if(ADV7533_ID == adv7533_drv.ReadID(ADV7533_CEC_DSI_I2C_ADDR))
  {
    return; /* Not supported for HDMI display */
  }
  else    
  {
    /* Send Display on DCS command to display */
    HAL_DSI_ShortWrite(&hdsi_discovery, 
                       LCD_OTM8009A_ID, 
                       DSI_DCS_SHORT_PKT_WRITE_P1, 
                       OTM8009A_CMD_WRDISBV, (uint16_t)(BrightnessValue * 255)/100);
  }  
}

/**
  * @brief  Initialize the BSP LCD Msp.
  * Application can surcharge if needed this function implementation
  */
__weak void BSP_LCD_MspInit(void)
{  
  /* Enable the LTDC clock */
  __HAL_RCC_LTDC_CLK_ENABLE();

  /* Toggle Sw reset of LTDC IP */
  __HAL_RCC_LTDC_FORCE_RESET();
  __HAL_RCC_LTDC_RELEASE_RESET();

  /* Enable DSI Host and wrapper clocks */
  __HAL_RCC_DSI_CLK_ENABLE();

  /* Soft Reset the DSI Host and wrapper */
  __HAL_RCC_DSI_FORCE_RESET();
  __HAL_RCC_DSI_RELEASE_RESET();

  /* NVIC configuration for LTDC interrupt that is now enabled */
  HAL_NVIC_SetPriority(LTDC_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(LTDC_IRQn);

  /* NVIC configuration for DSI interrupt that is now enabled */
  HAL_NVIC_SetPriority(DSI_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(DSI_IRQn);
}

/**
  * @brief  De-Initializes the BSP LCD Msp
  * Application can surcharge if needed this function implementation.
  */
__weak void BSP_LCD_MspDeInit(void)
{
  /* Disable IRQ of LTDC IP */
  HAL_NVIC_DisableIRQ(LTDC_IRQn);

  /* Disable IRQ of DSI IP */
  HAL_NVIC_DisableIRQ(DSI_IRQn);

  /* Force and let in reset state LTDC and DSI Host + Wrapper IPs */
  __HAL_RCC_LTDC_FORCE_RESET();
  __HAL_RCC_DSI_FORCE_RESET();

  /* Disable the LTDC and DSI Host and Wrapper clocks */
  __HAL_RCC_LTDC_CLK_DISABLE();
  __HAL_RCC_DSI_CLK_DISABLE();
}

/**
  * @brief  Returns the ID of connected screen by checking the HDMI
  *        (adv7533 component) ID or LCD DSI (via TS ID) ID.
  * @retval LCD ID
  */
static uint16_t LCD_IO_GetID(void)
{ 
  HDMI_IO_Init();
  
  HDMI_IO_Delay(60);
  
  if(ADV7533_ID == adv7533_drv.ReadID(ADV7533_CEC_DSI_I2C_ADDR))
  {
    return ADV7533_ID;
  }  
  else if(HDMI_IO_Read(LCD_DSI_ADDRESS, LCD_DSI_ID_REG) == LCD_DSI_ID)
  {
    return LCD_DSI_ID;
  }
  else
  {
    return 0;
  }
}

/**
  * @brief  DCS or Generic short/long write command
  * @param  NbrParams: Number of parameters. It indicates the write command mode:
  *                 If inferior to 2, a long write command is performed else short.
  * @param  pParams: Pointer to parameter values table.
  * @retval HAL status
  */
void DSI_IO_WriteCmd(uint32_t NbrParams, uint8_t *pParams)
{
  if(NbrParams <= 1)
  {
    HAL_DSI_ShortWrite(&hdsi_discovery, LCD_OTM8009A_ID, DSI_DCS_SHORT_PKT_WRITE_P1, pParams[0], pParams[1]);
  }
  else
  {
    HAL_DSI_LongWrite(&hdsi_discovery,  LCD_OTM8009A_ID, DSI_DCS_LONG_PKT_WRITE, NbrParams, pParams[NbrParams], pParams);
  }
}
