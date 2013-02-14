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

enum {
	ROOT_C,
	ROOT_CSHARP,
	ROOT_D,
	ROOT_DSHARP,
	ROOT_E,
	ROOT_F,
	ROOT_FSHARP,
	ROOT_G,
	ROOT_GSHARP,
	ROOT_A,
	ROOT_ASHARP,
	ROOT_B
};

// Extension types
enum {
	ADDED_NONE,
	SUS_4,
	ADD_6,
	ADD_9
};

enum {
	OPT_PLUCK 		= 0x01, // if set then notes are triggered only when contact is broken
	OPT_HOLDCHORD	= 0x02, // if set then chord plays on MIDI channel 1 as soon as pressed
	OPT_ADDNOTES	= 0x04, // additional notes can be added to the chord
	OPT_GUITAR		= 0x08,  // guitar type fingering
	OPT_RINGON		= 0x10  // let it ring till next chord pressed
};

byte options = OPT_GUITAR|OPT_PLUCK|OPT_RINGON;

// special note value
#define NO_NOTE 0xff

// Define the MIDI root notes mapped to each key
//byte roots[16]={36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51};

// bit mapped register of which strings are currently connected
// to the stylus (notes triggered when stylus breaks contact
// with the strings)
unsigned long strings =0;

byte velocity = 127;

// Notes for each string
byte notes[16] = {0};

typedef struct 
{
	byte chordType;
	byte rootNote;
	byte extension;
} CHORD_SELECTION;

CHORD_SELECTION lastChordSelection = { CHORD_NONE, NO_NOTE, ADDED_NONE };

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
// MAKE A CHORD BY "STACKING TRIADS"
void stackTriads(CHORD_SELECTION *pChordSelection, byte *chord)
{
	byte struc[5];
	byte len = 0;
	
	// root
	struc[len++] = 0; 
	
	// added 2/9
	if(pChordSelection->extension == ADD_9)		
		struc[len++] = 2;
		
	// sus 4
	if(pChordSelection->extension == SUS_4)	{		
		struc[len++] = 5;
	} else {
		switch(pChordSelection->chordType)		
		{		
			// minor 3rd
		case CHORD_MIN: case CHORD_MIN7: case CHORD_AUG: case CHORD_DIM: // minor 3rd
			struc[len++] = 3;
			break;
		default: // major 3rd
			struc[len++] = 4;
			break;		
		}
	}
	
	// 5th
	switch(pChordSelection->chordType)		
	{
	case CHORD_AUG:
		struc[len++] = 8;
		break;
	case CHORD_DIM:
		struc[len++] = 6;
		break;
	default:
		struc[len++] = 7;
		break;
	}


	if(pChordSelection->extension == ADD_6)		
		struc[len++] = 9;
			
	// 7th
	switch(pChordSelection->chordType)		
	{
	case CHORD_DOM7: case CHORD_MIN7:
		struc[len++] = 10;
		break;
	case CHORD_MAJ7:
		struc[len++] = 11;
		break;				
	}

	// fill the chord array with MIDI notes
	byte root = pChordSelection->rootNote + 36;
	int from = 0;
	for(int i=0;i<16;++i)
	{
		chord[i] = root+struc[from];		
		if(++from >= len)
		{
			root+=12;
			from = 0;
		}
	}
}

void guitarCShape(byte ofs, byte *chord)
{
	chord[0] = 43 + ofs;
	chord[1] = 48 + ofs;
	chord[2] = 52 + ofs;
	chord[3] = 55 + ofs;
	chord[4] = 60 + ofs;
	chord[5] = 64 + ofs;
}
void guitarAShape(byte ofs, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 45 + ofs;
	chord[2] = 52 + ofs;
	chord[3] = 57 + ofs;
	chord[4] = 61 + ofs;
	chord[5] = 64 + ofs;
}
void guitarAmShape(byte ofs, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 45 + ofs;
	chord[2] = 52 + ofs;
	chord[3] = 57 + ofs;
	chord[4] = 60 + ofs;
	chord[5] = 64 + ofs;
}
void guitarDShape(byte ofs, byte *chord)
{
	chord[0] = NO_NOTE;
	chord[1] = 45 + ofs;
	chord[2] = 50 + ofs;
	chord[3] = 57 + ofs;
	chord[4] = 62 + ofs;
	chord[5] = 66 + ofs;
}
void guitarDmShape(byte ofs, byte *chord)
{
	chord[0] = NO_NOTE;
	chord[1] = 45 + ofs;
	chord[2] = 50 + ofs;
	chord[3] = 57 + ofs;
	chord[4] = 62 + ofs;
	chord[5] = 65 + ofs;
}
void guitarEShape(byte ofs, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 47 + ofs;
	chord[2] = 52 + ofs;
	chord[3] = 56 + ofs;
	chord[4] = 59 + ofs;
	chord[5] = 64 + ofs;
}
void guitarEmShape(byte ofs, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 47 + ofs;
	chord[2] = 52 + ofs;
	chord[3] = 55 + ofs;
	chord[4] = 59 + ofs;
	chord[5] = 64 + ofs;
}
void guitarGShape(byte ofs, byte *chord)
{
	chord[0] = 43 + ofs;
	chord[1] = 47 + ofs;
	chord[2] = 50;
	chord[3] = 55;
	chord[4] = 59;
	chord[5] = 67 + ofs;
}

