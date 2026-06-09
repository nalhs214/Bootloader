################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/main.c \
../src/packet.c \
../src/uart.c 

C_DEPS += \
./src/main.d \
./src/packet.d \
./src/uart.d 

OBJS += \
./src/main.o \
./src/packet.o \
./src/uart.o 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c src/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I"/home/henry/eclipse-workspace/bl_sender2/inc" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-src

clean-src:
	-$(RM) ./src/main.d ./src/main.o ./src/packet.d ./src/packet.o ./src/uart.d ./src/uart.o

.PHONY: clean-src

