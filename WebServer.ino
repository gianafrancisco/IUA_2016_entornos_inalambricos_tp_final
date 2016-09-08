#include <SPI.h>
#include <Ethernet.h>
#include <Printers.h>
#include <XBee.h>

byte mac[] = { 
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED 
};
IPAddress ip(192,168,2,177);

EthernetServer server(80);

XBee xbee = XBee();

uint8_t isCmd[] = {'I','S'};
uint8_t irCmd[] = {'I','R'};
uint8_t d5Cmd[] = { 'D', '5' };
uint8_t d5ValueHigh[] = { 0x05 };
uint8_t d5ValueLow[] = { 0x04 };
//uint8_t irValue[] = { 0xB, 0xB8 }; // 0xB, 0xB8 == 3 seg. 0x03, 0xe8 == 1 Seg
uint8_t irValue[] = { 0x03, 0xe8 }; // 0xB, 0xB8 == 3 seg. 0x03, 0xe8 == 1 Seg

//XBeeAddress64 remoteAddressGas = XBeeAddress64(0x0013a200, 0x400a3e02);
//XBeeAddress64 remoteAddressHumedad = XBeeAddress64(0x0013a200, 0x403a3c93);
XBeeAddress64 remoteAddressGas = XBeeAddress64(0x0013a200, 0x403a3c93);
XBeeAddress64 remoteAddressHumedad = XBeeAddress64(0x0013a200, 0x400a3e02);

RemoteAtCommandRequest remoteAtRequest = RemoteAtCommandRequest(remoteAddressGas, d5Cmd, d5ValueLow, sizeof(d5ValueLow));
RemoteAtCommandResponse remoteAtResponse = RemoteAtCommandResponse();

AtCommandRequest atRequest = AtCommandRequest(isCmd);
AtCommandResponse atResponse = AtCommandResponse();

ZBRxIoSampleResponse ioSample = ZBRxIoSampleResponse();

#define ENCENDER(x,fondoEscala, estado) (x < fondoEscala*0.15 || x > fondoEscala*0.75) && estado == 0
#define APAGAR(x,fondoEscala, estado) (x > fondoEscala*0.20 && x < fondoEscala*0.70) && estado == 1

/*
 * Usamos los defined siguientes para inicializar la variable de control del ventilador
 */
#define CHECK_FUERA_LIMITES(x,fondoEscala) (x < fondoEscala*0.15 || x > fondoEscala*0.75)
#define CHECK_ENTRE_LIMITES(x,fondoEscala) (x > fondoEscala*0.20 && x < fondoEscala*0.70)


/*
 * Permite escalar la variable entre los rangos de trabajo
 */
#define NORMALIZAR(x,fondoEscala) (double)(fondoEscala*x/1023)

#define TEMP_FONDO_ESCALA 50.0
#define GAS_FONDO_ESCALA 5.0
#define HUMEDAD_FONDO_ESCALA 50.0
#define VALOR_DESCONOCIDO -1
#define FUERA_LIMITES 1
#define ENTRE_LIMITES 0

double temperatura = TEMP_FONDO_ESCALA/2;
int temperaturaDigital = VALOR_DESCONOCIDO;
int temperaturaFueraRango = VALOR_DESCONOCIDO;
double humedad = HUMEDAD_FONDO_ESCALA/2;
int humedadDigital = VALOR_DESCONOCIDO;
int humedadFueraRango = VALOR_DESCONOCIDO;
double gas = GAS_FONDO_ESCALA/2;
int gasDigital = VALOR_DESCONOCIDO;
int gasFueraRango = VALOR_DESCONOCIDO;
bool errorTemperatura = false;
bool errorGas = false;
bool errorHumedad = false;

unsigned long start_millis = 0;