////////////////////////////////////////////////////////////
// MAKE A GUITAR CHORD 
void guitarChord(CHORD_SELECTION *pChordSelection, byte *result)
{	
	int i;
	for(i=0;i<16;++i)
		result[i] = NO_NOTE;
	byte chord[6];
	switch(pChordSelection->chordType)
	{
		case CHORD_MAJ:
			switch(pChordSelection->rootNote)
			{
			case ROOT_C:		guitarCShape(0, chord);	break;
			case ROOT_CSHARP:  	guitarAShape(4, chord);	break;
			case ROOT_D:		guitarDShape(0, chord);	break;
			case ROOT_DSHARP:	guitarAShape(6, chord);	break;
			case ROOT_E:		guitarEShape(0, chord);	break;
			case ROOT_F:		guitarEShape(1, chord);	break;
			case ROOT_FSHARP:	guitarEShape(2, chord);	break;
			case ROOT_G:		guitarGShape(0, chord);	break;
			case ROOT_GSHARP:	guitarEShape(4, chord);	break;
			case ROOT_A:		guitarAShape(0, chord);	break;
			case ROOT_ASHARP:   guitarAShape(1, chord);	break;
			case ROOT_B:   		guitarAShape(2, chord);	break;
			}
			break;
		case CHORD_MIN:
			switch(pChordSelection->rootNote)
			{
			case ROOT_C:		guitarAmShape(3, chord);	break;
			case ROOT_CSHARP:  	guitarAmShape(4, chord);	break;
			case ROOT_D:		guitarDmShape(0, chord);	break;
			case ROOT_DSHARP:	guitarAmShape(6, chord);	break;
			case ROOT_E:		guitarEmShape(0, chord);	break;
			case ROOT_F:		guitarEmShape(1, chord);	break;
			case ROOT_FSHARP:	guitarEmShape(2, chord);	break;
			case ROOT_G:		guitarEmShape(3, chord);	break;
			case ROOT_GSHARP:	guitarEmShape(4, chord);	break;
			case ROOT_A:		guitarAmShape(0, chord);	break;
			case ROOT_ASHARP:   guitarAmShape(1, chord);	break;
			case ROOT_B:   		guitarAmShape(2, chord);	break;
			}
			break;
		}
		
		byte *src = chord;
		for(i=0;i<6;++i)
		{
			*result=*src;
			++result;
			*result=*src+12;
			++result;
			++src;
		}
}

////////////////////////////////////////////////////////////
// CALCULATE NOTES FOR A CHORD SHAPE AND MAP THEM
// TO THE STRINGS
void changeToChord(CHORD_SELECTION *pChordSelection)
{	
	lastChordSelection = *pChordSelection;
	
	int i,j;
	byte chord[16];
	
	if(CHORD_NONE == pChordSelection->chordType || 
		NO_NOTE == pChordSelection->rootNote)
	{
		if(!(options & OPT_RINGON))
		{
			// stop playing
			for(i=0;i<16;++i)
				chord[i] = NO_NOTE;
		}
	}
	else if(options & OPT_GUITAR)
	{
		guitarChord(pChordSelection, chord);
	}
	else
	{
		stackTriads(pChordSelection, chord);
	}

	if(CHORD_NONE != pChordSelection->chordType || 
		!(options & OPT_RINGON))
	{
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
					if(options & OPT_HOLDCHORD)
						startNote(1, notes[i], 0);
				}
			}		
		}
	
		// store the new chord
		for(i=0;i<16;++i)
		{
			notes[i] = chord[i];
			if((options & OPT_HOLDCHORD) && NO_NOTE != notes[i])
				startNote(1, notes[i], velocity);
		}	
	}
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

	CHORD_SELECTION chordSelection = { CHORD_NONE,  NO_NOTE, ADDED_NONE };
	unsigned long b = 1;
	byte stringCount = 0;
	
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
				chordSelection.rootNote = i;
				
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
			else if((options & OPT_ADDNOTES) && (chordSelection.extension == ADDED_NONE))
			{
				if(P_KEYS1)
					chordSelection.extension = SUS_4;
				else if(P_KEYS2)
					chordSelection.extension = ADD_6;
				else if(P_KEYS3)
					chordSelection.extension = ADD_9;
			}
		}
		
		// now check whether we got a signal
		// back from the stylus (meaning that
		// it's touching this string)
		//byte whichString = i;
		if(P_STYLUS)
		{
			++stringCount;

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
						startNote(0, notes[i],  (options & OPT_PLUCK)? 0 : velocity);
				
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
				startNote(0, notes[i], (options & OPT_PLUCK)? velocity : 0);
			
			// remember string is not being touched
			strings &= ~b;
		}	
		
		// shift the masking bit	
		b<<=1;
		
	}	
	
	// has the chord changed? note that if the stylus is bridging 2 strings we will not change
	// the chord selection. This is because this situation can confuse the keyboard matrix
	// causing unwanted chord changed
	if((stringCount < 2) && 0 != memcmp(&chordSelection, &lastChordSelection, sizeof(CHORD_SELECTION)))
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



