// TempServer 0.1
// Written by Mike Cherry <mcherry@inditech.org>
// Uses code examples from Ethernet library and various sources online
//
// Free for any use
//

#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Time.h>
#include <OneWire.h>

// CST DST == -5
#define TimeZoneOffset 5

// create a new 1wire object on digital input 2
OneWire ds(2);

// setup mac and IP address - CHANGE THIS!
byte mac[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
IPAddress ip(192, 168, 1, 250);

 // time.nist.gov NTP server
IPAddress timeServer(132, 163, 4, 101);

// local port to listen for UDP packets
unsigned int localPort = 8888;

// NTP time stamp is in the first 48 bytes of the message
const int NTP_PACKET_SIZE = 48; 

//buffer to hold incoming and outgoing packets
byte packetBuffer[NTP_PACKET_SIZE]; 

EthernetUDP Udp;
EthernetServer server(80);

int GetTempC()
{
  byte i;
  byte present = 0;
  byte data[12];
  byte addr[8];
  
  double tempC;
  double tempF;
  
  int isNeg = 0;
  int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract;
  
  char message[17];
  
  // If we can't find an address of a 1wire sensor to work with,
  // we need to keep looking. sometimes this takes 1 or 2 loops
  if (!ds.search(addr)) {
      ds.reset_search();
      return 10000;
  }
  
  // reset the 1wire object and connect to the address of the device
  // we found
  ds.reset();
  ds.select(addr);
  ds.write(0x44,1);
  
  // a small delay to let the 1wire device initialize
  delay(1000);
  
  present = ds.reset();
  
  ds.select(addr); 
  ds.write(0xBE);
  
  // read 9 bytes from the device
  for ( i = 0; i < 9; i++) {
    data[i] = ds.read();
  }
  
  LowByte = data[0];
  HighByte = data[1];
  TReading = (HighByte << 8) + LowByte;
  SignBit = TReading & 0x8000;
  
  // convert the hex data returned from the device to something usable
  if (SignBit)
  {
    TReading = (TReading ^ 0xffff) + 1;
  }
  Tc_100 = (6 * TReading) + TReading / 4;
  
  // separate off the whole and fractional portions of the temperature
  Whole = Tc_100 / 100;
  Fract = Tc_100 % 100;

  // this would be a negative (below 0 temperature)
  if (SignBit)
  {
     isNeg++;
  }
  
  // round the temperature down if the fractional portion is under .10 degrees
  if (Fract < 10)
  {
     Fract = 0;
  }

  // construct the actual temperature reading with the fraction  
  if (isNeg == 1)
  {
    sprintf(message, "-%d.%d", Whole, Fract);
  }
  else
  {
    sprintf(message, "%d.%d", Whole, Fract);
  }
  
  // convert it to a double from a char
  tempC = atof(message);
  
  // we just want to return whole numbers, so round it up
  if (Fract > .49)
  {
    tempC++;
  }
  
  return tempC;
}

// return temperature in F
int GetTempF()
{
  int tempC = 10000;
  int tempF = 0;
  int realTempF = 0;
  
  // if we didn't get a good reading, keep trying until we do
  while (tempC == 10000)
  {
    tempC = GetTempC();
  }
  
  tempF = tempC * 9 / 5 + 32;
  
  return tempF;
}

void setup()
{
  // start serial for debugging
  Serial.begin(9600);
  
  while (!Serial)
  {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  
  // start the Ethernet connection and the server:
  Ethernet.begin(mac, ip);
  Udp.begin(localPort);
  server.begin();
}

void loop()
{
  // listen for incoming clients
  EthernetClient client = server.available();
  
  if (client) {
    boolean currentLineIsBlank = true;
    char message[255];
    int tempF = 0;
    
    while (client.connected())
    {
      if (client.available())
      {
        char c = client.read();
        
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank)
        {
          time_t t = getTime();
          
          tempF = GetTempF();
          
          sprintf(message, "%d-%02d-%02d %02d:%02d:%02d,%lu,%d", year(t), month(t), day(t), hour(t-(TimeZoneOffset*60*60)), minute(t), second(t), t, tempF);
          Serial.println(message);
          
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connnection: close");
          client.println();
          
          // output the timestamp & temperature string
          client.println(message);
                    
          break;
        }
        if (c == '\n')
        {
          // you're starting a new line
          currentLineIsBlank = true;
        } 
        else if (c != '\r')
        {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    
    // give the web browser time to receive the data
    delay(1);
    
    // close the connection:
    client.stop();
  }
}

// send an NTP request to the time server at the given address 
unsigned long sendNTPpacket(IPAddress& address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE); 

  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision

  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49; 
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp: 		   
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer,NTP_PACKET_SIZE);
  Udp.endPacket(); 
}

unsigned long getTime()
{
  // send an NTP packet to a time server
  sendNTPpacket(timeServer);

  // wait to see if a reply is available
  delay(1000);
  
  if (Udp.parsePacket()) {  
    // We've received a packet, read the data from it
    Udp.read(packetBuffer,NTP_PACKET_SIZE);  // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;  
    
    // now convert NTP time into everyday time:
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;     
    
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;  
    
    return epoch;
  }
  
  return 0;
}