void setup() {
  Serial1.begin(9600);
  xbee.setSerial(Serial1);
  Serial.begin(9600);
   while (!Serial) {
    ;
  }

  Ethernet.begin(mac, ip);
  server.begin();
  Serial.print("server is at ");
  Serial.println(Ethernet.localIP());

/*
 * Enviamos comandos de configuracion del tiempo de muestreo ademas ponemos en Low los ventiladores
 */

  Serial.println("Inicializando configuracion");

  remoteAtRequest.setRemoteAddress64(remoteAddressGas);
  remoteAtRequest.setCommand(irCmd);
  remoteAtRequest.setCommandValue(irValue);
  remoteAtRequest.setCommandValueLength(sizeof(irValue));
  sendRemoteAtCommand();  
  remoteAtRequest.clearCommandValue();
  readResponse();
  remoteAtRequest.setRemoteAddress64(remoteAddressHumedad);
  remoteAtRequest.setCommand(irCmd);
  remoteAtRequest.setCommandValue(irValue);
  remoteAtRequest.setCommandValueLength(sizeof(irValue));
  sendRemoteAtCommand();  
  remoteAtRequest.clearCommandValue();
  readResponse();

  remoteAtRequest.setRemoteAddress64(remoteAddressGas);
  remoteAtRequest.setCommand(d5Cmd);
  remoteAtRequest.setCommandValue(d5ValueLow);
  remoteAtRequest.setCommandValueLength(sizeof(d5ValueLow));
  sendRemoteAtCommand();  
  remoteAtRequest.clearCommandValue();
  readResponse();
  remoteAtRequest.setRemoteAddress64(remoteAddressHumedad);
  remoteAtRequest.setCommand(d5Cmd);
  remoteAtRequest.setCommandValue(d5ValueLow);
  remoteAtRequest.setCommandValueLength(sizeof(d5ValueLow));
  sendRemoteAtCommand();
  remoteAtRequest.clearCommandValue();
  readResponse();
}


void loop() {

  if(int((millis() - start_millis)) > 5000){
    start_millis = millis();
    atRequest.setCommand(isCmd);
    atRequest.setCommandValue(NULL);
    atRequest.setCommandValueLength(0);
    Serial.println("get sample from local Xbee");
    sendAtCommand();
    atRequest.clearCommandValue();
    readResponse();
    controlarVentiladores();
  }
  readResponse();
  task_webServer();
}

void task_webServer(){
  EthernetClient client = server.available();
  if (client) {
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n' && currentLineIsBlank) {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();
          client.println("<!DOCTYPE HTML>");
          client.println("<html>");
          client.println("<meta http-equiv=\"refresh\" content=\"1\">");
          client.print("<head><style>.apagado { color: blue;} .encendido {color: green;} .error {color: red;}</style></head>");
          client.print("<body><h4>");
          client.print("Sensor Temperatura: ");
          client.println("<br />");
          client.print("<progress value='");
          client.print(temperatura);
          client.print("' max='");
          client.print(TEMP_FONDO_ESCALA);
          client.print("'></progress>&#32;");
          client.print(temperatura);
          client.println("<br />");

          client.print("Ventilador Temperatura [ &deg;C ]: ");
          if(temperaturaDigital == 1)
            client.print("<span class='encendido'>encendido</span>");
          else
            client.print("<span class='apagado'>apagado</span>");
            
          if(errorTemperatura == true){
            client.print("<span class='error'> &#32;(encendido/apagado ventilador)</span>");
          }

          client.println("<br />");
          
          client.println("<hr>");
          client.print("Sensor Humedad: ");
          client.println("<br />");
          client.print("<progress value='");
          client.print(humedad);
          client.print("' max='");
          client.print(HUMEDAD_FONDO_ESCALA);
          client.print("'></progress>&#32;");
          client.print(humedad);

          client.println("<br />");

          client.print("Ventilador Humedad [ % ]: ");
          if(humedadDigital == 1)
            client.print("<span class='encendido'>encendido</span>");
          else
            client.print("<span class='apagado'>apagado</span>");

          if(errorHumedad == true){
            client.print("<span class='error'> &#32;(encendido/apagado ventilador)</span>");
          }
          client.println("<br />");
          
          client.println("<hr>");
          client.print("Sensor Gas [ g/cm&sup3; ]: ");
          client.println("<br />");
          client.print("<progress value='");
          client.print(gas);
          client.print("' max='");
          client.print(GAS_FONDO_ESCALA);
          client.print("'></progress>&#32;");
          client.print(gas);
          client.println("<br />");

          client.print("Ventilador Gas: ");
          if(gasDigital == 1)
            client.print("<span class='encendido'>encendido</span>");
          else
            client.print("<span class='apagado'>apagado</span>");

          if(errorGas == true){
            client.print("<span class='error'> &#32;(encendido/apagado ventilador)</span>");
          }
            
          client.println("<br />");
          
          client.println("<hr>");
          client.print("</h4>");
          client.println("</body></html>");
          break;
        }
        if (c == '\n') {
          currentLineIsBlank = true;
        } 
        else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
    Serial.println("client disonnected");
  }
}

