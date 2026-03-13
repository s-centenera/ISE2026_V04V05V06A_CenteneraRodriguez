/**
 ******************************************************************************
 * @file    HTTP_Server.c
 * @author  Jose Vargas Gonzaga
 * @brief   Implementación del servidor HTTP
 * Contiene la lógica principal para atender las peticiones de la
 * red y servir las páginas estáticas y dinámicas. Este módulo
 * coordina las llamadas a CGI, gestión de sockets y mantenimiento
 * de la conexión con el hardware (ADC, RTC, etc.).
 ******************************************************************************
 */
    
#include <stdio.h>
#include "string.h"
#include <stdlib.h>

#include "rl_net.h"                     // Keil.MDK-Pro::Network:CORE
#include "stm32f4xx_hal.h"              // Keil::Device:STM32Cube HAL:Common
#include "Board_LED.h"                  // ::Board Support:LED
#include "main.h"

// Cabeceras de mis librerias
#include "lcd.h"
#include "adc.h"
#include "rtc.h"

/* --- CONFIGURACIÓN DEL HILO PRINCIPAL (app_main) --- */
// Reservo una pila estática para el hilo del servidor. 
// Hacerlo estático me evita usar 'malloc' y previene problemas de fragmentación de memoria.
#define APP_MAIN_STK_SZ (2048U)        
uint64_t app_main_stk[APP_MAIN_STK_SZ / 8];
const osThreadAttr_t app_main_attr = {
  .stack_mem  = &app_main_stk[0],
  .stack_size = sizeof(app_main_stk)
};

/* --- VARIABLES GLOBALES COMPARTIDAS CON LA WEB --- */
// Estas variables me permiten comunicar la interfaz web con el hardware
bool LEDrun;                                // Controla si permito el parpadeo de LEDs desde el navegador.
char lcd_text[2][20+1];                     // Array 2D para guardar las dos líneas de texto que me mandan a la pantalla LCD.
uint8_t iniciar_parpadeo_sntp = 0;          // Bandera que levanto cuando recibo la hora de internet para disparar el LED rojo.

/* --- VARIABLES PARA EL APARTADO 5 (Configuración Web) --- */
uint8_t sntp_server_index = 0;              // Índice para seleccionar cuál de los dos servidores NTP estoy utilizando (0 o 1).
const char* sntp_servers[] = {"Google NTP (216.239.35.0)", "Cloudflare NTP (162.159.200.1)"}; // Nombres para mostrar en la web.
const char* sntp_ips[] = {"216.239.35.0", "162.159.200.1"}; // IPs reales a las que me conectaré.

RTC_PeriodoAlarma_t periodo_seleccionado = ALARMA_CADA_1_MIN; // Periodo por defecto de mi alarma.
uint8_t alarma_habilitada_web = 1;          // Bandera para saber si el usuario ha activado el checkbox de la alarma en la web.

/* --- RECURSOS DE PERIFÉRICOS Y RED --- */
ADC_HandleTypeDef hadc1;                    // Estructura de control (Handler) para mi conversor Analógico-Digital.
static NET_ADDR server_addr;                // Estructura estática para guardar la IP y puerto del servidor SNTP sin perderla en memoria.

/* --- MIS RECURSOS DE TIMERS DEL RTOS --- */
// Guardo el identificador (ID) de mis timers para poder arrancarlos y pararlos a placer.
osTimerId_t timer_led_rojo;
osTimerId_t timer_led_verde;

// Funciones de reseteo que he implementado en stm32f4xx_it.c para limpiar la cuenta de parpadeos
extern void ResetPulsosRojo(void);
extern void ResetPulsosVerde(void);

/* --- DECLARACIÓN DE LAS FUNCIONES EXTERNAS --- */
extern void TimerRojo_Callback (void *argument);
extern void TimerVerde_Callback (void *argument);

/* Thread declarations */
void Time_Thread (void *argument);
void Sincronizacion_SNTP_Completada(uint32_t segundos_unix, uint32_t fraccion);
__NO_RETURN void app_main (void *arg);


