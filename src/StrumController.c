// STRUM CHORD CONTROLLER
// (c) 2010 J.Hotchkiss
// SOURCEBOOST C FOR PIC16F688
#include <system.h>
#include <memory.h>

// PIC CONFIG
#pragma DATA _CONFIG1, _FOSC_INTOSC & _WDTE_OFF & _MCLRE_OFF &_CLKOUTEN_OFF
#pragma DATA _CONFIG2, _WRT_OFF & _PLLEN_OFF & _STVREN_ON & _BORV_19 & _LVP_OFF
#pragma CLOCK_FREQ 8000000

// Define pins
#define P_CLK 			porta.2
#define P_DS 			portc.0
#define P_STYLUS 		portc.1
#define P_HEARTBEAT 	portc.2
#define P_KEYS1	 		porta.5
#define P_KEYS2	 		porta.4
#define P_KEYS3	 		portc.3

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

// special note value
#define NO_NOTE 0xff
//byte silent[1] = {NO_NOTE};

// Define the chord structures
byte maj[3] = {0,4,7};
byte min[3] = {0,3,7};
byte dom7[4] = {0,4,7,10};
byte maj7[4] = {0,4,7,11};
byte min7[4] = {0,3,7,10};
byte dim[3] = {0,3,6};
byte aug[3] = {0,3,8};

// Define the MIDI root notes mapped to each key
byte roots[16]={36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51};

// bit mapped register of which strings are currently connected
// to the stylus (notes triggered when stylus breaks contact
// with the strings)
unsigned long strings =0;

// Notes for each string
byte notes[16] = {0};

// current chord type
byte lastChordType = CHORD_NONE;

// current root note
byte lastRoot = NO_NOTE;

typedef struct 
{
	byte chordType;
	byte rootNote;
	byte extension;
} CHORD_SELECTION;

CHORD_SELECTION lastChordSelection = { CHORD_NONE, NO_NOTE, 0 };

////////////////////////////////////////////////////////////
// INITIALISE SERIAL PORT FOR MIDI
void init_usart()
{
	pir1.1 = 1;	//TXIF transmit enable
	pie1.1 = 0;	//TXIE no interrupts
	
	baudcon.4 = 0;		// synchronous bit polarity 
	baudcon.3 = 1;		// enable 16 bit brg
	baudcon.1 = 0;		// wake up enable off
	baudcon.0 = 0;		// disable auto baud detect
		
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
void startNote(byte channel, byte note, byte value)
{
	P_HEARTBEAT = 1;
	send(0x90 | channel);
	send(note&0x7f);
	send(value&0x7f);
	P_HEARTBEAT = 0;	
}

////////////////////////////////////////////////////////////
// CALCULATE NOTES FOR A CHORD SHAPE AND MAP THEM
// TO THE STRINGS
void changeToChord(CHORD_SELECTION *pChordSelection)
{	
	lastChordSelection = *pChordSelection;
	
	int i,j,len=0;
	byte *struc = maj;	
	byte chord[16];
	
	if(CHORD_NONE == pChordSelection->chordType || 
		NO_NOTE == pChordSelection->rootNote)
	{
		// stop playing
		for(i=0;i<16;++i)
			chord[i] = NO_NOTE;
	}
	else
	{
		// select the correct chord shape
		switch(pChordSelection->chordType)
		{
		case CHORD_MIN:
			struc = min;
			len = sizeof(min);
			break;
		case CHORD_DOM7:
			struc = dom7;
			len = sizeof(dom7);
			break;
		case CHORD_MAJ7:
			struc = maj7;
			len = sizeof(maj7);
			break;
		case CHORD_MIN7:
			struc = min7;
			len = sizeof(min7);
			break;
		case CHORD_AUG:
			struc = aug;
			len = sizeof(aug);
			break;
		case CHORD_DIM:
			struc = dim;
			len = sizeof(dim);
			break;
		case CHORD_MAJ:
		default:
			struc = maj;
			len = sizeof(maj);
			break;
			break;
		}
	
		// fill the chord array with MIDI notes
		byte root = pChordSelection->rootNote;
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
				startNote(0, notes[i], 0);
			}
		}		
	}

	// store the new chord
	for(i=0;i<16;++i)
		notes[i] = chord[i];
	
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

	CHORD_SELECTION chordSelection = { CHORD_NONE,  NO_NOTE, 0 };
