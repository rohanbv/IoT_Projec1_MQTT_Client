// ETH0 Library
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

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <eth0.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "tm4c123gh6pm.h"
#include "wait.h"
#include "gpio.h"
#include "spi0.h"
#include "uart0.h"

// Pins
#define CS PORTA,3
#define WOL PORTB,3
#define INT PORTC,6

// Ether registers
#define ERDPTL      0x00
#define ERDPTH      0x01
#define EWRPTL      0x02
#define EWRPTH      0x03
#define ETXSTL      0x04
#define ETXSTH      0x05
#define ETXNDL      0x06
#define ETXNDH      0x07
#define ERXSTL      0x08
#define ERXSTH      0x09
#define ERXNDL      0x0A
#define ERXNDH      0x0B
#define ERXRDPTL    0x0C
#define ERXRDPTH    0x0D
#define ERXWRPTL    0x0E
#define ERXWRPTH    0x0F
#define EIE         0x1B
#define EIR         0x1C
#define RXERIF  0x01
#define TXERIF  0x02
#define TXIF    0x08
#define PKTIF   0x40
#define ESTAT       0x1D
#define CLKRDY  0x01
#define TXABORT 0x02
#define ECON2       0x1E
#define PKTDEC  0x40
#define ECON1       0x1F
#define RXEN    0x04
#define TXRTS   0x08
#define ERXFCON     0x38
#define EPKTCNT     0x39
#define MACON1      0x40
#define MARXEN  0x01
#define RXPAUS  0x04
#define TXPAUS  0x08
#define MACON2      0x41
#define MARST   0x80
#define MACON3      0x42
#define FULDPX  0x01
#define FRMLNEN 0x02
#define TXCRCEN 0x10
#define PAD60   0x20
#define MACON4      0x43
#define MABBIPG     0x44
#define MAIPGL      0x46
#define MAIPGH      0x47
#define MACLCON1    0x48
#define MACLCON2    0x49
#define MAMXFLL     0x4A
#define MAMXFLH     0x4B
#define MICMD       0x52
#define MIIRD   0x01
#define MIREGADR    0x54
#define MIWRL       0x56
#define MIWRH       0x57
#define MIRDL       0x58
#define MIRDH       0x59
#define MAADR1      0x60
#define MAADR0      0x61
#define MAADR3      0x62
#define MAADR2      0x63
#define MAADR5      0x64
#define MAADR4      0x65
#define MISTAT      0x6A
#define MIBUSY  0x01
#define ECOCON      0x75

// Ether phy registers
#define PHCON1      0x00
#define PDPXMD 0x0100
#define PHSTAT1     0x01
#define LSTAT  0x0400
#define PHCON2      0x10
#define HDLDIS 0x0100
#define PHLCON      0x14

// Packets
#define IP_ADD_LENGTH 4
#define HW_ADD_LENGTH 6

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint8_t nextPacketLsb = 0x00;
uint8_t nextPacketMsb = 0x00;
uint8_t sequenceId = 1;
uint8_t macAddress[HW_ADD_LENGTH] = {2,3,4,5,6,7};
uint8_t ipAddress[IP_ADD_LENGTH] = {0,0,0,0};
uint8_t ipSubnetMask[IP_ADD_LENGTH] = {255,255,255,0};
uint8_t ipGwAddress[IP_ADD_LENGTH] = {0,0,0,0};
uint8_t mqttBrokerIpAddress[IP_ADD_LENGTH] = {0,0,0,0};
uint8_t mqttBrokerMacAddress[HW_ADD_LENGTH] = {0,0,0,0,0,0};
extern uint32_t sequenceNumber;
extern uint32_t acknowledgementNumber;
extern uint32_t payLoadLength;
bool    dhcpEnabled = true;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Buffer is configured as follows
// Receive buffer starts at 0x0000 (bottom 6666 bytes of 8K space)
// Transmit buffer at 01A0A (top 1526 bytes of 8K space)

void etherCsOn()
{
    setPinValue(CS, 0);
    _delay_cycles(4);                    // allow line to settle
}

void etherCsOff()
{
    setPinValue(CS, 1);
}