/**
 * @brief Lee el valor del potenciómetro a través del ADC (Conversor Analógico a Digital).
 * @param ch : Canal del ADC. Lo mantengo por compatibilidad con firmas estándar, aunque internamente fuerzo el canal 10.
 * @return uint16_t : Retorno el valor convertido (entre 0 y 4095, ya que es un ADC de 12 bits).
 */
uint16_t AD_in (uint32_t ch) {
  ADC_ChannelConfTypeDef sConfig = {0};
  
  sConfig.Channel = ADC_CHANNEL_10; // Fijo el canal 10, que corresponde a mi potenciómetro físico.
  sConfig.Rank = 1;    
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;     // Indico cuánto tiempo el ADC "mira" la señal eléctrica antes de convertir.
  
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
  HAL_ADC_Start(&hadc1); // Lanzo la conversión
  
  // Espero hasta 10ms a que termine de convertir. Si va bien, devuelvo el valor.
  if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
  }
  return 0; // Si falla, devuelvo 0 por seguridad.
}


// Funciones vacías requeridas por la librería de Keil que no necesito implementar
uint8_t get_button (void) { return 0; }
void netDHCP_Notify (uint32_t if_num, uint8_t option, const uint8_t *val, uint32_t len) { }


/**
 * @brief Hilo principal de gestión del reloj y tareas periódicas.
 * @param argument : Puntero genérico del RTOS (no lo uso).
 * Este hilo es el "corazón" de mi lógica temporal. Duerme la mayor parte del tiempo
 * para no saturar la CPU y delega el parpadeo de los LEDs en los timers del sistema operativo.
 */
void Time_Thread (void *argument) {
  MSGQUEUE_OBJ_LCD_t msg_lcd;      // Estructura donde prepararé los mensajes para la pantalla.
  char t_buffer[20], d_buffer[20]; // Buffers locales donde guardaré la hora y la fecha extraídas del hardware.
  
  uint32_t contador_sntp_segundos = 0; // Lleva la cuenta de los segundos desde el arranque para saber cuándo pedir la hora a internet.
  uint8_t  divisor_100ms = 0;          // Lo usaré para contar de 10 en 10 y saber cuándo ha pasado 1 segundo real.

  // 1. Inicializo mi hardware del reloj
  RTC_Init();
  RTC_ConfigurarAlarma(periodo_seleccionado); // Inicialmente ALARMA_CADA_1_MIN

  // 2. Configuro el pin físico del botón azul de la placa (PC13)
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  // 3. Configuro los pines físicos de mis LEDs (Verde=PB0, Rojo=PB14)
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  while (1) {
    // Duermo mi hilo exactamente 100ms. Así no bloqueo la CPU y permito que la red y la web funcionen fluidas.
    osDelay(100); 

    // --- LECTURA DEL BOTÓN AZUL ---
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
        RTC_Reset_A_2000();         // Fuerzo la hora al año 2000 para forzar y probar la desincronización
        contador_sntp_segundos = 0; // Reseteo mi contador para que vuelva a pedir la hora a los 5 segundos.
    }

    // --- GESTIÓN DE EVENTOS DE HARDWARE / RED ---
    
    // Si mi interrupción en stm32f4xx_it.c me avisa de que saltó la alarma física del RTC...
    if (alarma_activada == 1) {
        alarma_activada = 0; // Bajo la bandera de inmediato para no volver a entrar en la próxima iteración
        
        // Compruebo que el usuario no me haya apagado la alarma desde la web
        if (alarma_habilitada_web == 1) {
            // Aplico mi mecanismo de seguridad: Paro, reseteo el contador de parpadeos y arranco limpio
            osTimerStop(timer_led_verde); 
            ResetPulsosVerde();           
            osTimerStart(timer_led_verde, 200U); // Lanzo el timer para que parpadee cada 200ms en segundo plano
        }
    }

    // Si mi callback de red me avisa de que me ha llegado la nueva hora de internet...
    if (iniciar_parpadeo_sntp == 1) {
        iniciar_parpadeo_sntp = 0; // Bajo mi bandera
        
        // Aplico mi mecanismo de seguridad: Paro, reseteo y arranco limpio
        osTimerStop(timer_led_rojo); 
        ResetPulsosRojo();         
        osTimerStart(timer_led_rojo, 100U); // Lanzo el timer a 5Hz (100ms)
    }

    // --- TAREAS CADA 1 SEGUNDO ---
    divisor_100ms++;
    // Cada 10 iteraciones de 100 ms (es decir, 1 segundo exacto) se ejecuta este bloque:
    if (divisor_100ms >= 10) {
        divisor_100ms = 0; // Reinicio mi divisor
        
        // Actualizo mi pantalla mandando la hora a la cola de mensajes
        RTC_ObtenerHoraFecha(t_buffer, d_buffer);
        memset(&msg_lcd, 0, sizeof(msg_lcd));
        strcpy(msg_lcd.Lin1, t_buffer); 
        strcpy(msg_lcd.Lin2, d_buffer); 
        osMessageQueuePut(mid_messageQueueLCD, &msg_lcd, 0, 0); // IPC para no bloquear la pantalla
        
        // Gestiono peticiones de internet (SNTP)
        contador_sntp_segundos++;
        
        // Si el contador llega a 5 s, o ya ha pasado el segundo 5 y cada 180 s adicionales (3 min),
        // disparo una petición a la red pidiendo la hora.
        if (contador_sntp_segundos == 5 || (contador_sntp_segundos > 5 && (contador_sntp_segundos % 180 == 0))) {
            server_addr.addr_type = NET_ADDR_IP4;
            server_addr.port = 0; // Puerto 0 indica que use el puerto NTP por defecto (123)
            
            // Convierto la IP que tengo seleccionada en formato texto a formato binario
            netIP_aton(sntp_ips[sntp_server_index], NET_ADDR_IP4, server_addr.addr);
            
            // Lanzo la petición de forma asíncrona pasándole la IP y mi función Callback
            netSNTPc_GetTime(&server_addr, Sincronizacion_SNTP_Completada);
        }
    }
  }
}



