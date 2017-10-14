/*
 *  Copyright (C) 2014 -2016  Espressif System
 *
 *
* File:          GPIO.c
   Desined by:   Luis Felipe Echeverri
                 Alberth Moreno
				         Juan Manuel Gomez
   Description:  modified 02-08-2017, is added ISR and init_gpio function, also the interrupt 
                 is enable in  gpio_config  function when it is selected 
*/  

#include "esp_common.h"
#include "espressif/esp_common.h"
#include "freertos/portmacro.h"

#include "gpio.h"
#include "esp_system.h"
//---
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"
//--
extern uint8_t ButtonPressedFlag;
extern int32_t current_cnt_value;

GPIO_ConfigTypeDef pGPIOConfigButton;

void ICACHE_FLASH_ATTR
gpio_config(GPIO_ConfigTypeDef  *pGPIOConfig)
{
    uint16 gpio_pin_mask = pGPIOConfig->GPIO_Pin;
    uint32 io_reg;
    uint8 io_num = 0;
    uint32 pin_reg;

    if (pGPIOConfig->GPIO_Mode == GPIO_Mode_Input) {
        GPIO_AS_INPUT(gpio_pin_mask);
    } else if (pGPIOConfig->GPIO_Mode == GPIO_Mode_Output) {
        GPIO_AS_OUTPUT(gpio_pin_mask);
    }

    do {
        if ((gpio_pin_mask >> io_num) & 0x1) {
            io_reg = GPIO_PIN_REG(io_num);

            if ((0x1 << io_num) & (GPIO_Pin_0 | GPIO_Pin_2 | GPIO_Pin_4 | GPIO_Pin_5)) {
                PIN_FUNC_SELECT(io_reg, 0);
            } else {
                PIN_FUNC_SELECT(io_reg, 3);
            }

            if (pGPIOConfig->GPIO_Pullup) {
                PIN_PULLUP_EN(io_reg);
            } else {
                PIN_PULLUP_DIS(io_reg);
            }

            if (pGPIOConfig->GPIO_Mode == GPIO_Mode_Out_OD) {
                portENTER_CRITICAL();

                pin_reg = GPIO_REG_READ(GPIO_PIN_ADDR(io_num));
                pin_reg &= (~GPIO_PIN_DRIVER_MASK);
                pin_reg |= (GPIO_PAD_DRIVER_ENABLE << GPIO_PIN_DRIVER_LSB);
                GPIO_REG_WRITE(GPIO_PIN_ADDR(io_num), pin_reg);

                portEXIT_CRITICAL();
            } else if (pGPIOConfig->GPIO_Mode == GPIO_Mode_Sigma_Delta) {
                portENTER_CRITICAL();

                pin_reg = GPIO_REG_READ(GPIO_PIN_ADDR(io_num));
                pin_reg &= (~GPIO_PIN_SOURCE_MASK);
                pin_reg |= (0x1 << GPIO_PIN_SOURCE_LSB);
                GPIO_REG_WRITE(GPIO_PIN_ADDR(io_num), pin_reg);
                GPIO_REG_WRITE(GPIO_SIGMA_DELTA_ADDRESS, SIGMA_DELTA_ENABLE);

                portEXIT_CRITICAL();
            }
			/*---------------------------*
       * Added by Felipe 02-08-17  *
			 *---------------------------*/
			if (pGPIOConfig->GPIO_IntrType >0){
				GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_pin_mask);
				_xt_isr_unmask(1<<ETS_GPIO_INUM);
			}
      /*---------------------------*/
            gpio_pin_intr_state_set(io_num, pGPIOConfig->GPIO_IntrType);
        }

        io_num++;
    } while (io_num < 16);
}


/*-----------------------------------------------------------------------------
 * Change GPIO pin output by setting, clearing, or disabling pins.
 * In general, it is expected that a bit will be set in at most one
 * of these masks.  If a bit is clear in all masks, the output state
 * remains unchanged.
 *
 * There is no particular ordering guaranteed; so if the order of
 * writes is significant, calling code should divide a single call
 * into multiple calls.
 *------------------------------------------------------------------------------*/
