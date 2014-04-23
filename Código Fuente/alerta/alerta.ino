
/*
Proyecto: Alerta
RF: RF3 
Descripción: Lectura de la temperatura desde un dispositivo móvil.
*/

#include <OneWire.h>
#include <Bridge.h>
#include <Temboo.h>
#include "TembooAccount.h" // contains Temboo account information
#include "CorreoDestino.h" // Información de cuenta de correo para envío y destinatario.
#include <YunServer.h>
#include <YunClient.h>

// Config
float UMBRAL_TEMPERATURA = 21; // umbral de temperatura en grados centígrados.
int pinRele = 13; // pin que usamos para manejar el relé.
OneWire  ds(10);  // en el pin 10 (a 4.7K Ohm resistor is necessary)
YunServer server;
float _ultimaTemperaturaValida = -1;   

void setup() {
  // put your setup code here, to run once:
  pinMode(pinRele, OUTPUT);
  digitalWrite(pinRele, LOW);
  Bridge.begin();
  digitalWrite(pinRele, HIGH);
  server.listenOnLocalhost();
  server.begin();
}

void loop() {
  // put your main code here, to run repeatedly:

  // Get clients coming from server
  YunClient client = server.accept();
  
  float _temperaturaActual = getTemperatura();

     // There is a new client?
  if (client) {
    // Process request
       
    process(client,_ultimaTemperaturaValida);

    // Close connection and free resources.
    client.stop();
  }
  if (_temperaturaActual != -1)
  {   
    
    _ultimaTemperaturaValida = _temperaturaActual;
      
    Serial.print("Temperatura actual: ");
    Serial.println(_temperaturaActual);
    if (_temperaturaActual < UMBRAL_TEMPERATURA)
    {
      unsigned int correoEnviado = enviarCorreoAviso(_temperaturaActual);
      if (correoEnviado == 0)
        Serial.println("Correo enviado.");
      else
        Serial.println("Correo no enviado.");
    }
  }
  delay(50);
}

void process(YunClient client, float temp) {
  // read the command
  String command = client.readStringUntil('/');
  
  if (command == "temperatura") {
    svcTemperatura(client,temp);
  }
  if (command == "calentador") {
    svcRele(client);
  }
}

void svcTemperatura(YunClient client, float temp) {

  String comando = client.readStringUntil('/');
  comando.trim();
  if (comando == "leer") client.println(temp); 
  if (comando == "umbral") client.println(UMBRAL_TEMPERATURA); 

}

void svcRele(YunClient client) {

  String comando = client.readStringUntil('/');
  comando.trim();
  if (comando == "on") digitalWrite(pinRele, HIGH); 
  if (comando == "off") digitalWrite(pinRele, LOW);  
  if (comando == "leer") client.println(digitalRead(pinRele));  

}

unsigned int enviarCorreoAviso( float _temperaturaActual ) {

  TembooChoreo SendEmailChoreo;

  // invoke the Temboo client
  // NOTE that the client must be reinvoked, and repopulated with
  // appropriate arguments, each time its run() method is called.
  SendEmailChoreo.begin();
  
  // set Temboo account credentials
  SendEmailChoreo.setAccountName(TEMBOO_ACCOUNT);
  SendEmailChoreo.setAppKeyName(TEMBOO_APP_KEY_NAME);
  SendEmailChoreo.setAppKey(TEMBOO_APP_KEY);

  // identify the Temboo Library choreo to run (Google > Gmail > SendEmail)
  SendEmailChoreo.setChoreo("/Library/Google/Gmail/SendEmail");

  String _cuerpoMensaje("PROYECTO ALERTA");
  _cuerpoMensaje += "\n";
  _cuerpoMensaje += "\n";
  _cuerpoMensaje += "La temperatura ha descendido del umbral de seguridad de ";
  _cuerpoMensaje += UMBRAL_TEMPERATURA;
  _cuerpoMensaje += " grados.";
  _cuerpoMensaje += "\n";
  _cuerpoMensaje += "\n";
  _cuerpoMensaje += "Temperatura actual: ";
  _cuerpoMensaje += _temperaturaActual;
  _cuerpoMensaje += " grados.";
  _cuerpoMensaje += "\n";
  _cuerpoMensaje += "\n";
  _cuerpoMensaje += "\n";

  // Config
  SendEmailChoreo.addInput("Username", GMAIL_USER_NAME);
  SendEmailChoreo.addInput("Password", GMAIL_PASSWORD);
  SendEmailChoreo.addInput("ToAddress", TO_EMAIL_ADDRESS);
  SendEmailChoreo.addInput("Subject", "PROYECTO ALERTA - AVISO: La temperatura ha descendido del umbral de seguridad.");

   // next comes the message body, the main content of the email   
  SendEmailChoreo.addInput("MessageBody", _cuerpoMensaje);

  // tell the Choreo to run and wait for the results. The 
  // return code (returnCode) will tell us whether the Temboo client 
  // was able to send our request to the Temboo servers
  unsigned int returnCode = SendEmailChoreo.run();

  // a return code of zero (0) means everything worked
  if (returnCode == 0) {
      //Serial.println("Success! Email sent!");
      //success = true;
  } else {
    // a non-zero return code means there was an error
    // read and print the error message
    while (SendEmailChoreo.available()) {
      char c = SendEmailChoreo.read();
      Serial.print(c);
    }
  } 
  SendEmailChoreo.close();

  return returnCode;
}

float getTemperatura(void) {
  
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius, fahrenheit;
    
  if ( !ds.search(addr)) {
    ds.reset_search();
    delay(50);
    //Serial.println("Error search!");
    return -1;
  }
  
  if (OneWire::crc8(addr, 7) != addr[7]) {
      Serial.println("CRC is not valid!");
      return -1;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
  delay(300);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Read Scratchpad

  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
  }
 
  // Convert the data to actual temperature
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;

  return celsius;
}





