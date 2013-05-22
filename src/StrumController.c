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
#define P_LED 			portc.2
#define P_KEYS1	 		porta.5
#define P_KEYS2	 		porta.4
#define P_KEYS3	 		portc.3
#define P_MODE	 		portc.5
//portc.4 = TX



typedef unsigned char byte;

// CHORD SHAPES
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

// ROOT NOTES
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

// CHORD EXTENSIONS
enum {
	ADD_NONE,
	SUS_4,
	ADD_6,
	ADD_9
};

enum {
	OPT_PLUCK 		= 0x01, // if set then notes are triggered only when contact is broken
	OPT_HOLDCHORD	= 0x02, // if set then chord plays on MIDI channel 1 as soon as pressed
	OPT_ADDNOTES	= 0x04, // additional notes can be added to the chord
	OPT_GUITAR		= 0x08, // guitar type fingering
	OPT_RINGON		= 0x10, // let it ring till next chord pressed
	OPT_OCTAVEPAIR	= 0x20, // 12 string mode
	OPT_SPREAD		= 0x40, // spread mode
	OPT_DAMPCHANGE	= 0x80  // damp notes which are not in a new chord
};

byte options = OPT_PLUCK|OPT_DAMPCHANGE;

// special note value
#define NO_NOTE 0xff

// Define the MIDI root notes mapped to each key
//byte roots[16]={36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51};

// bit mapped register of which strings are currently connected
// to the stylus (notes triggered when stylus breaks contact
// with the strings)
unsigned long strings =0;

/*
C - Pluck
D - Hold
E - Extra Notes
F - 
G - Guitar
A - 12 String
B - Panic

*/

enum 
{
	MODE_PLUCK, 			// if set then notes are triggered only when contact is broken
	MODE_HOLDCHORD,			// if set then chord plays on MIDI channel 1 as soon as pressed
	MODE_ADDNOTES,			// additional notes can be added to the chord
	MODE_GUITAR,			// guitar type fingering
	MODE_RINGON, 			// let it ring till next chord pressed
	MODE_OCTAVEPAIR,		// 12 string mode: +1 octave between each mapped string
	MODE_SPREAD,			// spread mode: gap between each mapped string

	MODE_ALLNOTESOFF = 11,	// MIDI panic button
	
	
	MODE_OPT_ON = 0x10,
	MODE_OPT_OFF = 0x20,
	MODE_OPT_QUERY = 0x40,
	NO_MODE = 0xff
};
byte lastModeSelection = NO_MODE;

byte velocity = 127;

// Notes for each string
byte notes[16] = {0};

typedef struct 
{
	byte chordType;
	byte rootNote;
	byte extension;
} CHORD_SELECTION;

CHORD_SELECTION lastChordSelection = { CHORD_NONE, NO_NOTE, ADD_NONE };

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
	P_LED = 1;
	send(0xb0 | channel);
	send(controller&0x7f);
	send(value&0x7f);
	P_LED = 0;	
}

