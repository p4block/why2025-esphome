#pragma once
#include "esphome/core/component.h"
#include "driver/uart.h"
#include "driver/gpio.h"
namespace esphome::radio_bridge {
class RadioBridge : public Component {
 public:
  void setup() override {
    uart_config_t conf{};
    conf.baud_rate=460800;
    conf.data_bits=UART_DATA_8_BITS;
    conf.parity=UART_PARITY_DISABLE;
    conf.stop_bits=UART_STOP_BITS_1;
    conf.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;
    conf.source_clk=UART_SCLK_DEFAULT;
    for(auto port : {UART_NUM_0, UART_NUM_1}) {
      uart_param_config(port,&conf);
      uart_driver_install(port,8192,8192,0,nullptr,0);
    }
    uart_set_pin(UART_NUM_0,37,38,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    uart_set_pin(UART_NUM_1,16,15,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    gpio_set_direction(GPIO_NUM_13,GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_13,0); // C6 BOOT
    gpio_set_direction(GPIO_NUM_12,GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_12,0); // shared C6/keypad EN
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(GPIO_NUM_12,1);
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_flush_input(UART_NUM_0);
    uart_flush_input(UART_NUM_1);
  }
  void loop() override {
    uint8_t data[2048];
    int n=uart_read_bytes(UART_NUM_0,data,sizeof(data),0);
    if(n>0) uart_write_bytes(UART_NUM_1,data,n);
    n=uart_read_bytes(UART_NUM_1,data,sizeof(data),0);
    if(n>0) uart_write_bytes(UART_NUM_0,data,n);
  }
};
}
