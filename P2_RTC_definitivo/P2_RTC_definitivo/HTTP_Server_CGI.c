/**
  ******************************************************************************
  * @file    HTTP_Server_CGI.c
  * @author  Jose Vargas Gonzaga
  * @brief   Módulo puente entre la Web y el Hardware (CGI).
  *          Aquí se procesan los formularios enviados por el usuario y se
  *          preparan los datos dinámicos (Hora, Voltaje) para mostrar en web.
  ******************************************************************************
  */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cmsis_os2.h"
#include "rl_net.h"
#include "rtc.h"  				// Necesario para leer la hora
#include "lcd.h"          // Necesario para enviar mensajes al LCD
#include "Board_LED.h"    // ::Board Support:LED


#if      defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
#pragma  clang diagnostic push
#pragma  clang diagnostic ignored "-Wformat-nonliteral"
#endif

// Variables externas
extern uint16_t AD_in (uint32_t ch);
extern bool LEDrun;
extern char lcd_text[2][20+1];
extern uint8_t  get_button (void);

/* --- VARIABLES EXTERNAS DEL APARTADO 5 --- */
extern uint8_t sntp_server_index;
extern RTC_PeriodoAlarma_t periodo_seleccionado;
extern uint8_t alarma_habilitada_web;
extern const char* sntp_servers[];

// Variables Locales.
static uint8_t P2;		// Variable local para guardar el estado de los 8 LEDs de la placa mbed
static uint8_t ip_addr[NET_ADDR_IP6_LEN];
static char    ip_string[40];

// My structure of CGI status variable.
typedef struct {
  uint8_t idx;
  uint8_t unused[3];
} MY_BUF;
#define MYBUF(p)        ((MY_BUF *)p)

/*----------------------------------------------------------------------------
  1. netCGI_ProcessQuery: Procesa datos enviados por la URL (GET)
  Su único propósito en mi aplicación es ajustar la configuración de
  red cuando el usuario la edita en la web de configuración (network.cgi).
 *---------------------------------------------------------------------------*/
/**
 * @brief  Procesa la cadena de consulta (Query String) enviada por el navegador.
 * @param  qstr : Puntero a la cadena de texto que va justo después del '?' en la URL.
 * Por ejemplo: "i4=192.168.1.10&m4=255.255.255.0"
 */
