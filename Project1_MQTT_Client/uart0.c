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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "tm4c123gh6pm.h"
#include "uart0.h"
#include "gpio.h"

// Pins
#define UART_TX PORTA,1
#define UART_RX PORTA,0

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Initialize UART0
void initUart0(void)
{
    // Enable clocks
    SYSCTL_RCGCUART_R |= SYSCTL_RCGCUART_R0;
    _delay_cycles(3);
    enablePort(PORTA);

    // Configure UART0 pins
    selectPinPushPullOutput(UART_TX);
    selectPinDigitalInput(UART_RX);
    setPinAuxFunction(UART_TX, GPIO_PCTL_PA1_U0TX);
    setPinAuxFunction(UART_RX, GPIO_PCTL_PA0_U0RX);

    // Configure UART0 with default baud rate
    UART0_CTL_R = 0;                                    // turn-off UART0 to allow safe programming
    UART0_CC_R = UART_CC_CS_SYSCLK;                     // use system clock (usually 40 MHz)
}

// Set baud rate as function of instruction cycle frequency
void setUart0BaudRate(uint32_t baudRate, uint32_t fcyc)
{
    uint32_t divisorTimes128 = (fcyc * 8) / baudRate;   // calculate divisor (r) in units of 1/128,
                                                        // where r = fcyc / 16 * baudRate
    UART0_CTL_R = 0;                                    // turn-off UART0 to allow safe programming
    UART0_IBRD_R = divisorTimes128 >> 7;                // set integer value to floor(r)
    UART0_FBRD_R = ((divisorTimes128 + 1) >> 1) & 63;   // set fractional value to round(fract(r)*64)
    UART0_LCRH_R = UART_LCRH_WLEN_8 | UART_LCRH_FEN;    // configure for 8N1 w/ 16-level FIFO
    UART0_CTL_R = UART_CTL_TXE | UART_CTL_RXE | UART_CTL_UARTEN;
                                                        // turn-on UART0
}

// Blocking function that writes a serial character when the UART buffer is not full
void putcUart0(char c)
{
    while (UART0_FR_R & UART_FR_TXFF);               // wait if uart0 tx fifo full
    UART0_DR_R = c;                                  // write character to fifo
}

// Blocking function that writes a string when the UART buffer is not full
void putsUart0(char* str)
{
    uint8_t i = 0;
    while (str[i] != '\0')
        putcUart0(str[i++]);
}

// Blocking function that returns with serial data once the buffer is not empty
char getcUart0(void)
{
    while (UART0_FR_R & UART_FR_RXFE);               // wait if uart0 rx fifo empty
    return UART0_DR_R & 0xFF;                        // get character from fifo
}

// Returns the status of the receive buffer
bool kbhitUart0(void)
{
    return !(UART0_FR_R & UART_FR_RXFE);
}

// Function that returns with string of serial data when it exceeds MAXCHAR/Enter is pressed
void getsUart0(USER_DATA* data)
{
 uint8_t count=0;                   //variable to keep track of string length
 char c;
    while(count < MAX_CHARS)              //Iterite only when length is less than MAX
        {
            c=getcUart0();
            if((c == 8)||(c==127))  //check if its backspace
            {
                if (count>0)
                {
                    count=count-1;  //If characters already entered in serial data,decrement count
                    continue;
                }
                else
                {
                    continue;       //else get next character
                }
            }
            else if((c==13)||(c==10))//If character is carriage return or line space
            {
                data->buffer[count]= '\0' ;
                break;              //return the string
            }
            else if(c >= 32)
            {
                data->buffer[count++] = c;   //else move the character to the string
                if (count == MAX_CHARS)   //If it has reached maximum allowed character
                {
                 data->buffer[count]= '\0' ;
                 break;             //break and return the string
                }
            }
            else
            {
                continue;           //else get next Character
            }
        }
    return;                     //Return from function
}

//Function to parse the string and replace delimiters with Null and calculate position of useful literals
void parseFields(USER_DATA* data)
{
    uint8_t i=0;
    uint8_t j=0;
    while(data->buffer[i] != '\0')       //Loop Until Strings last character
    {
        if((data->buffer[i] >= 97) && (data->buffer[i] <= 122))     //convert all lower case characters to upper case for easier parsing
        {
            data->buffer[i] = data->buffer[i] - 32;
        }
        if(i==0)                //for first character check if useful or delimiter
        {
            if((data->buffer[i]>47 && data->buffer[i]<58) || (data->buffer[i]>64 && data->buffer[i]<91) || (data->buffer[i]>96 && data->buffer[i]<122) /*|| (data->buffer[i]==44) || (data->buffer[i]==46)*/)
            {
                data->fieldPositon[j]=i;       //if useful chracter store the position into pos array
                j++;            //increment index of pos array
                i++;            //increment string index to parse next character
            }
            else
            {
                data->buffer[i] = 0;     //if its a delimiter,replace it with null
                i++;            //increment string index to parse next character
            }
        }
        else
        {
            if((data->buffer[i]>47 && data->buffer[i]<58) || (data->buffer[i]>64 && data->buffer[i]<91) || (data->buffer[i]>96 && data->buffer[i]<122) /*|| (data->buffer[i]==44) || (data->buffer[i]==46) || (data->buffer[i]==45)*/) //check if 0+i characters are not delimiers
            {
                if(data->buffer[i-1] == 0)       //check if the previous character was a delimiter which is now 0
                {
                    data->fieldPositon[j]=i;           //if yes store the index into position array
                    j++;                //increment index of pos array
                    i++;                //increment string index to parse next character
                }
                else
                {
                    i++;                //if not just increment the string index
                }
            }
            else
            {
                data->buffer[i]=0;               //replace delimiter with null
                i++;                    //increment the string index
            }
        }
        if (j <= MAX_FIELDS-1)                  //check if its under maximum valid arguments
        {
            continue;                   //if yes continue for further iterations
        }
        else
        {
            putsUart0("Exceeded argument limit,discarding unnecessary arguments");
            putsUart0("\r\n");
            return;                     //else exit the loop
        }
    }
    data->fieldCount = j+1;
    return;
}


