/* 	Digital and Embedded Systems: Laboratory 2
This program used timer 0 on the ADuC841 microconverter
in order to estimate the period of a square wave. The square
wave, which is the output of a Schmitt trigger, is directed into
P3.2 (a digital I/O pin) on the microconverter. Timer 0 counts the
duration of half a pulse of the square wave. This duration, in clock
cycles, is added to a running sum, before an average is taken.

Authors: Dylan Boland (Student), Cuan de Burca (Student)
*/
	 
#include <ADUC841.H>
#include "typedef.h" // contains the various type definitions that we will use

// define the bits in the ADCCON1 control register
#define MD1_POS (7) // 1 enables ADC, 0 disables
#define EXT_REF (6)	// 1 external reference, 0 internal ref (2.5V)
#define CLK_DIV	(4)	// clk div ratio, 0=32, 1=4, 2=8, 3=2
#define ACQ_POS (2)	// acquisition time in (ADC) clock cycles - 1
#define T2C_POS	(1)	// 1 to start conversion on timer 2 overflows
#define EXC_POS	(0)	// 1 to start conversion on external signal

// define the bits in the ADCCON2 control register
#define ADCI  (7) // 1 generates interrupt when data is ready
#define DMA   (6)	// 1 enables burst of data into external memory
#define CCONV (5)	// 1 enables continuous conversion 
#define SCONV (4)	// 1 sets up ADC for single conversion
#define CS0_3 (0)	// last 4 bits for channel select

// defining extra special function registers
sbit LOAD = 0xA7; // defining P2.7 to be "LOAD"; 1-bit special function register
sfr16 ADCDATA = 0xD9; // 0xD9 is the address of the ADCDATAL register; because ADCDATA is defined
// as a 16-bit SFR, we can also grab the upper byte, ADCDATAH (at 0xDA), alongside the lower one

/*------------------------------------------------
Defining macros for the various registers on the 8 digit
7-segment display, which uses the MAX7219 display driver
circuit.

NOTE: the addresses of the various control registers
of the display driver circuit are specified in decimal
for ease - these can be changed to hex format if desired.
------------------------------------------------*/
#define DECODE_ADDR 9 // The address of the decode mode register on the display device
#define D1_ADDR 1 // address of data register 1 on the display device
#define D2_ADDR 2 // address of data register 2 on the display device
#define INTENSITY_ADDR 10
#define SCAN_LIMIT_ADDR 11
#define SHUTDOWN_ADDR 12
#define DISPLAY_ADDR 15
#define NUM_DIGITS 6 // Number of digits in use on the display

/*------------------------------------------------
Defining macros used in the main.
------------------------------------------------*/
#define NUM_SAMPLES 1024u // Number of ADC samples we will average over (measurement 1)
#define NUM_PULSE_SAMPLES 512u // Number of half pulses we will average over (measurement 3)
#define FCLOCK 11059200u // clock frequency in Hz

uint16 readADCSample(void);

/////////////////////////////////////////////////////////////////////////
//////////////////////// - FUNCTION DEFINITIONS - ///////////////////////
/////////////////////////////////////////////////////////////////////////

/*------------------------------------------------
Software delay function - argument is number of loops.
------------------------------------------------*/
void delay (uint16 delayVal)
{
	uint16 i;                 // counting variable 
	for (i = 0; i < delayVal; i++)    // repeat  
    {
		  // do nothing
    }
}	// end delay

// function to initialise the SPICON reg.
void setupSPICON(void)
{
	/*------------------------------------------------
	The only bits set are SPE and SPIM: enable the SPI interface
	and output SCLK -> ADuC842 is the master device. CPHA = 0, transmit 
	data from SPIDAT on leading edge
	------------------------------------------------*/
	SPICON = 0x30;
}

// function to set up ADC control registers
void setupADC(void)
{
	ADCCON1 = (1 << MD1_POS) |	// enable ADC
						(0 << EXT_REF) |	// use internal reference
						(3 << CLK_DIV) |	// ADC clock frequency is main clk/2
						(0 << ACQ_POS) |	// allow one clock cycle for acquisition
						(1 << T2C_POS) |	// use timer 2 overflows for conversion
						(0 << EXC_POS);	
			  
	ADCCON2 = (1 << ADCI) |			// enable interrupt when data ready
						(0 << DMA) |	    // disable data burst
						(0 << CCONV) |		// disable continuous conversion
						(0 << SCONV) |		// disable single conversion
						(0 << CS0_3);			// select channel 0
}

// function to set up interrupt enable register (IE)
void setupIE(void)
{
	EA = 1;		// enable desired interrupts
    EADC = 1;	// enable ADC interrupts
	ET2 = 0;	// disable Timer 2 interrupts
	ES = 0;		// disable serial port interrupts
	ET1 = 0;    // disable timer 1 interrupts
	EX1 = 0;	// disable external interrupts
	ET0 = 0;	// enabling for frequency counter measurement
	EX0 = 1;	// disable external interrupts
}

