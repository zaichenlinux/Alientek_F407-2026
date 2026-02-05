/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information
 */

/*
 * STM32F407 Ethernet Driver Implementation
 * Copyright 2026
 */

#include "stm32f407_eth_driver.h"
#include <string.h>

/* Static ethernet handle */
static stm32f407_eth_handle_t eth_handle = {
    .rx_idx = 0,
    .tx_idx = 0
};

/* Static ETH handle for HAL library */
static ETH_HandleTypeDef heth;

/* DMA descriptors */
static ETH_DMADescTypeDef DMARxDscrTab[STM32F407_ETH_RX_DESC_COUNT];
static ETH_DMADescTypeDef DMATxDscrTab[STM32F407_ETH_TX_DESC_COUNT];

/* Buffer arrays */
static uint8_t Rx_Buff[STM32F407_ETH_RX_DESC_COUNT][STM32F407_ETH_MAX_FRAME_SIZE];
static uint8_t Tx_Buff[STM32F407_ETH_TX_DESC_COUNT][STM32F407_ETH_MAX_FRAME_SIZE];

/**
 * Ethernet MSP initialization
 * @param heth ETH handle
 */
void HAL_ETH_MspInit(ETH_HandleTypeDef *heth)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    /* Enable Ethernet clocks */
    __HAL_RCC_ETH1MAC_CLK_ENABLE();
    __HAL_RCC_ETH1TX_CLK_ENABLE();
    __HAL_RCC_ETH1RX_CLK_ENABLE();
    
    /* Enable GPIO clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    
    /* Configure RMII pins */
    
    /* GPIOA pins: RMII_REF_CLK (PA1), RMII_MDIO (PA2), RMII_MDC (PA3), RMII_CRS_DV (PA7) */
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    /* GPIOB pins: RMII_TX_EN (PB11), RMII_TXD0 (PB12), RMII_TXD1 (PB13) */
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    /* GPIOC pins: RMII_RXD0 (PC4), RMII_RXD1 (PC5) */
    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    
    /* Configure Ethernet NVIC interrupt */
    HAL_NVIC_SetPriority(ETH_IRQn, 0x07, 0x00);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
}

/**
 * Ethernet MSP deinitialization
 * @param heth ETH handle
 */
void HAL_ETH_MspDeInit(ETH_HandleTypeDef *heth)
{
    /* Disable Ethernet NVIC interrupt */
    HAL_NVIC_DisableIRQ(ETH_IRQn);
    
    /* Disable Ethernet clocks */
    __HAL_RCC_ETH1MAC_CLK_DISABLE();
    __HAL_RCC_ETH1TX_CLK_DISABLE();
    __HAL_RCC_ETH1RX_CLK_DISABLE();
}

/**
 * PHY initialization
 * @return 0 on success, -1 on failure
 */
static int stm32f407_eth_phy_init(void)
{
    uint32_t tickstart;
    uint16_t phy_reg;
    
    /* Reset PHY */
    if (HAL_ETH_WritePHYRegister(&heth, STM32F407_PHY_ADDRESS, 0x0000, 0x8000) != HAL_OK)
    {
        return -1;
    }
    
    /* Wait for PHY reset completion */
    tickstart = HAL_GetTick();
    while ((HAL_GetTick() - tickstart) < 1000)
    {
        if (HAL_ETH_ReadPHYRegister(&heth, STM32F407_PHY_ADDRESS, 0x0000, &phy_reg) == HAL_OK)
        {
            if (!(phy_reg & 0x8000))
            {
                break;
            }
        }
        HAL_Delay(10);
    }
    
    /* Check if PHY reset timed out */
    if ((HAL_GetTick() - tickstart) >= 1000)
    {
        return -1;
    }
    
    /* Configure PHY in RMII mode */
    if (HAL_ETH_WritePHYRegister(&heth, STM32F407_PHY_ADDRESS, 0x0004, 0x0100) != HAL_OK)
    {
        return -1;
    }
    
    return 0;
}

/**
 * Initialize STM32F407 Ethernet driver
 * @return 0 on success, -1 on failure
 */
int stm32f407_eth_driver_init(void)
{
    /* Initialize ETH handle */
    heth.Instance = ETH;
    heth.Init.AutoNegotiation = ETH_AUTONEGOTIATION_ENABLE;
    heth.Init.Speed = ETH_SPEED_100M;
    heth.Init.DuplexMode = ETH_MODE_FULLDUPLEX;
    heth.Init.PhyAddress = STM32F407_PHY_ADDRESS;
    heth.Init.MACAddr[0] = 0x00;
    heth.Init.MACAddr[1] = 0x11;
    heth.Init.MACAddr[2] = 0x22;
    heth.Init.MACAddr[3] = 0x33;
    heth.Init.MACAddr[4] = 0x44;
    heth.Init.MACAddr[5] = 0x55;
    heth.Init.RxDesc = DMARxDscrTab;
    heth.Init.TxDesc = DMATxDscrTab;
    heth.Init.RxBuffLen = STM32F407_ETH_MAX_FRAME_SIZE;
    heth.Init.MediaInterface = ETH_MEDIA_INTERFACE_RMII;
    
    /* Initialize Ethernet */
    if (HAL_ETH_Init(&heth) != HAL_OK)
    {
        return -1;
    }
    
    /* Initialize PHY */
    if (stm32f407_eth_phy_init() != 0)
    {
        return -1;
    }
    
    /* Start Ethernet with interrupt mode */
    if (HAL_ETH_Start_IT(&heth) != HAL_OK)
    {
        return -1;
    }
    
    /* Update eth_handle */
    eth_handle.heth = &heth;
    for (int i = 0; i < STM32F407_ETH_RX_DESC_COUNT; i++)
    {
        eth_handle.rx_buffers[i] = Rx_Buff[i];
    }
    for (int i = 0; i < STM32F407_ETH_TX_DESC_COUNT; i++)
    {
        eth_handle.tx_buffers[i] = Tx_Buff[i];
    }
    
    return 0;
}

