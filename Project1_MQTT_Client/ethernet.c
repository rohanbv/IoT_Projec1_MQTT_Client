// Ethernet Example
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// ENC28J60 Ethernet controller on SPI0
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS (SW controlled) on PA3
//   WOL on PB3
//   INT on PC6

// Pinning for IoT projects with wireless modules:
// N24L01+ RF transceiver
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS on PE0
//   INT on PB2
// Xbee module
//   DIN (UART1TX) on PC5
//   DOUT (UART1RX) on PC4

// My MAC address assigned
// 02-03-04-05-06-112
// My IP address assigned
// 192.168.1.112

//-----------------------------------------------------------------------------
// Configuring Wireshark to examine packets
//-----------------------------------------------------------------------------

// sudo ethtool --offload eno2 tx off rx off
// in wireshark, preferences->protocol->ipv4->validate the checksum if possible
// in wireshark, preferences->protocol->udp->validate the checksum if possible

//-----------------------------------------------------------------------------
// Sending UDP test packets
//-----------------------------------------------------------------------------

// test this with a udp send utility like sendip
//   if sender IP (-is) is 192.168.1.1, this will attempt to
//   send the udp datagram (-d) to 192.168.1.199, port 1024 (-ud)
// sudo sendip -p ipv4 -is 192.168.1.1 -p udp -ud 1024 -d "on" 192.168.1.199
// sudo sendip -p ipv4 -is 192.168.1.1 -p udp -ud 1024 -d "off" 192.168.1.199

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "clock.h"
#include "gpio.h"
#include "spi0.h"
#include "uart0.h"
#include "wait.h"
#include "eth0.h"
#include "eeprom.h"
#include "tm4c123gh6pm.h"

// Pins
#define RED_LED PORTF,1
#define BLUE_LED PORTF,2
#define GREEN_LED PORTF,3
#define PUSH_BUTTON PORTF,4

//Macros
#define IP_STORED_PERSISTENTLY  100
#define MQTT_STORED_PERSITENTLY 200
#define IP_IN_EEPROM            readEeprom(0x0000) == IP_STORED_PERSISTENTLY
#define MQTT_IN_EEPROM          readEeprom(0x0010) == MQTT_STORED_PERSITENTLY

//Globals
uint32_t sequenceNumber        = 0;
uint32_t acknowledgementNumber = 0;

//-----------------------------------------------------------------------------
// Subroutines                
//-----------------------------------------------------------------------------

// Initialize Hardware
void initHw()
{
    // Initialize system clock to 40 MHz
    initSystemClockTo40Mhz();

    // Enable clocks
    enablePort(PORTF);
    _delay_cycles(3);

    // Configure LED and pushbutton pins
    selectPinPushPullOutput(RED_LED);
    selectPinPushPullOutput(GREEN_LED);
    selectPinPushPullOutput(BLUE_LED);
    selectPinDigitalInput(PUSH_BUTTON);
}

void displayConnectionInfo()
{
    uint8_t i;
    char str[10];
    uint8_t mac[6];
    uint8_t ip[4];
    etherGetMacAddress(mac);
    putsUart0("HW: ");
    for (i = 0; i < 6; i++)
    {
        sprintf(str, "%02x", mac[i]);
        putsUart0(str);
        if (i < 6-1)
            putcUart0(':');
    }
    putsUart0("\r\n");
    etherGetIpAddress(ip);
    putsUart0("IP: ");
    for (i = 0; i < 4; i++)
    {
        sprintf(str, "%u", ip[i]);
        putsUart0(str);
        if (i < 4-1)
            putcUart0('.');
    }
    if (etherIsDhcpEnabled())
        putsUart0(" (dhcp)");
    else
        putsUart0(" (static)");
    putsUart0("\r\n");
    etherGetIpSubnetMask(ip);
    putsUart0("SN: ");
    for (i = 0; i < 4; i++)
    {
        sprintf(str, "%u", ip[i]);
        putsUart0(str);
        if (i < 4-1)
            putcUart0('.');
    }
    putsUart0("\r\n");
    etherGetIpGatewayAddress(ip);
    putsUart0("GW: ");
    for (i = 0; i < 4; i++)
    {
        sprintf(str, "%u", ip[i]);
        putsUart0(str);
        if (i < 4-1)
            putcUart0('.');
    }
    putsUart0("\r\n");
    etherGetMqttBrokerIpAddress(ip);
    putsUart0("MQTT: ");
    for (i = 0; i < 4; i++)
    {
        sprintf(str, "%u", ip[i]);
        putsUart0(str);
        if (i < 4-1)
            putcUart0('.');
    }
    putsUart0("\r\n");
    etherGetMqttBrokerMacAddress(mac);
    putsUart0("MQTT HW: ");
    for (i = 0; i < 6; i++)
    {
        sprintf(str, "%02x", mac[i]);
        putsUart0(str);
        if (i < 6-1)
            putcUart0(':');
    }
    putsUart0("\r\n");
    if (etherIsLinkUp())
        putsUart0("Link is up\r\n");
    else
        putsUart0("Link is down\r\n");
}

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

