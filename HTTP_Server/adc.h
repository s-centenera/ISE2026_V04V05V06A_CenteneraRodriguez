#include "stm32f4xx_hal.h"
#ifndef __ADC_H
#define __ADC_H  // NOOOOOO SE SI PONER EST
	void ADC1_pins_F429ZI_config(void);
	int ADC_Init_Single_Conversion(ADC_HandleTypeDef *, ADC_TypeDef  *);
	float ADC_getVoltage(ADC_HandleTypeDef * , uint32_t );
#endif