/**
 * Deinitialize STM32F407 Ethernet driver
 * @return 0 on success, -1 on failure
 */
int stm32f407_eth_driver_deinit(void)
{
    /* Stop Ethernet with interrupt mode */
    if (HAL_ETH_Stop_IT(&heth) != HAL_OK)
    {
        return -1;
    }
    
    /* Deinitialize Ethernet */
    if (HAL_ETH_DeInit(&heth) != HAL_OK)
    {
        return -1;
    }
    
    /* Reset eth_handle */
    memset(&eth_handle, 0, sizeof(stm32f407_eth_handle_t));
    
    return 0;
}

/**
 * Send Ethernet frame
 * @param data Pointer to frame data
 * @param length Frame length
 * @return Number of bytes sent, -1 on error
 */
int stm32f407_eth_send_frame(const uint8_t *data, uint32_t length)
{
    if (!data || length == 0 || length > STM32F407_ETH_MAX_FRAME_SIZE)
    {
        return -1;
    }
    
    /* Prepare transmit packet configuration */
    ETH_TxPacketConfigTypeDef tx_config;
    tx_config.pData = (uint8_t *)data;
    tx_config.Length = length;
    tx_config.TxBufferSize = length;
    
    /* Transmit Ethernet frame */
    if (HAL_ETH_Transmit(&heth, &tx_config, 100) != HAL_OK)
    {
        return -1;
    }
    
    return (int)length;
}

/**
 * Receive Ethernet frame
 * @param buffer Pointer to receive buffer
 * @param length Pointer to receive length (output)
 * @return 0 on success, -1 on error
 */
int stm32f407_eth_recv_frame(uint8_t *buffer, uint32_t *length)
{
    if (!buffer || !length)
    {
        return -1;
    }
    
    /* Read received frame */
    void *rx_data;
    if (HAL_ETH_ReadData(&heth, &rx_data) == HAL_OK)
    {
        /* Get frame length */
        uint32_t frame_length = heth.RxDescList.RxDataLength;
        
        /* Check frame length */
        if (frame_length > STM32F407_ETH_MAX_FRAME_SIZE)
        {
            *length = 0;
            return -1;
        }
        
        /* Copy data to buffer */
        memcpy(buffer, rx_data, frame_length);
        *length = frame_length;
        
        return 0;
    }
    
    *length = 0;
    return -1;
}

/**
 * Get Ethernet link status
 * @return 1 if link is up, 0 if link is down
 */
uint32_t stm32f407_eth_get_link_status(void)
{
    uint16_t phy_reg;
    
    /* Read PHY status register (register 1) */
    if (HAL_ETH_ReadPHYRegister(&heth, STM32F407_PHY_ADDRESS, 0x0001, &phy_reg) == HAL_OK)
    {
        /* Check link status bit (bit 2) */
        if (phy_reg & (1 << 2))
        {
            return 1; /* Link is up */
        }
    }
    
    return 0; /* Link is down */
}

/**
 * Ethernet interrupt service routine
 * Should be called from the main Ethernet interrupt handler
 */
void stm32f407_eth_isr_handler(void)
{
    /* Handle Ethernet interrupts */
    HAL_ETH_IRQHandler(&heth);
}

/**
 * Ethernet RX callback
 * @param heth ETH handle
 */
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth)
{
    /* RX callback implementation */
    /* This function is called when a frame is received */
}

/**
 * Ethernet TX callback
 * @param heth ETH handle
 */
void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *heth)
{
    /* TX callback implementation */
    /* This function is called when a frame is transmitted */
}

/**
 * Ethernet error callback
 * @param heth ETH handle
 */
void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *heth)
{
    /* Error callback implementation */
    /* This function is called when an Ethernet error occurs */
}

/**
 * Ethernet Rx Allocate callback
 * @param buff Pointer to allocated buffer
 */
void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    /* Allocate buffer from static buffer pool */
    static uint32_t rx_buff_idx = 0;
    *buff = Rx_Buff[rx_buff_idx];
    rx_buff_idx = (rx_buff_idx + 1) % STM32F407_ETH_RX_DESC_COUNT;
}

/**
 * Ethernet Rx Link callback
 * @param pStart Pointer to packet start
 * @param pEnd Pointer to packet end
 * @param buff Pointer to received data
 * @param Length Received data length
 */
void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
    /* Link received data */
    if (*pStart == NULL)
    {
        *pStart = buff;
    }
    *pEnd = buff + Length;
}

/**
 * Ethernet Tx Free callback
 * @param buff Pointer to buffer to free
 */
void HAL_ETH_TxFreeCallback(uint32_t *buff)
{
    /* No action needed for static buffers */
}

/**
 * Get MAC address from Ethernet interface
 * @param mac_addr Pointer to 6-byte MAC address buffer
 * @return 0 on success, -1 on failure
 */
int stm32f407_get_mac_address(uint8_t *mac_addr)
{
    if (!mac_addr)
    {
        return -1;
    }
    
    /* Read MAC address from ETH handle */
    memcpy(mac_addr, heth.Init.MACAddr, 6);
    
    return 0;
}

