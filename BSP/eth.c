#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_eth.h"

// ETH句柄（全局，供中断服务函数调用）
ETH_HandleTypeDef heth;
// 网络配置参数（与前文一致，可直接复用）
static uint8_t macAddr[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static uint8_t ipAddr[] = {192, 168, 1, 100};
static uint8_t netMask[] = {255, 255, 255, 0};
static uint8_t gateway[] = {192, 168, 1, 1};

// 优化：增加接收缓冲区（双缓冲区，避免中断与主循环冲突）
#define ETH_RX_BUFFER_LEN 1500  // 以太网最大帧长（适配多数场景）
uint8_t eth_rx_buf1[ETH_RX_BUFFER_LEN] = {0};
uint8_t eth_rx_buf2[ETH_RX_BUFFER_LEN] = {0};
uint8_t *cur_rx_buf = eth_rx_buf1;  // 当前接收缓冲区指针
uint8_t rx_buf_flag = 0;            // 缓冲区切换标志（0：buf1，1：buf2）

// 优化：ETH底层初始化（强化中断配置、GPIO稳定性）
static HAL_StatusTypeDef ETH_MspInit(ETH_HandleTypeDef *heth) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  
  // 1. 使能相关时钟（增加时钟稳定延迟）
  __HAL_RCC_ETH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  HAL_Delay(10);  // 优化：时钟使能后延迟，确保外设上电稳定
  
  // 2. 配置ETH GPIO引脚（增加下拉/上拉，提升抗干扰能力）
  // PA1: ETH_MDC, PA2: ETH_MDIO
  GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;  // 优化：改为上拉，减少信号干扰
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  
  // PG11: ETH_RMII_TX_EN, PG13: ETH_RMII_TXD0, PG14: ETH_RMII_TXD1
  GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
  
  // PA7: ETH_RMII_CRS_DV（优化：增加下拉，避免空闲时电平漂移）
  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  
  // PD3: ETH_RESET（优化：复位逻辑完善，确保PHY复位彻底）
  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_RESET);  // 拉低复位
  HAL_Delay(100);                                         // 复位延迟100ms
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_SET);    // 释放复位
  HAL_Delay(50);                                          // 等待PHY稳定
  
  // 3. 优化中断配置（合理设置优先级，避免被抢占）
  // 优先级：抢占优先级1，响应优先级0（低于SysTick，高于普通外设）
  HAL_NVIC_SetPriority(ETH_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(ETH_IRQn);
  
  return HAL_OK;
}

// 优化：ETH初始化（配置为中断接收模式，启用DMA高效传输）
HAL_StatusTypeDef ETH_InitConfig(void) {
  HAL_StatusTypeDef ret = HAL_OK;
  
  // 1. ETH句柄基础配置（重点：设置为中断接收模式）
  heth.Instance = ETH;
  heth.Init.AutoNegotiation = ENABLE;          // 使能自动协商
  heth.Init.Speed = ETH_SPEED_100M;            // 速率自动协商，此处仅为默认
  heth.Init.DuplexMode = ETH_MODE_FULLDUPLEX;  // 双工模式自动协商
  heth.Init.PhyAddress = ETH_PHY_ADDRESS;      // LAN8720A默认PHY地址0x00
  heth.Init.MACAddr = macAddr;                 // 本地MAC地址
  heth.Init.RxMode = ETH_RXINTERRUPT_MODE;     // 优化：中断接收模式
  heth.Init.ChecksumMode = ETH_CHECKSUM_BY_HW; // 硬件校验和，提升效率
  heth.Init.MediaInterface = ETH_MEDIA_INTERFACE_RMII; // RMII接口
  
  // 2. 底层初始化
  if (HAL_ETH_MspInit(&heth) != HAL_OK) {
    ret = HAL_ERROR;
    goto exit;
  }
  
  // 3. ETH外设初始化
  if (HAL_ETH_Init(&heth) != HAL_OK) {
    ret = HAL_ERROR;
    goto exit;
  }
  
  // 4. 优化：启用ETH接收中断（手动使能，确保中断生效）
  HAL_ETH_Receive_IT(&heth, cur_rx_buf, ETH_RX_BUFFER_LEN);
  
exit:
  return ret;
}

// 优化：ETH数据发送函数（增加超时处理，适配中断模式）
HAL_StatusTypeDef ETH_SendData(uint8_t *pData, uint16_t len) {
  if (len == 0 || pData == NULL) {
    return HAL_ERROR;  // 优化：增加参数校验，避免异常
  }
  
  ETH_TxPacketConfigTypeDef txConfig = {0};
  txConfig.Length = len;
  txConfig.TxBuffer = pData;
  txConfig.ChecksumCtrl = ETH_CHECKSUM_NONE;  // 硬件已使能校验和
  
  // 优化：超时时间设置为500ms，适配不同传输场景
  return HAL_ETH_Transmit(&heth, &txConfig, 500);
}

// 优化：中断服务函数（增加异常处理，确保中断标志彻底清除）
void ETH_IRQHandler(void) {
  // 优化：先判断ETH外设是否就绪，避免无效中断处理
  if (__HAL_ETH_GET_FLAG(&heth, ETH_FLAG_RX_OK) != RESET) {
    HAL_ETH_IRQHandler(&heth);  // 调用HAL库中断处理函数
    __HAL_ETH_CLEAR_FLAG(&heth, ETH_FLAG_RX_OK); // 手动清除接收中断标志
  }
  
  // 优化：处理异常中断（如溢出中断），避免中断卡死
  if (__HAL_ETH_GET_FLAG(&heth, ETH_FLAG_RX_OVERFLOW) != RESET) {
    __HAL_ETH_CLEAR_FLAG(&heth, ETH_FLAG_RX_OVERFLOW); // 清除溢出标志
    HAL_ETH_Receive_IT(&heth, cur_rx_buf, ETH_RX_BUFFER_LEN); // 重新使能接收
  }
}

// 优化：接收完成回调函数（双缓冲区切换，避免数据覆盖）
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth) {
  uint16_t rx_len = 0;
  
  // 1. 读取接收数据长度（优化：增加长度校验，避免无效数据）
  rx_len = HAL_ETH_GetRxDataLength(&heth, cur_rx_buf);
  if (rx_len > 0 && rx_len <= ETH_RX_BUFFER_LEN) {
    // 此处可添加数据处理逻辑（如解析、上报）
    // 示例：将接收数据转发到串口（可根据需求修改）
    HAL_UART_Transmit(&huart1, cur_rx_buf, rx_len, 100);
  }
  
  // 2. 双缓冲区切换（优化：避免中断与主循环数据冲突）
  rx_buf_flag = !rx_buf_flag;
  cur_rx_buf = (rx_buf_flag == 0) ? eth_rx_buf1 : eth_rx_buf2;
  
  // 3. 重新使能接收中断（必须调用，确保下次中断能触发）
  HAL_ETH_Receive_IT(&heth, cur_rx_buf, ETH_RX_BUFFER_LEN);
}

// 优化：ETH中断模式异常恢复函数（供主循环调用，提升稳定性）
void ETH_Interrupt_Recovery(void) {
  // 检查ETH外设状态，若异常则重新初始化
  if (HAL_ETH_GetState(&heth) == HAL_ETH_STATE_ERROR) {
    HAL_ETH_DeInit(&heth);
    ETH_InitConfig();
  }
}