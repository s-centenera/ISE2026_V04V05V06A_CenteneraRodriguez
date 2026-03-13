/**
  ******************************************************************************
  * @file    Templates/Src/stm32f4xx_it.c 
  * @author  MCD Application Team
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  *
  * @note    modified by ARM
  *          The modifications allow to use this file as User Code Template
  *          within the Device Family Pack.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT(c) 2017 STMicroelectronics</center></h2>
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
#include "rtc.h"
#include "cmsis_os2.h" // Necesario para reconocer las funciones del RTOS

/* Private variables ---------------------------------------------------------*/
extern RTC_HandleTypeDef hrtc;

// Traigo de forma externa los timers que he creado en mi HTTP_Server.c
// para poder detenerlos desde aquí cuando el parpadeo termine.
extern osTimerId_t timer_led_rojo;
extern osTimerId_t timer_led_verde;

/* --- VARIABLES GLOBALES PARA LOS PULSOS --- */
uint32_t pulsos_rojo = 0;
uint32_t pulsos_verde = 0;

/* ---  FUNCIONES DE RESETEO --- */
// Estas funciones me permiten poner a cero la cuenta desde otros archivos
void ResetPulsosRojo(void) { 
    pulsos_rojo = 0; 
}

void ResetPulsosVerde(void) { 
    pulsos_verde = 0; 
}

/******************************************************************************/
/* Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
 * @brief Manejador físico de la interrupción de la Alarma del RTC.
 * Cuando el hardware detecta la coincidencia de tiempo, salta aquí.
 * Yo llamo a la función de la HAL para que limpie los flags y ejecute mi callback.
 */
void RTC_Alarm_IRQHandler(void) {
  HAL_RTC_AlarmIRQHandler(&hrtc);
}

/**
 * @brief Callback de la HAL que se ejecuta cuando salta la alarma del hardware.
 * Aquí aplico mis filtros para saber si realmente debo activar la alarma según
 * lo que he configurado en la web (10 seg o 5 min).
 */
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc) {
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    // Obligatorio leer hora y luego fecha para que la librería HAL funcione bien
    HAL_RTC_GetTime(hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(hrtc, &sDate, RTC_FORMAT_BIN);

    // Si el usuario me ha pedido 10 segundos, filtro matemáticamente
    if (periodo_actual == ALARMA_CADA_10_SEG) {
        if (sTime.Seconds % 10 != 0) return; 
    }
    // Si el usuario me ha pedido 5 minutos
    else if (periodo_actual == ALARMA_CADA_5_MIN) {
        if (sTime.Minutes % 5 != 0 || sTime.Seconds != 0) return; 
    }

    // Si supero los filtros, le digo a mi hilo principal que active el timer visual
    alarma_activada = 1; 
}

/**
 * @brief Callback para el parpadeo del LED ROJO (Evento SNTP completado)
 * El RTOS llamará a esta función automáticamente cada 100ms porque así lo he
 * configurado al crear el timer.
 */
void TimerRojo_Callback (void *argument) {

    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_14);
    pulsos_rojo++;
    
    if (pulsos_rojo >= 40) {
        pulsos_rojo = 0; // Reseteo mi contador para la próxima vez
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET); // Apago el LED por seguridad
        osTimerStop(timer_led_rojo); // Le digo al Sistema Operativo que detenga este timer
    }
}

/**
 * @brief Callback para el parpadeo del LED VERDE (Evento de Alarma RTC)
 * El RTOS llamará a esta función automáticamente cada 200ms.
 */
void TimerVerde_Callback (void *argument) {

    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
    pulsos_verde++;
    
    if (pulsos_verde >= 25) {
        pulsos_verde = 0; // Lo preparo para la siguiente alarma
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); 
        osTimerStop(timer_led_verde); // Detengo la alarma visual
    }
}

// ... (Resto de los handlers como NMI_Handler, HardFault_Handler, etc., quedan igual) ...


/**
  * @brief   This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  /* Go to infinite loop when Hard Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
#ifndef RTE_CMSIS_RTOS2_RTX5
void SVC_Handler(void)
{
}
#endif

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
#ifndef RTE_CMSIS_RTOS2_RTX5
void PendSV_Handler(void)
{
}
#endif

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
#ifndef RTE_CMSIS_RTOS2_RTX5
void SysTick_Handler(void)
{
  HAL_IncTick();
}
#endif

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/


/**
  * @}
  */ 

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
