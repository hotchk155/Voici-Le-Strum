// STRUM CHORD CONTROLLER
// (c) 2010 J.Hotchkiss
// SOURCEBOOST C FOR PIC16F688
#include <system.h>
#include <memory.h>

// PIC CONFIG
#pragma DATA _CONFIG, _MCLRE_OFF&_WDT_OFF&_INTRC_OSC_NOCLKOUT
#pragma CLOCK_FREQ 8000000

// Define pins
#define P_CLK 			porta.2
#define P_DS 			portc.0
#define P_STYLUS 		portc.1
#define P_HEARTBEAT 	portc.2
#define P_KEYS1	 		portc.3
#define P_KEYS2	 		porta.4
#define P_KEYS3	 		porta.5
//#define P_MODE 			porta.0
#define P_MODE 			1

typedef unsigned char byte;

// Chord types
enum {
	CHORD_NONE,
	CHORD_MAJ,
	CHORD_MIN,
	CHORD_DOM7,
	CHORD_MAJ7,
	CHORD_MIN7,
	CHORD_AUG,
	CHORD_DIM
};

enum {
	SUS_NONE,
	SUS_2,
	SUS_4,
	ADD_6
};

// special note value
#define NO_NOTE 0xff
#define NO_SELECTION 0xff

// Define the chord structures
/*byte maj[3] = {0,4,7};
byte min[3] = {0,3,7};
byte dom7[4] = {0,4,7,10};
byte maj7[4] = {0,4,7,11};
byte min7[4] = {0,3,7,10};
byte dim[3] = {0,3,6};
byte aug[3] = {0,3,8};*/

// Define the MIDI root notes mapped to each key
byte roots[16]={36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51};

// bit mapped register of which strings are currently connected
// to the stylus (notes triggered when stylus breaks contact
// with the strings)
unsigned long strings =0;
unsigned long keyState1 =0;
unsigned long keyState2 =0;
unsigned long keyState3 =0;
byte modeButtonSelection = NO_SELECTION;

// Notes for each string
byte notes[16] = {0};

// current chord type
byte lastChordType = CHORD_NONE;

// current root note
byte lastButtonSelection = NO_SELECTION;


byte lastSuspension = SUS_NONE;

////////////////////////////////////////////////////////////
// INITIALISE SERIAL PORT FOR MIDI
void init_usart()
{
	pir1.1 = 1;	//TXIF transmit enable
	pie1.1 = 0;	//TXIE no interrupts
	
	baudctl.4 = 0;		// synchronous bit polarity 
	baudctl.3 = 1;		// enable 16 bit brg
	baudctl.1 = 0;		// wake up enable off
	baudctl.0 = 0;		// disable auto baud detect
		
	txsta.6 = 0;	// 8 bit transmission
	txsta.5 = 1;	// transmit enable
	txsta.4 = 0;	// async mode
	txsta.2 = 0;	// high baudrate BRGH

	rcsta.7 = 1;	// serial port enable
	rcsta.6 = 0;	// 8 bit operation
	rcsta.4 = 0;	// enable receiver
		
	spbrgh = 0;		// brg high byte
	spbrg = 15;		// brg low byte (31250)	
}
		
////////////////////////////////////////////////////////////
// SEND A MIDI BYTE
void send(unsigned char c)
{
	txreg = c;
	while(!txsta.1);
}

////////////////////////////////////////////////////////////
// CONTINUOUS CONTROLLER MESSAGE
void sendController(byte channel, byte controller, byte value)
{
	P_HEARTBEAT = 1;
	send(0xb0 | channel);
	send(controller&0x7f);
	send(value&0x7f);
	P_HEARTBEAT = 0;	
}

////////////////////////////////////////////////////////////
// NOTE MESSAGE
void sendNote(byte channel, byte note, byte velocity)
{
	P_HEARTBEAT = 1;
	send(0x90 | channel);
	send(note&0x7f);
	send(velocity&0x7f);
	P_HEARTBEAT = 0;	
}

