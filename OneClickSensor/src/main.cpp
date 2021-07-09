// Author: Filip Georgiev - filip.vic.georgiev@gmail.com
// Got inspiration from: https://github.com/BirchJD/RPi_433MHz


#include <Arduino.h>
#include <ESP8266WiFi.h>


// Pin variables
#define BUTTON_D0 16
#define RX_D3 0
#define TX_D1 5


//Communication variables
// Digital level to switch transmitter off.
#define TX_OFF_LEVEL 0
// Digital level to switch transmitter on.
#define TX_ON_LEVEL 1

// Period to signify end of Tx message. In milisec
#define TX_END_PERIOD (0.04 * 1000)
#define TX_LEVEL_PERIOD (0.002 * 1000)

// Period to signify end of Rx message. In microsec
#define RX_END_PERIOD (0.04 * 1000000)
#define RX_REJECT_PERIOD (0.0015 * 1000000)

// Timeout for transmission message. In millisec
#define TX_TIMEOUT 5000

// Smallest period of high or low signal to consider noise rather than data, and flag as bad data. In microsec 
// Start bits transmitted to signify start of transmission.
#define RX_START_BITS 1
// Minimum received valid packet size.
#define MIN_RX_BYTES 4

#define TX_START_BITS 1


#define PACKET_SIZE 4
#define MAX_DATA_SIZE 255


// Initialise data.
bool StartBitFlag = true;
unsigned int ThisPeriod = RX_END_PERIOD;
unsigned int StartBitPeriod = RX_END_PERIOD;
unsigned int LastBitPeriod = RX_END_PERIOD;
int LastGpioLevel = 1;
int BitCount = 0;
int ByteDataCount = 0;
int ByteData[MAX_DATA_SIZE];

int CurrentTxLevel;

int PACKET_SIGNATURE[PACKET_SIZE] = {0x63, 0xF9, 0x5C, 0x1B};

struct DataPacket {
  int signature[PACKET_SIZE];
  int data_length = 0;
  int data[MAX_DATA_SIZE];
  int checksum = 0; 
}packet;


//Button variables
int buttonState;             
int lastButtonState = LOW; 

unsigned long lastDebounceTime = 0;  
unsigned long debounceDelay = 50; 

//state
enum receive_transmit {button_wait_signal, receive_state, transmit_state, wifi_connect, connected}state;
unsigned long transmit_time;

void setup() {
  
  pinMode(TX_D1, OUTPUT);
  pinMode(BUTTON_D0, INPUT);
  pinMode(RX_D3, INPUT_PULLUP);

  state = button_wait_signal; 


  Serial.begin(9600); 
   
}


void button_wait()
{
  int reading = digitalRead(BUTTON_D0);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == HIGH) {
        
        state = transmit_state;
        Serial.println("Gone into transmit");
        transmit_time = millis();
        return;


      }
    }
  }

  lastButtonState = reading;
}



void reset_data()
{
	StartBitFlag = true;
	StartBitPeriod = RX_END_PERIOD;
	BitCount = 0;
	memset(ByteData, 0, (sizeof(int) * ByteDataCount));
	ByteDataCount = 0;

	memset(packet.signature, 0, (sizeof(int) * PACKET_SIZE));
	memset(packet.data, 0, (sizeof(int) * packet.data_length));
	packet.data_length = 0;
	packet.checksum = 0;
}

void Tx433Byte(int data)
{
  uint8_t BitMask = ( 1 << 7 );
  for (size_t i = 0; i < 8; i++)
  {
    uint8_t Bit = (data & BitMask);
    BitMask = (BitMask >> 1);

    if(CurrentTxLevel == TX_OFF_LEVEL)
    {
      CurrentTxLevel = TX_ON_LEVEL;
    }
    else
    {
      CurrentTxLevel = TX_OFF_LEVEL;
    }

    digitalWrite(TX_D1, CurrentTxLevel);
    delay(TX_LEVEL_PERIOD);

    if(Bit > 0)
    {
      delay(TX_LEVEL_PERIOD);
    }

  }
  
}


void transmit()
{
  int message_size = 6;
  char message[message_size] = "Hello";
  
  packet.signature[0] = 0x63;
	packet.signature[1] = 0xF9;
	packet.signature[2] = 0x5C;
	packet.signature[3] = 0x1B;

  packet.data_length = message_size - 1; // no end character
  packet.checksum = 0;
  for (int i = 0; i < packet.data_length; i++)
  {
    packet.data[i] = message[i];
    packet.checksum ^= packet.data[i];
  }

  CurrentTxLevel = TX_ON_LEVEL;
  digitalWrite(TX_D1, CurrentTxLevel);

  for (int i = 0; i < TX_START_BITS; i++)
  {
    delay(TX_LEVEL_PERIOD);
  }

  for (int i = 0; i < PACKET_SIZE; i++)
  {
    Tx433Byte(packet.signature[i]);
  }

  Tx433Byte(packet.data_length);
  
  for (int i = 0; i < packet.data_length; i++)
  {
    Tx433Byte(packet.data[i]);
  }

  Tx433Byte(packet.checksum);
  
  CurrentTxLevel = TX_OFF_LEVEL;
  digitalWrite(TX_D1, CurrentTxLevel);

  delay(TX_END_PERIOD);

  if((millis() - transmit_time) > TX_TIMEOUT)
  {
    Serial.println("Gone in receive");
    state = receive_state;
    digitalWrite(TX_D1, TX_OFF_LEVEL);
    return;
  }

}

