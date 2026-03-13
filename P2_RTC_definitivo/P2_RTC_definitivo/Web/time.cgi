t <html><head><title>Estado del RTC</title>
t <script language=JavaScript>
t // Función para enviar el formulario
t function submit_form() {
t   document.rtc_form.submit();
t }
t 
t // Función AJAX para actualizar la hora en segundo plano
t function updateTime() {
t   var xhr = new XMLHttpRequest();
t   // Añado Math.random() para engañar al navegador y que no guarde la hora en caché
t   xhr.open("GET", "rtc.cgx?" + Math.random(), true); 
t   xhr.onreadystatechange = function() {
t     if (xhr.readyState == 4 && xhr.status == 200) {
t       var xmlDoc = xhr.responseXML;
t       if (xmlDoc) {
t         // Extraigo los datos del XML "invisible"
t         var t_val = xmlDoc.getElementsByTagName("rtc_time")[0].childNodes[0].nodeValue;
t         var d_val = xmlDoc.getElementsByTagName("rtc_date")[0].childNodes[0].nodeValue;
t         // Inyecto los textos nuevos directamente en el HTML
t         document.getElementById("val_time").innerHTML = t_val;
t         document.getElementById("val_date").innerHTML = d_val;
t       }
t     }
t   };
t   xhr.send();
t }
t 
t // Le digo al navegador que ejecute la función updateTime() cada 1000 milisegundos (1 segundo)
t setInterval(updateTime, 1000);
t </script>
t </head>
i pg_header.inc
t <h2 align=center><br>Estado del Reloj Interno (RTC) y SNTP</h2>
t <p><font size="2">Esta pagina muestra la <b>Hora</b> y la <b>Fecha</b> actuales
t  almacenadas en el microcontrolador STM32.<br><br>
t  <b>Nota:</b> La hora se actualiza automaticamente cada segundo.</font></p>
t 
t <br>
t 
t <table border=0 width=99%><font size="3">
t <tr bgcolor=#aaccff>
t  <th width=40%>Parametro</th>
t  <th width=60%>Valor Actual</th></tr>
t <tr><td><img src=pabb.gif>Hora del sistema:</td>
t  <td><b id="val_time">
c h1
t </b></td></tr>
t <tr><td><img src=pabb.gif>Fecha del sistema:</td>
t <td><b id="val_date">
c h2
t </b></td></tr>
t <tr><td><img src=pabb.gif>Servidor SNTP en uso:</td>
t <td><b style="color:blue;">
c s4
t </b></td></tr>
t </font></table>
t <br>
t 
t <form action="time.cgi" method="POST" name="rtc_form">
t <table border=0 width=99%><font size="3">
t <tr bgcolor=#aaccff>
t  <th width=40%>Configuracion (Apartado 5)</th>
t  <th width=60%>Opciones</th></tr> 
t 
t <tr><td><img src=pabb.gif>Cambiar Servidor SNTP:</td>
t <td>
t   <input type="radio" name="sntp" value="0"
c s10
t   > Google NTP (216.239.35.0)<br>
t   <input type="radio" name="sntp" value="1"
c s11
t   > Cloudflare NTP (162.159.200.1)
t </td></tr>
t 
t <tr><td><img src=pabb.gif>Estado de la Alarma:</td>
t <td>
t   <input type="checkbox" name="alm_en" value="on"
c s2
t   > Habilitar alarma del RTC (LED Verde)
t </td></tr>
t 
t <tr><td><img src=pabb.gif>Periodo de la Alarma:</td>
t <td>
t   <select name="periodo">
t     <option value="1"
c s31
t     >Cada 10 segundos</option>
t     <option value="2"
c s32
t     >Cada 1 minuto</option>
t     <option value="3"
c s33
t     >Cada 5 minutos</option>
t   </select>
t </td></tr>
t 
t <tr><td colspan="2"><br></td></tr>
t 
t <tr bgcolor=#aaccff>
t  <th width=40%>Ajuste Manual</th>
t  <th width=60%>Insertar Hora y Fecha</th></tr> 
t 
t <tr><td><img src=pabb.gif>Establecer Hora:</td>
t <td>
t   <input type="time" name="m_time" step="1">
t </td></tr>
t 
t <tr><td><img src=pabb.gif>Establecer Fecha:</td>
t <td>
t   <input type="date" name="m_date">
t </td></tr>
t 
t <tr><td colspan="2"><br></td></tr>
t 
t <tr><td colspan="2" align="center">
t   <input type="button" value="Aplicar Cambios" onclick="submit_form()">
t </td></tr>
t 
t </font></table>
t </form>
i pg_footer.inc
.