void etherWriteReg(uint8_t reg, uint8_t data)
{
    etherCsOn();
    writeSpi0Data(0x40 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(data);
    readSpi0Data();
    etherCsOff();
}

uint8_t etherReadReg(uint8_t reg)
{
    uint8_t data;
    etherCsOn();
    writeSpi0Data(0x00 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(0);
    data = readSpi0Data();
    etherCsOff();
    return data;
}

void etherSetReg(uint8_t reg, uint8_t mask)
{
    etherCsOn();
    writeSpi0Data(0x80 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(mask);
    readSpi0Data();
    etherCsOff();
}

void etherClearReg(uint8_t reg, uint8_t mask)
{
    etherCsOn();
    writeSpi0Data(0xA0 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(mask);
    readSpi0Data();
    etherCsOff();
}

void etherSetBank(uint8_t reg)
{
    etherClearReg(ECON1, 0x03);
    etherSetReg(ECON1, reg >> 5);
}

void etherWritePhy(uint8_t reg, uint16_t data)
{
    etherSetBank(MIREGADR);
    etherWriteReg(MIREGADR, reg);
    etherWriteReg(MIWRL, data & 0xFF);
    etherWriteReg(MIWRH, (data >> 8) & 0xFF);
}

uint16_t etherReadPhy(uint8_t reg)
{
    uint16_t data, dataH;
    etherSetBank(MIREGADR);
    etherWriteReg(MIREGADR, reg);
    etherWriteReg(MICMD, MIIRD);
    waitMicrosecond(11);
    etherSetBank(MISTAT);
    while ((etherReadReg(MISTAT) & MIBUSY) != 0);
    etherSetBank(MICMD);
    etherWriteReg(MICMD, 0);
    data = etherReadReg(MIRDL);
    dataH = etherReadReg(MIRDH);
    data |= (dataH << 8);
    return data;
}

void etherWriteMemStart()
{
    etherCsOn();
    writeSpi0Data(0x7A);
    readSpi0Data();
}

void etherWriteMem(uint8_t data)
{
    writeSpi0Data(data);
    readSpi0Data();
}

void etherWriteMemStop()
{
    etherCsOff();
}

void etherReadMemStart()
{
    etherCsOn();
    writeSpi0Data(0x3A);
    readSpi0Data();
}

uint8_t etherReadMem()
{
    writeSpi0Data(0);
    return readSpi0Data();
}

void etherReadMemStop()
{
    etherCsOff();
}

// Initializes ethernet device
// Uses order suggested in Chapter 6 of datasheet except 6.4 OST which is first here
void etherInit(uint16_t mode)
{
    // Initialize SPI0
    initSpi0(USE_SSI0_RX);
    setSpi0BaudRate(4e6, 40e6);
    setSpi0Mode(0, 0);

    // Enable clocks
    enablePort(PORTA);
    enablePort(PORTB);
    enablePort(PORTC);

    // Configure pins for ethernet module
    selectPinPushPullOutput(CS);
    selectPinDigitalInput(WOL);
    selectPinDigitalInput(INT);

    // make sure that oscillator start-up timer has expired
    while ((etherReadReg(ESTAT) & CLKRDY) == 0) {}

    // disable transmission and reception of packets
    etherClearReg(ECON1, RXEN);
    etherClearReg(ECON1, TXRTS);

    // initialize receive buffer space
    etherSetBank(ERXSTL);
    etherWriteReg(ERXSTL, LOBYTE(0x0000));
    etherWriteReg(ERXSTH, HIBYTE(0x0000));
    etherWriteReg(ERXNDL, LOBYTE(0x1A09));
    etherWriteReg(ERXNDH, HIBYTE(0x1A09));
   
    // initialize receiver write and read ptrs
    // at startup, will write from 0 to 1A08 only and will not overwrite rd ptr
    etherWriteReg(ERXWRPTL, LOBYTE(0x0000));
    etherWriteReg(ERXWRPTH, HIBYTE(0x0000));
    etherWriteReg(ERXRDPTL, LOBYTE(0x1A09));
    etherWriteReg(ERXRDPTH, HIBYTE(0x1A09));
    etherWriteReg(ERDPTL, LOBYTE(0x0000));
    etherWriteReg(ERDPTH, HIBYTE(0x0000));

    // setup receive filter
    // always check CRC, use OR mode
    etherSetBank(ERXFCON);
    etherWriteReg(ERXFCON, (mode | ETHER_CHECKCRC) & 0xFF);

    // bring mac out of reset
    etherSetBank(MACON2);
    etherWriteReg(MACON2, 0);
  
    // enable mac rx, enable pause control for full duplex
    etherWriteReg(MACON1, TXPAUS | RXPAUS | MARXEN);

    // enable padding to 60 bytes (no runt packets)
    // add crc to tx packets, set full or half duplex
    if ((mode & ETHER_FULLDUPLEX) != 0)
        etherWriteReg(MACON3, FULDPX | FRMLNEN | TXCRCEN | PAD60);
    else
        etherWriteReg(MACON3, FRMLNEN | TXCRCEN | PAD60);

    // leave MACON4 as reset

    // set maximum rx packet size
    etherWriteReg(MAMXFLL, LOBYTE(1518));
    etherWriteReg(MAMXFLH, HIBYTE(1518));

    // set back-to-back inter-packet gap to 9.6us
    if ((mode & ETHER_FULLDUPLEX) != 0)
        etherWriteReg(MABBIPG, 0x15);
    else
        etherWriteReg(MABBIPG, 0x12);

    // set non-back-to-back inter-packet gap registers
    etherWriteReg(MAIPGL, 0x12);
    etherWriteReg(MAIPGH, 0x0C);

    // leave collision window MACLCON2 as reset

    // setup mac address
    etherSetBank(MAADR0);
    etherWriteReg(MAADR5, macAddress[0]);
    etherWriteReg(MAADR4, macAddress[1]);
    etherWriteReg(MAADR3, macAddress[2]);
    etherWriteReg(MAADR2, macAddress[3]);
    etherWriteReg(MAADR1, macAddress[4]);
    etherWriteReg(MAADR0, macAddress[5]);

    // initialize phy duplex
    if ((mode & ETHER_FULLDUPLEX) != 0)
        etherWritePhy(PHCON1, PDPXMD);
    else
        etherWritePhy(PHCON1, 0);

    // disable phy loopback if in half-duplex mode
    etherWritePhy(PHCON2, HDLDIS);

    // Flash LEDA and LEDB
    etherWritePhy(PHLCON, 0x0880);
    waitMicrosecond(100000);

    // set LEDA (link status) and LEDB (tx/rx activity)
    // stretch LED on to 40ms (default)
    etherWritePhy(PHLCON, 0x0472);

    // enable reception
    etherSetReg(ECON1, RXEN);
}

// Returns true if link is up
bool etherIsLinkUp()
{
    return (etherReadPhy(PHSTAT1) & LSTAT) != 0;
}

// Returns TRUE if packet received
bool etherIsDataAvailable()
{
    return ((etherReadReg(EIR) & PKTIF) != 0);
}

// Returns true if rx buffer overflowed after correcting the problem
bool etherIsOverflow()
{
    bool err;
    err = (etherReadReg(EIR) & RXERIF) != 0;
    if (err)
        etherClearReg(EIR, RXERIF);
    return err;
}

// Returns up to max_size characters in data buffer
// Returns number of bytes copied to buffer
// Contents written are 16-bit size, 16-bit status, payload excl crc
uint16_t etherGetPacket(etherHeader *ether, uint16_t maxSize)
{
    uint16_t i = 0, size, tmp16, status;
    uint8_t *packet = (uint8_t*)ether;

    // enable read from FIFO buffers
    etherReadMemStart();

    // get next packet information
    nextPacketLsb = etherReadMem();
    nextPacketMsb = etherReadMem();

    // calc size
    // don't return crc, instead return size + status, so size is correct
    size = etherReadMem();
    tmp16 = etherReadMem();
    size |= (tmp16 << 8);

    // get status (currently unused)
    status = etherReadMem();
    tmp16 = etherReadMem();
    status |= (tmp16 << 8);

    // copy data
    if (size > maxSize)
        size = maxSize;
    while (i < size)
        packet[i++] = etherReadMem();

    // end read from FIFO buffers
    etherReadMemStop();

    // advance read pointer
    etherSetBank(ERXRDPTL);
    etherWriteReg(ERXRDPTL, nextPacketLsb); // hw ptr
    etherWriteReg(ERXRDPTH, nextPacketMsb);
    etherWriteReg(ERDPTL, nextPacketLsb);   // dma rd ptr
    etherWriteReg(ERDPTH, nextPacketMsb);

    // decrement packet counter so that PKTIF is maintained correctly
    etherSetReg(ECON2, PKTDEC);

    return size;
}

// Writes a packet
bool etherPutPacket(etherHeader *ether, uint16_t size)
{
    uint16_t i;
    uint8_t *packet = (uint8_t*) ether;

    // clear out any tx errors
    if ((etherReadReg(EIR) & TXERIF) != 0)
    {
        etherClearReg(EIR, TXERIF);
        etherSetReg(ECON1, TXRTS);
        etherClearReg(ECON1, TXRTS);
    }

    // set DMA start address
    etherSetBank(EWRPTL);
    etherWriteReg(EWRPTL, LOBYTE(0x1A0A));
    etherWriteReg(EWRPTH, HIBYTE(0x1A0A));

    // start FIFO buffer write
    etherWriteMemStart();

    // write control byte
    etherWriteMem(0);

    // write data
    for (i = 0; i < size; i++)
        etherWriteMem(packet[i]);

    // stop write
    etherWriteMemStop();
  
    // request transmit
    etherWriteReg(ETXSTL, LOBYTE(0x1A0A));
    etherWriteReg(ETXSTH, HIBYTE(0x1A0A));
    etherWriteReg(ETXNDL, LOBYTE(0x1A0A+size));
    etherWriteReg(ETXNDH, HIBYTE(0x1A0A+size));
    etherClearReg(EIR, TXIF);
    etherSetReg(ECON1, TXRTS);

    // wait for completion
    while ((etherReadReg(ECON1) & TXRTS) != 0);

    // determine success
    return ((etherReadReg(ESTAT) & TXABORT) == 0);
}

// Calculate sum of words
// Must use getEtherChecksum to complete 1's compliment addition
void etherSumWords(void* data, uint16_t sizeInBytes, uint32_t* sum)
{
    uint8_t* pData = (uint8_t*)data;
    uint16_t i;
    uint8_t phase = 0;
    uint16_t data_temp;
    for (i = 0; i < sizeInBytes; i++)
    {
        if (phase)
        {
            data_temp = *pData;
            *sum += data_temp << 8;
        }
        else
          *sum += *pData;
        phase = 1 - phase;
        pData++;
    }
}

// Completes 1's compliment addition by folding carries back into field
uint16_t getEtherChecksum(uint32_t sum)
{
    uint16_t result;
    // this is based on rfc1071
    while ((sum >> 16) > 0)
      sum = (sum & 0xFFFF) + (sum >> 16);
    result = sum & 0xFFFF;
    return ~result;
}

void etherCalcIpChecksum(ipHeader *ip)
{
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    uint32_t sum = 0;
    // 32-bit sum over ip header
    ip->headerChecksum = 0;
    etherSumWords(ip, ipHeaderLength, &sum);
    ip->headerChecksum = getEtherChecksum(sum);
}

// Converts from host to network order and vice versa
uint16_t htons(uint16_t value)
{
    return ((value & 0xFF00) >> 8) + ((value & 0x00FF) << 8);
}
#define ntohs htons


uint32_t htonl(uint32_t value)
{
    return ((value & 0xFF000000) >> 24) + ((value & 0x00FF0000) >> 8)
            + ((value & 0x0000FF00) << 8) + ((value & 0x000000FF) << 24);
}

// Determines whether packet is IP datagram
bool etherIsIp(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    uint32_t sum = 0;
    bool ok;
    ok = (ether->frameType == htons(0x0800));
    if (ok)
    {
        etherSumWords(&ip->revSize, ipHeaderLength, &sum);
        ok = (getEtherChecksum(sum) == 0);
    }
    return ok;
}

// Determines whether packet is unicast to this ip
// Must be an IP packet
bool etherIsIpUnicast(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t i = 0;
    bool ok = true;
    while (ok & (i < IP_ADD_LENGTH))
    {
        ok = (ip->destIp[i] == ipAddress[i]);
        i++;
    }
    return ok;
}

// Determines whether packet is ping request
// Must be an IP packet
bool etherIsPingRequest(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    icmpHeader *icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    return (ip->protocol == 0x01 & icmp->type == 8);
}

// Sends a ping response given the request data
void etherSendPingResponse(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    icmpHeader *icmp = (icmpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t i, tmp;
    uint16_t icmp_size;
    uint32_t sum = 0;
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        tmp = ether->destAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = tmp;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp = ip->destIp[i];
        ip->destIp[i] = ip ->sourceIp[i];
        ip->sourceIp[i] = tmp;
    }
    // this is a response
    icmp->type = 0;
    // calc icmp checksum
    icmp->check = 0;
    icmp_size = ntohs(ip->length) - ipHeaderLength;
    etherSumWords(icmp, icmp_size, &sum);
    icmp->check = getEtherChecksum(sum);
    // send packet
    etherPutPacket(ether, sizeof(etherHeader) + ntohs(ip->length));
}

// Determines whether packet is ARP request
bool etherIsArpRequest(etherHeader *ether)
{
    arpPacket *arp = (arpPacket*)ether->data;
    bool ok;
    uint8_t i = 0;
    ok = (ether->frameType == htons(0x0806));
    while (ok & (i < IP_ADD_LENGTH))
    {
        ok = (arp->destIp[i] == ipAddress[i]);
        i++;
    }
    if (ok)
        ok = (arp->op == htons(1));
    return ok;
}

//Determines whether packet is ARP reply
bool etherIsArpReply(etherHeader* ether)
{
    arpPacket *arp = (arpPacket*)ether->data;
    bool ok;
    uint8_t i = 0;
    ok = (ether->frameType == htons(0x0806));
    while (ok & (i < IP_ADD_LENGTH))
    {
        ok = (arp->destIp[i] == ipAddress[i]);
        i++;
    }
    if (ok)
        ok = (arp->op == htons(2));
    return ok;
}

// Sends an ARP response given the request data
void etherSendArpResponse(etherHeader *ether)
{
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i, tmp;
    // set op to response
    arp->op = htons(2);
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        arp->destAddress[i] = arp->sourceAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = arp->sourceAddress[i] = macAddress[i];
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp = arp->destIp[i];
        arp->destIp[i] = arp->sourceIp[i];
        arp->sourceIp[i] = tmp;
    }
    // send packet
    etherPutPacket(ether, sizeof(etherHeader) + sizeof(arpPacket));
}

// Sends an ARP request
void etherSendArpRequest(etherHeader *ether, uint8_t ip[])
{
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i;
    // fill ethernet frame
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = 0xFF;
        ether->sourceAddress[i] = macAddress[i];
    }
    ether->frameType = 0x0608;
    // fill arp frame
    arp->hardwareType = htons(1);
    arp->protocolType = htons(0x0800);
    arp->hardwareSize = HW_ADD_LENGTH;
    arp->protocolSize = IP_ADD_LENGTH;
    arp->op = htons(1);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        arp->sourceAddress[i] = macAddress[i];
        arp->destAddress[i] = 0xFF;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        arp->sourceIp[i] = ipAddress[i];
        arp->destIp[i] = ip[i];
    }
    // send packet
    etherPutPacket(ether, sizeof(etherHeader) + sizeof(arpPacket));
}

// Determines whether packet is UDP datagram
// Must be an IP packet
bool etherIsUdp(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    bool ok;
    uint16_t tmp16;
    uint32_t sum = 0;
    ok = (ip->protocol == 0x11);
    if (ok)
    {
        // 32-bit sum over pseudo-header
        etherSumWords(ip->sourceIp, 8, &sum);
        tmp16 = ip->protocol;
        sum += (tmp16 & 0xff) << 8;
        etherSumWords(&udp->length, 2, &sum);
        // add udp header and data
        etherSumWords(udp, ntohs(udp->length), &sum);
        ok = (getEtherChecksum(sum) == 0);
    }
    return ok;
}

// Gets pointer to UDP payload of frame
uint8_t * etherGetUdpData(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    return udp->data;
}

// Send responses to a udp datagram 
// destination port, ip, and hardware address are extracted from provided data
// uses destination port of received packet as destination of this packet
void etherSendUdpResponse(etherHeader *ether, uint8_t *udpData, uint8_t udpSize)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t *copyData;
    uint8_t i, tmp8;
    uint16_t tmp16;
    uint16_t udpLength;
    uint32_t sum = 0;

    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        tmp8 = ether->destAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = tmp8;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp8 = ip->destIp[i];
        ip->destIp[i] = ip->sourceIp[i];
        ip->sourceIp[i] = tmp8;
    }
    // set source port of resp will be dest port of req
    // dest port of resp will be left at source port of req
    // unusual nomenclature, but this allows a different tx
    // and rx port on other machine
    udp->sourcePort = udp->destPort;
    // adjust lengths
    udpLength = 8 + udpSize;
    ip->length = htons(ipHeaderLength + udpLength);
    // 32-bit sum over ip header
    etherCalcIpChecksum(ip);
    // set udp length
    udp->length = htons(udpLength);
    // copy data
    copyData = udp->data;
    for (i = 0; i < udpSize; i++)
        copyData[i] = udpData[i];
    // 32-bit sum over pseudo-header
    etherSumWords(ip->sourceIp, 8, &sum);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&udp->length, 2, &sum);
    // add udp header
    udp->check = 0;
    etherSumWords(udp, udpLength, &sum);
    udp->check = getEtherChecksum(sum);

    // send packet with size = ether + udp hdr + ip header + udp_size
    etherPutPacket(ether, sizeof(etherHeader) + ipHeaderLength + udpLength);
}

