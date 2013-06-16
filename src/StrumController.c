////////////////////////////////////////////////////////////
//
// STRUM CHORD CONTROLLER
//
// (c) 2013 J.Hotchkiss
// SOURCEBOOST C FOR PIC16F1825
//
////////////////////////////////////////////////////////////

// INCLUDE FILES
#include <system.h>
#include <memory.h>

// PIC CONFIG
#pragma DATA _CONFIG1, _FOSC_INTOSC & _WDTE_OFF & _MCLRE_OFF &_CLKOUTEN_OFF
#pragma DATA _CONFIG2, _WRT_OFF & _PLLEN_OFF & _STVREN_ON & _BORV_19 & _LVP_OFF
#pragma CLOCK_FREQ 8000000

typedef unsigned char byte;

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

// CHORD SHAPES
enum {
	CHORD_NONE 	= 0b000,
	CHORD_MAJ 	= 0b001,
	CHORD_MIN	= 0b010,
	CHORD_DOM7	= 0b100,
	CHORD_MAJ7  = 0b101,
	CHORD_MIN7  = 0b110,
	CHORD_AUG   = 0b111,
	CHORD_DIM	= 0b011
};

// CHORD ROOT NOTES
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

// CONTROLLING FLAGS
enum {
	OPT_PLAYONMAKE 			= 0x0001, // start playing a note when stylus makes contact with pad
	OPT_PLAYONBREAK			= 0x0002, // start playing a note when stylus breaks contact with pad 
	OPT_DRONE				= 0x0004, // play "drone" chords on 
	OPT_GUITAR				= 0x0008, 
	OPT_GUITAR2 			= 0x0010,
	OPT_SUSTAIN				= 0x0020, 
	OPT_SUSTAINCOMMON		= 0x0040,
	OPT_SUSTAINDRONE		= 0x0080,
	OPT_SUSTAINDRONECOMMON	= 0x0100,
	OPT_ADDNOTES			= 0x0200, 
	OPT_OCTAVEPAIR			= 0x0400, 
	OPT_UNISONPAIR			= 0x0800, 
	OPT_SPREAD				= 0x1000 
};

#define OPTIONS_C (OPT_PLAYONBREAK|OPT_ADDNOTES|OPT_SUSTAIN|OPT_SUSTAINCOMMON)
#define OPTIONS_D (OPT_DRONE|OPT_PLAYONBREAK|OPT_ADDNOTES|OPT_SUSTAIN|OPT_SUSTAINCOMMON)
#define OPTIONS_E (OPT_PLAYONMAKE|OPT_ADDNOTES|OPT_SUSTAIN|OPT_SUSTAINCOMMON)
#define OPTIONS_F (OPT_DRONE|OPT_PLAYONMAKE|OPT_ADDNOTES|OPT_SUSTAIN|OPT_SUSTAINCOMMON)
#define OPTIONS_G (OPT_DRONE|OPT_GUITAR|OPT_SUSTAIN|OPT_PLAYONBREAK|OPT_ADDNOTES)
#define OPTIONS_A (OPT_GUITAR|OPT_OCTAVEPAIR|OPT_PLAYONBREAK|OPT_ADDNOTES)

unsigned int options = OPT_DRONE|OPT_SUSTAINDRONE|OPT_PLAYONBREAK|OPT_ADDNOTES|OPT_SUSTAIN|OPT_SUSTAINCOMMON;

// special note value
#define NO_NOTE 0xff

// bit mapped register of which strings are currently connected
// to the stylus (notes triggered when stylus breaks contact
// with the strings)
unsigned long strings =0;

// Define the information relating to string play
byte playChannel = 0;
byte playVelocity = 127;
byte playNotes[16];

// Define the information relating to chord button drone
byte droneChannel = 1;
byte droneVelocity = 127;
byte droneNotes[16];

// This structure is used to define a specific chord setup
typedef struct 
{
	byte chordType;
	byte rootNote;
	byte extension;
} CHORD_SELECTION;

// This structure records the previous chord selection so we know
// if it has changed
CHORD_SELECTION lastChordSelection = { CHORD_NONE, NO_NOTE, ADD_NONE };


////////////////////////////////////////////////////////////
//
// BLINK DIAGNOSTIC LED A CERTAIN NUMBER OF TIMES
//
////////////////////////////////////////////////////////////
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

////////////////////////////////////////////////////////////
//
// INITIALISE SERIAL PORT FOR MIDI
//
////////////////////////////////////////////////////////////
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
//
// SEND A MIDI BYTE
//
////////////////////////////////////////////////////////////
void send(unsigned char c)
{
	txreg = c;
	while(!txsta.1);
}

