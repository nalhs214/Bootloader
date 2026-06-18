/*
 * boot.h
 *
 * Created: 2026-06-08 오전 11:03:25
 *  Author: USER
 */ 


#ifndef INCFILE1_H_
#define INCFILE1_H_

#include <stdint.h>

void flash_page_write(uint32_t addr, uint8_t *buf);
void jump_to_app(void);


#endif /* INCFILE1_H_ */