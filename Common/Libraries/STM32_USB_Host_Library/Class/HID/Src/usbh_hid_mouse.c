/**
  ******************************************************************************
  * @file    usbh_hid_mouse.c
  * @author  MCD Application Team
  * @brief   This file is the application layer for USB Host HID Mouse Handling.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      www.st.com/SLA0044
  *
  ******************************************************************************
  */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
- "stm32xxxxx_{eval}{discovery}{adafruit}_lcd.c"
- "stm32xxxxx_{eval}{discovery}_sdram.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "usbh_hid_mouse.h"
#include "usbh_hid_parser.h"


/** @addtogroup USBH_LIB
  * @{
  */

/** @addtogroup USBH_CLASS
  * @{
  */

/** @addtogroup USBH_HID_CLASS
  * @{
  */

/** @defgroup USBH_HID_MOUSE
  * @brief    This file includes HID Layer Handlers for USB Host HID class.
  * @{
  */

/** @defgroup USBH_HID_MOUSE_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Defines
  * @{
  */
/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Macros
  * @{
  */
/**
  * @}
  */

/** @defgroup USBH_HID_MOUSE_Private_FunctionPrototypes
  * @{
  */
static USBH_StatusTypeDef USBH_HID_MouseDecode(USBH_HandleTypeDef *phost);
static HID_Report_ItemTypedef* USBH_HID_FindMouseItem(HID_HandleTypeDef *HID_Handle, uint16_t usage_page, uint16_t usage_id);
static void USBH_HID_MapMouseItem(HID_Report_ItemTypedef **prop, HID_HandleTypeDef *HID_Handle, uint16_t usage_page, uint16_t usage_id);

/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Variables
  * @{
  */
HID_MOUSE_Info_TypeDef    mouse_info;
uint32_t                  mouse_report_data[8];
uint32_t                  mouse_rx_report_buf[8];

/* Structures defining how to access items in a HID mouse report */
/* Access button 1 state. */
static HID_Report_ItemTypedef *prop_b1 = NULL;

/* Access button 2 state. */
static HID_Report_ItemTypedef *prop_b2 = NULL;

/* Access button 3 state. */
static HID_Report_ItemTypedef *prop_b3 = NULL;

/* Access x coordinate change. */
static HID_Report_ItemTypedef *prop_x = NULL;

/* Access y coordinate change. */
static HID_Report_ItemTypedef *prop_y = NULL;

/* Access wheel direction. */
static HID_Report_ItemTypedef *prop_wheel_dir = NULL;


/**
  * @}
  */


/** @defgroup USBH_HID_MOUSE_Private_Functions
  * @{
  */

/**
  * @brief  USBH_HID_MouseInit
  *         The function init the HID mouse.
  * @param  phost: Host handle
  * @retval USBH Status
  */
USBH_StatusTypeDef USBH_HID_MouseInit(USBH_HandleTypeDef *phost)
{
  uint32_t i;
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  mouse_info.x = 0U;
  mouse_info.y = 0U;
  mouse_info.buttons[0] = 0U;
  mouse_info.buttons[1] = 0U;
  mouse_info.buttons[2] = 0U;

  for (i = 0U; i < (sizeof(mouse_report_data) / sizeof(uint32_t)); i++)
  {
    mouse_report_data[i] = 0U;
    mouse_rx_report_buf[i] = 0U;
  }
  
  /* Usage Page 0x09 (Buttons), Usage 1, 2, 3 */
  USBH_HID_MapMouseItem(&prop_b1, HID_Handle, 0x09, 0x02);
  USBH_HID_MapMouseItem(&prop_b2, HID_Handle, 0x09, 0x01);
  USBH_HID_MapMouseItem(&prop_b3, HID_Handle, 0x09, 0x03);

  /* Usage Page 0x01 (Generic Desktop), Usage 0x30(X), 0x31(Y), 0x38(Wheel) */
  USBH_HID_MapMouseItem(&prop_x, HID_Handle, 0x01, 0x30);
  USBH_HID_MapMouseItem(&prop_y, HID_Handle, 0x01, 0x31);
  USBH_HID_MapMouseItem(&prop_wheel_dir, HID_Handle, 0x01, 0x38);  

  if (HID_Handle->length > sizeof(mouse_report_data))
  {
    HID_Handle->length = sizeof(mouse_report_data);
  }
  HID_Handle->pData = (uint8_t *)(void *)mouse_rx_report_buf;
  USBH_HID_FifoInit(&HID_Handle->fifo, phost->device.Data, HID_QUEUE_SIZE * sizeof(mouse_report_data));

  return USBH_OK;
}

/**
  * @brief  USBH_HID_GetMouseInfo
  *         The function return mouse information.
  * @param  phost: Host handle
  * @retval mouse information
  */
HID_MOUSE_Info_TypeDef *USBH_HID_GetMouseInfo(USBH_HandleTypeDef *phost)
{
  if (USBH_HID_MouseDecode(phost) == USBH_OK)
  {
    return &mouse_info;
  }
  else
  {
    return NULL;
  }
}

/**
  * @brief  USBH_HID_MouseDecode
  *         The function decode mouse data.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_MouseDecode(USBH_HandleTypeDef *phost)
{
  HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

  if (HID_Handle->length == 0U)
  {
    return USBH_FAIL;
  }
  /*Fill report */
  if (USBH_HID_FifoRead(&HID_Handle->fifo, &mouse_report_data, HID_Handle->length) ==  HID_Handle->length)
  {
    /* Extraemos el primer byte del buffer entrante asumiendo que podría ser un Report ID */
    uint8_t received_id = ((uint8_t*)mouse_report_data)[0];      
      
    /*Decode report */
    if (prop_x && (prop_x->ReportID == 0 || prop_x->ReportID == received_id)) 
        mouse_info.x = (uint8_t)HID_ReadItem(prop_x, 0U);
    else
        mouse_info.x = 0U;
    
    if (prop_y && (prop_y->ReportID == 0 || prop_y->ReportID == received_id)) 
        mouse_info.y = (uint8_t)HID_ReadItem(prop_y, 0U);
    else
        mouse_info.y = 0U;
    
    if (prop_wheel_dir && (prop_wheel_dir->ReportID == 0 || prop_wheel_dir->ReportID == received_id)) 
        mouse_info.wheel_dir = (uint8_t)HID_ReadItem(prop_wheel_dir, 0U);
    else
        mouse_info.wheel_dir = 0U;

    if (prop_b1 && (prop_b1->ReportID == 0 || prop_b1->ReportID == received_id)) 
        mouse_info.buttons[0] = (uint8_t)HID_ReadItem(prop_b1, 0U);
    if (prop_b2 && (prop_b2->ReportID == 0 || prop_b2->ReportID == received_id)) 
        mouse_info.buttons[1] = (uint8_t)HID_ReadItem(prop_b2, 0U);
    if (prop_b3 && (prop_b3->ReportID == 0 || prop_b3->ReportID == received_id)) 
        mouse_info.buttons[2] = (uint8_t)HID_ReadItem(prop_b3, 0U);

    return USBH_OK;
  }
  return   USBH_FAIL;
}

/**
  * @brief  Search for an item in the parsed descriptor using UsagePage and UsageID.
  */
static HID_Report_ItemTypedef* USBH_HID_FindMouseItem(HID_HandleTypeDef *HID_Handle, uint16_t usage_page, uint16_t usage_id)
{
  for (uint32_t i = 0; i < HID_Handle->Report_Items.ItemNb; i++)
  {
    HID_Report_ItemTypedef *item = &HID_Handle->Report_Items.Item[i];
    if ((item->UsagePage == usage_page) && (item->Usage == usage_id))
    {
      return item;
    }
  }
  return NULL;
}

/**
  * @brief  Dynamically assigns the data pointer and bit offset.
  */
static void USBH_HID_MapMouseItem(HID_Report_ItemTypedef **prop, HID_HandleTypeDef *HID_Handle, uint16_t usage_page, uint16_t usage_id) 
{
  HID_Report_ItemTypedef *item = USBH_HID_FindMouseItem(HID_Handle, usage_page, usage_id);
  if (item != NULL) 
  {
    /* Map the data pointer to the read buffer using the bit offset */
    item->data = (uint8_t *)mouse_report_data + (item->bit_offset / 8U);
    item->shift = (uint8_t)(item->bit_offset % 8U);
    *prop = item;
  } 
  else 
  {
    *prop = NULL;
  }
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */


/**
  * @}
  */
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
