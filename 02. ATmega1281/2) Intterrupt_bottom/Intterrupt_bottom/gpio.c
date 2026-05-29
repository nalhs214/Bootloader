/*
	gpio.c
 */ 

#include "gpio.h"

extern volatile uint8_t count;
extern volatile uint8_t flag;
extern void LED_display(uint8_t val);

/* ── 인터럽트 초기설정, PE7 INT7 내부풀업, falling edge ── */
void INT7_Init(void)
{
    DDRE  &= ~(1 << PE7);   // INPUT 설정
    PORTE |=  (1 << PE7);   // 내부풀업 활성화

    EICRB  = (EICRB & ~(3 << ISC70)) | (2 << ISC70);  // 10 : falling edge
    EIFR  |=  (1 << INTF7);     // 플래그 클리어 먼저
    EIMSK |=  (1 << INT7);      // INT7 enable
}

/* ── INT7 인터럽트 핸들러 ── */
ISR(INT7_vect)
{
    EIFR |= (1 << INTF7);
    _delay_ms(50);
    if (!(PINE & (1 << PE7)))
    {
        count = (count + 1) & 0x0F;
        LED_display(count);
        flag = 1;
    }
    EIFR |= (1 << INTF7);
}
