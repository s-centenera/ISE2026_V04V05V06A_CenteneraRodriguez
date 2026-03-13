/**
 ******************************************************************************
 * @file    rtc.c
 * @author  Jose Vargas Gonzaga
 * @brief   Implementación del módulo Real-Time Clock (RTC) para la Práctica 2.
 * Este archivo contiene las funciones para inicializar el reloj interno,
 * leer la hora/fecha y configurar alarmas periódicas por interrupción.
 * * @details 
 * - Hardware: STM32F429ZI (NUCLEO-F429ZI)
 * - Fuente de reloj: LSI (Low Speed Internal) aprox. 32kHz.
 * - Formato: 24 Horas.
 * - Sincronización: Requiere lectura de Hora seguido de Fecha para desbloquear
 * los registros "shadow" del RTC.
 ******************************************************************************
 */

#include "rtc.h"
#include <stdio.h>
#include <time.h>       // Libreria estándar de C para el tiempo

/* --- VARIABLES GLOBALES DEL MÓDULO --- */
RTC_HandleTypeDef hrtc;                 // Estructura principal (Handler) para controlar el RTC.
uint8_t alarma_activada = 0;            // Bandera que levanto cuando la alarma física salta.
RTC_PeriodoAlarma_t periodo_actual = ALARMA_DESACTIVADA; // Guardo el estado actual de la alarma.

/**
 * @brief Inicialización de bajo nivel (Hardware) del RTC.
 * @param hrtc : Puntero al handler del RTC. La librería HAL llama a esta función automáticamente.
 */
void HAL_RTC_MspInit(RTC_HandleTypeDef* hrtc) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  // 1. Habilito el reloj del controlador de energía y doy acceso a los registros de Backup.
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  // 2. Configuro y enciendo el LSI (Low Speed Internal).
  // Uso este oscilador interno de ~32kHz porque no necesito soldar un cristal externo en la placa.
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI;  
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;  
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  // Selecciono el LSI recién encendido como la fuente de reloj para mi RTC.
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

  // 3. Habilito el reloj del periférico RTC para que empiece a funcionar.
  __HAL_RCC_RTC_ENABLE();

  // 4. Configuro el canal de interrupciones para la Alarma.
  // Le doy prioridad 5 (una prioridad media) y enciendo el canal en el controlador NVIC.
  HAL_NVIC_SetPriority(RTC_Alarm_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(RTC_Alarm_IRQn);
}

/**
 * @brief Inicializa la configuración principal del RTC y fija la fecha/hora de la práctica.
 */
void RTC_Init(void) {
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;   // Quiero trabajar en formato de 24 horas.

  // --- MATEMÁTICAS DEL RELOJ ---
  // El oscilador LSI va a 32768 Hz. Para que el reloj cuente segundos reales (1 Hz), 
  // tengo que dividir esa frecuencia. La fórmula del hardware es: 
  // Frecuencia final = LSI / ((AsynchPrediv + 1) * (SynchPrediv + 1))
  // Es decir: 32768 / ((127 + 1) * (255 + 1)) = 32768 / (128 * 256) = 32768 / 32768 = 1 Hz exacto.
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  
  // Deshabilito la salida del pulso hacia un pin físico, ya que no voy a calibrarlo con osciloscopio.
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;    
  
  // Inicializo el hardware. Esta función llamará automáticamente a HAL_RTC_MspInit().
  HAL_RTC_Init(&hrtc);  

  // --- CONFIGURACIÓN DE LA HORA INICIAL ---
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  // Preparo la hora
  sTime.Hours = PRACTICA_HORA;
  sTime.Minutes = PRACTICA_MIN;
  sTime.Seconds = PRACTICA_SEG;
  // Guardo en formato BCD (Binary-Coded Decimal) porque es como los registros del STM32 guardan los datos físicamente.
  HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD);       

  // Preparo la fecha
  sDate.WeekDay = RTC_WEEKDAY_FRIDAY;
  sDate.Month = PRACTICA_MES;
  sDate.Date = PRACTICA_DIA;
  sDate.Year = PRACTICA_YEAR;
  HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD);       
}

/**
 * @brief Extrae la hora y la fecha actuales del hardware y las formatea como texto.
 * @param timeStr : Buffer donde escribiré la hora (ej: "09:00:00").
 * @param dateStr : Buffer donde escribiré la fecha (ej: "06/03/2026").
 */
void RTC_ObtenerHoraFecha(char *timeStr, char *dateStr) {
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;

  // ¡IMPORTANTE! El manual de ST obliga a leer PRIMERO el tiempo y LUEGO la fecha.
  // Si no se hace en este orden, los "Shadow Registers" (registros sombra) del hardware se bloquean.
  // Pido la hora en FORMAT_BIN para que me devuelva números naturales (0-59) y sea fácil imprimirlos.
  HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

  // Formateo los números rellenando con ceros a la izquierda (%02d) si es necesario.
  sprintf(timeStr, "%02d:%02d:%02d", sTime.Hours, sTime.Minutes, sTime.Seconds);
  sprintf(dateStr, "%02d/%02d/20%02d", sDate.Date, sDate.Month, sDate.Year);
}

