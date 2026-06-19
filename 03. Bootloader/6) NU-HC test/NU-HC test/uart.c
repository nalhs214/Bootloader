/*
 * uart.c
 */

#include "uart.h"
#include <avr/interrupt.h>

#define BUF_SIZE 256
#define BUF_MASK	(BUF_SIZE - 1)

/* 원형 큐 채널 생성 */
static volatile uint8_t rx0_buf[BUF_SIZE], tx0_buf[BUF_SIZE];
static volatile uint8_t rx0_head = 0, rx0_tail = 0, tx0_head = 0, tx0_tail = 0;

static volatile uint8_t rx1_buf[BUF_SIZE], tx1_buf[BUF_SIZE];
static volatile uint8_t rx1_head = 0, rx1_tail = 0, tx1_head = 0, tx1_tail = 0;


/******* USART0 인터럽트 설정 ******/
ISR(USART0_RX_vect)			/*interrupt 발생하면 함수 호출*/
{
	uint8_t c = UDR0;
	uint8_t next = (rx0_head + 1) & BUF_MASK;
	if (next != rx0_tail){			/* 가득차지 않았을 때만 적재*/
		rx0_buf[rx0_head] = c;
		rx0_head = next;
	}
}

ISR(USART0_UDRE_vect)
{
	if(tx0_head == tx0_tail){
		UCSR0B &= ~(1<<UDRIE0);
	}
	else {
		UDR0 = tx0_buf[tx0_tail];
		tx0_tail = (tx0_tail + 1) & BUF_MASK;
	}
		
}




/******* USART1 인터럽트 설정 ********/
ISR(USART1_RX_vect)			/*interrupt 발생하면 함수 호출*/
{
	uint8_t c = UDR1;
	uint8_t next = (rx1_head + 1) & BUF_MASK;
	if (next != rx1_tail){			/* 가득차지 않았을 때만 적재*/
		rx1_buf[rx1_head] = c;
		rx1_head = next;
	}
}

ISR(USART1_UDRE_vect)
{
	if(tx1_head == tx1_tail){
		UCSR1B &= ~(1<<UDRIE1);
	}
	else {
		UDR1 = tx1_buf[tx1_tail];
		tx1_tail = (tx1_tail + 1) & BUF_MASK;
	}
	
}



/******* UART 초기설정 **********/
void UART0_init(void)
{
	UBRR0H = 0;
	UBRR0L = 51;   // 16MHz에서 115200 Baud Rate 설정  9600 = 103 / 19200 = 51
    //UCSR0A = (1 << U2X0);		// baud rate에 따라 설정
	UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);      // receiver, transmitter enable
	UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);    // 8bit character size, no parity
}

void UART1_init(void)
{
	UBRR1H = 0;
	UBRR1L = 51;   // 16MHz에서 115200 Baud Rate 설정  9600 = 103 / 19200 = 51
	//UCSR0A = (1 << U2X0);		// baud rate에 따라 설정
	UCSR1B = (1 << RXEN1) | (1 << TXEN1) | (1 << RXCIE1);      // receiver, transmitter enable
	UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);    // 8bit character size, no parity
}

/******** 송신 ***********/
void USART0_Transmit(unsigned char data)
{
	uint8_t next = (tx0_head + 1) & BUF_MASK;
	while (next == tx0_tail);		// 큐가 가득 차면 빌 때까지만 대기
	tx0_buf[tx0_head] = data;
	tx0_head = next;
	UCSR0B |= (1 << UDRIE0);        // 송신 인터럽트 켜기
}

void USART1_Transmit(unsigned char data)
{
	uint8_t next = (tx1_head + 1) & BUF_MASK;
	while (next == tx1_tail);
	tx1_buf[tx1_head] = data;
	tx1_head = next;
	UCSR1B |= (1 << UDRIE1);
}



/************ 수신 *************/
unsigned char USART0_Receive(void)
{
	while (rx0_head == rx0_tail);
	uint8_t c = rx0_buf[rx0_tail];
	rx0_tail = (rx0_tail + 1) & BUF_MASK;
	return c;
}
unsigned char USART1_Receive(void)
{
	while (rx1_head == rx1_tail);
	uint8_t c = rx1_buf[rx1_tail];
	rx1_tail = (rx1_tail + 1) & BUF_MASK;
	return c;
}


/* ===== 큐 상태 ===== */
int USART0_Available(void) {
	return (rx0_head != rx0_tail); 
}
int USART1_Available(void) {
	return (rx1_head != rx1_tail); 
}



/* ── printf → UART 연결 ── */
static int uart_putchar(char c, FILE *stream)
{
	USART0_Transmit((unsigned char)c);
	return 0;
}
static FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);

void UART_stdout_init(void) {
	 stdout = &uart_stdout; 
}