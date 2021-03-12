// UART0 Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL
// Target uC:       TM4C123GH6PM
// System Clock:    -

// Hardware configuration:
// UART Interface:
//   U0TX (PA1) and U0RX (PA0) are connected to the 2nd controller
//   The USB on the 2nd controller enumerates to an ICDI interface and a virtual COM port

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef UART0_H_
#define UART0_H_

//-----------------------------------------------------------------------------
// GLOBAL Declarations
//-----------------------------------------------------------------------------

#define MAX_CHARS 80
#define MAX_FIELDS 30
typedef struct _USER_DATA
        {
            char buffer[MAX_CHARS+1];
            uint8_t fieldCount;
            uint8_t fieldPositon[MAX_FIELDS];
            char fieldType[MAX_FIELDS];
        } USER_DATA;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initUart0(void);
void setUart0BaudRate(uint32_t baudRate, uint32_t fcyc);
void putcUart0(char c);
void putsUart0(char* str);
char getcUart0(void);
bool kbhitUart0(void);
void getsUart0(USER_DATA* data);
void parseFields(USER_DATA* data);
char* getFieldString(USER_DATA* data,uint8_t fieldNumber);
uint32_t getFieldInt(USER_DATA* data,uint8_t fieldNumber);
float getFieldFloat(USER_DATA* data,uint8_t fieldNumber);
bool isCommand(USER_DATA* data,char verb[20],uint8_t minField);
bool stringCompare(char str1[], char str2[]);
char* itoa(int num,char* str, int base);
void reverse(char* s, uint8_t l);

#endif
