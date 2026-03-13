#ifndef __RTC_H
#define __RTC_H

#include "stm32f4xx_hal.h"

/* --- CONFIGURACIÓN DE FECHA Y HORA POR DEFECTO --- */
// Estos son los valores con los que arrancará mi reloj la primera vez que encienda la placa.
#define PRACTICA_HORA    0x09
#define PRACTICA_MIN     0x00
#define PRACTICA_SEG     0x00
#define PRACTICA_DIA     0x06
#define PRACTICA_MES   RTC_MONTH_MARCH
#define PRACTICA_YEAR    0x26  /* Año 2026 */

/* --- TIPOS DE DATOS PERSONALIZADOS --- */
// Creo una enumeración para que mi código sea más legible al elegir la alarma
typedef enum {
    ALARMA_DESACTIVADA = 0,
    ALARMA_CADA_10_SEG,
    ALARMA_CADA_1_MIN,
    ALARMA_CADA_5_MIN
} RTC_PeriodoAlarma_t;

/* --- VARIABLES GLOBALES (EXTERNAS) --- */
// Declaro estas variables como 'extern' para que otros archivos (como mi servidor web) 
// sepan que existen y puedan leerlas o modificarlas.
extern RTC_PeriodoAlarma_t periodo_actual;
extern char* sntp_server_list[];
extern uint8_t sntp_server_index; // Selecciona el servidor (0 o 1)

// Bandera para la alarma (se activa en la interrupción en stm32f4xx_it.c)
extern uint8_t alarma_activada;

/* --- DECLARACIÓN DE MIS FUNCIONES --- */
// Funciones para el Apartado 1, 2 y 3
void RTC_Init(void);
void RTC_ObtenerHoraFecha(char *timeStr, char *dateStr);
void RTC_PonerAlarma_CadaMinuto(void); // (Función legacy de pruebas)

// Funciones para el Apartado 4 (SNTP)
void RTC_ActualizarDesdeUnix(uint32_t segundos_unix);
void RTC_Reset_A_2000(void);

// Funciones para el Apartado 5 (Alarma dinámica)
void RTC_ConfigurarAlarma(RTC_PeriodoAlarma_t periodo);
void RTC_DesactivarAlarma(void);

void RTC_SetTimeComponents(uint8_t hours, uint8_t minutes, uint8_t seconds);


void RTC_SetHoraManual(uint8_t horas, uint8_t minutos, uint8_t segundos);
void RTC_SetFechaManual(uint8_t dia, uint8_t mes, uint16_t anio);

#endif