void ICACHE_FLASH_ATTR
gpio_output_conf(uint32 set_mask, uint32 clear_mask, uint32 enable_mask, uint32 disable_mask)
{
    GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, set_mask);
    GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, clear_mask);
    GPIO_REG_WRITE(GPIO_ENABLE_W1TS_ADDRESS, enable_mask);
    GPIO_REG_WRITE(GPIO_ENABLE_W1TC_ADDRESS, disable_mask);
}

/*
 * Sample the value of GPIO input pins and returns a bitmask.
 */
uint32 ICACHE_FLASH_ATTR
gpio_input_get(void)
{
    return GPIO_REG_READ(GPIO_IN_ADDRESS);
}

/*
 * Register an application-specific interrupt handler for GPIO pin
 * interrupts.  Once the interrupt handler is called, it will not
 * be called again until after a call to gpio_intr_ack.  Any GPIO
 * interrupts that occur during the interim are masked.
 *
 * The application-specific handler is called with a mask of
 * pending GPIO interrupts.  After processing pin interrupts, the
 * application-specific handler may wish to use gpio_intr_pending
 * to check for any additional pending interrupts before it returns.
 */
void ICACHE_FLASH_ATTR
gpio_intr_handler_register(void *fn)
{
    _xt_isr_attach(ETS_GPIO_INUM, fn);
}

/*
  only highlevel and lowlevel intr can use for wakeup
*/
void ICACHE_FLASH_ATTR
gpio_pin_wakeup_enable(uint32 i, GPIO_INT_TYPE intr_state)
{
    uint32 pin_reg;

    if ((intr_state == GPIO_PIN_INTR_LOLEVEL) || (intr_state == GPIO_PIN_INTR_HILEVEL)) {
        portENTER_CRITICAL();

        pin_reg = GPIO_REG_READ(GPIO_PIN_ADDR(i));
        pin_reg &= (~GPIO_PIN_INT_TYPE_MASK);
        pin_reg |= (intr_state << GPIO_PIN_INT_TYPE_LSB);
        pin_reg |= GPIO_PIN_WAKEUP_ENABLE_SET(GPIO_WAKEUP_ENABLE);
        GPIO_REG_WRITE(GPIO_PIN_ADDR(i), pin_reg);

        portEXIT_CRITICAL();
    }
}

void ICACHE_FLASH_ATTR
gpio_pin_wakeup_disable(void)
{
    uint8  i;
    uint32 pin_reg;

    for (i = 0; i < GPIO_PIN_COUNT; i++) {
        pin_reg = GPIO_REG_READ(GPIO_PIN_ADDR(i));

        if (pin_reg & GPIO_PIN_WAKEUP_ENABLE_MASK) {
            pin_reg &= (~GPIO_PIN_INT_TYPE_MASK);
            pin_reg |= (GPIO_PIN_INTR_DISABLE << GPIO_PIN_INT_TYPE_LSB);
            pin_reg &= ~(GPIO_PIN_WAKEUP_ENABLE_SET(GPIO_WAKEUP_ENABLE));
            GPIO_REG_WRITE(GPIO_PIN_ADDR(i), pin_reg);
        }
    }
}

void ICACHE_FLASH_ATTR
gpio_pin_intr_state_set(uint32 i, GPIO_INT_TYPE intr_state)
{
    uint32 pin_reg;

    portENTER_CRITICAL();

    pin_reg = GPIO_REG_READ(GPIO_PIN_ADDR(i));
    pin_reg &= (~GPIO_PIN_INT_TYPE_MASK);
    pin_reg |= (intr_state << GPIO_PIN_INT_TYPE_LSB);
    GPIO_REG_WRITE(GPIO_PIN_ADDR(i), pin_reg);

    portEXIT_CRITICAL();
}

