/**
  ******************************************************************************
  * @file    lcd.c
  * @author  Michelle Conto y Jose Vargas
  * @brief   Implementación del control del LCD Gráfico.
  * * Arquitectura de Software:
  * - Utiliza un buffer en RAM de 512 bytes (128x32 pixeles / 8 bits por pagina).
  * - Todas las funciones de dibujo modifican este buffer.
  * - La función LCD_update() vuelca el buffer completo vía SPI.
  * - El hilo serializa el acceso: recibe mensajes, dibuja en RAM y actualiza.
  ******************************************************************************
  */

#include "lcd.h"
#include "stm32f4xx_hal.h"
#include "Driver_SPI.h"
#include "Arial12x12.h"
#include "string.h"
#include <stdio.h>
#include <stdlib.h>
#include "cmsis_os2.h"

/* --- Recursos Hardware --- */
extern ARM_DRIVER_SPI Driver_SPI1;
ARM_DRIVER_SPI* SPIdrv = &Driver_SPI1;

GPIO_InitTypeDef GPIO_InitStruct_LCD;
TIM_HandleTypeDef htim7; // Usado para retardos de microsegundos

/* --- Buffer de Video (RAM) --- */
/* 4 Páginas de 128 columnas de 8 bits cada una = 512 bytes */
unsigned char buffer[512];
uint16_t positionL1 = 0;
uint16_t positionL2 = 0;

/* --- Recursos RTOS --- */
int Init_ThLCD (void); 
void ThLCD (void *argument);
osThreadId_t tid_ThLCD;     
osMessageQueueId_t mid_messageQueueLCD;

MSGQUEUE_OBJ_LCD_t LCD_mensaje; 

/* --- Implementación de Funciones --- */

/**
  * @brief  Inicialización del módulo LCD (Hilo y Cola).
  */
int Init_ThLCD (void) {
  tid_ThLCD = osThreadNew(ThLCD, NULL, NULL);
  if (tid_ThLCD == NULL) {
    return(-1);
  }
  
  // Creamos la cola con el tamaño correcto de la estructura
  mid_messageQueueLCD = osMessageQueueNew(50, sizeof(MSGQUEUE_OBJ_LCD_t), NULL);
  if (mid_messageQueueLCD == NULL) {
      while (1); // Error fatal
  }
  
  // Secuencia de arranque del hardware
  LCD_reset();
  LCD_ClearBuffer();
  LCD_init();
  LCD_update();
  
  return(0);
}

/**
  * @brief  Hilo Principal del LCD.
  * Actúa como servidor gráfico: espera mensajes y actualiza la pantalla.
  */
void ThLCD (void *argument) {
    while(1){
        // Bloqueo hasta recibir datos para pintar
        if(osMessageQueueGet(mid_messageQueueLCD, &LCD_mensaje, NULL, osWaitForever) == osOK){
            
            // 1. Borrar buffer anterior (RAM)
            LCD_ClearBuffer();
            
            // 2. Escribir texto en RAM
            LCD_StrigToBuffer(LCD_mensaje.Lin1);
            LCD_StrigToBuffer2(LCD_mensaje.Lin2);
            
            // 3. Dibujar elementos gráficos si se solicitan
            if (LCD_mensaje.barra > 0){
                LCD_SelectBar(LCD_mensaje.barra); // Barra de selección (Reloj)
            }
            
            if (LCD_mensaje.amplitud > 0) {
                LCD_DrawAudioBar(LCD_mensaje.amplitud); // Barra de progreso/volumen
            }

            // 4. Volcar RAM a Pantalla (SPI)
            LCD_update();
        }
    }
}

/**
  * @brief  Reset Hardware y configuración de GPIOs.
  */