// Max packet is calculated as:
// Ether frame header (18) + Max MTU (1500) + CRC (4)
#define MAX_PACKET_SIZE 1522

int main(void)
{
    uint8_t* udpData;
    uint8_t buffer[MAX_PACKET_SIZE];
    etherHeader *data = (etherHeader*) buffer;
    socket s;
    USER_DATA info;
    state currentState = idle;

    // Init controller
    initHw();

    // Setup UART0
    initUart0();
    setUart0BaudRate(115200, 40e6);

    // Setup EEPROM
    initEeprom();

    // Init ethernet interface (eth0)
    putsUart0("Starting eth0\r\n");
    etherSetMacAddress(2, 3, 4, 5, 6, 112);
    etherDisableDhcpMode();

    // Retrieve IP address from the one stored in EEPROM
    if(IP_IN_EEPROM)
        etherSetIpAddress(readEeprom(0x0060),readEeprom(0x0061),readEeprom(0x0062),readEeprom(0x0063));
    else
        etherSetIpAddress(192, 168, 1, 112);


    //Retrieve Mqtt Broker IP address if stored in EEPROM
    if(MQTT_IN_EEPROM)
        etherSetMqttBrokerIp(readEeprom(0x0080),readEeprom(0x0081),readEeprom(0x0082),readEeprom(0x0083));
    else
        etherSetMqttBrokerIp(0, 0, 0, 0);

    etherSetIpSubnetMask(255, 255, 255, 0);
    etherSetIpGatewayAddress(192, 168, 1, 1);
    etherInit(ETHER_UNICAST | ETHER_BROADCAST | ETHER_HALFDUPLEX);
    waitMicrosecond(100000);
//  displayConnectionInfo();

    // Flash LED
    setPinValue(GREEN_LED, 1);
    waitMicrosecond(100000);
    setPinValue(GREEN_LED, 0);
    waitMicrosecond(100000);

    // Main Loop
    // RTOS and interrupts would greatly improve this code,
    // but the goal here is simplicity
    while (true)
    {
        // Put terminal processing here
        if (kbhitUart0())
        {
            getsUart0(&info);        //get uart data into a USER INFO structure
            putsUart0(info.buffer);  //display the info onto the terminal
            putsUart0("\n\r");
            parseFields(&info);      //parse information in the buffer and store it in the structure
            //Is Command Reboot, Yes then perform Reset
            if(isCommand(&info, "REBOOT", 0))
            {
                putsUart0("Is a Valid Command for Reset,Performing System Reset\r\n");
                waitMicrosecond(10000);
                NVIC_APINT_R = NVIC_APINT_VECTKEY | NVIC_APINT_SYSRESETREQ ;
            }
            //Is command Status,if yes display
            else if(isCommand(&info, "STATUS", 0))
            {
                displayConnectionInfo();
            }
            //Is Command Set, if yes set IP
            else if(isCommand(&info, "SET", 2))
            {
                if(stringCompare(getFieldString(&info, 2), "IP"))
                {
                    etherSetIpAddress(getFieldInt(&info,3),getFieldInt(&info,4),getFieldInt(&info,5),getFieldInt(&info,6));
                    writeEeprom(0x0000,IP_STORED_PERSISTENTLY);
                    writeEeprom(0x0060,getFieldInt(&info,3));
                    writeEeprom(0x0061,getFieldInt(&info,4));
                    writeEeprom(0x0062,getFieldInt(&info,5));
                    writeEeprom(0x0063,getFieldInt(&info,6));
                }
                else if(stringCompare(getFieldString(&info, 2), "MQTT"))
                {
                    etherSetMqttBrokerIp(getFieldInt(&info, 3), getFieldInt(&info, 4), getFieldInt(&info, 5), getFieldInt(&info, 6));
                    writeEeprom(0x0010, MQTT_STORED_PERSITENTLY);
                    writeEeprom(0x0080,getFieldInt(&info,3));
                    writeEeprom(0x0081,getFieldInt(&info,4));
                    writeEeprom(0x0082,getFieldInt(&info,5));
                    writeEeprom(0x0083,getFieldInt(&info,6));
                }
            }
            //Is command Connect,Send arp and get MAC address of MQTT server
            else if(isCommand(&info, "CONNECT", 0))
            {
                currentState = sendArpReq;
            }
            else
            {
                putsUart0("Enter a Valid Command\r\n");
            }

        }

        //Check if the machine is in sendArpReq,if it is then send and wait for Arp Response
        if(currentState == sendArpReq)
        {
            uint8_t mqttBIp[4];
            etherGetMqttBrokerIpAddress(mqttBIp);
            etherSendArpRequest(data,mqttBIp);
            currentState = waitArpRes;
        }

        //Check if the machine is in sendSync state,if its send sync message and wait for syncack
        if(currentState == sendTcpSyn)
        {
            etherSendTcp(data, &s, TCP_SYNC);
            currentState = waitTcpSynAck;
        }

        if(currentState == sendTcpAck)
        {
            etherSendTcp(data, &s, TCP_ACK);
            setPinValue(BLUE_LED, 1);
            currentState = tcpConnectionActive;
        }

        // Packet processing
        if (etherIsDataAvailable())
        {
            if (etherIsOverflow())
            {
                setPinValue(RED_LED, 1);
                waitMicrosecond(100000);
                setPinValue(RED_LED, 0);
            }

            // Get packet
            etherGetPacket(data, MAX_PACKET_SIZE);

            // Handle ARP request
            if (etherIsArpRequest(data))
            {
                etherSendArpResponse(data);
            }

            //Handle ARP Reply
            if(etherIsArpReply(data) && (currentState == waitArpRes))
            {
                etherStoreMqttMacAddress(data);
                currentState = sendTcpSyn;
            }

            // Handle IP datagram
            if (etherIsIp(data))
            {
            	if (etherIsIpUnicast(data))
            	{
            	    //Handle TCP Datagrams
            	    if(etherIsTcp(data))
            	    {
            	        //Is it Ack To a Sync Message?
            	        if(etherIsTcpAck(data) && (currentState == waitTcpSynAck))
            	        {
            	            //Update Sequence and Acknowledge Numbers
                            etherHeader* ether = (etherHeader*)data;
            	            ipHeader *ip = (ipHeader*)ether->data;
                            uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
                            tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
                            acknowledgementNumber = tcp->sequenceNumber;
                            sequenceNumber = tcp->acknowledgementNumber;
                            currentState = sendTcpAck;
            	        }
            	    }

            		// handle icmp ping request
					if (etherIsPingRequest(data))
					{
					  etherSendPingResponse(data);
					}

					// Process UDP datagram
					if (etherIsUdp(data))
					{
						udpData = etherGetUdpData(data);
						if (strcmp((char*)udpData, "on") == 0)
			                setPinValue(GREEN_LED, 1);
                        if (strcmp((char*)udpData, "off") == 0)
			                setPinValue(GREEN_LED, 0);
						etherSendUdpResponse(data, (uint8_t*)"Received", 9);
					}
                }
            }
        }
    }
}