////////////////////////////////////////////////////////////
//
// START NOTE MESSAGE
//
////////////////////////////////////////////////////////////
void startNote(byte channel, byte note, byte value)
{
	P_LED = 1;
	send(0x90 | channel);
	send(note&0x7f);
	send(value&0x7f);
	P_LED = 0;	
}

////////////////////////////////////////////////////////////
//
// STOP NOTE MESSAGE
//
////////////////////////////////////////////////////////////
void stopNote(byte channel, byte note)
{
	P_LED = 1;
	send(0x90 | channel);
	send(note&0x7f);
	send(0x00);
	P_LED = 0;	
}


////////////////////////////////////////////////////////////
//
// GUITAR CHORD SHAPE DEFINITIONS
//
////////////////////////////////////////////////////////////
void guitarCShape(byte ofs, byte extension, byte *chord)
{
	chord[0] = 43 + ofs;
	chord[1] = 48 + ofs;
	chord[2] = 52 + ofs + (extension == SUS_4);
	chord[3] = 55 + ofs + 2 * (extension == ADD_6);
	chord[4] = 60 + ofs + 2 * (extension == ADD_9);
	chord[5] = 64 + ofs + (extension == SUS_4);
}
void guitarC7Shape(byte ofs, byte extension, byte *chord)
{
	chord[0] = 43 + ofs;
	chord[1] = 48 + ofs;
	chord[2] = 52 + ofs + (extension == SUS_4);
	chord[3] = 58 + ofs - (extension == ADD_6);
	chord[4] = 60 + ofs + 2 * (extension == ADD_9);
	chord[5] = 64 + ofs + (extension == SUS_4);
}
void guitarAShape(byte ofs, byte extension, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 45 + ofs;
	chord[2] = 52 + ofs + 2 * (extension == ADD_6);
	chord[3] = 57 + ofs + 2 * (extension == ADD_9);;
	chord[4] = 61 + ofs + (extension == SUS_4);
	chord[5] = 64 + ofs;
}
void guitarAmShape(byte ofs, byte extension, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 45 + ofs;
	chord[2] = 52 + ofs + 2 * (extension == ADD_6);
	chord[3] = 57 + ofs + 2 * (extension == ADD_9);;
	chord[4] = 60 + ofs  + (extension == SUS_4);
	chord[5] = 64 + ofs;
}
void guitarA7Shape(byte ofs, byte extension, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 45 + ofs;
	chord[2] = 50 + ofs + 4 * (extension == ADD_6);
	chord[3] = 57 + ofs + 2 * (extension == ADD_9);;
	chord[4] = 60 + ofs  + (extension == SUS_4);
	chord[5] = 64 + ofs;
}
void guitarDShape(byte ofs, byte extension, byte *chord)
{
	chord[0] = NO_NOTE;
	chord[1] = 45 + ofs;
	chord[2] = 50 + ofs;
	chord[3] = 57 + ofs + 2 * (extension == ADD_6);
	chord[4] = 62 + ofs;
	chord[5] = 66 + ofs  + (extension == SUS_4) - 2*(extension == ADD_9);
}
void guitarDmShape(byte ofs, byte extension, byte *chord)
{
	chord[0] = NO_NOTE;
	chord[1] = 45 + ofs;
	chord[2] = 50 + ofs;
	chord[3] = 57 + ofs + 2 * (extension == ADD_6);
	chord[4] = 62 + ofs;
	chord[5] = 65 + ofs  + (extension == SUS_4) - (extension == ADD_9);
}
void guitarD7Shape(byte ofs, byte extension, byte *chord)
{
	chord[0] = NO_NOTE;
	chord[1] = 45 + ofs;
	chord[2] = 50 + ofs;
	chord[3] = 57 + ofs + 2 * (extension == ADD_6);
	chord[4] = 60 + ofs;
	chord[5] = 66 + ofs  + (extension == SUS_4)- 2*(extension == ADD_9);
}
void guitarEShape(byte ofs, byte extension, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 47 + ofs;
	chord[2] = 52 + ofs + 2 * (extension == ADD_9);
	chord[3] = 56 + ofs  + (extension == SUS_4);
	chord[4] = 59 + ofs + 2 * (extension == ADD_6);
	chord[5] = 64 + ofs;
}
void guitarEmShape(byte ofs, byte extension, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 47 + ofs;
	chord[2] = 52 + ofs + 2 * (extension == ADD_9);
	chord[3] = 55 + ofs  + (extension == SUS_4);
	chord[4] = 59 + ofs + 2 * (extension == ADD_6);
	chord[5] = 64 + ofs;
}
void guitarE7Shape(byte ofs, byte extension, byte *chord)
{
	chord[0] = 40 + ofs;
	chord[1] = 47 + ofs;
	chord[2] = 50 + ofs + 4 * (extension == ADD_9);
	chord[3] = 56 + ofs  + (extension == SUS_4);
	chord[4] = 59 + ofs + 2 * (extension == ADD_6);
	chord[5] = 64 + ofs;
}
void guitarGShape(byte ofs, byte extension, byte *chord)
{
	chord[0] = 43 + ofs;
	chord[1] = 47 + ofs  + (extension == SUS_4);
	chord[2] = 50 + ofs + 2 * (extension == ADD_6);
	chord[3] = 55 + ofs  + 2*(extension == ADD_9);
	chord[4] = 59 + ofs + (extension == SUS_4);
	chord[5] = 67 + ofs;
}