/*
 * Estas funciones son las encargadas de inicializar las variables de control e identificar si hay un error en el sistema
 */
void checkVentiladorTemp(){
  if(temperaturaFueraRango == VALOR_DESCONOCIDO) 
  {
    if(CHECK_FUERA_LIMITES(temperatura, TEMP_FONDO_ESCALA)){
      temperaturaFueraRango = FUERA_LIMITES;
      encender();
    }else if(CHECK_ENTRE_LIMITES(temperatura, TEMP_FONDO_ESCALA)){
      temperaturaFueraRango = ENTRE_LIMITES;
      apagar();
    }
  }
  if(temperaturaDigital == temperaturaFueraRango){
    errorTemperatura = false;
  }else {
    errorTemperatura = true;
  }
}

void checkVentiladorGas(){
  if(gasFueraRango == VALOR_DESCONOCIDO) 
  {
    if(CHECK_FUERA_LIMITES(gas, GAS_FONDO_ESCALA)){
      gasFueraRango = FUERA_LIMITES;
      encenderRemoto(remoteAddressGas);
    }else if(CHECK_ENTRE_LIMITES(gas, GAS_FONDO_ESCALA)){
      gasFueraRango = ENTRE_LIMITES;
      apagarRemoto(remoteAddressGas);
    }
  }
  if(gasDigital == gasFueraRango){
    errorGas = false;
  }else {
    errorGas = true;
  }
}

void checkVentiladorHumedad(){
  if(gasFueraRango == VALOR_DESCONOCIDO) 
  {
    if(CHECK_FUERA_LIMITES(humedad, HUMEDAD_FONDO_ESCALA)){
      humedadFueraRango = FUERA_LIMITES;
      encenderRemoto(remoteAddressHumedad);
    }else if(CHECK_ENTRE_LIMITES(humedad, HUMEDAD_FONDO_ESCALA)){
      humedadFueraRango = ENTRE_LIMITES;
      apagarRemoto(remoteAddressHumedad);
    }
  }
  if(humedadDigital == humedadFueraRango){
    errorHumedad = false;
  }else {
    errorHumedad = true;
  }
}

/*
 * Comandos de encendido y apagado de los ventiladores
 */

void sendAtCommand() {
  Serial.println("Sending AT command to the local XBee");
  xbee.send(atRequest);
}

void sendRemoteAtCommand() {
  Serial.println("Sending command to the remote XBee");
  xbee.send(remoteAtRequest);
}
 
void encenderRemoto(XBeeAddress64 dst){
    remoteAtRequest.setRemoteAddress64(dst);
    remoteAtRequest.setCommand(d5Cmd);
    remoteAtRequest.clearCommandValue();
    remoteAtRequest.setCommandValue(d5ValueHigh);
    remoteAtRequest.setCommandValueLength(sizeof(d5ValueHigh));
    sendRemoteAtCommand();
    remoteAtRequest.clearCommandValue();
  
}

void apagarRemoto(XBeeAddress64 dst){
    remoteAtRequest.setRemoteAddress64(dst);
    remoteAtRequest.setCommand(d5Cmd);
    remoteAtRequest.clearCommandValue();
    remoteAtRequest.setCommandValue(d5ValueLow);
    remoteAtRequest.setCommandValueLength(sizeof(d5ValueLow));
    sendRemoteAtCommand();
    remoteAtRequest.clearCommandValue();
}

