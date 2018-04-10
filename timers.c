/* Purpose: Use interrupt driven timers to read ADC data and request
 * a specific number of clock cycles
 *
 * Class: CEC322
 * Authors: Sean Link and Andrew Hostetter
 * Date: 2/20/2018
 *
 */
// Base includes with the timers Examples
#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/debug.h"
#include "driverlib/fpu.h"
#include "driverlib/interrupt.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "driverlib/rom.h"
#include "grlib/grlib.h"
#include "drivers/cfal96x64x16.h"

// Necessary for UART
#include "driverlib/uart.h"
#include <string.h>

// Necessary for lower case string conversion
#include <ctype.h>

// Necessary to write data to a temporary character buffer to ultimately display
#include <stdio.h>

// Necessary for ADC
#include "driverlib/adc.h"

// Necessary for blinking LED
#include "driverlib/gpio.h"

//****************************************************************************
// Defines and typedefs
//****************************************************************************
// Necessary for ADC
#define ADC_SEQUENCE_3 3

// Necessary to switch between 16 MHz and 66 MHz
//#define SIXTYSIX_MHZ 1

// Necessary for blinking light clock
#define FIVE_PERCENT_CYCLE_ON 20000
#define NIENTYFIVE_PERCENT_CYCLE_OFF 380000

// Necessary for displayInfoOnBoard()
typedef enum {
    DISPLAY_OFF = 0, DISPLAY_NUMBER = 1, DISPLAY_BAR = 2, DISPLAY_COUNT = 3
} DisplayMode;

//****************************************************************************
// Globals
//****************************************************************************
tContext sContext;
uint32_t countsPerSecond;
uint32_t characterFromComputer;
//bool enableCounter;
DisplayMode servicedDisplay;
DisplayMode requestedDisplay;


//*****************************************************************************
// Prototypes
//*****************************************************************************
void diplayADCInfoOnBoard(uint8_t* formatString,uint32_t ADCValue,
                          uint32_t yLocationOnDisplay, DisplayMode displayMode);
void clearBlack(void);
void UARTSend(const uint8_t *pui8Buffer);
void printMainMenu(void);
void diplaySplashOnOLED(void);

//*****************************************************************************
// The interrupt handler for the first timer interrupt triggers every second.
// Displays information to the board, checks for UART Characters, and scales
// the requested cycles from the ADC
//*****************************************************************************
void
Timer0IntHandler(void)
{
    // Clear the timer interrupt.
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);

    // Initializing variables
    uint32_t ADCValue[1];
    uint32_t desiredFrequency;

    //Clear OLED
    clearBlack();

    if (ADCIntStatus(ADC0_BASE, ADC_SEQUENCE_3, false)) {

        // Clear the ADC interrupt flag.
        ADCIntClear(ADC0_BASE, ADC_SEQUENCE_3);

        // Get the data
        ADCSequenceDataGet(ADC0_BASE, ADC_SEQUENCE_3, ADCValue);

        // Re-triggering ADC conversion
        ADCProcessorTrigger(ADC0_BASE, ADC_SEQUENCE_3);

        // Calculating desired frequency
        #ifdef SIXTYSIX_MHZ
            desiredFrequency = ADCValue[0] * 500;
        #else
            desiredFrequency = ADCValue[0] * 159;
        #endif

        // Changing Frequency Requested should the requested and actual not match
        if (countsPerSecond != desiredFrequency) {
            TimerDisable(TIMER1_BASE, TIMER_A);
            TimerLoadSet(TIMER1_BASE, TIMER_A, SysCtlClockGet() / desiredFrequency);
            TimerEnable(TIMER1_BASE,TIMER_A);
        }

        // Displaying request frequency
        diplayADCInfoOnBoard("Reqst: %d", desiredFrequency, 20, requestedDisplay);
    }

    // Display serviced frequency
    diplayADCInfoOnBoard("Srv: %d", countsPerSecond, 35, servicedDisplay);

    // Checking if a character is available
    if (UARTCharsAvail(UART0_BASE)) {
        characterFromComputer = UARTCharGetNonBlocking(UART0_BASE);
    }
    else {
        characterFromComputer = '\0';
    }

    // Resetting counts per second
    countsPerSecond = 0;
}