// we might be able to remove this function
// and simply retain the code inside
void setupT0(void)
{
	/* Configuring timer 0. High nibble is 0
	as we are not using timer 1. Lower nibble
	is 9 or 1001 {GATE, C/T, M1, M0}.
	GATE = 1
	C/T = 0, as we want to count clock cycles (measure time)
	M1 = 0
	M0 = 1, so we are in 16-bit mode
	*/
	TMOD = 0x09;
	TCON = 0x11; // set the timer 0 run bit; and enable external interrupt 0 - square wave on P3.2 triggers this
}

void setupT2(void)
{
	T2CON = 0x04;	// turn on timer 2
	RCAP2L = 0x00;	// load in 0 into the low byte
	RCAP2H = 0x00; // and load in 0 into the high byte
}

void sendData(uint8 destAddress, uint16 value) {
	LOAD = 0; // set the slave-select signal low to allow transmission
	ISPI = 0; // setting to 0 so we can catch the moment it is set to 1 when transaction is done
	SPIDAT = destAddress; // load first 8 bits into SPIDAT reg.
	while(ISPI == 0){ // doing nothing while ISPI == 0, the transfer is not complete yet
	}
	delay(1);
	ISPI = 0;
	SPIDAT = value; // load in the next 8 bits (the data) to be sent - this starts
	// the transaction over the SPI interface
	while(ISPI == 0) { // again, do nothing until ISPI is set to 1 by the hardware
	}
	ISPI = 0;
	LOAD = 1; // unselecting the slave device - transaction complete
}

// function to send a number to the display device
// to be displayed
void displayNumber(uint16 value)
{
	uint8 digitValue;
	int i;
	
	for(i=1; i<=NUM_DIGITS; i++)
	{ 
		digitValue = value % 10;
		sendData(i, digitValue);
		value = value/10;
	}
}
// function for setting up registers in the display 
void setupDisplay(void) 
{
	sendData(DECODE_ADDR, 255); // configuring the decode mode reg. to 1 (standard
	sendData(SHUTDOWN_ADDR, 1); // loading in 1 to power device up
	sendData(DISPLAY_ADDR, 0); // loading in 0 into DISPLAY TEST reg. for standard operation
	sendData(INTENSITY_ADDR, NUM_DIGITS); // setting intensity to be no. of digits in use
	sendData(SCAN_LIMIT_ADDR, NUM_DIGITS-1); // scan limit = (no. of digits in use) - 1
}

// function to retrieve an ADC sample
uint16 readADCSample(void)
{
	uint16 adcValue = ADCDATA;	// assign adcValue the value in the ADCDATAH register; assigning the value in the ADCDATAL register
	adcValue &= 0x0FFF;	// we only want the 12 lower bits as first 4 are indicating which channel
	return adcValue;
}

// volatile and global variables; used by main and the ISRs
volatile uint16 halfPulseCount; // keep track of the number of half pulses encountered
volatile uint32 halfPulseSum = 0; // a variable to keep track of the sum of the half pulse durations
volatile uint32 currentCount = 0; // current count value; 32 bits are needed in case timer 0 overflows while P3.2 is high

void main (void) 
{
	uint16 halfPulseAverage = 0; // a variable in which we will store the average of the half pulses
	uint32 halfPulseSumCopy = 0; // a variable into which we will copy the value of halfPulseSum
	// setup phase:
	setupSPICON(); // setting up the SPICON reg.
	setupDisplay(); // setting up the display
	setupIE(); // configuring the interrupt enable reg.
	setupT0(); // setting up timer 0 (used for measuring frequency)
	halfPulseCount = 0;
	while (1)
	{
		if(halfPulseCount == NUM_PULSE_SAMPLES) // if we have counted enough half pulses
		{
			halfPulseSumCopy = halfPulseSum; // we will work with the copy within this loop
			halfPulseSum = 0; // reset the sum, for the next averaging cycle
			halfPulseCount = 0; // and reset the halfPulseCount
			halfPulseAverage = halfPulseSumCopy >> 9; // right shift by 9 is the same as dividing by 512 (NUM_PULSE_SAMPLES)
			/* To get the number of clock cycles in one full period, we multiply
			halfPulseAverage by 2. This is the same as left shifting by 1.*/
			N = (halfPulseAverage << 1); // number of clock cycles in 1 full period
			frequency = FCLOCK/N; // the input signal frequency in Hz; FCLOCK is the clock frequency in Hz
			displayNumber(frequency); // send the data to the display
			delay(20000);
		}
	}		
} // end main

// T0 ISR:
// Address is 0x000BH, or 11 in decimal
// 11 = 3 + 8n, gives interrupt number n = 1
void T0ISR(void) interrupt 1
{
    overflowCount++; // increment the overflow counter
}

// IE0 ISR:
// Address is 0x0003H, or 3 in decimal
// 3 = 3 + 8n, gives interrupt number n = 0
void INT0ISR(void) interrupt 0
{
	currentCount = (TH0 << 8) | TL0; // capture half period in clk cycles; got from timer 0 count value
	currentCount += overflowCount*65536uL; // account for any overflows that might have occurred
	overflowCount = 0; // reset the overflow counter for the next half pulse
	TH0 = 0x00;	// reset high Byte of timer 0
	TL0 = 0x00;	// reset low Byte of timer 0
	halfPulseCount++; // increase the half pulse count
	halfPulseSum += currentCount; // add the duration of the half pulse to the running sum
}

