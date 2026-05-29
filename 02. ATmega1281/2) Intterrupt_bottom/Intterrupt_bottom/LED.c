/*
 * led.c
 */


#include "led.h"

/* ── LED 초기설정, PC0~PC3 output, 초기화 ── */
void LED_init(void)
{
    LED_DDR  |=  LED_MASK;   // PC0~PC3 출력 설정
    LED_PORT &= ~LED_MASK;   // 초기값 0 (모두 OFF)
}

/* ── LED에 2진수로 count 값 출력 ── */
void LED_display(uint8_t val)
{
    LED_PORT = (val & LED_MASK);
}