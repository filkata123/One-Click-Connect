// Author: Filip Georgiev - filip.vic.georgiev@gmail.com
// Got inspiration from: https://github.com/BirchJD/RPi_433MHz

#include <iostream>
#include <cstring>
#include <math.h>
#include <wiringPi.h>
using namespace std;

// GPIO Pin connected to 433MHz receiver.
#define GPIO_RX_PIN  22  // 8th pin on inside of pi (GPIO22)
// GPIO Pin connected to 433MHz transmitter.
#define GPIO_TX_PIN  17  // 6th pin on inside of pi (GPIO17)
// GPIO level to switch transmitter off.
#define TX_OFF_LEVEL 0
// GPIO level to switch transmitter on.
#define TX_ON_LEVEL  1
// Period to signify end of Rx message. In microsec
#define RX_END_PERIOD (0.04 * 1000000) //TEST: if this is slightly less than 0.04, what is going to happen? -> Maybe faster
#define RX_REJECT_PERIOD (0.0015 * 1000000)

// Period to signify end of Tx message. In millisec
#define TX_END_PERIOD (0.04 * 1000)
#define TX_LEVEL_PERIOD (0.002 * 1000)
// Timeout for transmission message. In millisec
#define TX_TIMEOUT 5000 * 2 //make sure timeout in other device has finished

// Smallest period of high or low signal to consider noise rather than data, and flag as bad data. In microsec 
// Start bits transmitted to signify start of transmission.
#define RX_START_BITS 1
// Minimum received valid packet size.
#define MIN_RX_BYTES 4

#define TX_START_BITS 1


#define PACKET_SIZE 4
#define MAX_DATA_SIZE 255

enum receive_transmit {receive_state, transmit_state}state;

unsigned long transmit_time; //Timer to not transmit infinitely

// Initialise data.
bool StartBitFlag = true;
unsigned int ThisPeriod = RX_END_PERIOD;
unsigned int StartBitPeriod = RX_END_PERIOD;
unsigned int LastBitPeriod = RX_END_PERIOD;
int LastGpioLevel = 1; //TEST: change this to 0 and see whether it changes receiving speed :)
int BitCount = 0;
int ByteDataCount = 0;
int ByteData[MAX_DATA_SIZE];

int CurrentTxLevel;

int PACKET_SIGNATURE[PACKET_SIZE] = {0x63, 0xF9, 0x5C, 0x1B};

//Data packet to transmit.
struct DataPacket {
  int signature[PACKET_SIZE];
  int data_length = 0;
  int data[MAX_DATA_SIZE];
  int checksum = 0; 
}packet;

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

void receive()
{
	bool ExitFlag = false;

	while(!ExitFlag)
	{
		// Check if data is currently being received.
		unsigned int ThisPeriod = micros();
		unsigned int DiffPeriod = ThisPeriod - LastBitPeriod;

		//If data level changes, decode long period = 1, short period = 0.
		int GpioLevel = digitalRead(GPIO_RX_PIN);
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
					cout<< ByteData[DataCount] << endl;
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
					cout<< "INVALID PACKET SIGNATURE" << endl;
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
						cout << "Invalid Packet Checksum" << endl;
					}
					else
					{
						char Data[MAX_DATA_SIZE];
						char hello[] = "Hello";
						cout << "DECRYPTED DATA: ";
						for (int i = 0; i < packet.data_length; i++)
						{
							Data[i] = packet.data[i];
							cout << Data[i];

						}
						Data[packet.data_length] = '\0';
						cout << endl;
						int result = strcmp(Data, hello);
   						if (result==0)
						{
							digitalWrite(GPIO_TX_PIN, TX_OFF_LEVEL);
							state = transmit_state;
							reset_data();
							cout << "Went into transmission" << endl;
							transmit_time = millis();
							return;
						}
						
					}
					

				}
				
			}
			reset_data();

		}
	}
}

void Tx433Byte(int data)
{
  uint8_t BitMask = ( 1 << 7);
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

    digitalWrite(GPIO_TX_PIN, CurrentTxLevel);
    delay(TX_LEVEL_PERIOD);

    if(Bit > 0)
    {
      delay(TX_LEVEL_PERIOD);
    }

  }
  
}


void transmit()
{
	int message_size = 32;
	char message[message_size] = ""; // <SSID>:<PASSWORD>

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
	digitalWrite(GPIO_TX_PIN, CurrentTxLevel);

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
	digitalWrite(GPIO_TX_PIN, CurrentTxLevel);

	delay(TX_END_PERIOD);

	if((millis() - transmit_time) > TX_TIMEOUT)
  	{
		cout << "Packet transmitted" << endl;
		exit(0);
	}
}

void setup(){

	pinMode(GPIO_TX_PIN,OUTPUT);
	pinMode(GPIO_RX_PIN,INPUT);
	pullUpDnControl(GPIO_RX_PIN, PUD_UP);
	digitalWrite(GPIO_TX_PIN, TX_OFF_LEVEL); // An important step, as otherwise the PI transmitter will implement noise to the externally transmitted data
	state = receive_state;
}

void loop() {
	switch (state)
	{
	case transmit_state:
		transmit();
		break;
	case receive_state:
		receive();
		break;
	default:
		break;
	}
}

int main(void)
{
	if(wiringPiSetupGpio()<0){
		cout<<"setup wiring pi failed"<<endl;
		return 1;
	}
	setup();
	while(1){
		loop();
	}
	
	return 0;
}