////////////////////////////////////////////////////////////
//
// MAKE A GUITAR CHORD 
//
////////////////////////////////////////////////////////////
byte guitarChord(CHORD_SELECTION *pChordSelection, byte *result)
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
			case ROOT_C:		guitarCShape(0, pChordSelection->extension, chord);	break;
			case ROOT_CSHARP:  	guitarAShape(4, pChordSelection->extension, chord);	break;
			case ROOT_D:		guitarDShape(0, pChordSelection->extension, chord);	break;
			case ROOT_DSHARP:	guitarAShape(6, pChordSelection->extension, chord);	break;
			case ROOT_E:		guitarEShape(0, pChordSelection->extension, chord);	break;
			case ROOT_F:		guitarEShape(1, pChordSelection->extension, chord);	break;
			case ROOT_FSHARP:	guitarEShape(2, pChordSelection->extension, chord);	break;
			case ROOT_G:		guitarGShape(0, pChordSelection->extension, chord);	break;
			case ROOT_GSHARP:	guitarEShape(4, pChordSelection->extension, chord);	break;
			case ROOT_A:		guitarAShape(0, pChordSelection->extension, chord);	break;
			case ROOT_ASHARP:   guitarAShape(1, pChordSelection->extension, chord);	break;
			case ROOT_B:   		guitarAShape(2, pChordSelection->extension, chord);	break;
			}
			break;
		case CHORD_MIN:
			switch(pChordSelection->rootNote)
			{
			case ROOT_C:		guitarAmShape(3, pChordSelection->extension, chord);	break;
			case ROOT_CSHARP:  	guitarAmShape(4, pChordSelection->extension, chord);	break;
			case ROOT_D:		guitarDmShape(0, pChordSelection->extension, chord);	break;
			case ROOT_DSHARP:	guitarAmShape(6, pChordSelection->extension, chord);	break;
			case ROOT_E:		guitarEmShape(0, pChordSelection->extension, chord);	break;
			case ROOT_F:		guitarEmShape(1, pChordSelection->extension, chord);	break;
			case ROOT_FSHARP:	guitarEmShape(2, pChordSelection->extension, chord);	break;
			case ROOT_G:		guitarEmShape(3, pChordSelection->extension, chord);	break;
			case ROOT_GSHARP:	guitarEmShape(4, pChordSelection->extension, chord);	break;
			case ROOT_A:		guitarAmShape(0, pChordSelection->extension, chord);	break;
			case ROOT_ASHARP:   guitarAmShape(1, pChordSelection->extension, chord);	break;
			case ROOT_B:   		guitarAmShape(2, pChordSelection->extension, chord);	break;
			}
			break;
		case CHORD_DOM7:
			switch(pChordSelection->rootNote)
			{
			case ROOT_C:		guitarC7Shape(0, pChordSelection->extension, chord);	break;
			case ROOT_CSHARP:  	guitarA7Shape(4, pChordSelection->extension, chord);	break;
			case ROOT_D:		guitarD7Shape(0, pChordSelection->extension, chord);	break;
			case ROOT_DSHARP:	guitarA7Shape(6, pChordSelection->extension, chord);	break;
			case ROOT_E:		guitarE7Shape(0, pChordSelection->extension, chord);	break;
			case ROOT_F:		guitarE7Shape(1, pChordSelection->extension, chord);	break;
			case ROOT_FSHARP:	guitarE7Shape(2, pChordSelection->extension, chord);	break;
			case ROOT_G:		guitarE7Shape(3, pChordSelection->extension, chord);	break;
			case ROOT_GSHARP:	guitarE7Shape(4, pChordSelection->extension, chord);	break;
			case ROOT_A:		guitarA7Shape(0, pChordSelection->extension, chord);	break;
			case ROOT_ASHARP:   guitarA7Shape(1, pChordSelection->extension, chord);	break;
			case ROOT_B:   		guitarA7Shape(2, pChordSelection->extension, chord);	break;
			}
			break;
		default:
			return 0;
	}
	
	for(i=0;i<6;++i)
	{
		if(chord[i] == NO_NOTE)
			result[i] = NO_NOTE;
		else			
			result[i]=chord[i]+12;
	}
	return 6;
}

