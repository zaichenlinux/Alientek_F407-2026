/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information
 */

/*
 * STM32F407 Ethernet Driver Adaptation Layer
 * Copyright 2026
 */

#ifndef __STM32F407_ETH_DRIVER__
#define __STM32F407_ETH_DRIVER__

#include <stdint.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ethernet interface configuration */
#define STM32F407_ETH_RX_DESC_COUNT    16
#define STM32F407_ETH_TX_DESC_COUNT    16
#define STM32F407_ETH_MAX_FRAME_SIZE   1536

/* PHY configuration - LAN8720A or KSZ8081RND */
#define STM32F407_PHY_ADDRESS          0x00

/* Ethernet interface handle */
typedef struct {
    ETH_HandleTypeDef *heth;
    uint8_t *rx_buffers[STM32F407_ETH_RX_DESC_COUNT];
    uint8_t *tx_buffers[STM32F407_ETH_TX_DESC_COUNT];
    uint32_t rx_idx;
    uint32_t tx_idx;
} stm32f407_eth_handle_t;

/* Function prototypes */
int stm32f407_eth_driver_init(void);
int stm32f407_eth_driver_deinit(void);
int stm32f407_eth_send_frame(const uint8_t *data, uint32_t length);
int stm32f407_eth_recv_frame(uint8_t *buffer, uint32_t *length);
uint32_t stm32f407_eth_get_link_status(void);
void stm32f407_eth_isr_handler(void);
int stm32f407_get_mac_address(uint8_t *mac_addr);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F407_ETH_DRIVER__ */