/*typedef struct 
{
	byte chordType;
	byte extension;
	byte rootNote;
} CHORD_SELECTION;

	// get ready to scan
	int root = NO_NOTE;
	int chordType = CHORD_NONE;*/
	unsigned long b = 1;
	
	
	// scan for each string
	for(int i=0;i<16;++i)
	{	
		// clock pulse to shift the bit (note that
		// the first bit does not appear until the
		// second clock pulse, since we tied shift and store
		// clock lines together)
		P_CLK = 0;				
		P_CLK = 1;
		
		// did we get a signal back on any of the 
		// keyboard scan rows?
		if(P_KEYS1 || P_KEYS2 || P_KEYS3)
		{
			// have we decided on the root note yet?
			if(NO_NOTE == chordSelection.rootNote)
			{
				// look up the root note
				chordSelection.rootNote = roots[i];
				
				// get the correct chord shape
				switch(
					(P_KEYS1? 0b100:0)|
					(P_KEYS2? 0b010:0)|
					(P_KEYS3? 0b001:0))
				{
					case 0b111:
						chordSelection.chordType = CHORD_AUG;
						break;
					case 0b110:
						chordSelection.chordType = CHORD_DIM;
						break;
					case 0b100:
						chordSelection.chordType = CHORD_MAJ;
						break;
					case 0b101:
						chordSelection.chordType = CHORD_MAJ7;
						break;
					case 0b010:
						chordSelection.chordType = CHORD_MIN;
						break;
					case 0b011:
						chordSelection.chordType = CHORD_MIN7;
						break;
					case 0b001:
						chordSelection.chordType = CHORD_DOM7;
						break;
					default:
						chordSelection.chordType = CHORD_NONE;
						break;
				}
			}						
			else if(!chordSelection.extension)
			{
				if(P_KEYS1)
					chordSelection.extension = 1;
				else if(P_KEYS2)
					chordSelection.extension = 2;
				else if(P_KEYS3)
					chordSelection.extension = 3;
			}
		}
		
	
		// now check whether we got a signal
		// back from the stylus (meaning that
		// it's touching this string)
		//byte whichString = i;
		if(P_STYLUS)
		{
			// string is being touched... was
			// it being touched before?
			if(!(strings & b))
			{
				// stop the note playing (if
				// it is currently playing). When 
				// stylus is touching a string it
				// is "damped" and does not play
				// till contact is broken
				if(notes[i] != NO_NOTE)
				{
					startNote(0, notes[i],  0);
				}
							
				// remember this string is being touched
				strings |= b;
			}
		}
		// stylus not touching string now, but was it 
		// touching the string before?
		else if(strings & b)
		{
			// start a note playing
			if(notes[i] != NO_NOTE)
			{
				startNote(0, notes[i], 127);
			}
			
			// remember string is not being touched
			strings &= ~b;
		}	
		
		// shift the masking bit	
		b<<=1;
		
	}	
	
	// has the chord changed?
	if(0 != memcmp(&chordSelection, &lastChordSelection, sizeof(CHORD_SELECTION)))
		changeToChord(&chordSelection);
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
	osccon = 0b01110010;
	
	// timer0... configure source and prescaler
	//option_reg.7 = 0b10000011;
	//cmcon0 = 7;                      
	
	// configure io
	trisa = 0b00110000;              	
    trisc = 0b00001010;              
	ansela = 0b00000000;
	anselc = 0b00000000;
      
    // blink twice
    blink(2);
    
	// initialise MIDI comms
	init_usart();

	// blink 3 more times (total 5)
    blink(3);

	// initialise the notes array
	memset(notes,NO_NOTE,sizeof(notes));
	for(;;)
	{
		// and now just repeatedly
		// check for input
		pollIO();
	}
}