/**
 * @brief Configura la alarma para que salte en el segundo 00 de cada minuto (Función legacy).
 */
void RTC_PonerAlarma_CadaMinuto(void) {
  RTC_AlarmTypeDef sAlarm = {0};

  sAlarm.AlarmTime.Seconds = 0x00;  // Le indico que me interesa el segundo '00'.
  
  // Enmascaro (ignoro) las horas, los minutos y el día de la semana. 
  // Esto significa: "No me importa qué hora sea, avísame siempre que los segundos sean 00".
  sAlarm.AlarmMask = RTC_ALARMMASK_HOURS | RTC_ALARMMASK_MINUTES | RTC_ALARMMASK_DATEWEEKDAY;
  sAlarm.Alarm = RTC_ALARM_A;   
  HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD);
}

/**
 * @brief Resetea el RTC al 1 de Enero del 2000 a las 00:00:00.
 * La utilizo al pulsar el botón azul para simular un desajuste y probar visualmente
 * que mi cliente SNTP funciona correctamente y recupera la hora actual.
 */
void RTC_Reset_A_2000(void) {
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  // Pongo la hora a 00:00:00
  sTime.Hours = 0;
  sTime.Minutes = 0;
  sTime.Seconds = 0;
  HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

  // Pongo la fecha a Sábado, 1 de Enero del 2000. 
  // Para el hardware, el año 2000 se representa con un '0'.
  sDate.WeekDay = RTC_WEEKDAY_SATURDAY; 
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 1;
  sDate.Year = 0; 
  HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

/**
 * @brief Programa la alarma del hardware según el periodo elegido en la web.
 * @param periodo : Enumeración con el periodo deseado (10s, 1m, 5m o desactivado).
 */
void RTC_ConfigurarAlarma(RTC_PeriodoAlarma_t periodo) {
    RTC_AlarmTypeDef sAlarm = {0};
    periodo_actual = periodo; // Guardo la elección en mi variable global.

    // Si me piden apagarla, desactivo directamente el hardware de la Alarma A.
    if (periodo == ALARMA_DESACTIVADA) {
        HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
        return;
    }

    sAlarm.Alarm = RTC_ALARM_A;

    // --- TRUCO MAESTRO PARA ALARMAS COMPLEJAS ---
    // El hardware no sabe contar "cada 5 minutos" por sí solo sin desincronizarse con los cambios de hora.
    // Por eso, para 10 segundos o 5 minutos, enmascaro TODOS los campos de tiempo.
    // Esto provoca que el hardware dispare una interrupción incondicional *cada vez que el segundo cambia*.
    // Luego, en mi archivo stm32f4xx_it.c, decido si hago caso a esa interrupción o la ignoro.
    if (periodo == ALARMA_CADA_10_SEG || periodo == ALARMA_CADA_5_MIN) {
        sAlarm.AlarmMask = RTC_ALARMMASK_HOURS | RTC_ALARMMASK_MINUTES | RTC_ALARMMASK_SECONDS | RTC_ALARMMASK_DATEWEEKDAY;
    } 
    // Para 1 minuto exacto, el hardware sí puede gestionarlo solo:
    // Le pido que salte cuando los segundos sean 0, ignorando los minutos y horas.
    else if (periodo == ALARMA_CADA_1_MIN) {
        sAlarm.AlarmTime.Seconds = 0;
        sAlarm.AlarmMask = RTC_ALARMMASK_HOURS | RTC_ALARMMASK_MINUTES | RTC_ALARMMASK_DATEWEEKDAY;
    }

    // Aplico los cambios al hardware, habilitando la interrupción (_IT).
    HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BIN);
}

/**
 * @brief Convierte los segundos brutos recibidos desde el servidor SNTP en una fecha y hora 
 * legibles y las guarda directamente en el hardware del reloj.
 * @param segundos_unix : Valor de 32 bits con los segundos transcurridos.
 */
