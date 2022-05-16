/* 	Digital and Embedded Systems: Laboratory 2
This program uses the ADC present on the ADuC841 microconverter
in order to sample a DC voltage on one of the analog inputs. An average
of the samples is taken, converted to a voltage, and sent to the 
MAX7219 display driver circuit. The voltage level, being some value between 0
and 2.5 V (the on-chip reference voltage), is displayed in mV.

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
sbit LOAD = 0xA7; // defining P2.7 to be "LOAD"; this is a 1-bit special function register
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
#define DECODE_ADDR 9 // the address of the decode mode register on the display device
#define D1_ADDR 1 // address of data register 1 on the display device
#define D2_ADDR 2 // address of data register 2 on the display device
#define INTENSITY_ADDR 10
#define SCAN_LIMIT_ADDR 11
#define SHUTDOWN_ADDR 12
#define DISPLAY_ADDR 15
#define NUM_DIGITS 8	// number of digits that will be used on the display device

/*------------------------------------------------
Defining macros that are used in the main.
------------------------------------------------*/
#define NUM_SAMPLES 1024u // number of ADC samples that we will average over
#define VREF 2500uL // the on-chip reference voltage (Vref) expressed in mV

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

// function to set up the interrupt enable (IE) register
void setupIE(void)
{
	EA = 1;		// enable desired interrupts
    EADC = 1;	// enable ADC interrupts
	ET2 = 0;	// disable Timer 2 interrupts
	ES = 0;		// disable serial port interrupts
	ET1 = 0;    // disable timer 1 interrupts
	EX1 = 0;	// disable external interrupts
	ET0 = 0;	// disable Timer 0 interrupts
	EX0 = 0;	// disable external interrupts
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

// function for setting up the registers in the display circuit
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
volatile uint16 sampleCount = 0; // used to keep track of the number of ADC samples collected
volatile uint32 sampleSum = 0; // a variable to keep track of the sum of the samples taken so far (running sum)
// the variable below will store the average of the samples of the DC signal. 16 bits will
// be sufficient to store the final result, but the scaling process will require 32 bits.
volatile uint32 dcAverage = 0;

void main (void) 
{
	uint16 sampleAverage = 0; // A variable in which we will store the average of the ADC samples
	// setup phase:
	setupSPICON(); // setting up the SPICON reg.
	setupDisplay(); // setting up the display
	setupADC(); // setting up the ADC
	setupIE(); // configuring the interrupt enable reg.
	setupT2(); // setting up timer 2 (used for triggering ADC conversions)

	while (1)
	{
		if (sampleCount == NUM_SAMPLES) { // have we collected enough ADC samples?
			uint32 sampleSumCopy = sampleSum; // first make a quick copy which we'll work with inside this loop
			sampleSum = 0; // reset sample sum - the ADC ISR can now make use of this variable again
			/* Now we want to reset the sampleCount to 0. If the ADC ISR is triggered
			and increases sampleCount from NUM_SAMPLES to (NUM_SAMPLES+1), then we
			want that sample to be counted properly - hence why we use modulo division.
			If the ISR increases sampleCount from, say, 1024 to 1025, then the
			line below will assign sampleCount a value of 1025%1024 or 1. This is what
			we want as we are starting to collect samples for a new average! If we were to simply
			reset sampleCount to 0, then sampleSum would still have a value, meaning when
			sampleCount reaches NUM_SAMPLES again sampleSum would actually consist of
			(NUM_SAMPLES+1) ADC samples - our average would be thrown off a little bit.
			*/
			sampleCount %= NUM_SAMPLES;
			// now we compute the average of the samples using the copy we made above:
			sampleAverage = sampleSumCopy >> 10; // right shift by 10 is same as dividing by 2^10, or 1024 (NUM_SAMPLES)
			// convert to volts:
			dcAverage = (sampleAverage*VREF); // VREF is the on-chip reference voltage
			dcAverage >>= 12; // right shift by 12 is the same as dividing by 2^12 or 4096
			// now send this average to the display:
			displayNumber((uint16)dcAverage); // cast to uint16 for the displayNumber() function
		}
		// if we have haven't collected a enough samples, then wait:
		// maybe add a small delay here?
	}		
}	// end main

// ADC ISR:
// two main objectives: add the ADC sample value to the running
// sum; and increase the sample count by 1.
void adcI(void) interrupt 6
{
	sampleSum += readADCSample(); // add ADC sample to the current sum
	sampleCount ++; // increase the sample count by 1
	// now we can return
}