void netCGI_ProcessQuery (const char *qstr) {
  
  // --- VARIABLES LOCALES ---
  
  // 'opt' guarda qué campo de la interfaz de red voy a configurar (IP, máscara, gateway…).
  // La inicializo a un valor neutro (MAC_Address) por si acaso el parámetro no se reconoce,
  // así evito modificar algo por accidente.
  netIF_Option opt = netIF_OptionMAC_Address;
  
  // 'typ' almacenará el tipo de dirección que voy a parsear (IPv4 o IPv6).
  int16_t      typ = 0;       
  
  // 'var' es mi buffer temporal. Aquí la función netCGI_GetEnvVar copiará 
  // un parámetro completo cada vez que pase por el bucle (ej. "i4=192.168.1.10").
  char var[40];       

  // Bucle principal: extraigo los parámetros uno a uno hasta que no queden más.
  do {
    // Extraigo un parámetro de 'qstr' y lo guardo en 'var'. 
    // La función me devuelve el puntero avanzado hacia el siguiente parámetro.
    qstr = netCGI_GetEnvVar (qstr, var, sizeof (var));

    // Analizo el PRIMER carácter del parámetro (var[0]) para saber QUÉ me están enviando.
    switch (var[0]) {
      case 'i': // 'i' significa Local IP address
        if (var[1] == '4') { opt = netIF_OptionIP4_Address;       }
        else               { opt = netIF_OptionIP6_StaticAddress; }
        break;

      case 'm': // 'm' significa Local network mask (Máscara de subred)
        if (var[1] == '4') { opt = netIF_OptionIP4_SubnetMask; }
        break;

      case 'g': // 'g' significa Default gateway (Puerta de enlace)
        // Nota: Keil tiene una peculiaridad aquí, por eso configuro IPv6 como fallback
        if (var[1] == '4') { opt = netIF_OptionIP6_DefaultGateway; } // OJO: Según la librería esto suele ser IP4_DefaultGateway
        else               { opt = netIF_OptionIP6_DefaultGateway; }
        break;

      case 'p': // 'p' significa Primary DNS server
        if (var[1] == '4') { opt = netIF_OptionIP4_PrimaryDNS; }
        else               { opt = netIF_OptionIP6_PrimaryDNS; }
        break;

      case 's': // 's' significa Secondary DNS server
        if (var[1] == '4') { opt = netIF_OptionIP4_SecondaryDNS; }
        else               { opt = netIF_OptionIP6_SecondaryDNS; }
        break;
      
      // Si no reconozco la letra, anulo la cadena poniendo un fin de string ('\0') en la primera posición.
      default: var[0] = '\0'; break;
    }

    // Analizo el SEGUNDO carácter del parámetro (var[1]) para saber LA VERSIÓN DE IP.
    switch (var[1]) {
      case '4': typ = NET_ADDR_IP4; break; // Es IPv4
      case '6': typ = NET_ADDR_IP6; break; // Es IPv6

      // Si no es ni 4 ni 6, anulo la cadena porque no es un formato válido.
      default: var[0] = '\0'; break;
    }

    // Finalmente, compruebo si el formato es correcto:
    // 1. La cadena no ha sido anulada (var[0] != '\0').
    // 2. El tercer carácter es un signo igual (var[2] == '='), ej: "i4="
    if ((var[0] != '\0') && (var[2] == '=')) {
      
      // Convierto el valor del texto (que empieza en var[3], justo después del '=') 
      // a un formato binario que la placa entienda y lo guardo en mi array global 'ip_addr'.
      netIP_aton (&var[3], typ, ip_addr);
      
      // Aplico los cambios físicamente en el stack de red de la placa (clase Ethernet).
      netIF_SetOption (NET_IF_CLASS_ETH, opt, ip_addr, sizeof(ip_addr));
    }
    
  } while (qstr); // Repito si qstr no es NULL (es decir, si quedan parámetros por procesar)
}


/*----------------------------------------------------------------------------
  2. netCGI_ProcessData: Procesa datos enviados por formularios (POST)
  Se ejecuta cuando el usuario pulsa "Send" o cambia un Checkbox en la web.
 *---------------------------------------------------------------------------*/
/**
 * @brief  Procesa los datos recibidos desde un formulario Web (Método POST).
 * @param  code : Código de estado de la petición. Si es 0, significa que los datos son válidos.
 * @param  data : Puntero a la cadena de texto con los datos del formulario (ej. "led0=on&sntp=1").
 * @param  len  : Longitud en bytes de los datos recibidos.
 */
