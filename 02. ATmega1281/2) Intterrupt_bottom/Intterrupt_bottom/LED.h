/*
 * led.h
 */


#ifndef LED_H_
#define LED_H_

#include <avr/io.h>

#define LED_DDR     DDRC
#define LED_PORT    PORTC
#define LED_MASK    0x0F    // PC0~PC3

void LED_init(void);
void LED_display(uint8_t val);

#endif /* LED_H_ */