uint16_t etherGetId()
{
    return htons(sequenceId);
}

void etherIncId()
{
    sequenceId++;
}

// Enable or disable DHCP mode
void etherEnableDhcpMode()
{
    dhcpEnabled = true;
}

void etherDisableDhcpMode()
{
    dhcpEnabled = false;
}

bool etherIsDhcpEnabled()
{
    return dhcpEnabled;
}

// Determines if the IP address is valid
bool etherIsIpValid()
{
    return ipAddress[0] || ipAddress[1] || ipAddress[2] || ipAddress[3];
}

// Sets IP address
void etherSetIpAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3)
{
    ipAddress[0] = ip0;
    ipAddress[1] = ip1;
    ipAddress[2] = ip2;
    ipAddress[3] = ip3;
}

// Gets IP address
void etherGetIpAddress(uint8_t ip[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        ip[i] = ipAddress[i];
}

// Sets IP subnet mask
void etherSetIpSubnetMask(uint8_t mask0, uint8_t mask1, uint8_t mask2, uint8_t mask3)
{
    ipSubnetMask[0] = mask0;
    ipSubnetMask[1] = mask1;
    ipSubnetMask[2] = mask2;
    ipSubnetMask[3] = mask3;
}

// Gets IP subnet mask
void etherGetIpSubnetMask(uint8_t mask[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        mask[i] = ipSubnetMask[i];
}

// Sets IP gateway address
void etherSetIpGatewayAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3)
{
    ipGwAddress[0] = ip0;
    ipGwAddress[1] = ip1;
    ipGwAddress[2] = ip2;
    ipGwAddress[3] = ip3;
}

// Gets IP gateway address
void etherGetIpGatewayAddress(uint8_t ip[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        ip[i] = ipGwAddress[i];
}

// Sets MAC address
void etherSetMacAddress(uint8_t mac0, uint8_t mac1, uint8_t mac2, uint8_t mac3, uint8_t mac4, uint8_t mac5)
{
    macAddress[0] = mac0;
    macAddress[1] = mac1;
    macAddress[2] = mac2;
    macAddress[3] = mac3;
    macAddress[4] = mac4;
    macAddress[5] = mac5;
}

// Gets MAC address
void etherGetMacAddress(uint8_t mac[6])
{
    uint8_t i;
    for (i = 0; i < 6; i++)
        mac[i] = macAddress[i];
}

// Set MQTT Broker Address
void etherSetMqttBrokerIp(uint8_t mqttBIp0,uint8_t mqttBIp1,uint8_t mqttBIp2,uint8_t mqttBIp3)
{
    mqttBrokerIpAddress[0] = mqttBIp0;
    mqttBrokerIpAddress[1] = mqttBIp1;
    mqttBrokerIpAddress[2] = mqttBIp2;
    mqttBrokerIpAddress[3] = mqttBIp3;
}

// Gets MqttBroker IP Address
void etherGetMqttBrokerIpAddress(uint8_t mqttBIp[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        mqttBIp[i] = mqttBrokerIpAddress[i];
}

// Sets MQTT Broker MAC Address
void etherSetMqttBrokerHW(uint8_t mqttBMac0,uint8_t mqttBMac1,uint8_t mqttBMac2,uint8_t mqttBMac3,uint8_t mqttBMac4,uint8_t mqttBMac5)
{
    mqttBrokerMacAddress[0] = mqttBMac0;
    mqttBrokerMacAddress[1] = mqttBMac1;
    mqttBrokerMacAddress[2] = mqttBMac2;
    mqttBrokerMacAddress[3] = mqttBMac3;
    mqttBrokerMacAddress[4] = mqttBMac4;
    mqttBrokerMacAddress[5] = mqttBMac5;
}

// Gets MQTT Broker MAC address
void etherGetMqttBrokerMacAddress(uint8_t mqttBMac[6])
{
    uint8_t i;
    for (i = 0; i < 6; i++)
        mqttBMac[i] = mqttBrokerMacAddress[i];
}

//Store MAC Address from ARP response
void etherStoreMqttMacAddress(etherHeader* ether)
{
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i;
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        mqttBrokerMacAddress[i] = arp->sourceAddress[i];
    }
}

//Fills up the Socket
void etherFillUpMqttConnectionSocket(socket* s)
{
    etherGetMqttBrokerMacAddress(s->destAddress);
    etherGetMacAddress(s->sourceAddress);
    etherGetMqttBrokerIpAddress(s->destIp);
    etherGetIpAddress(s->sourceIp);
    s->destPort = htons(1883);
    s->sourcePort = htons(110);
}

//Sends a TCP Message
void etherSendTcp(etherHeader* ether,socket* s,uint16_t flags,uint8_t* tcpData,uint16_t dataLength)
{
    uint8_t i,tcpDataOffset;
    uint8_t* copyData;
    etherFillUpMqttConnectionSocket(s);
    //Fill up ethernet Header
    for(i = 0;i < HW_ADD_LENGTH;i++)
        ether->sourceAddress[i] = s->sourceAddress[i];
    for(i = 0;i < HW_ADD_LENGTH;i++)
        ether->destAddress[i] = s->destAddress[i];
    ether->frameType = htons(0x0800);

    ipHeader* ip = (ipHeader*)&ether->data;
    //Fill up IP Header from socket
    for(i = 0;i < IP_ADD_LENGTH;i++)
        ip->sourceIp[i] = s->sourceIp[i];
    for(i = 0;i < IP_ADD_LENGTH;i++)
        ip->destIp[i] = s->destIp[i];
    ip->revSize = 0x45;
    ip->typeOfService = 0x00;
    ip->id = 0x0000;
    ip->flagsAndOffset = 0x0000;
    ip->protocol = 6;
    ip->ttl = 128;

    tcpHeader* tcp = (tcpHeader*)((uint8_t*)ip +((ip->revSize & 0xF) * 4));
    //Fill up Tcp Header
    tcp->sourcePort = s->sourcePort;
    tcp->destPort = s->destPort;
    if(flags == TCP_SYNC)
    {
        sequenceNumber = 0;
        acknowledgementNumber = 0;
        tcp->sequenceNumber = sequenceNumber;
        tcp->acknowledgementNumber = acknowledgementNumber;
    }
    else
    {
        tcp->sequenceNumber = sequenceNumber;
        tcp->acknowledgementNumber = acknowledgementNumber;
    }
    tcp->urgentPointer = 0x0000;
    tcp->windowSize = htons(1220);


    if(flags == TCP_SYNC)
    {
        tcpDataOffset = 6;
        tcp->offsetFields = htons((tcpDataOffset << 12) + TCP_SYNC);
        //Hardcoding up the options field
         uint8_t *tcpOptions = (uint8_t*)&tcp->data;
         tcpOptions[0] = 0x02;  //Kind
         tcpOptions[1] = 0x04;  //Length
         tcpOptions[2] = 0x04;  //Value
         tcpOptions[3] = 0xc4;
    }
    if(flags == TCP_ACK || flags == TCP_FIN_ACK || flags == TCP_RESET || flags == TCP_REST_ACK || flags== TCP_FIN)
    {
        tcpDataOffset = 5;
        tcp->offsetFields = htons((tcpDataOffset << 12) + flags);
    }
    if(flags == TCP_PUSH_ACK)
    {
        tcpDataOffset = 5;
        tcp->offsetFields = htons((tcpDataOffset << 12) + TCP_PUSH_ACK);
        copyData = tcp->data;
        for(i = 0;i < dataLength;i++)
            copyData[i] = tcpData[i];
    }
    uint16_t tcpLength = (tcpDataOffset*4) + dataLength;
    //Calculate length of IP message and IP header Checksum
    ip->length = htons(((ip->revSize & 0xF) * 4) + tcpLength);
    etherCalcIpChecksum(ip);

    //Calculate size of TCP Header
    tcp->checksum = 0;
    uint32_t sum = 0;
    uint16_t temp = 0;
    etherSumWords(ip->sourceIp, 8, &sum);
    temp = (ip->protocol & 0xff) << 8;
    sum += temp;
    sum+= htons(tcpLength);
    //add contents of tcp message
    etherSumWords(tcp, (tcpLength), &sum);
    tcp->checksum = getEtherChecksum(sum);
    etherPutPacket(ether, sizeof(etherHeader) + ((ip->revSize & 0xF) * 4) + tcpLength);
}


//Is it a TCP packet
bool etherIsTcp(etherHeader *ether)
{
    bool ok;
    ipHeader *ip = (ipHeader*)ether->data;
    ok = (ip->protocol == 0x06);
    return ok;
}

//Is it a TCP Acknowledgement
bool etherIsTcpAck(etherHeader *ether)
{
    bool ok = false;
    if(etherIsTcp(ether))
            {
                ipHeader *ip = (ipHeader*)ether->data;
                uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
                tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
                uint16_t tcpFieldType = htons(tcp->offsetFields) & 0x0FFF;
                ok = (tcpFieldType == TCP_SYNACK || tcpFieldType == TCP_ACK || tcpFieldType == TCP_PUSH_ACK || tcpFieldType == TCP_FIN_ACK || tcpFieldType == TCP_REST_ACK);
                return ok;
            }
    return ok;
}

bool etherIsTcpFinAck(etherHeader* ether)
{
    bool ok = true;
    uint8_t i = 0;
    for(i = 0;i < HW_ADD_LENGTH;i++)
        ok &= (ether->destAddress[i] == macAddress[i]);
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint16_t tcpFieldType = htons(tcp->offsetFields) & 0x0FFF;
    ok = (tcpFieldType == TCP_FIN_ACK);
    return ok;
}

bool etherIsTcpResetAck(etherHeader* ether)
{
    bool ok = true;
    uint8_t i = 0;
    for(i = 0;i < HW_ADD_LENGTH;i++)
        ok &= (ether->destAddress[i] == macAddress[i]);
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint16_t tcpFieldType = htons(tcp->offsetFields) & 0x0FFF;
    ok = (tcpFieldType == TCP_REST_ACK);
    return ok;
}

//Check if the packet is Mqtt Connection Acknowledgement
bool etherIsMqttConnectAck(etherHeader* ether)
{
    bool ok = true;
    uint8_t i = 0;
    for(i = 0;i < HW_ADD_LENGTH;i++)
        ok &= (ether->destAddress[i] == macAddress[i]);
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t* copyData = tcp->data;
    if(ok)
    {
        ok = (copyData[0] == 0x20); //Is it Connect Ack Mqtt Payload
        ok &= (copyData[2] == 0x00);//It it an accepted connection
    }
    return ok;
}

//Check if the packet is Mqtt SubscribeAck Packet
bool etherIsMqttSubAck(etherHeader* ether)
{
    bool ok = true;
    uint8_t i = 0;
    for(i = 0;i < HW_ADD_LENGTH;i++)
        ok &= (ether->destAddress[i] == macAddress[i]);
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t* copyData = tcp->data;
    if(ok)
    {
        ok &= (copyData[0] == 0x90);
        payLoadLength = copyData[1] + 2;
    }
    return ok;
}

//Check if the packet is Mqtt UnSubscribeAck Packet
bool etherIsMqttUnSubAck(etherHeader* ether)
{
    bool ok = true;
    uint8_t i = 0;
    for(i = 0;i < HW_ADD_LENGTH;i++)
        ok &= (ether->destAddress[i] == macAddress[i]);
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t* copyData = tcp->data;
    if(ok)
    {
        ok &= (copyData[0] == 0xb0);
        payLoadLength = copyData[1] + 2;
    }
    return ok;
}

//Check if the packet is Mqtt UnSubscribeAck Packet
bool etherIsMqttPublish(etherHeader* ether)
{
    bool ok = true;
    uint8_t i = 0;
    for(i = 0;i < HW_ADD_LENGTH;i++)
        ok &= (ether->destAddress[i] == macAddress[i]);
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t* copyData = tcp->data;
    if(ok)
    {
        ok &= (copyData[0] == 0x30);
        payLoadLength = copyData[1] + 2;
    }
    return ok;
}


//Create a MQTT connect Data Payload
uint8_t* etherMqttCreateConnectPayload(uint8_t* mqttPayload)
{
    mqttPayload[0] = 0x10;
    mqttPayload[1] = 15;
    mqttPayload[2] = 0x00;
    mqttPayload[3] = 0x04;
    mqttPayload[4] = (uint8_t)'M';
    mqttPayload[5] = (uint8_t)'Q';
    mqttPayload[6] = (uint8_t)'T';
    mqttPayload[7] = (uint8_t)'T';
    mqttPayload[8] = 0x04;
    mqttPayload[9] = 0x02;
    mqttPayload[10] = 0x00;
    mqttPayload[11] = 0x3c;
    mqttPayload[12] = 0x00;
    mqttPayload[13] = 0x03;
    mqttPayload[14] = (uint8_t)'r';
    mqttPayload[15] = (uint8_t)'b';
    mqttPayload[16] = (uint8_t)'v';
    payLoadLength = mqttPayload[1]+2;
    return mqttPayload;
}

//Creates a Mqtt Subscribe payload to topic subTopic
uint8_t* etherMqttCreateSubscribePayload(uint8_t* mqttPayload,char* subTopic)
{
    uint16_t subTopicLength = strLen(subTopic);
    uint8_t i;
    mqttPayload[0] = 0x82;
    mqttPayload[1] = subTopicLength + 2 + 2 + 1;
    mqttPayload[2] = 0x00;
    mqttPayload[3] = 0x0C;
    uint16_t* ptr = (uint16_t*)&mqttPayload[4];
    *ptr = htons(subTopicLength);
    for(i = 0;i < subTopicLength; i++)
        mqttPayload[i+6] = subTopic[i];
    mqttPayload[i+6] = 0x00;
    payLoadLength = mqttPayload[1]+2;
    return mqttPayload;
}

//Creates a Mqtt Unsubscribe Payload to topic subTopic
uint8_t* etherMqttCreateUnSubscribePayload(uint8_t* mqttPayload,char* subTopic)
{
    uint16_t subTopicLength = strLen(subTopic);
    uint8_t i;
    mqttPayload[0] = 0xA2;
    mqttPayload[1] = subTopicLength + 2 + 2;
    mqttPayload[2] = 0x00;
    mqttPayload[3] = 0x0C;
    uint16_t* ptr = (uint16_t*)&mqttPayload[4];
    *ptr = htons(subTopicLength);
    for(i = 0;i < subTopicLength; i++)
        mqttPayload[i+6] = subTopic[i];
    mqttPayload[i+6] = 0x00;
    payLoadLength = mqttPayload[1]+2;
    return mqttPayload;
}

//Creates a Mqtt Topic Payload with passed parametres topic and data
uint8_t* etherMqttCreatePublishPayload(uint8_t* mqttPayload,char* topic,char* data)
{
    uint8_t i,j;
    uint16_t topicLength = strLen(topic);
    uint16_t dataLength = strLen(data);
    mqttPayload[0] = 0x30;
    mqttPayload[1] = topicLength + dataLength + 2 + 2;
    uint16_t* ptr = (uint16_t*)&mqttPayload[2];
    *ptr = htons(topicLength);
    for(i = 0;i < topicLength;i++)
        mqttPayload[i+4] = topic[i];
    mqttPayload[i+4] = 0x10;
    mqttPayload[i+5] = 0x11 + i;
    for(j = 0;j < dataLength;j++)
        mqttPayload[i+6+j] = data[j];
    payLoadLength = mqttPayload[1] + 2;
    return mqttPayload;
}

uint8_t* etherMqttCreateDisconnectPayload(uint8_t* mqttPayload)
{
    mqttPayload[0] = 0xE0;
    mqttPayload[1] = 0x00;
    payLoadLength = 2;
    return mqttPayload;
}

void printPublishData(etherHeader* ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint8_t* copyData = tcp->data;
    uint16_t* ptr = (uint16_t*)&copyData[2];
    uint16_t topicLength = htons(*ptr);
    char str[20];
    uint8_t i,j;
    putsUart0("There has been a publish to topic you have subscribed\r\n");
    putsUart0("Topic Length : ");
    itoa((uint16_t)topicLength,str,10);
    putsUart0(str);
    putsUart0("\r\n");
    putsUart0("Topic : ");
    for(i = 0;i < topicLength;i++)
    {
        putcUart0((char)copyData[i+4]);
    }
    putsUart0("\r\n");
    uint8_t dataLength = copyData[1] - 4 - topicLength;
    putsUart0("Data Length : ");
    itoa(dataLength, str, 10);
    putsUart0(str);
    putsUart0("\r\n");
    putsUart0("Data : ");
    for(j = 0;j < dataLength;j++)
    {
        putcUart0((char)copyData[i+j+6]);
    }
    putsUart0("\r\n");

}