/**
 * @brief Callback que la librería de red llama automáticamente cuando recibe respuesta del servidor SNTP.
 * @param segundos_unix : Los segundos transcurridos desde 1900 que me manda el servidor.
 * @param fraccion      : Fracción de segundo (no la necesito para esta práctica).
 */
void Sincronizacion_SNTP_Completada(uint32_t segundos_unix, uint32_t fraccion) {
    if (segundos_unix > 0) {
        // Le paso los segundos a mi librería para que calcule la fecha y actualice el hardware
        RTC_ActualizarDesdeUnix(segundos_unix);
        
        // Levanto la bandera para que mi hilo principal dispare el Timer rojo en su próxima iteración
        iniciar_parpadeo_sntp = 1; 
    }
}

/*----------------------------------------------------------------------------
  Main Thread 'main': Run Network
 *---------------------------------------------------------------------------*/
/**
 * @brief Hilo de arranque de mi aplicación.
 * @param arg : Argumentos del hilo (no se usan).
 * Aquí inicializo todos los periféricos, configuro la red y creo mis timers y mi hilo principal.
 */
__NO_RETURN void app_main (void *arg) {
  (void)arg;

  // 1. Inicialización de Periféricos (Hardware)
  LED_Initialize();
  Init_ThLCD(); 
    
  ADC1_pins_F429ZI_config(); 
  ADC_Init_Single_Conversion(&hadc1, ADC1); 
    
  // 2. Inicialización del Stack de Red de Keil
  netInitialize (); 

  // 3. Timers del Sistema Operativo para los LEDs.
  timer_led_rojo = osTimerNew(TimerRojo_Callback, osTimerPeriodic, NULL, NULL);
  timer_led_verde = osTimerNew(TimerVerde_Callback, osTimerPeriodic, NULL, NULL);

  // 4. Creo y arranco mi hilo del tiempo
  osThreadNew (Time_Thread, NULL, NULL); 
  
  // 5. Destruyo este hilo inicial (app_main) porque ya no lo necesito, liberando recursos.
  osThreadExit();
}