void LCD_reset(void){
    SPIdrv->Initialize(NULL);
    SPIdrv->PowerControl(ARM_POWER_FULL);
    // Configuración SPI: Master, 8 bits, 20 Mbit/s
    SPIdrv->Control(ARM_SPI_MODE_MASTER | ARM_SPI_CPOL1_CPHA1 | ARM_SPI_MSB_LSB | ARM_SPI_DATA_BITS(8), 2000000);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    
    GPIO_InitStruct_LCD.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct_LCD.Pull = GPIO_PULLUP;
    GPIO_InitStruct_LCD.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    
    GPIO_InitStruct_LCD.Pin = GPIO_PIN_6; // RESET (PA6)
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct_LCD);
    
    GPIO_InitStruct_LCD.Pin = GPIO_PIN_14; // CS (PD14)
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct_LCD);
    
    GPIO_InitStruct_LCD.Pin = GPIO_PIN_13; // A0 (PF13)
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct_LCD);
    
    // Secuencia de pulso de Reset
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
    delay(1);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    delay(1000);
}

/**
  * @brief  Retardo bloqueante preciso usando TIM7.
  */
void delay(uint32_t n_microsegundos){
    HAL_TIM_Base_MspInit(&htim7);
    __HAL_RCC_TIM7_CLK_ENABLE();
    
    htim7.Instance = TIM7;
    htim7.Init.Prescaler = 83; // Ajustado para 1us por tick (aprox)
    htim7.Init.Period = n_microsegundos - 1;
    htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim7.Init.ClockDivision = 1;
    
    HAL_TIM_Base_Init(&htim7);
    HAL_TIM_Base_Start_IT(&htim7);
    
    while(__HAL_TIM_GetCounter(&htim7) < (n_microsegundos - 1));
    
    __HAL_TIM_CLEAR_IT(&htim7, TIM_IT_UPDATE);
    __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
    
    HAL_TIM_Base_Stop_IT(&htim7);
    HAL_TIM_Base_DeInit(&htim7);
}

/**
  * @brief  Envía un byte de DATOS (A0=1).
  */
void LCD_wr_data(unsigned char data){
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_RESET); // CS Low
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_13, GPIO_PIN_SET);   // A0 High
    
    SPIdrv->Send(&data,1);
    while(SPIdrv->GetStatus().busy);
    
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);   // CS High
}

/**
  * @brief  Envía un byte de COMANDO (A0=0).
  */
void LCD_wr_cmd(unsigned char cmd){
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_RESET); // CS Low
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_13, GPIO_PIN_RESET); // A0 Low
    
    SPIdrv->Send(&cmd,1);
    while(SPIdrv->GetStatus().busy);
    
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);   // CS High
}

/**
  * @brief  Configuración inicial del controlador LCD.
  */
void LCD_init(void){
    LCD_wr_cmd(0xAE); // Display OFF
    LCD_wr_cmd(0xA2); // Bias 1/9
    LCD_wr_cmd(0xA0); // ADC Select Normal
    LCD_wr_cmd(0xC8); // COM Output Normal
    LCD_wr_cmd(0x22); // Resistor Ratio
    LCD_wr_cmd(0x2F); // Power Control
    LCD_wr_cmd(0x40); // Start Line 0
    LCD_wr_cmd(0xAF); // Display ON
    LCD_wr_cmd(0x81); // Contrast Set
    LCD_wr_cmd(0x0F); // Contrast Value
    LCD_wr_cmd(0xA4); // Display All Points Normal
    LCD_wr_cmd(0xA6); // Display Normal
}

/**
  * @brief  Vuelca el buffer RAM completo al LCD.
  */
void LCD_update(void){
    int i;
    // Pagina 0
    LCD_wr_cmd(0x00); LCD_wr_cmd(0x10); LCD_wr_cmd(0xB0);
    for(i=0;i<128;i++) LCD_wr_data(buffer[i]);
    
    // Pagina 1
    LCD_wr_cmd(0x00); LCD_wr_cmd(0x10); LCD_wr_cmd(0xB1);
    for(i=128;i<256;i++) LCD_wr_data(buffer[i]);
    
    // Pagina 2
    LCD_wr_cmd(0x00); LCD_wr_cmd(0x10); LCD_wr_cmd(0xB2);
    for(i=256;i<384;i++) LCD_wr_data(buffer[i]);
    
    // Pagina 3
    LCD_wr_cmd(0x00); LCD_wr_cmd(0x10); LCD_wr_cmd(0xB3);
    for(i=384;i<512;i++) LCD_wr_data(buffer[i]);
}