void RTC_ActualizarDesdeUnix(uint32_t segundos_unix) {
    // 1. CORRECCIÓN DE LA ERA NTP A LA ERA UNIX (Efecto 1900)
    // El protocolo SNTP (NTP) cuenta los segundos desde el 1 de Enero de 1900.
    // El estándar UNIX los cuenta desde el 1 de Enero de 1970. 
    // Como han pasado 70 años entre ambas fechas, le resto la diferencia en segundos (2208988800).
    if (segundos_unix > 2208988800UL) {
        segundos_unix -= 2208988800UL;
    }

    // 2. CORRECCIÓN DE HUSO HORARIO
    // Estando en España Peninsular (UTC+1 en horario de invierno), le sumo 3600 segundos (1 hora).
    uint32_t tiempo_local = segundos_unix + 3600;

    // --- 3. EXTRACCIÓN DE LA HORA ---
    // Saco el módulo de los segundos que tiene un día (86400) para quedarme solo con el tiempo transcurrido "hoy".
    uint32_t segundos_hoy = tiempo_local % 86400; 
    
    RTC_TimeTypeDef sTime = {0};
    sTime.Hours   = segundos_hoy / 3600;            // Divido entre los segundos de una hora
    sTime.Minutes = (segundos_hoy % 3600) / 60;     // Me quedo con el resto de la hora y divido entre los segundos de un minuto
    sTime.Seconds = segundos_hoy % 60;              // El resto son los segundos finales
    
    // Le inyecto primero la hora al hardware
    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

    // --- 4. EXTRACCIÓN DE LA FECHA (CÁLCULO MATEMÁTICO PURO) ---
    // Divido todo el tiempo histórico entre los segundos de un día para saber cuántos días han pasado desde 1970.
    uint32_t dias_totales = tiempo_local / 86400;
    uint32_t anio = 1970;

    // A) CÁLCULO DEL AÑO: 
    // Voy restando a mi "saco" de días totales los días que tiene el año actual, comprobando siempre si es bisiesto.
    while (1) {
        // ¿Es múltiplo de 4, pero no de 100 (salvo que sea múltiplo de 400)? -> Bisiesto (366 días)
        uint32_t dias_este_anio = ((anio % 4 == 0 && anio % 100 != 0) || (anio % 400 == 0)) ? 366 : 365;
        if (dias_totales >= dias_este_anio) {
            dias_totales -= dias_este_anio;
            anio++;
        } else {
            break; // Si me quedan menos días que un año completo, he llegado al año actual.
        }
    }

    // B) CÁLCULO DEL MES:
    uint8_t mes = 1;
    uint32_t dias_por_mes[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; // Array normal
    
    // Si he detectado que el año actual en el que he parado es bisiesto, corrijo febrero a 29 días.
    if ((anio % 4 == 0 && anio % 100 != 0) || (anio % 400 == 0)) {
        dias_por_mes[1] = 29; 
    }

    // Voy restando días mes a mes hasta no poder más.
    for (int i = 0; i < 12; i++) {
        if (dias_totales >= dias_por_mes[i]) {
            dias_totales -= dias_por_mes[i];
            mes++;
        } else {
            break; // He llegado al mes actual
        }
    }

    // --- 5. ESCRITURA FINAL DE LA FECHA EN EL HARDWARE ---
    RTC_DateTypeDef sDate = {0};
    
    // Los días que han sobrado en mi "saco" son los días del mes actual (+1 porque no hay día cero).
    uint8_t dia = dias_totales + 1;

    // Calculo matemáticamente el Día de la semana. 
    // Históricamente sabemos que el 1 de Enero de 1970 fue Jueves (Día 4 en la convención).
    uint8_t dia_semana = (( (tiempo_local / 86400) + 3 ) % 7 ) + 1; // 1=Lunes, 7=Domingo

    sDate.WeekDay = dia_semana;
    sDate.Month   = mes;
    sDate.Date    = dia;
    sDate.Year    = anio - 2000; // Al hardware solo le caben 2 dígitos para el año, así que le resto la centena del 2000.
    
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

/**
 * @brief Establece la hora del RTC a partir de componentes (hora, minuto, segundo).
 *        Realiza la lectura previa necesaria y escribe la nueva hora en formato BIN.
 */
void RTC_SetTimeComponents(uint8_t hours, uint8_t minutes, uint8_t seconds) {
    RTC_TimeTypeDef sTime = {0};

    /* Validación básica por seguridad */
    if (hours > 23) hours = 23;
    if (minutes > 59) minutes = 59;
    if (seconds > 59) seconds = 59;

    /* Preparo la estructura y escribo la hora en el hardware */
    sTime.Hours = hours;
    sTime.Minutes = minutes;
    sTime.Seconds = seconds;

    /* Uso formato BIN para facilidad */
    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
}


/**
 * @brief Configura la hora del RTC de forma manual.
 * @param horas, minutos, segundos: Valores a configurar.
 */
void RTC_SetHoraManual(uint8_t horas, uint8_t minutos, uint8_t segundos) {
    RTC_TimeTypeDef sTime = {0};
    
    // Cargo los valores en la estructura
    sTime.Hours = horas;
    sTime.Minutes = minutos;
    sTime.Seconds = segundos;
    
    // Guardo en el hardware en formato binario
    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
}

/**
 * @brief Configura la fecha del RTC de forma manual.
 * @param dia, mes: Día y mes a configurar.
 * @param anio: Año completo (ej. 2026).
 */
void RTC_SetFechaManual(uint8_t dia, uint8_t mes, uint16_t anio) {
    RTC_DateTypeDef sDate = {0};
    
    sDate.Date = dia;
    sDate.Month = mes;
    
    // Como el navegador me manda el año completo (ej. 2026), 
    // le resto 2000 porque a mi hardware solo le caben 2 dígitos (26).
    if (anio >= 2000) {
        sDate.Year = anio - 2000;
    } else {
        sDate.Year = 0;
    }
    
    // Para no complicar la matemática del día de la semana, pongo un día por defecto (Lunes).
    // El microcontrolador lo requiere, pero para pintar en pantalla no lo solemos usar.
    sDate.WeekDay = RTC_WEEKDAY_MONDAY; 
    
    // Guardo en el hardware (Siempre después de haber guardado la hora)
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}