////////////////////////////////////////////////////////////
//
// MAKE A CHORD BY "STACKING TRIADS"
//
////////////////////////////////////////////////////////////
byte stackTriads(CHORD_SELECTION *pChordSelection, byte maxReps, byte *chord)
{
	byte struc[5];
	byte len = 0;

	memset(chord, NO_NOTE, 16);
	
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
		if(++from >= len)
		{
			if(!--maxReps)
				return to;
			root+=12;
			from = 0;
		}
	}
	return to;
}

////////////////////////////////////////////////////////////
//
// START PLAYING THE NOTES OF THE NEW CHORD
//
////////////////////////////////////////////////////////////
void playChordNotes(byte *oldNotes, byte *newNotes, byte channel, byte velocity, byte sustainCommon)
{
	int i,j;
	
	// Start by silencing old notes which are not in the new chord
	for(i=0;i<16;++i)
	{		
		if(NO_NOTE != oldNotes[i])
		{
			if(sustainCommon)
			{
				for(j=0;j<16;++j)
				{
					if(oldNotes[i] == newNotes[j])
						break;
				}
				if(j==16)
				{
					stopNote(channel, oldNotes[i]);
					oldNotes[i] = NO_NOTE;
				}
			}
			else
			{
				stopNote(channel, oldNotes[i]);
				oldNotes[i] = NO_NOTE;
			}
		}	
	}
	
	// Now play notes which are not already playing
	for(i=0;i<16;++i)
	{		
		if(NO_NOTE != newNotes[i])
		{
			for(j=0;j<16;++j)
			{
				if(oldNotes[j] == newNotes[i])
					break;
			}
			if(j==16)
			{
				startNote(channel, newNotes[i], velocity);
			}
		}
	}

	// remember the notes
	memcpy(oldNotes, newNotes, 16);
}

////////////////////////////////////////////////////////////
//
// RELEASE THE NOTES OF A CHORD
//
////////////////////////////////////////////////////////////
void releaseChordNotes(byte *oldNotes, byte channel, byte sustain)
{
	int i;
	
	if(sustain)
		return;
		
	// Start by silencing old notes which are not in the new chord
	for(i=0;i<16;++i)
	{		
		if(NO_NOTE != oldNotes[i])
		{
			stopNote(channel, oldNotes[i]);
			oldNotes[i] = NO_NOTE;
		}	
	}

}