int getChordStructure(int which, byte suspension, byte *struc)
{
	int len=0;
	// select the correct chord shape
	switch(which)
	{
	case CHORD_MIN:
		struc[len++] = 0; struc[len++] = 3; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9; 
		break;
	case CHORD_DOM7:
		struc[len++] = 0; struc[len++] = 4; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9;
		struc[len++] = 10; 
		break;
	case CHORD_MAJ7:
		struc[len++] = 0; struc[len++] = 4; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9;
		struc[len++] = 11; 
		break;
	case CHORD_MIN7:
		struc[len++] = 0; struc[len++] = 3; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9;
		struc[len++] = 10; 
		break;
	case CHORD_AUG:
		struc[len++] = 0; struc[len++] = 3; struc[len++] = 8; 
		if(suspension == ADD_6) struc[len++] = 9;		
		break;
	case CHORD_DIM:
		struc[len++] = 0; struc[len++] = 3; struc[len++] = 6; 
		if(suspension == ADD_6) struc[len++] = 9;		
		break;
	case CHORD_MAJ:
	default:
		struc[len++] = 0; struc[len++] = 4; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9;
		break;
	}
		
	if(suspension == SUS_2)
	{
		struc[1] = 2;
	}
	else if(suspension == SUS_4)
	{
		struc[1] = 5;
	}		
	return len;
}