//function to return an field from the String based on field number
char* getFieldString(USER_DATA* data,uint8_t fieldNumber)
{
    return &data->buffer[data->fieldPositon[fieldNumber-1]];
}


//function to convert field String into Integer
uint32_t getFieldInt(USER_DATA* data,uint8_t fieldNumber)
{
    return atoi(getFieldString(data,fieldNumber));      //return integer of argument number argPos from sting str
}

//function to convert field String into Integer
float getFieldFloat(USER_DATA* data,uint8_t fieldNumber)
{
    return atof(getFieldString(data,fieldNumber));      //return float of argument number argPos from sting str
}

//Function to check if the argument is valid
bool isCommand(USER_DATA* data,char verb[20],uint8_t minField)
{
    if (stringCompare(verb,"REBOOT") == true)
    {
        if ((stringCompare(getFieldString(data,1), "REBOOT") == true) && (data->fieldCount >= minField+1))
        return true;
    }
    else if (stringCompare(verb,"STATUS") == true)
    {
        if ((stringCompare(getFieldString(data,1), "STATUS") == true) && (data->fieldCount >= minField+1))
        return true;
    }
    else if (stringCompare(verb,"CONNECT") == true)
    {
        if ((stringCompare(getFieldString(data,1), "CONNECT") == true) && (data->fieldCount >= minField+1))
        return true;
    }
    else if (stringCompare(verb,"DISCONNECT") == true)
    {
        if ((stringCompare(getFieldString(data,1), "DISCONNECT") == true) && (data->fieldCount >= minField+1))
        return true;
    }
    else if (stringCompare(verb,"SUBSCRIBE") == true)
    {
        if ((stringCompare(getFieldString(data,1), "SUBSCRIBE") == true) && (data->fieldCount >= minField+2))
        return true;
    }
    else if (stringCompare(verb,"UNSUBSCRIBE") == true)
    {
        if ((stringCompare(getFieldString(data,1), "UNSUBSCRIBE") == true) && (data->fieldCount >= minField+2))
        return true;
    }
    else if (stringCompare(verb,"PUBLISH") == true)
    {
        if ((stringCompare(getFieldString(data,1), "PUBLISH") == true) && (data->fieldCount >= minField+2))
        return true;
    }
    else if (stringCompare(verb,"SET") == true)
    {
        if ((stringCompare(getFieldString(data,1), "SET") == true) && ((stringCompare(getFieldString(data,2), "IP") == true) || (stringCompare(getFieldString(data,2), "MQTT") == true)) && (data->fieldCount >= minField+2))
        return true;
    }
    else
    {
        return false;
    }
    return false;
}


//Personal String Compare function
bool stringCompare(char str1[], char str2[])
{
    int ctr=0;
    while(str1[ctr]==str2[ctr])
    {
        if(str1[ctr]=='\0'||str2[ctr]=='\0')
            break;
        ctr++;
    }
    if(str1[ctr]=='\0' && str2[ctr]=='\0')
        return true;
    else
        return false;
}


void reverse(char* s, uint8_t l)
{
    uint8_t i=0,j=l-1;
    char temp;
    for(i=0;i<j;i++)
    {
       temp=s[i];
       s[i]=s[j];
       s[j]=temp;
       j--;
    }
}

//reffered from geeksforgeeks
char* itoa(int num,char* str, int base)
{
    int i = 0;
    bool isNegative = false;

    /* Handle 0 explicitly, otherwise empty string is printed for 0 */
    if (num == 0)
    {
        str[i++] = '0';
        while(num == 0 && i <= 7 && base == 16)
        {
            str[i++] = '0';
        }
        str[i] = '\0';
        return str;
    }

    // In standard itoa(), negative numbers are handled only with
    // base 10. Otherwise numbers are considered unsigned.
    if (num < 0 && base == 10)
    {
        isNegative = true;
        num = -num;
    }

    // Process individual digits
    while (num != 0)
    {
        int rem = num % base;
        str[i++] = (rem > 9)? (rem-10) + 'A' : rem + '0';
        num = num/base;
    }

    // If number is negative, append '-'
    if (isNegative)
        str[i++] = '-';

    while(num == 0 && i <= 7 && base == 16)
    {
        str[i++] = '0';
    }
    str[i] = '\0'; // Append string terminator

    // Reverse the string
    reverse(str, i);

    return str;
}

uint16_t strLen(char* string)
{
    uint16_t i = 0;
    while(string[i] != 0)
    {
        i++;
    }
    return i;
}