////////////////////////////////////////////////////////////
//
// CALCULATE NOTES FOR A CHORD SHAPE AND MAP THEM TO THE STRINGS
//
////////////////////////////////////////////////////////////
void changeToChord(CHORD_SELECTION *pChordSelection)
{	
	
	int i,j;
	byte chord[16];
	byte chordLen;
	
	// is the new chord a "no chord"
	if(CHORD_NONE == pChordSelection->chordType)
	{
		releaseChordNotes(playNotes, playChannel, (options & OPT_SUSTAIN));
		releaseChordNotes(droneNotes, droneChannel, (options & OPT_SUSTAINDRONE));		
	}
	else 	
	{
			
		// are we in guitar mode?
		if(options & OPT_GUITAR)
		{
			// build the guitar chord, using stacked triads
			// as a fallback if there is no chord mapping
			chordLen = guitarChord(pChordSelection, chord);
			if(!chordLen)
			{
				stackTriads(pChordSelection, -1, chord);
				chordLen = 6;
			}
				
			// double up the guitar chords
			if(options & OPT_GUITAR2)
			{
				for(i=0;i<6;++i)
					chord[10+i] = 12+chord[i];
				chordLen = 16;
			}
		}
		else
		{
			// stack triads
			chordLen = stackTriads(pChordSelection, -1, chord);
		}
	
	
		// map the new chord to the 16 strings
		int getPos =0;
		for(i=0;i<16;)
		{
			if(getPos >= chordLen)
			{
				// no more notes in the chord... padding
				playNotes[i++] = NO_NOTE;
			}
			else
			{
				// store the next note
				playNotes[i++] = chord[getPos];
	
				// add a blank space?
				if(!!(options & OPT_SPREAD) && i<16)
				{
					playNotes[i++] = NO_NOTE;
				}
				
				// pair octaves?
				if(!!(options & (OPT_OCTAVEPAIR|OPT_UNISONPAIR)) && i<16)
				{				
					if(options & OPT_OCTAVEPAIR)
					{
						playNotes[i++] = 12 + chord[getPos];
					}
					else
					{
						playNotes[i++] = chord[getPos];
					}
					
					if(!!(options & OPT_SPREAD) && i<16)
					{
						playNotes[i++] = NO_NOTE;
					}
				}				
				++getPos;
			}
		}
		playChordNotes(playNotes, chord, playChannel, playVelocity, (options & OPT_SUSTAINCOMMON));

		if(options & OPT_DRONE)
		{
			stackTriads(pChordSelection, 1, chord);
			playChordNotes(droneNotes, chord, droneChannel, droneVelocity, (options & OPT_SUSTAINDRONECOMMON));
		}
	}
	lastChordSelection = *pChordSelection;
	
}

				
////////////////////////////////////////////////////////////
//
// POLL INPUT AND MANAGE THE SENDING OF MIDI INFO
//
////////////////////////////////////////////////////////////
byte lastRootNoteSelection = NO_NOTE;
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
				//
			}
			else
			{				
				// Is this the first column with a button held (which provides
				// the root note)?
				if(chordSelection.rootNote == NO_NOTE)
				{
					// This logic allows more buttons to be registered without clearing
					// old buttons if the root note is unchanged. This is to ensure that
					// new chord shapes are not accidentally applied as the user releases
					// the buttons
					chordSelection.rootNote = i;					
					if(i == lastRootNoteSelection)
						chordSelection.chordType = lastChordSelection.chordType;
					chordSelection.chordType |= (P_KEYS1? CHORD_MAJ:CHORD_NONE)|(P_KEYS2? CHORD_MIN:CHORD_NONE)|(P_KEYS3? CHORD_DOM7:CHORD_NONE);					
				}	
				// Check for chord extension, which is where an additional
				// button is held in a column to the right of the root 
				// column
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

		// if MODE is pressed the stylus is used to change the MIDI velocity
		if(!P_MODE)
		{
			if(P_STYLUS)
				playVelocity = 0x0f | (i<<4);
		}
		// otherwise check whether we got a signal back from the stylus (meaning that
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
				
				// does it map to a real note?
				if(playNotes[i] != NO_NOTE)
				{
					// play or damp the note as needed
					if(options & OPT_PLAYONMAKE)
						startNote(playChannel, playNotes[i],  playVelocity);						
					else
						stopNote(playChannel, playNotes[i]);						
				}
			}
		}
		// stylus not touching string now, but was it 
		// touching the string before?
		else if(strings & b)
		{
			// remember string is not being touched
			strings &= ~b;
			
			// does it map to a real note?
			if(playNotes[i] != NO_NOTE)
			{
				// play or damp the note as needed
				if(options & OPT_PLAYONBREAK)
					startNote(playChannel, playNotes[i],  playVelocity);						
				else
					stopNote(playChannel, playNotes[i]);						
			}
		}	
		
		// shift the masking bit	
		b<<=1;		
	}	
		
	lastRootNoteSelection = chordSelection.rootNote;
	
	// has the chord changed? note that if the stylus is bridging 2 strings we will not change
	// the chord selection. This is because this situation can confuse the keyboard matrix
	// causing unwanted chord changed
	if((stringCount < 2) && 0 != memcmp(&chordSelection, &lastChordSelection, sizeof(CHORD_SELECTION)))
		changeToChord(&chordSelection);	
}

////////////////////////////////////////////////////////////
//
// ENTRY POINT
//
////////////////////////////////////////////////////////////
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
      
	// initialise MIDI comms
	init_usart();

    blink(2);

	// initialise the notes array
	memset(playNotes,NO_NOTE,sizeof(playNotes));
	memset(droneNotes,NO_NOTE,sizeof(droneNotes));
	
	
	for(;;)
	{
		// and now just repeatedly
		// check for input
		pollIO();
	}
}