////////////////////////////////////////////////////////////
// NOTE MESSAGE
void startNote(byte channel, byte note, byte value)
{
	P_LED = 1;
	send(0x90 | channel);
	send(note&0x7f);
	send(value&0x7f);
	P_LED = 0;	
}
void stopNote(byte channel, byte note)
{
	P_LED = 1;
	send(0x90 | channel);
	send(note&0x7f);
	send(0x00);
	P_LED = 0;	
}
void stopAllNotes(byte channel)
{
	for(byte i=0; i<128; ++i)
		stopNote(channel, i);
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
	int to = 0;
	while(to < 16)
	{
		chord[to++] = root+struc[from];		
		if((options & OPT_SPREAD) && (to<16))
			chord[to++] = NO_NOTE;
		else if((options & OPT_OCTAVEPAIR) && (to<16))
			chord[to++] = root+struc[from]+12;		
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
void guitarC7Shape(byte ofs, byte *chord)
{
	chord[0] = 43 + ofs;
	chord[1] = 47 + ofs;
	chord[2] = 53 + ofs;
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
void guitarA7Shape(byte ofs, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 45 + ofs;
	chord[2] = 50 + ofs;
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
void guitarD7Shape(byte ofs, byte *chord)
{
	chord[0] = NO_NOTE;
	chord[1] = 45 + ofs;
	chord[2] = 50 + ofs;
	chord[3] = 57 + ofs;
	chord[4] = 60 + ofs;
	chord[5] = 66 + ofs;
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
void guitarE7Shape(byte ofs, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 47 + ofs;
	chord[2] = 50 + ofs;
	chord[3] = 56 + ofs;
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
		case CHORD_DOM7:
			switch(pChordSelection->rootNote)
			{
			case ROOT_C:		guitarC7Shape(3, chord);	break;
			case ROOT_CSHARP:  	guitarA7Shape(4, chord);	break;
			case ROOT_D:		guitarD7Shape(0, chord);	break;
			case ROOT_DSHARP:	guitarA7Shape(6, chord);	break;
			case ROOT_E:		guitarE7Shape(0, chord);	break;
			case ROOT_F:		guitarE7Shape(1, chord);	break;
			case ROOT_FSHARP:	guitarE7Shape(2, chord);	break;
			case ROOT_G:		guitarE7Shape(3, chord);	break;
			case ROOT_GSHARP:	guitarE7Shape(4, chord);	break;
			case ROOT_A:		guitarA7Shape(0, chord);	break;
			case ROOT_ASHARP:   guitarA7Shape(1, chord);	break;
			case ROOT_B:   		guitarA7Shape(2, chord);	break;
			}
			break;
		}
		
		byte *src = chord;
		for(i=0;i<6;++i)
		{
			if(*src == NO_NOTE)
				*result = NO_NOTE;
			else			
				*result=*src+12;
			++result;
			if(options & OPT_SPREAD) {
				*result=NO_NOTE;
				++result;
			}
			else if(options & OPT_OCTAVEPAIR) 
			{
				*result=*src+24;
				++result;
			}
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
				if(options & OPT_DAMPCHANGE)
				{
					startNote(0, notes[i], 0);
					if(options & OPT_HOLDCHORD)
						stopNote(1, notes[i]);
				}
				else
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
							stopNote(1, notes[i]);
					}
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

	CHORD_SELECTION chordSelection = { CHORD_NONE,  NO_NOTE, ADD_NONE };
	unsigned long b = 1;
	byte stringCount = 0;
	byte modeSelection = NO_MODE;
	
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
			// is the mode button pressed?
			if(!P_MODE)
			{
				if(NO_MODE == modeSelection)
				{
					modeSelection = i;
					if(P_KEYS1) modeSelection |= MODE_OPT_ON;
					if(P_KEYS2) modeSelection |= MODE_OPT_OFF;
					if(P_KEYS3) modeSelection |= MODE_OPT_QUERY;
				}
			}
			// have we decided on the root note yet?
			else
			{
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
				else if((options & OPT_ADDNOTES) && (chordSelection.extension == ADD_NONE))
				{
					if(P_KEYS1)
						chordSelection.extension = SUS_4;
					else if(P_KEYS2)
						chordSelection.extension = ADD_6;
					else if(P_KEYS3)
						chordSelection.extension = ADD_9;
				}
			}
		}

		if(!P_MODE)
		{
			if(P_STYLUS)
				velocity = 0x0f | (i<<4);
		}
		// now check whether we got a signal
		// back from the stylus (meaning that
		// it's touching this string)
		else if(P_STYLUS)
		{
			++stringCount;
			
			// string is being touched... was
			// it being touched before?
			if(!(strings & b))
			{
				// remember this string is being touched
				strings |= b;
				
				// stop the note playing (if
				// it is currently playing). When 
				// stylus is touching a string it
				// is "damped" and does not play
				// till contact is broken
				if(notes[i] != NO_NOTE)
					startNote(0, notes[i],  (options & OPT_PLUCK)? 0 : velocity);						
			}
		}
		// stylus not touching string now, but was it 
		// touching the string before?
		else if(strings & b)
		{
			// remember string is not being touched
			strings &= ~b;
			
			// start a note playing
			if(notes[i] != NO_NOTE)
				startNote(0, notes[i], (options & OPT_PLUCK)? velocity : 0);
		}	
		
		// shift the masking bit	
		b<<=1;
		
	}	
	
	if(modeSelection!=NO_MODE)
	{
		byte optionBit = 0;
		switch(modeSelection&0xF)
		{
			case MODE_PLUCK: 		optionBit = OPT_PLUCK; 			break;
			case MODE_HOLDCHORD: 	optionBit = OPT_HOLDCHORD; 		break;
			case MODE_ADDNOTES: 	optionBit = OPT_ADDNOTES; 		break;
			case MODE_GUITAR: 		optionBit = OPT_GUITAR; 		break;
			case MODE_RINGON: 		optionBit = OPT_RINGON; 		break;
			case MODE_OCTAVEPAIR: 	optionBit = MODE_OCTAVEPAIR; 	break;		
			case MODE_SPREAD: 		optionBit = MODE_SPREAD; 		break;		
			case MODE_ALLNOTESOFF: 
				stopAllNotes(0); 
				stopAllNotes(1); 
				break;
		}
		if(modeSelection & MODE_OPT_ON)
			options |= optionBit;
		else if(modeSelection & MODE_OPT_OFF)
			options &= ~optionBit;		
		P_LED = (options & optionBit)? 1:0;
		delay_ms(100);
		P_LED = 0;
	}
	else
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
		P_LED = 1;
		delay_ms(500);
		P_LED = 0;
		delay_ms(500);
	}
}

void main()
{ 
	// osc control / 8MHz / internal
	osccon = 0b01110010;

	// weak pull up on A0 and C5
	wpua = 0b00000001;
	wpuc = 0b00100000;
	option_reg.7 = 0;
	
	
	// configure io
			//76543210
	trisa = 0b00110000;              	
    trisc = 0b00101010;              
    
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