void netCGI_ProcessData (uint8_t code, const char *data, uint32_t len) {
  // --- VARIABLES LOCALES ---
  char var[40];               // Buffer donde iré guardando cada variable extraída del formulario.
  char passw[12];             // Buffer para gestionar el cambio de contraseña (si procede).
  MSGQUEUE_OBJ_LCD_t msg_lcd; // Estructura para empaquetar el mensaje que enviaré a la pantalla LCD.
  bool update_lcd = false;    // Bandera que pongo a 'true' si detecto que me han enviado un texto nuevo para el LCD.

  // Si el código no es 0, ignoro la petición porque no es un formulario válido.
  if (code != 0) {
    return; 
  }

  // Reseteo la variable global de mis LEDs. 
  // En la web, si un checkbox no está marcado, el navegador no lo envía. 
  // Por eso parto de 0 y solo enciendo los que me lleguen en el formulario.
  P2 = 0;           
  LEDrun = true;    // Por defecto, asumo que los LEDs pueden parpadear.

  // Si me llega un formulario vacío (longitud 0), simplemente aplico el reseteo de los LEDs y salgo.
  if (len == 0) {
    LED_SetOut (P2);
    return;
  }

  passw[0] = 1;

  // --- VARIABLE PARA EL APARTADO 5 ---
  // Uso esta bandera local para saber si en este envío del formulario viene el checkbox de la alarma.
  // Empieza en 0 (apagada) y si encuentro "alm_en=on", la pondré a 1.
  uint8_t alarma_en_formulario = 0;

  // Bucle principal: voy recorriendo toda la cadena de datos extrayendo variable por variable.
  do {
    // Extraigo la siguiente variable usando la función de la librería de Keil.
    data = netCGI_GetEnvVar (data, var, sizeof (var));
    
    // Si la variable no está vacía, analizo qué me han enviado.
    if (var[0] != 0) {
      
      /* --- GESTIÓN DE LEDS --- */
      // Voy encendiendo bit a bit la variable P2 según los checkboxes que me lleguen.
      if      (strcmp (var, "led0=on") == 0) P2 |= 0x01;
      else if (strcmp (var, "led1=on") == 0) P2 |= 0x02;
      else if (strcmp (var, "led2=on") == 0) P2 |= 0x04;
      else if (strcmp (var, "led3=on") == 0) P2 |= 0x08;
      else if (strcmp (var, "led4=on") == 0) P2 |= 0x10;
      else if (strcmp (var, "led5=on") == 0) P2 |= 0x20;
      else if (strcmp (var, "led6=on") == 0) P2 |= 0x40;
      else if (strcmp (var, "led7=on") == 0) P2 |= 0x80;
      else if (strcmp (var, "ctrl=Browser") == 0) LEDrun = false;

      /* --- GESTIÓN DE PASSWORD --- */
      else if ((strncmp (var, "pw0=", 4) == 0) || (strncmp (var, "pw2=", 4) == 0)) {
        if (netHTTPs_LoginActive()) {
          if (passw[0] == 1) strcpy (passw, var+4);
          else if (strcmp (passw, var+4) == 0) netHTTPs_SetPassword (passw);
        }
      }

      /* --- GESTIÓN DE MI PANTALLA LCD --- */
      // Si la variable empieza por "lcd1=", guardo el valor y levanto la bandera.
      else if (strncmp (var, "lcd1=", 5) == 0) {
        strcpy (lcd_text[0], var+5); 
        update_lcd = true;
      }
      else if (strncmp (var, "lcd2=", 5) == 0) {
        strcpy (lcd_text[1], var+5); 
        update_lcd = true;
      }

      /* --- NUEVA GESTIÓN APARTADO 5 (SNTP Y ALARMA RTC) --- */
      
      // 1. Servidor SNTP: Capturo el servidor elegido (0 o 1) usando atoi para pasar de texto a entero.
      else if (strncmp(var, "sntp=", 5) == 0) {
        sntp_server_index = (uint8_t)atoi(&var[5]);
      }
      
      // 2. Alarma: Si el checkbox me llega marcado, enciendo mi bandera local.
      else if (strcmp(var, "alm_en=on") == 0) {
        alarma_en_formulario = 1;
      }
      
      // 3. Periodo: Capturo el valor del desplegable (0, 1, 2 o 3) y reprogramo mi hardware al momento.
      else if (strncmp(var, "periodo=", 8) == 0) {
        periodo_seleccionado = (RTC_PeriodoAlarma_t)atoi(&var[8]);
        RTC_ConfigurarAlarma(periodo_seleccionado); // LLamo directamente a mi librería rtc.c
      }
      
      /* --- Ajuste manual de la hora (desde la web) --- */
      /* --- NUEVA GESTIÓN: AJUSTE MANUAL DE HORA Y FECHA --- */
      
      // 4. Capturo la nueva Hora Manual (Si el usuario escribió algo)
      // El navegador envía el formato "HH:MM:SS" (o similar si omiten segundos)
      else if (strncmp(var, "m_time=", 7) == 0) {
          // Si el valor no está vacío (var[7] tiene algo)
          if (var[7] != '\0') {
              int horas = 0, minutos = 0, segundos = 0;
              // Uso sscanf para "despiezar" el texto y extraer los números separados por ':'
              sscanf(&var[7], "%d:%d:%d", &horas, &minutos, &segundos);
              
              // Llamo a mi nueva función del RTC para guardar la hora
              RTC_SetHoraManual((uint8_t)horas, (uint8_t)minutos, (uint8_t)segundos);
          }
      }
      
      // 5. Capturo la nueva Fecha Manual
      // El navegador envía el formato de fecha ISO: "YYYY-MM-DD"
      else if (strncmp(var, "m_date=", 7) == 0) {
          if (var[7] != '\0') {
              int anio = 0, mes = 0, dia = 0;
              // Uso sscanf para despiezar el texto separándolo por los guiones '-'
              sscanf(&var[7], "%d-%d-%d", &anio, &mes, &dia);
              
              // Llamo a mi nueva función del RTC para guardar la fecha
              RTC_SetFechaManual((uint8_t)dia, (uint8_t)mes, (uint16_t)anio);
          }
      }

      
    }
  } while (data); // Repito hasta que no queden más variables en la cadena.

  // Actualizo la variable global con el resultado final del checkbox de la alarma.
  alarma_habilitada_web = alarma_en_formulario;

  // Aplico los cambios a los LEDs de la placa.
  LED_SetOut (P2);

  // Si he recibido datos para el LCD, uso el RTOS para enviar un mensaje a la cola.
  if (update_lcd) {
    memset(&msg_lcd, 0, sizeof(MSGQUEUE_OBJ_LCD_t)); // Limpio la estructura por seguridad
    
    // Copio mis cadenas globales a la estructura del mensaje.
    strncpy(msg_lcd.Lin1, lcd_text[0], sizeof(msg_lcd.Lin1) - 1);
    strncpy(msg_lcd.Lin2, lcd_text[1], sizeof(msg_lcd.Lin2) - 1);
    
    msg_lcd.barra = 0;
    msg_lcd.amplitud = 0;

    // Envío el mensaje a la cola (IPC) con tiempo de espera 0.
    osMessageQueuePut(mid_messageQueueLCD, &msg_lcd, 0, 0);
  }
}