/*

C		C-Open		
C#		A-Shape4
D		D-Open
D#		A-Shape6
E		E-Shape0
F		E-Shape1
F#		E-Shape2
G		G-Open
G#		E-Shape4
A		A-Shape0
A#		A-Shape1
B		A-Shape2

*/
int getChordStructureGTOpen(byte firstButtonSelection, int which, byte suspension, byte *struc)
{
	enum {
		AMAJ_BAR,
		AMIN_BAR,
		EMAJ_BAR,
		EMIN_BAR,
		CMAJ_OPEN,
		DMAJ_OPEN,
		GMAJ_OPEN
	}
	enum {
		BUTTON_C,
		BUTTON_CSHARP,
		BUTTON_D,
		BUTTON_DSHARP,
		BUTTON_E,
		BUTTON_F,
		BUTTON_FSHARP,
		BUTTON_G,
		BUTTON_GSHARP,
		BUTTON_A,
		BUTTON_ASHARP,
		BUTTON_B
	}
	byte shape - ;
	byte fret = 0;

	switch(which)
	{
	case CHORD_MAJ:
		
	case CHORD_MIN:
	case CHORD_DOM7:
	case CHORD_MAJ7:
	case CHORD_MIN7:
	case CHORD_AUG:
	case CHORD_DIM:
	case CHORD_MAJ:

	
	switch(firstButtonSelection)
	{
	case 0: // C
	case 2: // D
	case 8: // G
	case 10: // 
	case 11: // C
	}
	
	int len=0;
	// select the correct chord shape
	switch(which)
	{
	case CHORD_MIN:
		struc[len++] = 0; struc[len++] = 3; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9; 
		break;
	case CHORD_DOM7:
		struc[len++] = 0; struc[len++] = 4; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9;
		struc[len++] = 10; 
		break;
	case CHORD_MAJ7:
		struc[len++] = 0; struc[len++] = 4; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9;
		struc[len++] = 11; 
		break;
	case CHORD_MIN7:
		struc[len++] = 0; struc[len++] = 3; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9;
		struc[len++] = 10; 
		break;
	case CHORD_AUG:
		struc[len++] = 0; struc[len++] = 3; struc[len++] = 8; 
		if(suspension == ADD_6) struc[len++] = 9;		
		break;
	case CHORD_DIM:
		struc[len++] = 0; struc[len++] = 3; struc[len++] = 6; 
		if(suspension == ADD_6) struc[len++] = 9;		
		break;
	case CHORD_MAJ:
	default:
		struc[len++] = 0; struc[len++] = 4; struc[len++] = 7; 
		if(suspension == ADD_6) struc[len++] = 9;
		break;
	}
		
	if(suspension == SUS_2)
	{
		struc[1] = 2;
	}
	else if(suspension == SUS_4)
	{
		struc[1] = 5;
	}		
	return len;
}

////////////////////////////////////////////////////////////
// CALCULATE NOTES FOR A CHORD SHAPE AND MAP THEM
// TO THE STRINGS
void changeToChord(byte firstButtonSelection, byte which, byte suspension)
{	
	int i,j;
	byte chord[16];
	byte struc[5];
	
	if(CHORD_NONE == which || NO_SELECTION == firstButtonSelection)
	{
		// stop playing
		for(i=0;i<16;++i)
			chord[i] = NO_NOTE;
	}
	else
	{
		int len = getChordStructure(which, suspension, struc);		
		byte root = roots[firstButtonSelection];
		
		// fill the chord array with MIDI notes
		int from = 0;
		for(i=0;i<16;++i)
		{
			chord[i] = root+struc[from];		
			if(++from >= len)
			{
				root+=12;
				from = 0;
			}
		}
	}
	
	// stop previous notes from playing if they are not a 
	// part of the new chord
	for(i=0;i<16;++i)
	{
		if(notes[i] != NO_NOTE)
		{
			// check to see if it is part of the new chord
			byte foundIt = 0;
			for(j=0;j<16;++j)
			{
				if(chord[j] == notes[i])
				{
					foundIt = true;
					break;
				}
			}
			
			// if not, then make sure its not playing
			if(!foundIt)				
			{
				sendNote(0, notes[i], 0);
			}
		}		
	}

	// store the new chord
	for(i=0;i<16;++i)
		notes[i] = chord[i];
	
}

////////////////////////////////////////////////////////////
// HANDLER FOR WHEN BUTTON SELECTION CHANGES 
void onChordSelectionChange(byte firstButtonSelection, byte suspension)
{
	int chordType = CHORD_NONE;
	if(firstButtonSelection != NO_SELECTION)
	{
		unsigned long mask = 1<<firstButtonSelection;	
		
		// get the correct chord shape
		switch(
			((keyState1&mask)? 0b001:0)|
			((keyState2&mask)? 0b010:0)|
			((keyState3&mask)? 0b100:0))
		{
			case 0b111:
				chordType = CHORD_AUG;
				break;
			case 0b110:
				chordType = CHORD_DIM;
				break;
			case 0b100:
				chordType = CHORD_MAJ;
				break;
			case 0b101:
				chordType = CHORD_MAJ7;
				break;
			case 0b010:
				chordType = CHORD_MIN;
				break;
			case 0b011:
				chordType = CHORD_MIN7;
				break;
			case 0b001:
				chordType = CHORD_DOM7;
				break;
			default:
				chordType = CHORD_NONE;
				break;
		}		
	}
	if(chordType != lastChordType || 
		firstButtonSelection != lastButtonSelection || 
		suspension != lastSuspension)
	{
		// change to the new chord
		lastChordType = chordType;
		lastButtonSelection = firstButtonSelection;
		lastSuspension = suspension;
		changeToChord(firstButtonSelection, chordType, suspension);
	}	
}

////////////////////////////////////////////////////////////
// HANDLER FOR WHEN STYLUS MAKES CONTACT WITH A STRING
void  onTouchString(int whichString)
{
	// stop the note playing (if
	// it is currently playing). When 
	// stylus is touching a string it
	// is "damped" and does not play
	// till contact is broken
	if(notes[whichString] != NO_NOTE)
	{
		sendNote(0, notes[whichString],  0);
	}
}

////////////////////////////////////////////////////////////
// HANDLER FOR WHEN STYLUS BREAKS CONTACT WITH A STRING
void onReleaseString(int whichString)
{
	// start a note playing
	if(notes[whichString] != NO_NOTE)
	{
		sendNote(0, notes[whichString], 127);
	}
}

////////////////////////////////////////////////////////////
// HANDLER FOR WHEN THERE IS A CHANGE IN THE MODE SELECTION
void onModeButtonSelection(int modeButtonSelection)
{
}

////////////////////////////////////////////////////////////
// HANDLER FOR WHEN A STRING IS TOUCHED WITH STYLUS WHEN
// THE MODE BUTTON IS PRESSED
void onModeStringSelection(int whichString)
{
}
				
////////////////////////////////////////////////////////////
// POLL KEYBOARD MATRIX AND STRINGS
void pollIO()
{
	// clock a single bit into the shift register
	P_CLK = 0;
	P_DS = 1;	
	P_CLK = 1;
	P_DS = 0;	

	// get ready to scan
	byte firstButtonSelection = NO_SELECTION;
	byte suspension = SUS_NONE;
	unsigned long b = 1;
	byte chordChange = 0;
	char thisModeButtonSelection = NO_SELECTION;
	
	// scan for each string
	for(int i=0;i<16;++i)
	{	
		// clock pulse to shift the bit (note that
		// the first bit does not appear until the
		// second clock pulse, since we tied shift and store
		// clock lines together)
		P_CLK = 0;				
		P_CLK = 1;

		// store the first button selection
		if(P_KEYS1 || P_KEYS2 || P_KEYS3)
		{
			if(firstButtonSelection == NO_SELECTION)
				firstButtonSelection = i;
			if(P_KEYS1 && i>firstButtonSelection) suspension = ADD_6;
			else if(P_KEYS2 && i>firstButtonSelection) suspension = SUS_2;
			else if(P_KEYS3 && i>firstButtonSelection) suspension = SUS_4;
		}
			
		// is the mode button pressed?		
		if(!P_MODE) 
		{		
			// find out what other button is held
			// in combination with the mode button
			if(P_KEYS1)
				thisModeButtonSelection = i;
			else if(P_KEYS2)
				thisModeButtonSelection = i+16;
			else if(P_KEYS3)
				thisModeButtonSelection = i+32;							
		}
		// normal play?
		else if(modeButtonSelection == NO_SELECTION)
		{					
			// Key matrix row 1
			if(P_KEYS1)
			{			
				if(!(keyState1 & b))
				{
					chordChange = 1;				
					keyState1 |= b;
				}
			}
			else if(!!(keyState1 & b))
			{
				chordChange = 1;
				keyState1 &= ~b;
			}
			
			// Key matrix row 2
			if(P_KEYS2)
			{
				if(!(keyState2 & b))
				{
					chordChange = 1;
					keyState2 |= b;
				}
			}
			else if(!!(keyState2 & b))
			{
				chordChange = 1;
				keyState2 &= ~b;
			}
				
			// Key matrix row 3
			if(P_KEYS3)
			{
				if(!(keyState3 & b))
				{
					chordChange = 1;
					keyState3 |= b;
				}
			}
			else if(!!(keyState3 & b))
			{
				chordChange = 1;
				keyState3 &= ~b;
			}
		}
		
		// now check whether we got a signal
		// back from the stylus (meaning that
		// it's touching this string)
		if(P_STYLUS)
		{
			// string is being touched... was
			// it being touched before?
			if(!(strings & b))
			{
				if(!!P_MODE)
					onTouchString(i);
				else
					onModeStringSelection(i);
				strings |= b;
			}
		}
		// stylus not touching string now, but was it 
		// touching the string before?
		else if(strings & b)
		{
			if(!!P_MODE)
				onReleaseString(i);			
			strings &= ~b;
		}	
		
		// shift the masking bit	
		b<<=1;		
	}	
	
	if(thisModeButtonSelection != modeButtonSelection)
	{
		modeButtonSelection = thisModeButtonSelection;
		onModeButtonSelection(modeButtonSelection);
	}
	else if(chordChange)
	{
		onChordSelectionChange(firstButtonSelection, suspension);
	}	
}


void blink(int i)
{
	while(i-->0)
	{
		P_HEARTBEAT = 1;
		delay_ms(500);
		P_HEARTBEAT = 0;
		delay_ms(500);
	}
}

void main()
{ 
	// osc control / 8MHz / internal
	osccon = 0b01110001;
	
	// timer0... configure source and prescaler
	option_reg = 0b10000011;
	cmcon0 = 7;                      
	
	// configure io
	trisa = 0b00110001; // porta 0,4,5 as input
    trisc = 0b00001010; // portc 1,3 as input
	ansel = 0b00000000; // analog inputs off	
	wpua =  0b00000001; // weak pull up on porta.0
	option_reg.7 = 0;	// enable pull ups
      
    // blink twice
    //blink(2);
blink(1);

	// initialise MIDI comms
	init_usart();

	// blink 3 more times (total 5)
    //blink(3);

	// initialise the notes array
	memset(notes,NO_NOTE,sizeof(notes));
	for(;;)
	{
		// and now just repeatedly
		// check for input
		pollIO();
	}
}




