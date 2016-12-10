#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>

#define DEBUG 1

/*

    This program reads values from either the analogue port
    and other waits for signals from Müller-Ziegler Drehstromzähler
    12 kwh  consumption
    13 kwh  delivery
    14 GND
    15 +    20mA current amperes (-20 to 0 to 20kW)
    16 -    20mA current amperes

    Target: Arduino
    Arduino    Müller-Zieger Drehstromzähler    Comments
    D2          13            Strombezug        over 2200 Ohm to +5V, Arduino D2 is interrupt handler 0
    D3          12            Stromlieferung    over 2200 Ohm to +5V, Arduino D3 is interrupt handler 1
    A0          15            + 20mA            over 250 Ohm high precision to 16 and from there to ground

    (c) 2011 by Alexander Hofmann
*/

#define PORT_ANALOGUE A0  // port number on arduino for analogue conversion of the 20mA loop, that shows the current power consumption
//   (0-10mA for delivering power and 10mA up to 20mA (40kW) for consuming power)
#define P_min  -15.0    // Power at I_min in kW
#define P_max  15.0     // Power at I_max in kW
#define I_min  0.0         // I_min in A
#define I_max  0.0025      // I_max in A
#define R_used 250.0       // Resistor used in Ohm
#define U_max  5.0         // U = R * I in V
#define ADC_res 10.0       // bits of Analog Digital Conversion resoltion
#define ADC_div 1024.0       // = 2^ADC_res

unsigned long lasttime = 0;
unsigned long currenttime = 0;

//the target function = y = f(x) = kx + d
int config_zero = 0;      //y value for x = 0
float config_incline = 0.0;   

int out_cnt = 0; // Wh - delivered into the power network
char out_cnt_str[17];
int in_cnt = 0;  // Wh - consumbed from the power network
char in_cnt_str[17];

char message_buff[100];
int analog_count;
int analog_plain = 0;
int analog = 0;
String analog_str;
char analog_chr[16];
String analog_plain_str;
char analog_plain_chr[16];


byte mac[]    = {0xDE, 0xED, 0xBA, 0xFE, 0xFE, 0xED };
IPAddress ip(192, 168, 178, 200);   //device IP
IPAddress server(192, 168, 178, 2); //MQTT broker

EthernetClient ethClient;
PubSubClient mqttClient(ethClient);



void update_config(char* topic, byte* payload, unsigned int length)
{
  int i=0;
  String top = String(topic);
  for(i=0; i<length; i++) {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
  String payl = String(message_buff);

  if (top.endsWith("zero")) {
      config_zero = payl.toInt();
      #ifdef DEBUG
        Serial.print("new zero = ");
        Serial.println(config_zero);
      #endif
  }
  if (top.endsWith("incline")) {
      config_incline = payl.toFloat();
      #ifdef DEBUG
        Serial.print("new incline = ");
        Serial.println(config_incline, 2);
      #endif
  }
}


/*
   This function reconnects to the MQTT server.
   used.

*/
void reconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
#ifdef DEBUG
    Serial.println("Attempting MQTT connection...");
#endif
    // Attempt to connect
    if (mqttClient.connect("arduino1")) {
#ifdef DEBUG
      Serial.println("connected");
#endif
      mqttClient.setCallback(update_config);
      // Defaults to QoS 0 subscription:
      boolean rc = mqttClient.subscribe("home/techroom/mz/config/#");
#ifdef DEBUG
      if (rc) {
        Serial.println("config subscripted");      
      }
#endif
    } else {
#ifdef DEBUG
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
#endif
      delay(5000);
    }
  }
}

/*
   This function converts the read analogue values to the real values for current Amperes
   used.

*/
int analog2watt(int analog_value)
{
  float f =   config_incline * ((float)analog_value);
  return (config_zero) + f;
}

/*
   Interrupt routine for impulses at PORT_IN_WH that
   count the consumed watthours from the power network.
*/
void out()
{
  out_cnt++;
}
/*
   Interrupt routine for impulses at PORT_OUT_WH that
   count the delivered watthours to the power network.
*/
void in()
{
  in_cnt++;
}

/*
   Setup routine, setup serial, setup interrupts and intialize the time counters.
*/
void setup()
{
#ifdef DEBUG
  Serial.begin(9600);
#endif
  mqttClient.setServer(server, 1883);
  Ethernet.begin(mac, ip);
  // Allow the hardware to sort itself out
  delay(1500);
  attachInterrupt(0, in, RISING);
  attachInterrupt(1, out, RISING);
  lasttime = currenttime = millis();
}

/*
   Endless loop for measurements and sending the values
*/
void loop()
{
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();
  currenttime = millis();
  if ((currenttime - lasttime) > 1000) {
    // one second has been passed, so deliver values over serial connection
    lasttime = currenttime; // remember the last time the values have been delivered
    analog_plain = analogRead(PORT_ANALOGUE);
    analog = analog2watt(analog_plain);
    analog_str = String(analog);
    analog_str.toCharArray(analog_chr, 16);
    analog_plain_str = String(analog_plain);
    analog_plain_str.toCharArray(analog_plain_chr, 16);
    mqttClient.publish("home/techroom/mz/analog", analog_chr);
    delay(100);
    mqttClient.publish("home/techroom/mz/analog_plain", analog_plain_chr);
    delay(100);
    if (out_cnt > 0) {
      mqttClient.publish("home/techroom/mz/out", itoa(out_cnt, out_cnt_str, 10));
      out_cnt = 0;
      delay(100);
    }
    if (in_cnt > 0) {
      mqttClient.publish("home/techroom/mz/in", itoa(in_cnt, in_cnt_str, 10));
      in_cnt = 0;
      delay(100);
    }


    
#ifdef DEBUG
    Serial.print(analog_plain, DEC);
    Serial.print(", ");    
    Serial.println(analog, DEC);  // send it
#endif
    analog = 0;
  }
}