/*----------------------------------------------------------------------------
  3. netCGI_Script: El "Generador" de contenido dinámico.
  Se ejecuta cuando el servidor lee una línea que empieza por 't' en un .cgi o HTML.
  Sustituye los comandos especiales por datos reales del micro.
 *---------------------------------------------------------------------------*/
/**
 * @brief  Genera el texto dinámico que se inyectará en la página web.
 * @param  env    : Cadena con la etiqueta identificativa (por ejemplo "h1" para la hora).
 * @param  buf    : Puntero al buffer donde debo escribir el resultado (el HTML generado).
 * @param  buflen : Tamaño máximo del buffer para no desbordarlo.
 * @param  pcgi   : Puntero de estado de la sesión, útil si necesito enviar tablas muy largas en varias pasadas.
 * @return uint32_t: Retorno el número de bytes que he escrito en 'buf'.
 */
uint32_t netCGI_Script (const char *env, char *buf, uint32_t buflen, uint32_t *pcgi) {
  // --- VARIABLES LOCALES ---
  int32_t socket;
  netTCP_State state;
  NET_ADDR r_client;
  const char *lang;
  uint32_t len = 0U;            // Acumulador de la longitud del texto generado.
  uint8_t id;
  static uint32_t adv;          // Lectura estática del potenciómetro.
  netIF_Option opt = netIF_OptionMAC_Address;
  int16_t      typ = 0;
    
  char t_str[20], d_str[20];    // Buffers temporales para almacenar la hora y fecha leídas del RTC.

  // Evalúo el primer carácter de la etiqueta que me piden resolver.
  switch (env[0]) {
    
    // CASOS a, b, c, d, e... se encargan de red, LEDs, Sockets TCP, contraseñas e idioma.
    // (Omito documentar cada caso por defecto para centrarnos en lo importante).
    case 'a' :
      switch (env[3]) {
        case '4': typ = NET_ADDR_IP4; break;
        case '6': typ = NET_ADDR_IP6; break;
        default: return (0);
      }
      switch (env[2]) {
        case 'l': if (env[3] == '4') return (0); else opt = netIF_OptionIP6_LinkLocalAddress; break;
        case 'i': if (env[3] == '4') opt = netIF_OptionIP4_Address; else opt = netIF_OptionIP6_StaticAddress; break;
        case 'm': if (env[3] == '4') opt = netIF_OptionIP4_SubnetMask; else return (0); break;
        case 'g': if (env[3] == '4') opt = netIF_OptionIP4_DefaultGateway; else opt = netIF_OptionIP6_DefaultGateway; break;
        case 'p': if (env[3] == '4') opt = netIF_OptionIP4_PrimaryDNS; else opt = netIF_OptionIP6_PrimaryDNS; break;
        case 's': if (env[3] == '4') opt = netIF_OptionIP4_SecondaryDNS; else opt = netIF_OptionIP6_SecondaryDNS; break;
      }
      netIF_GetOption (NET_IF_CLASS_ETH, opt, ip_addr, sizeof(ip_addr));
      netIP_ntoa (typ, ip_addr, ip_string, sizeof(ip_string));
      len = (uint32_t)sprintf (buf, &env[5], ip_string);
      break;

    case 'b': // --- CASO 'b': Estado de los LEDs ---
      if (env[2] == 'c') {
        len = (uint32_t)sprintf (buf, &env[4], LEDrun ?     ""     : "selected",
                                               LEDrun ? "selected" :    ""     );
        break;
      }
      id = env[2] - '0';
      if (id > 7) id = 0;
      id = (uint8_t)(1U << id);
      len = (uint32_t)sprintf (buf, &env[4], (P2 & id) ? "checked" : "");
      break;

    case 'c': // --- CASO 'c': Estado de los Sockets TCP ---
      while ((uint32_t)(len + 150) < buflen) {
        socket = ++MYBUF(pcgi)->idx;
        state  = netTCP_GetState (socket);
        if (state == netTCP_StateINVALID) return ((uint32_t)len);
        
        len += (uint32_t)sprintf (buf+len,   "<tr align=\"center\">");
        if (state <= netTCP_StateCLOSED) {
          len += (uint32_t)sprintf (buf+len, "<td>%d</td><td>%d</td><td>-</td><td>-</td><td>-</td><td>-</td></tr>\r\n", socket, netTCP_StateCLOSED);
        } else if (state == netTCP_StateLISTEN) {
          len += (uint32_t)sprintf (buf+len, "<td>%d</td><td>%d</td><td>%d</td><td>-</td><td>-</td><td>-</td></tr>\r\n", socket, netTCP_StateLISTEN, netTCP_GetLocalPort(socket));
        } else {
          netTCP_GetPeer (socket, &r_client, sizeof(r_client));
          netIP_ntoa (r_client.addr_type, r_client.addr, ip_string, sizeof (ip_string));
          len += (uint32_t)sprintf (buf+len, "<td>%d</td><td>%d</td><td>%d</td><td>%d</td><td>%s</td><td>%d</td></tr>\r\n", socket, netTCP_StateLISTEN, netTCP_GetLocalPort(socket), netTCP_GetTimer(socket), ip_string, r_client.port);
        }
      }
      len |= (1u << 31); // Flag para que el servidor vuelva a llamar a esta función si no ha cabido toda la tabla.
      break;

    case 'd': // Contraseñas
      switch (env[2]) {
        case '1': len = (uint32_t)sprintf (buf, &env[4], netHTTPs_LoginActive() ? "Enabled" : "Disabled"); break;
        case '2': len = (uint32_t)sprintf (buf, &env[4], netHTTPs_GetPassword()); break;
      }
      break;

    case 'e': // Idioma del navegador
      lang = netHTTPs_GetLanguage();
      if      (strncmp (lang, "en", 2) == 0) lang = "English";
      else if (strncmp (lang, "de", 2) == 0) lang = "German";
      else if (strncmp (lang, "fr", 2) == 0) lang = "French";
      else if (strncmp (lang, "sl", 2) == 0) lang = "Slovene";
      else                                   lang = "Unknown";
      len = (uint32_t)sprintf (buf, &env[2], lang, netHTTPs_GetLanguage());
      break;

    // --- CASO 'g': Entrada del ADC (Potenciómetro) ---
    case 'g': 
      switch (env[2]) {
        case '1': // Valor crudo del ADC (0-4095)
          adv = AD_in (0); 
          len = (uint32_t)sprintf (buf, &env[4], adv);
          break;
        case '2': // Valor en Voltios (escalado a 3.3V)
          len = (uint32_t)sprintf (buf, &env[4], (double)((float)adv*3.3f)/4096);
          break;
        case '3': // Valor en porcentaje (0-100%)
          adv = (adv * 100) / 4096;
          len = (uint32_t)sprintf (buf, &env[4], adv);
          break;
      }
      break;

    // --- CASO 'h': Mostrar Hora y Fecha del RTC ---
    case 'h': 
      // Le pido a mi librería rtc.c la hora actual y la cargo en mis buffers.
      RTC_ObtenerHoraFecha(t_str, d_str);
      
      // Si el archivo Web me pide la etiqueta %h1, escribo la Hora en el buffer.
      if (env[1] == '1') {
        len = (uint32_t)sprintf(buf, "%s", t_str);
      }
      // Si me pide la etiqueta %h2, escribo la Fecha.
      else if (env[1] == '2') {
        len = (uint32_t)sprintf(buf, "%s", d_str);
      }
      break;
        
    // --- NUEVO CASO 's': Estado de la configuración SNTP/Alarma (Apartado 5) ---
    case 's':
      switch (env[1]) {
        // ¿Qué servidor SNTP (Radio Button) está seleccionado?
        case '1': 
          // Si el ID del servidor coincide con mi variable global, inyecto la palabra "checked" en el HTML.
          if ((env[2] - '0') == sntp_server_index) {
            len = (uint32_t)sprintf(buf, "%s", "checked"); 
          } else {
            len = (uint32_t)sprintf(buf, "%s", "");
          }
          break;
          
        // ¿Está activada la alarma? (Checkbox)
        case '2': 
          if (alarma_habilitada_web) {
            len = (uint32_t)sprintf(buf, "%s", "checked");
          } else {
            len = (uint32_t)sprintf(buf, "%s", "");
          }
          break;
          
        // ¿Qué periodo de alarma está seleccionado? (Desplegable)
        case '3': 
          // Si la opción que se está renderizando coincide con mi variable, inyecto "selected".
          if ((env[2] - '0') == (int)periodo_seleccionado) {
            len = (uint32_t)sprintf(buf, "%s", "selected");
          } else {
            len = (uint32_t)sprintf(buf, "%s", "");
          }
          break;
          
        // Muestro el texto del servidor actual
        case '4': 
          len = (uint32_t)sprintf(buf, "%s", sntp_servers[sntp_server_index]);
          break;
      }
      break;

    case 'x':
      adv = AD_in (0);
      len = (uint32_t)sprintf (buf, &env[1], adv);
      break;

    case 'y':
      len = (uint32_t)sprintf (buf, "<checkbox><id>button%c</id><on>%s</on></checkbox>",
                               env[1], (get_button () & (1 << (env[1]-'0'))) ? "true" : "false");
      break;
  }
  
  // Devuelvo el número de caracteres que he escrito en 'buf', para que el servidor sepa cuánto enviar.
  return (len);
}

#if      defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
#pragma  clang diagnostic pop
#endif