void ICACHE_FLASH_ATTR
gpio16_output_conf(void)
{
    WRITE_PERI_REG(PAD_XPD_DCDC_CONF,
                   (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32)0x1); 	// mux configuration for XPD_DCDC to output rtc_gpio0

    WRITE_PERI_REG(RTC_GPIO_CONF,
                   (READ_PERI_REG(RTC_GPIO_CONF) & (uint32)0xfffffffe) | (uint32)0x0);	//mux configuration for out enable

    WRITE_PERI_REG(RTC_GPIO_ENABLE,
                   (READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32)0xfffffffe) | (uint32)0x1);	//out enable
}

void ICACHE_FLASH_ATTR
gpio16_output_set(uint8 value)
{
    WRITE_PERI_REG(RTC_GPIO_OUT,
                   (READ_PERI_REG(RTC_GPIO_OUT) & (uint32)0xfffffffe) | (uint32)(value & 1));
}

void ICACHE_FLASH_ATTR
gpio16_input_conf(void)
{
    WRITE_PERI_REG(PAD_XPD_DCDC_CONF,
                   (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32)0x1); 	// mux configuration for XPD_DCDC and rtc_gpio0 connection

    WRITE_PERI_REG(RTC_GPIO_CONF,
                   (READ_PERI_REG(RTC_GPIO_CONF) & (uint32)0xfffffffe) | (uint32)0x0);	//mux configuration for out enable

    WRITE_PERI_REG(RTC_GPIO_ENABLE,
                   READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32)0xfffffffe);	//out disable
}

uint8 ICACHE_FLASH_ATTR
gpio16_input_get(void)
{
    return (uint8)(READ_PERI_REG(RTC_GPIO_IN_DATA) & 1);
}

/*#############################################################
   # Interrupt service routine for GPIO                       #
   # Added by Felipe 02-08-17                                 #
   ############################################################*/
void gpio_intr_handler(void){
	  uint8_t ButtonS=1;
    uint32_t i=0;
    uint16 gpio_pin_mask = pGPIOConfigButton.GPIO_Pin;
    uint8 io_num = 0;
    uint32 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

      if ((gpio_status & gpio_pin_mask) ) 
      {

         //disable interrupt
         gpio_pin_intr_state_set(io_num, GPIO_PIN_INTR_DISABLE);
         
		 // call func here
      
      
      //delay for debounce, this is no the best practice
      for(i=0;i<10000;i++){
       if (i%5000==0){
        printf(".");
        }
      }
      ButtonS = GPIO_INPUT_GET(Button);
      if (ButtonS==0){
            //read count from  EEPROM//
          (void) system_rtc_mem_read(64,&current_cnt_value,1);
           current_cnt_value++;
            //save in EEPROM//
           (void) system_rtc_mem_write(64,&current_cnt_value,1);
          ButtonPressedFlag=1;
   		   printf("Button Interrupt \n");
              
          //sendData();
        }
		 //clear interrupt status
         GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & gpio_pin_mask);
     // restore
         gpio_pin_intr_state_set(io_num, pGPIOConfigButton.GPIO_IntrType);
      }
}

/*#############################################################
   # Init GPIO                                                #
   # Added by Felipe 02-08-17                                 #
   ############################################################*/
   
      
void gpio_init(void){

  GPIO_ConfigTypeDef pGPIOConfig;
	pGPIOConfig.GPIO_IntrType = GPIO_PIN_INTR_DISABLE;
	pGPIOConfig.GPIO_Mode     = GPIO_Mode_Output;
	pGPIOConfig.GPIO_Pullup   = GPIO_PullUp_EN;
	
	pGPIOConfig.GPIO_Pin = (BIT(LED));
	gpio_config(&pGPIOConfig);
	
	pGPIOConfigButton.GPIO_IntrType = GPIO_PIN_INTR_NEGEDGE;
	pGPIOConfigButton.GPIO_Mode     = GPIO_Mode_Input;
	pGPIOConfigButton.GPIO_Pullup   = GPIO_PullUp_EN;
	
	pGPIOConfigButton.GPIO_Pin = (BIT(Button));
	gpio_config(&pGPIOConfigButton);

	gpio_intr_handler_register(gpio_intr_handler);	
}