/* --- Funciones de Dibujo en RAM --- */

void LCD_symbolToLocalBuffer_L1(uint8_t symbol){
    uint8_t i, value1, value2;
    uint16_t offset = 0;
    
    offset = 25*(symbol - ' ');
    
    for(i=0; i<12; i++){
        value1 = Arial12x12[offset + i*2 + 1];
        value2 = Arial12x12[offset + i*2 + 2];
        buffer[i + positionL1] = value1;
        buffer[i + 128 + positionL1] = value2;
    }
    positionL1 = positionL1 + Arial12x12[offset];
}

void LCD_symbolToLocalBuffer_L2(uint8_t symbol){
    uint8_t i, value1, value2;
    uint16_t offset = 0;
    
    offset = 25*(symbol - ' ');
    
    for(i=0; i<12; i++){
        value1 = Arial12x12[offset + i*2 + 1];
        value2 = Arial12x12[offset + i*2 + 2];
        buffer[i + 256 + positionL2] = value1;
        buffer[i + 384 + positionL2] = value2;
    }
    positionL2 = positionL2 + Arial12x12[offset];
}

void symbolToLocalBuffer(uint8_t line,uint8_t symbol){
    if(line==1){
        LCD_symbolToLocalBuffer_L1(symbol);            
    }else{
        LCD_symbolToLocalBuffer_L2(symbol);        
    }
}

void LCD_ClearBuffer(){
    int i;
    for( i=0; i<512;i++){
        buffer[i] = 0x00;
    }
    positionL1=0;
    positionL2=0;
}

void LCD_StrigToBuffer(char str[]){    
    int i;
    for ( i = 0; i < strlen(str); ++i) {
        LCD_symbolToLocalBuffer_L1(str[i]);
    }
}

void LCD_StrigToBuffer2(char str[]){    
    int i;
    for ( i = 0; i < strlen(str); ++i) {
        LCD_symbolToLocalBuffer_L2(str[i]);
    }
}

/**
  * @brief  Dibuja la barra de selección para el modo Configuración de Reloj.
  * Incluye calibración manual de posición X.
  */
void LCD_SelectBar(uint8_t posicionHorizontal){
    int i;
    
    // --- ZONA DE CALIBRACION ---
    // Ajuste fino para centrar la barra debajo de los números
    int16_t OFFSET_X = -20;
    // ---------------------------

    uint16_t pos_base = 0;

    switch(posicionHorizontal){
        case 1: // Horas
            pos_base = 34; 
            break;
        case 2: // Minutos
            pos_base = 48; 
            break;
        case 3: // Segundos
            pos_base = 62; 
            break;
        default: 
            return;
    }
    
    // Dibuja una línea de 9 píxeles de ancho
    for(i=0; i<9; i++){
        int index = 384 + 10 + pos_base + OFFSET_X + i;
        
        // Protección de límites del buffer (0-511)
        if (index >= 384 && index < 512) {
            // OR lógico para añadir la barra sin borrar lo que haya (letras)
            buffer[index] |= 0x04; 
        }
    }
}

/**
  * @brief  Dibuja una barra de progreso pixel a pixel (0-128 pixeles).
  * @param  amplitude: Valor de 0 a 100 indicando el llenado.
  */
void LCD_DrawAudioBar(uint8_t amplitude) {
    int i;
    // 1. Convertimos porcentaje (0-100) a anchura en píxeles (0-128)
    uint8_t width = (amplitude * 128) / 100;
    
    if (width > 128) width = 128;

    // 2. Rellenamos la parte inferior del buffer (Página 3)
    for (i = 0; i < 128; i++) {
        if (i < width) {
            // Pintamos el bit más bajo (0x80) para hacer una barra fina en la base
            buffer[384 + i] |= 0x80; 
        } else {
            // Borramos el resto para que la barra decrezca si baja el valor
            buffer[384 + i] &= ~0x80;
        }
    }
}