//*****************************************************************************
// Purpose: Count every time this interrupt is called.
//*****************************************************************************
void
Timer1IntHandler(void)
{
    // Clear the timer interrupt.
    TimerIntClear(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
    countsPerSecond++;

}

int
main(void)
{
    // Enable lazy stacking for interrupt handlers.  This allows floating-point
    // instructions to be used within interrupt handlers, but at the expense of
    // extra stack usage.
    FPULazyStackingEnable();

    #ifdef SIXTYSIX_MHZ
    SysCtlClockSet(SYSCTL_SYSDIV_3 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
                           SYSCTL_XTAL_16MHZ);
    #else
    // Set the clocking to run directly from the crystal.
    SysCtlClockSet(SYSCTL_SYSDIV_1 | SYSCTL_USE_OSC | SYSCTL_OSC_MAIN |
                       SYSCTL_XTAL_16MHZ);
    #endif

    // Initialize the display driver.
    CFAL96x64x16Init();

    // Disabling all Interrupts
    IntMasterDisable();


    // Initialize the graphics context and find the middle X coordinate.
    GrContextInit(&sContext, &g_sCFAL96x64x16);
    GrContextFontSet(&sContext, g_psFontFixed6x8);

    //************************************************************************
    // Enabling the peripherals
    //************************************************************************
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);     // For UART
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOG);      // For LED

    //*************************************************************************
    // Checking if the peripheral is turned on
    //*************************************************************************
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_UART0) ||
            !SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0) ||
            !SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0) ||
            !SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER1) ||
            !SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA) ||
            !SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOG));

    //*************************************************************************
    // Configuration
    //*************************************************************************
    // Set GPIO A0 and A1 as UART pins.
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    // UART for 115,200, 8-N-1 operation.
    // Configure the UART for 115,200, 8-N-1 operation.
    UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200,
                            (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                             UART_CONFIG_PAR_NONE));

    // Disable ADC Sequence for configuration
    ADCSequenceDisable(ADC0_BASE, ADC_SEQUENCE_3);

    // Configuring the ADC to read from the ADC0_BASE, 3rd sequence,
    // trigger processing, and to take priority
    ADCSequenceConfigure(ADC0_BASE, ADC_SEQUENCE_3, ADC_TRIGGER_PROCESSOR, 0);

    // Configuring the ADCSequence to read from the 0 step
    ADCSequenceStepConfigure(ADC0_BASE, ADC_SEQUENCE_3, 0, ADC_CTL_CH7 |
                                              ADC_CTL_IE | ADC_CTL_END);
    // Enabling sequence
    ADCSequenceEnable(ADC0_BASE, ADC_SEQUENCE_3);

    // Enable the GPIO pin for the LED (PG2).  Set the direction as output, and
    // enable the GPIO pin for digital function.
    GPIOPinTypeGPIOOutput(GPIO_PORTG_BASE, GPIO_PIN_2);

    //************************************************************************
    // Enable processor interrupts.
    //************************************************************************
    IntMasterEnable();

    // Configuring the two 32-bit periodic timers.
    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    TimerConfigure(TIMER1_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(TIMER0_BASE, TIMER_A, SysCtlClockGet());
    TimerLoadSet(TIMER1_BASE, TIMER_A, SysCtlClockGet() / 2);

    //
    // Setup the interrupts for the timer timeouts.
    //
    IntEnable(INT_TIMER0A);
    IntEnable(INT_TIMER1A);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    TimerIntEnable(TIMER1_BASE, TIMER_TIMA_TIMEOUT);

    //
    // Enable the timers.
    //
    TimerEnable(TIMER0_BASE, TIMER_A);
    TimerEnable(TIMER1_BASE, TIMER_A);

    //************************************************************************
    // Initializing Variables
    //************************************************************************
    uint32_t blinkingLightCounter = 0;
    countsPerSecond = 0;
    servicedDisplay = DISPLAY_NUMBER;
    requestedDisplay = DISPLAY_NUMBER;
    bool enableLED = 1;
    //enableCounter = 0;

    //************************************************************************
    // starting functional calls and main while loop
    //************************************************************************

    // Displaying Splash Screen
    diplaySplashOnOLED();

    // Displaying UART Menu
    printMainMenu();

    // Clearing interrupt flag for the ADC
    ADCIntClear(ADC0_BASE, ADC_SEQUENCE_3);

    // Start reading the ADC
    ADCProcessorTrigger(ADC0_BASE, ADC_SEQUENCE_3);

    while(tolower(characterFromComputer != 'q'))
    {
        // Blinking the LED
        if (blinkingLightCounter %
                (FIVE_PERCENT_CYCLE_ON + NIENTYFIVE_PERCENT_CYCLE_OFF) <=
                FIVE_PERCENT_CYCLE_ON && enableLED == 1) {
            GPIOPinWrite(GPIO_PORTG_BASE, GPIO_PIN_2, GPIO_PIN_2);
        }
        else {
            GPIOPinWrite(GPIO_PORTG_BASE, GPIO_PIN_2, 0);
        }

        //********************************************************************
        // Functional calls dependent on menu selection
        //********************************************************************
        switch(tolower(characterFromComputer)) {
            case 't' :
                enableLED = !enableLED;
                break;
            case 's' :
                IntMasterDisable();
                clearBlack();
                diplaySplashOnOLED();
                IntMasterEnable();
                break;
            case 'c' :
                //enableCounter = !enableCounter;
                break;
            case '1' :
                // Toggle the display
                requestedDisplay = (requestedDisplay + 1) % DISPLAY_COUNT;
                break;
            case '2' :
                // Toggle the display
                servicedDisplay = (servicedDisplay + 1) % DISPLAY_COUNT;
                break;
        }
        // Changing charter to null manually in case the interrupt does
        // not trigger in time
        characterFromComputer = '\0';

        blinkingLightCounter++;
    }
    clearBlack();
}