void receive()
{
  // Check if data is currently being received.
  unsigned int ThisPeriod = micros();
  unsigned int DiffPeriod = ThisPeriod - LastBitPeriod;

  //If data level changes, decode long period = 1, short period = 0.
  int GpioLevel = digitalRead(RX_D3);
  //cout<< GpioLevel << endl;
  if (GpioLevel != LastGpioLevel)
  {
    //Ignore noise.
    if (DiffPeriod > RX_REJECT_PERIOD)
    {
      //Wait for start of communication.
      if (StartBitFlag == true)
      {
        //Calculate start bit period, consider as period for all following bits.
        if (StartBitPeriod == RX_END_PERIOD)
        {
          StartBitPeriod = ThisPeriod;
        }
        else
        {
          StartBitPeriod = (ThisPeriod - StartBitPeriod) * 0.90;
          StartBitFlag = false;
        }
      }
      else
      {
        if (DiffPeriod < StartBitPeriod)
        {
          StartBitPeriod = DiffPeriod;
        }

        //Receiving a data level, convert into a data bit.
        int Bits = (int)(round(DiffPeriod / StartBitPeriod));
        if ((BitCount % 8) == 0)
        {
          ByteData[ByteDataCount] = 0;
          yield(); //needed so that esp8266 can function correctly
          ByteDataCount += 1;
        }
        BitCount += 1;
        ByteData[ByteDataCount - 1] = (ByteData[ByteDataCount - 1] << 1);
        if(Bits > 1)
        {
          ByteData[ByteDataCount - 1] |= 1;
        }
      }
      LastBitPeriod = ThisPeriod;
    }
    LastGpioLevel = GpioLevel;
  }
  else if(DiffPeriod > RX_END_PERIOD)
  {	
    //End of data reception.
    if((ByteDataCount >= MIN_RX_BYTES) && (StartBitPeriod > RX_REJECT_PERIOD))
    {
      int DataCount = 0;
      //Validate packet signature
      bool MatchFlag = true;
      for (int i = 0; i < PACKET_SIZE; i++) //packet signature
      {
        packet.signature[i] = ByteData[DataCount];
        if (packet.signature[DataCount] != PACKET_SIGNATURE[i])
        {
          MatchFlag = false;
          break;
        }
        DataCount += 1;
      }
      if (MatchFlag == false)
      {
        Serial.println("INVALID PACKET SIGNATURE");
      }
      else
      {
        packet.data_length = ByteData[DataCount];
        DataCount += 1;
        for (int i = 0; i < packet.data_length; i++)
        {
          packet.data[i] = ByteData[DataCount];
          DataCount += 1;
        }
        packet.checksum = ByteData[DataCount];
        DataCount += 1;

        int Checksum = 0;
        for (int i = 0; i < packet.data_length; i++)
        {
          Checksum ^= packet.data[i];
        }
        if (Checksum != packet.checksum)
        {
          Serial.println("Invalid Packet Checksum");
        }
        else
        {
          Serial.println("Gone in wifi connect");
          state = wifi_connect;
          return;
          
        }
        

      }
      
    }
    reset_data();

  }
  
}

void connect_wifi()
{
  char NetworkSSID[MAX_DATA_SIZE];
  char NetworkPassword[MAX_DATA_SIZE];

  bool pass_flag = false; //is data part ssid or password. Evaluated when it sees :, as incoming packet is <SSID>:<password>
  int data_position = 0;

  for (int i = 0; i < packet.data_length; i++)
  {
    if(!pass_flag)
    {
      NetworkSSID[i] = packet.data[i];
      if(NetworkSSID[i] == ':')
      {
        NetworkSSID[i] = '\0';
        pass_flag = true;
        continue;
      }
    }
    else
    {
      NetworkPassword[data_position++] = packet.data[i];
    }
  }
  NetworkPassword[data_position] = '\0';

  WiFi.begin(NetworkSSID, NetworkPassword);
  state = connected;
}

void loop() {
  
  switch (state)
  {
  case button_wait_signal:
    button_wait();
    break;
  case transmit_state:
    transmit();
    break;
  case receive_state:
    receive();
    break;
  case wifi_connect:
    connect_wifi();
    break;
  case connected:
    break;
  default:
    break;
  }
  
}