void encender(){
  atRequest.setCommand(d5Cmd);
  atRequest.setCommandValue(d5ValueHigh);
  atRequest.setCommandValueLength(sizeof(d5ValueHigh));
  sendAtCommand();
  atRequest.clearCommandValue();
}

void apagar(){
  atRequest.setCommand(d5Cmd);
  atRequest.setCommandValue(d5ValueLow);
  atRequest.setCommandValueLength(sizeof(d5ValueLow));
  sendAtCommand();
  atRequest.clearCommandValue();
}

/*
 * Encargado de verificar el rango de trabajo y ejecutar los comandos de encendido o apagado de los ventiladores
 */
void controlarVentiladores(){

  Serial.println("checking cooler status");
  
  //Check Temperatura
  if(temperaturaDigital != VALOR_DESCONOCIDO){
    
    if(ENCENDER(temperatura, TEMP_FONDO_ESCALA, temperaturaFueraRango)){
      temperaturaFueraRango = FUERA_LIMITES;
      Serial.println("Encender ventilador Temp");
      encender();

    }else if(APAGAR(temperatura, TEMP_FONDO_ESCALA, temperaturaFueraRango)){
      temperaturaFueraRango = ENTRE_LIMITES;
      Serial.println("Apagar ventilador Temp");
      apagar();
    }
  }
  
  //Check gas
  if(gasDigital != VALOR_DESCONOCIDO){
    if(ENCENDER(gas, GAS_FONDO_ESCALA, gasFueraRango)){
      gasFueraRango = FUERA_LIMITES;
      Serial.println("Encender ventilador Gas");
      encenderRemoto(remoteAddressGas);
    }else if(APAGAR(gas, GAS_FONDO_ESCALA, gasFueraRango)){
      gasFueraRango = ENTRE_LIMITES;
      Serial.println("Apagar ventilador Gas");
      apagarRemoto(remoteAddressGas);
    }
  }
  //Check humedad
  if(humedadDigital != VALOR_DESCONOCIDO){
    if(ENCENDER(humedad, HUMEDAD_FONDO_ESCALA, humedadFueraRango)){
      humedadFueraRango = FUERA_LIMITES;
      Serial.println("Encender ventilador Humedad");
      encenderRemoto(remoteAddressHumedad);
    }else if(APAGAR(humedad, HUMEDAD_FONDO_ESCALA, humedadFueraRango)){
      humedadFueraRango = ENTRE_LIMITES;
      Serial.println("Apagar ventilador Humedad");
      apagarRemoto(remoteAddressHumedad);
    }
  }
}

/*
 * Funcion que recibe los paqueres desde el Xbee y los procesa segun sea la funcion.
 */
void readResponse(){
    if (xbee.readPacket(5000)) {
      if (xbee.getResponse().getApiId() == REMOTE_AT_COMMAND_RESPONSE) {
        read_REMOTE_AT_COMMAND_RESPONSE();
      } else if (xbee.getResponse().getApiId() == AT_COMMAND_RESPONSE) {
        read_AT_COMMAND_RESPONSE();   
      } else if (xbee.getResponse().getApiId() == ZB_IO_SAMPLE_RESPONSE) {
        read_ZB_IO_SAMPLE_RESPONSE();
      } else {
        Serial.print("Expected Remote AT response but got ");
        Serial.print(xbee.getResponse().getApiId(), HEX);
      }
  } else if (xbee.getResponse().isError()) {
    Serial.print("Error reading packet.  Error code: ");  
    Serial.println(xbee.getResponse().getErrorCode());
  } else {
    Serial.print("No response from radio");  
  }

}

/*
 * Funcion para procesar la respuesta REMOTE_AT_COMMAND_RESPONSE
 */
void read_REMOTE_AT_COMMAND_RESPONSE(){
  Serial.println("got REMOTE_AT_COMMAND_RESPONSE");
  xbee.getResponse().getRemoteAtCommandResponse(remoteAtResponse);
  if (remoteAtResponse.isOk()) {
    Serial.print("Command [");
    Serial.print(remoteAtResponse.getCommand()[0]);
    Serial.print(remoteAtResponse.getCommand()[1]);
    Serial.println("] was successful!");
  
    if (remoteAtResponse.getValueLength() > 0) {
      Serial.print("Command value length is ");
      Serial.println(remoteAtResponse.getValueLength(), DEC);
  
      Serial.print("Command value: ");
      
      for (int i = 0; i < remoteAtResponse.getValueLength(); i++) {
        Serial.print(remoteAtResponse.getValue()[i], HEX);
        Serial.print(" ");
      }
  
      Serial.println("");
    }
  } else {
    Serial.print("Command returned error code: ");
    Serial.println(remoteAtResponse.getStatus(), HEX);
  }
}