// Purpose: Display data to the OLED display
void diplayADCInfoOnBoard(uint8_t* formatString,uint32_t ADCValue,
                          uint32_t yLocationOnDisplay, DisplayMode displayMode) {
    if(displayMode == DISPLAY_NUMBER) {
        uint8_t displayDataBuffer[16];

        // Setting the text to be white
        GrContextForegroundSet(&sContext, ClrWhite);

        sprintf((char*)displayDataBuffer, (char*)formatString, ADCValue);

        // Prints number to the OLED display
        GrStringDrawCentered(&sContext,(char*)displayDataBuffer, -1,
                             GrContextDpyWidthGet(&sContext) / 2, yLocationOnDisplay, true);
    }
    else if(displayMode == DISPLAY_BAR) {
        tRectangle sRect;

        sRect.i16XMin = 0;
        sRect.i16YMin = yLocationOnDisplay - 4;
        sRect.i16YMax = yLocationOnDisplay + 4;

        // Scaling data down to fit on screen
        sRect.i16XMax = ADCValue / 6783;

        // Setting color of display
        GrContextForegroundSet(&sContext, ClrWhite);

        // Writing bar to screen
        GrRectFill(&sContext, &sRect);
    }
}

// Purpose: Prints a black box over the entire OLED display
void clearBlack(void){

    tRectangle sRect;
    sRect.i16XMin = 0;
    sRect.i16YMin = 0;
    sRect.i16XMax = GrContextDpyWidthGet(&sContext) - 1;
    sRect.i16YMax = 69;
    GrContextForegroundSet(&sContext, ClrBlack);
    GrRectFill(&sContext, &sRect);
}

// Purpose: Send a message to the Console display
void UARTSend(const uint8_t *pui8Buffer)
{
    // Loop while there are more characters to send.
    for(uint32_t index = 0; index < strlen((const char *)pui8Buffer); index++)
    {
        // Write the next character to the UART.
        UARTCharPut(UART0_BASE, pui8Buffer[index]);
    }
}

// Purpose: Display the splash screen on the OLED display
void diplaySplashOnOLED(void) {

    tRectangle sRect;

    clearBlack();

    // Fill the top part of the screen with blue to create the banner.
    sRect.i16XMin = 0;
    sRect.i16YMin = 0;
    sRect.i16XMax = GrContextDpyWidthGet(&sContext) - 1;
    sRect.i16YMax = 9;
    GrContextForegroundSet(&sContext, ClrDarkBlue);
    GrRectFill(&sContext, &sRect);

    // Change foreground for white text.
    GrContextForegroundSet(&sContext, ClrWhite);

    // Put the lab name in the middle of the banner.
    GrContextFontSet(&sContext, g_psFontFixed6x8);
    GrStringDrawCentered(&sContext, "Link_Hostetter", -1,
                         GrContextDpyWidthGet(&sContext) / 2, 4, 0);

}

// Purpose: Print the main menu to the UART Console
// Note to grader: I am keeping the multiple successive calls
// beacuse it reads better and I do not want to have one long string that
// goes past the 80th character. I think it is justified having slightly less
// Efficient code if the code reads better and the code in question is user driven
// and does not need to run as fast as possible.
void printMainMenu(void) {
    UARTSend("\r\n\nT - Toggle the LED\r\n"
            "S - Splash Screen (2s)\r\n"
            "1 - Toggle Data Display Requested\n\r"
            "2 - Toggle Data Display Serviced\n\r"
            "C - Toggle Count Display\n\r"
            "Q - Quit Program \n\r");
}

//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
#ifdef DEBUG
void
__error__(char *pcFilename, uint32_t ui32Line)
{
}
#endif