/*
 * Funcion para procesar la respuesta AT_COMMAND_RESPONSE
 */
void read_AT_COMMAND_RESPONSE(){
  Serial.println("got AT_COMMAND_RESPONSE");
  xbee.getResponse().getAtCommandResponse(atResponse);
  
  if (atResponse.isOk()) {
    Serial.print("Command [");
    Serial.print(atResponse.getCommand()[0]);
    Serial.print(atResponse.getCommand()[1]);
    Serial.println("] was successful!");
  
    if (atResponse.getValueLength() > 0) {
      Serial.print("Command value length is ");
      Serial.println(atResponse.getValueLength(), DEC);
  
      Serial.print("Command value: ");
      for (int i = 0; i < atResponse.getValueLength(); i++) {
        Serial.print(atResponse.getValue()[i], HEX);
        Serial.print(" ");
      }
      Serial.println("");
      if (atResponse.getValueLength() > 1) {
        int value = (0xFF & atResponse.getValue()[atResponse.getValueLength() - 1]) + ( (0x00FF & atResponse.getValue()[atResponse.getValueLength() - 2])<<8);
        temperatura = NORMALIZAR(value,TEMP_FONDO_ESCALA);
        temperaturaDigital = (int)((0x20 & atResponse.getValue()[5])>>5);
        checkVentiladorTemp();
      }
  
    }
  } 
  else {
    Serial.print("Command return error code: ");
    Serial.println(atResponse.getStatus(), HEX);
  }
}
/*
 * Funcion para procesar la respuesta ZB_IO_SAMPLE_RESPONSE
 */
void read_ZB_IO_SAMPLE_RESPONSE(){
  Serial.println("got ZB_IO_SAMPLE_RESPONSE");
  xbee.getResponse().getZBRxIoSampleResponse(ioSample);
  
  Serial.print("Received I/O Sample from: ");
  Serial.print(ioSample.getRemoteAddress64().getMsb(), HEX);
  Serial.print(ioSample.getRemoteAddress64().getLsb(), HEX);
  Serial.println("");
  
  int value = 0;
  int valueDigital = 0;
  if (ioSample.containsAnalog()) {
    for (int i = 0; i <= 4; i++) {
      if (ioSample.isAnalogEnabled(i)) {
        Serial.print("Analog (AI");
        Serial.print(i, DEC);
        Serial.print(") is ");
        Serial.println(ioSample.getAnalog(i), DEC);
        value = ioSample.getAnalog(i);
      }
    }
  }
  
  if (ioSample.containsAnalog()) {
    for (int i = 0; i <= 12; i++) {
      if (ioSample.isDigitalEnabled(i)) {
        Serial.print("Digital (DI");
        Serial.print(i, DEC);
        Serial.print(") is ");
        Serial.println(ioSample.isDigitalOn(i), DEC);
        valueDigital = ioSample.isDigitalOn(i);
      }
    }
  }
  
  if(remoteAddressGas.getMsb() == ioSample.getRemoteAddress64().getMsb() && remoteAddressGas.getLsb() == ioSample.getRemoteAddress64().getLsb()){
    gasDigital = valueDigital;
    gas = NORMALIZAR(value,GAS_FONDO_ESCALA);
    checkVentiladorGas();
  }else if(remoteAddressHumedad.getMsb() == ioSample.getRemoteAddress64().getMsb() && remoteAddressHumedad.getLsb() == ioSample.getRemoteAddress64().getLsb()){
    humedadDigital = valueDigital;
    humedad = NORMALIZAR(value,HUMEDAD_FONDO_ESCALA);
    checkVentiladorHumedad();
  }
  
}

