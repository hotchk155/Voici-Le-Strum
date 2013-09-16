////////////////////////////////////////////////////////////
//
//    //                ////  //
//    //     ///       //     //                 
//    //    // //       ///   ///   //// //  //  //////
//    //    /////         //  //   //    //  // // // //
//    //    //        //  //  //   //    //  // // // //
//    /////  ///       ////    /// //     ///// //    //
//    STRUMMED CHORD MIDI CONTROLLER
//
//    SOURCEBOOST C FOR PIC16F1825
//
// This work is licensed under the Creative Commons 
// Attribution-NonCommercial 3.0 Unported License. 
// To view a copy of this license, please visit:
// http://creativecommons.org/licenses/by-nc/3.0/
//
// Please contact me directly if you'd like a CC 
// license allowing use for commercial purposes:
// jason_hotchkiss<at>hotmail.com
//
// Full repository with hardware information:
// https://github.com/hotchk155/Voici-Le-Strum
//
// Ver Date 
// 1.0 16Jun2013 Initial baseline release for new PCB
// 1.1 23Jun2013 First release
// 1.2 02Jul2013 Reverse Strum Mode Added
// 1.3 16Sep2013 Allow settling time in pollIO
//
#define VERSION_MAJOR 1
#define VERSION_MINOR 3
//
////////////////////////////////////////////////////////////

// INCLUDE FILES
#include <system.h>
#include <memory.h>
#include <eeprom.h>

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

// special EEPROM addresses
#define EEPROM_ADDR_MAGIC_COOKIE 0
#define EEPROM_ADDR_OPTIONS_HIGH 1
#define EEPROM_ADDR_OPTIONS_LOW 2
#define EEPROM_ADDR_REV_STRUM 3

// special token used to indicate initialised eeprom
#define EEPROM_MAGIC_COOKIE 123

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
	OPT_STOPONBREAK 		= 0x0002, // stop playing a note when stylus breaks contact with pad
	OPT_PLAYONBREAK			= 0x0004, // start playing a note when stylus breaks contact with pad 
	OPT_STOPONMAKE			= 0x0008, // stop playing a note when stylus makes contact with pad 
	OPT_DRONE				= 0x0010, // play chord triad on MIDI channel 2 as soon as chord button is pressed and stop when released
	OPT_GUITAR				= 0x0020, // use guitar voicing
	OPT_GUITAR2 			= 0x0040, // use octave shifted guitar chord map on strings 10-16
	OPT_GUITARBASSNOTES		= 0x0080, // enable bottom guitar strings (that are usually damped) but can provide alternating bass notes
	OPT_SUSTAIN				= 0x0100, // do not kill all strings when chord button is released
	OPT_SUSTAINCOMMON		= 0x0200, // when switching to a new chord, allow common notes to sustain (do not retrig) on strings
	OPT_SUSTAINDRONE		= 0x0400, // do not kill drone chord when chord button is released
	OPT_SUSTAINDRONECOMMON	= 0x0800, // when switching to a new chord, allow common notes to sustain (do not retrig) on drone chord
	OPT_ADDNOTES			= 0x1000, // enable adding of sus4, add6, add9 to chord
	OPT_CHROMATIC			= 0x2000, // map strings to chromatic scale from C instead of chord
	OPT_DIATONIC			= 0x4000, // map strings to diatonic major scale 
	OPT_PENTATONIC			= 0x8000  // map strings to pentatonic scale 
};

// Byte type
typedef unsigned char byte;

// This structure is used to define a specific chord setup
typedef struct 
{
	byte chordType;	// The chord shape
	byte rootNote;  // root note from 0 (C)
	byte extension; // added note if applicable
} CHORD_SELECTION;

// special note value
#define NO_NOTE 0xff

// bit mapped register of which strings are currently connected to the stylus 
unsigned long strings =0;

// The first column containing a pressed chord button during the last key scan
byte lastRootNoteSelection = NO_NOTE;

// Define the information relating to string play
byte playChannel = 0;
byte playVelocity = 127;
byte playNotes[16];

// Define the information relating to chord button drone
byte droneChannel = 1;
byte droneVelocity = 127;
byte droneNotes[16];

// This structure records the previous chord selection so we can
// detected if it has changed
CHORD_SELECTION lastChordSelection = { CHORD_NONE, NO_NOTE, ADD_NONE };

////////////////////////////////////////////////////////////
//
//
// DEFINE THE PATCHES
//
//
////////////////////////////////////////////////////////////

// Basic strum
const unsigned int patch_BasicStrum = 
	OPT_PLAYONBREAK			|		
	OPT_STOPONMAKE			|		
	OPT_SUSTAINCOMMON		;

// Guitar strum
const unsigned int patch_GuitarStrum = 
	OPT_PLAYONBREAK			|		
	OPT_STOPONMAKE			|		
	OPT_GUITAR				|
	OPT_GUITAR2				|
	OPT_SUSTAINCOMMON		|
	OPT_ADDNOTES			;

// Guitar sustained
const unsigned int patch_GuitarSustain = 
	OPT_PLAYONBREAK			|		
	OPT_STOPONMAKE			|		
	OPT_GUITAR				|
	OPT_GUITAR2				|
	OPT_SUSTAIN				|
	OPT_SUSTAINCOMMON		|
	OPT_ADDNOTES			;

// Chords and melody
const unsigned int patch_OrganButtons = 
	OPT_PLAYONBREAK			|		
	OPT_STOPONMAKE			|		
	OPT_SUSTAIN				|
	OPT_SUSTAINCOMMON		|
	OPT_DRONE				|
	OPT_SUSTAINDRONE		|
	OPT_SUSTAINDRONECOMMON	;

// Chords with adds and melody
const unsigned int patch_OrganButtonsAddedNotes = 
	OPT_PLAYONBREAK			|		
	OPT_STOPONMAKE			|		
	OPT_SUSTAIN				|
	OPT_SUSTAINCOMMON		|
	OPT_DRONE				|
	OPT_SUSTAINDRONE		|
	OPT_SUSTAINDRONECOMMON	|
	OPT_ADDNOTES	;

// Chords and melody
const unsigned int patch_OrganButtonsAddedNotesRetrig = 
	OPT_PLAYONBREAK			|		
	OPT_STOPONMAKE			|		
	OPT_SUSTAIN				|
	OPT_SUSTAINCOMMON		|
	OPT_DRONE				|
	OPT_SUSTAINDRONE		|
	OPT_ADDNOTES	;

// Chords and chromatic scale
const unsigned int patch_OrganButtonsChromatic = 
	OPT_PLAYONBREAK			|		
	OPT_STOPONMAKE			|		
	OPT_CHROMATIC			|
	OPT_SUSTAIN				|
	OPT_DRONE				|
	OPT_SUSTAINDRONE		|
	OPT_SUSTAINDRONECOMMON	;


unsigned int options = patch_BasicStrum;
byte reverseStrum = 0;

////////////////////////////////////////////////////////////
//
// TOGGLE A USER OPTION
//
////////////////////////////////////////////////////////////
void toggleOption(unsigned long o)
{
	if(options & o)
	{
		options &= ~o;
		P_LED = 1;
		delay_ms(10);
		P_LED = 0;
	}
	else
	{
		options |= o;
		P_LED = 1;
		delay_ms(10);
		P_LED = 0;
		delay_ms(100);
		P_LED = 1;
		delay_ms(10);
		P_LED = 0;
	}
}

////////////////////////////////////////////////////////////
//
// CLEAR USER OPTION
//
////////////////////////////////////////////////////////////
void clearOptions(unsigned long o)
{
	options &= ~o;
}

////////////////////////////////////////////////////////////
//
// LOAD USER OPTIONS FROM EEPROM
//
////////////////////////////////////////////////////////////
void loadUserOptions()
{
	// 123 is a magic cookie which tells us the user
	// patch is initialised
	if(eeprom_read(EEPROM_ADDR_MAGIC_COOKIE) != EEPROM_MAGIC_COOKIE)
		options = patch_BasicStrum;
	else
		options = 
				(unsigned int)eeprom_read(EEPROM_ADDR_OPTIONS_HIGH)<<8 | 
				(unsigned int)eeprom_read(EEPROM_ADDR_OPTIONS_LOW);
	P_LED = 1; delay_ms(10); P_LED = 0; delay_ms(100);
	P_LED = 1; delay_ms(10); P_LED = 0; delay_ms(100);
	P_LED = 1; delay_ms(10); P_LED = 0; delay_ms(100);
}

////////////////////////////////////////////////////////////
//
// SAVE USER OPTIONS TO EEPROM
//
////////////////////////////////////////////////////////////
void saveUserOptions()
{
	eeprom_write(EEPROM_ADDR_MAGIC_COOKIE, EEPROM_MAGIC_COOKIE);
	eeprom_write(EEPROM_ADDR_OPTIONS_HIGH, (options >> 8) & 0xff);
	eeprom_write(EEPROM_ADDR_OPTIONS_LOW, options & 0xff);
	P_LED = 1;	delay_s(2);	P_LED = 0;
}

////////////////////////////////////////////////////////////
//
// TOGGLE STRUM DIRECTION SETTING
//
////////////////////////////////////////////////////////////
byte toggleStrumDirection()
{
	reverseStrum = !reverseStrum;
	eeprom_write(EEPROM_ADDR_REV_STRUM, reverseStrum);
	P_LED = 1; delay_s(2);	P_LED = 0;
}

////////////////////////////////////////////////////////////
//
// PRESET PATCH
//
////////////////////////////////////////////////////////////
void presetPatch(unsigned int o)
{
	options = o;
	P_LED = 1; delay_ms(10); P_LED = 0; delay_ms(100);
	P_LED = 1; delay_ms(10); P_LED = 0; delay_ms(100);
	P_LED = 1; delay_ms(10); P_LED = 0; delay_ms(100);
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
// STOP ALL NOTES
//
////////////////////////////////////////////////////////////
void stopAllNotes(byte channel)
{
	for(int i=0;i<128;++i)
		stopNote(channel,i);
}

////////////////////////////////////////////////////////////
//
// GUITAR CHORD SHAPE DEFINITIONS
//
////////////////////////////////////////////////////////////
void guitarCShape(byte ofs, byte extension, byte *chord)
{
	if(options & OPT_GUITARBASSNOTES)
		chord[0] = 43 + ofs;
	chord[1] = 48 + ofs;
	chord[2] = 52 + ofs + (extension == SUS_4);
	chord[3] = 55 + ofs + 2 * (extension == ADD_6);
	chord[4] = 60 + ofs + 2 * (extension == ADD_9);
	chord[5] = 64 + ofs + (extension == SUS_4);
}
void guitarC7Shape(byte ofs, byte extension, byte *chord)
{
	if(options & OPT_GUITARBASSNOTES)
		chord[0] = 43 + ofs;
	chord[1] = 48 + ofs;
	chord[2] = 52 + ofs + (extension == SUS_4);
	chord[3] = 58 + ofs - (extension == ADD_6);
	chord[4] = 60 + ofs + 2 * (extension == ADD_9);
	chord[5] = 64 + ofs + (extension == SUS_4);
}
void guitarAShape(byte ofs, byte extension, byte *chord)
{
	if(options & OPT_GUITARBASSNOTES)
		chord[0] = 40 + ofs;
	chord[1] = 45 + ofs;
	chord[2] = 52 + ofs + 2 * (extension == ADD_6);
	chord[3] = 57 + ofs + 2 * (extension == ADD_9);;
	chord[4] = 61 + ofs + (extension == SUS_4);
	chord[5] = 64 + ofs;
}
void guitarAmShape(byte ofs, byte extension, byte *chord)
{
	if(options & OPT_GUITARBASSNOTES)
		chord[0] = 40 + ofs;
	chord[1] = 45 + ofs;
	chord[2] = 52 + ofs + 2 * (extension == ADD_6);
	chord[3] = 57 + ofs + 2 * (extension == ADD_9);;
	chord[4] = 60 + ofs  + 2 * (extension == SUS_4);
	chord[5] = 64 + ofs;
}
void guitarA7Shape(byte ofs, byte extension, byte *chord)
{
	if(options & OPT_GUITARBASSNOTES)
		chord[0] = 40 + ofs;
	chord[1] = 45 + ofs;
	chord[2] = 52 + ofs + 2 * (extension == ADD_6);
	chord[3] = 55 + ofs + 4 * (extension == ADD_9);;
	chord[4] = 61 + ofs + (extension == SUS_4);
	chord[5] = 64 + ofs;
}
void guitarDShape(byte ofs, byte extension, byte *chord)
{
	if(options & OPT_GUITARBASSNOTES)
		chord[1] = 45 + ofs;
	chord[2] = 50 + ofs;
	chord[3] = 57 + ofs + 2 * (extension == ADD_6);
	chord[4] = 62 + ofs;
	chord[5] = 66 + ofs  + (extension == SUS_4) - 2*(extension == ADD_9);
}
void guitarDmShape(byte ofs, byte extension, byte *chord)
{
	if(options & OPT_GUITARBASSNOTES)
		chord[1] = 45 + ofs;
	chord[2] = 50 + ofs;
	chord[3] = 57 + ofs + 2 * (extension == ADD_6);
	chord[4] = 62 + ofs;
	chord[5] = 65 + ofs  + 2 * (extension == SUS_4) - (extension == ADD_9);
}
void guitarD7Shape(byte ofs, byte extension, byte *chord)
{
	if(options & OPT_GUITARBASSNOTES)
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
	chord[3] = 55 + ofs  + 2 * (extension == SUS_4);
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
// GUITAR CHORD MAPPING
//
////////////////////////////////////////////////////////////
byte guitarChord(CHORD_SELECTION *pChordSelection, byte transpose, byte *chord)
{	
	memset(chord, NO_NOTE, 16);
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
	for(int i=0;i<16;++i)
		if(chord[i] != NO_NOTE)
			chord[i] += transpose;
	return 6;
}

////////////////////////////////////////////////////////////
//
// MAKE A CHORD BY "STACKING TRIADS"
//
////////////////////////////////////////////////////////////
byte stackTriads(CHORD_SELECTION *pChordSelection, byte maxReps, byte transpose, byte size, byte *chord)
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
		case CHORD_MIN: case CHORD_MIN7: case CHORD_DIM: // minor 3rd
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
	byte root = pChordSelection->rootNote + transpose;
	int from = 0;
	int to = 0;
	while(to < size)
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
// MAKE A SCALE BY MASKING NOTES
//
////////////////////////////////////////////////////////////
byte makeScale(int root, byte transpose, unsigned long mask, byte *chord)
{
	memset(chord,NO_NOTE,16);
	unsigned long b = 0;
	while(root < transpose + 16)
	{
			//       210987654321
		if(!b) b = 0b100000000000;		
		if(mask & b)
		{
			if(root >= transpose)
				chord[root - transpose] = root;
		}
		++root;
		b>>=1;
	}	
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
	if(velocity)
	{
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

	// override allowed by sustain option
	if(sustain)
		return;
		
	// Silence notes 
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
// CALCULATE NOTES FOR A CHORD SHAPE, MAP THEM TO THE STRINGS
// AND START PLAYING DRONE IF APPROPRIATE
//
////////////////////////////////////////////////////////////
void changeToChord(CHORD_SELECTION *pChordSelection)
{	
	
	int i,j;
	byte chord[16];
	byte chordLen;
	byte notes[16];
		
	// is the new chord a "no chord"
	if(CHORD_NONE == pChordSelection->chordType)
	{
		releaseChordNotes(playNotes, playChannel, !!(options & OPT_SUSTAIN));
		releaseChordNotes(droneNotes, droneChannel, !!(options & OPT_SUSTAINDRONE));		
	}
	else 	
	{			
		// are we in guitar mode?
		if(options & OPT_GUITAR)
		{
			// build the guitar chord, using stacked triads
			// as a fallback if there is no chord mapping
			chordLen = guitarChord(pChordSelection, 12, chord);
			if(!chordLen)
			{
				stackTriads(pChordSelection, -1, 60, 6, chord);
				chordLen = 6;
			}
				
			// double up the guitar chords
			if(options & OPT_GUITAR2)
			{
				for(i=0;i<6;++i)
					if(chord[i] != NO_NOTE)
						chord[10+i] = 12+chord[i];
				chordLen = 16;
			}
		}
		// should we have a chromatic scale mapped to the strings?
		else if(options & OPT_CHROMATIC)
		{
			makeScale(pChordSelection->rootNote, 48, 0b111111111111, chord);
			chordLen=16;
		}
		// diatonic major or minor
		else if(options & OPT_DIATONIC)
		{
			if((pChordSelection->chordType == CHORD_MIN)||(pChordSelection->chordType == CHORD_MIN7))
				makeScale(pChordSelection->rootNote, 48, 0b101101011010, chord);
			else
				makeScale(pChordSelection->rootNote, 48, 0b101011010101, chord);
			chordLen=16;
		}
		// pentatonic 
		else if(options & OPT_PENTATONIC)
		{
			makeScale(pChordSelection->rootNote, 48, 0b101010010100, chord);
			chordLen=16;
		}
		else	
		{
			// stack triads
			stackTriads(pChordSelection, -1, 36, 16, chord);
			chordLen = 16;
		}
	
		// copy chord to notes and pad with null notes
		memset(notes, NO_NOTE, 16);
		memcpy(notes, chord, chordLen);
		
		// damp notes which are not a part of the new chord
		playChordNotes(playNotes, notes, playChannel, 0, !!(options & OPT_SUSTAINCOMMON));

		// deal with drone
		if(options & OPT_DRONE)
		{
			// for the drone chord we only play the triad (not stacked)
			stackTriads(pChordSelection, 1, 36, 16, notes);
			playChordNotes(droneNotes, notes, droneChannel, droneVelocity, !!(options & OPT_SUSTAINDRONECOMMON));
		}
	}
	
	// Store the chord, so we can recognise when it changes
	lastChordSelection = *pChordSelection;
	
}

	
////////////////////////////////////////////////////////////
//
// POLL INPUT AND MANAGE THE SENDING OF MIDI INFO
//
////////////////////////////////////////////////////////////
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
		int whichString = reverseStrum? (15-i) : i;
		
		// clock pulse to shift the bit (the first bit does not appear until the
		// second clock pulse, since we tied shift and store clock lines together)
		P_CLK = 0;				
		P_CLK = 1;

		// Allow inputs to settle
		delay_ms(1);
		
		// did we get a signal back on any of the  keyboard scan rows?
		if(P_KEYS1 || P_KEYS2 || P_KEYS3)
		{
			// Is this the first column with a button held (which provides
			// the root note)?
			if(chordSelection.rootNote == NO_NOTE)
			{
				// This logic allows more buttons to be registered without clearing
				// old buttons if the root note is unchanged. This is to ensure that
				// new chord shapes are not applied as the user releases the buttons
				chordSelection.rootNote = i;					
				if(i == lastRootNoteSelection)
					chordSelection.chordType = lastChordSelection.chordType;
				chordSelection.chordType |= (P_KEYS1? CHORD_MAJ:CHORD_NONE)|(P_KEYS2? CHORD_MIN:CHORD_NONE)|(P_KEYS3? CHORD_DOM7:CHORD_NONE);					
			}	
			// Check for chord extension, which is where an additional
			// button is held in a column to the right of the root column
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

		// if MODE is pressed the stylus is used to change the MIDI velocity
		if(!P_MODE)
		{
			if(P_STYLUS)
				playVelocity = 0x0f | (whichString<<4);
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
				if(playNotes[whichString] != NO_NOTE)
				{
					// play or damp the note as needed
					if(options & OPT_PLAYONMAKE)
						startNote(playChannel, playNotes[whichString],  playVelocity);						
					else
					if(options & OPT_STOPONMAKE)
						stopNote(playChannel, playNotes[whichString]);						
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
			if(playNotes[whichString] != NO_NOTE)
			{
				// play or damp the note as needed
				if(options & OPT_PLAYONBREAK)
					startNote(playChannel, playNotes[whichString],  playVelocity);						
				else
				if(options & OPT_STOPONBREAK)
					stopNote(playChannel, playNotes[whichString]);						
			}
		}	
		
		// shift the masking bit	
		b<<=1;		
	}	
		

	if(!P_MODE)
	{		
		// MODE is pressed, has a chord button been newly pressed?
		if(lastRootNoteSelection != chordSelection.rootNote)
		{
			switch(chordSelection.chordType)
			{
			case CHORD_MAJ: // ROW 1
				switch(chordSelection.rootNote)
				{
				case 0: presetPatch(patch_BasicStrum); break;
				case 2: presetPatch(patch_GuitarStrum); break;
				case 4: presetPatch(patch_GuitarSustain); break;
				case 5: presetPatch(patch_OrganButtons); break;
				case 7: presetPatch(patch_OrganButtonsAddedNotes); break;
				case 9: presetPatch(patch_OrganButtonsAddedNotesRetrig); break;
				case 10: toggleStrumDirection(); break;
				case 11: loadUserOptions(); break;
				}
				break;
				
			case CHORD_MIN: // ROW 2
				switch(chordSelection.rootNote)
				{
				case 0: toggleOption(OPT_PLAYONMAKE); break;
				case 1: toggleOption(OPT_PLAYONBREAK); break;
				case 2: toggleOption(OPT_GUITAR); break;
				case 3: toggleOption(OPT_ADDNOTES); break;
				case 4: toggleOption(OPT_SUSTAIN); break;
				case 5: toggleOption(OPT_CHROMATIC); clearOptions(OPT_DIATONIC|OPT_PENTATONIC); break;
				case 9: toggleOption(OPT_DRONE); break;
				case 10: toggleOption(OPT_SUSTAINDRONE); break;
				case 11: saveUserOptions(); break;
				}
				break;	
				
			case CHORD_DOM7: // ROW3
				switch(chordSelection.rootNote)
				{
				case 0: toggleOption(OPT_STOPONMAKE); break;
				case 1: toggleOption(OPT_STOPONBREAK); break;
				case 2: toggleOption(OPT_GUITAR2); break;
				case 3: toggleOption(OPT_GUITARBASSNOTES); break;
				case 4: toggleOption(OPT_SUSTAINCOMMON); break;
				case 5: toggleOption(OPT_DIATONIC); clearOptions(OPT_CHROMATIC|OPT_PENTATONIC); break;
				case 6: toggleOption(OPT_PENTATONIC); clearOptions(OPT_DIATONIC|OPT_CHROMATIC); break;
				case 10: toggleOption(OPT_SUSTAINDRONECOMMON); break;
				case 11:
					// MIDI Panic
					stopAllNotes(playChannel);
					if(options & OPT_DRONE)
						stopAllNotes(droneChannel);
					break;
				}
				break;		
			}	
		}
	}
	else
	{
		// has the chord changed? note that if the stylus is bridging 2 strings we will not change
		// the chord selection. This is because this situation can confuse the keyboard matrix
		// causing unwanted chord changed
		if((stringCount < 2) && 0 != memcmp(&chordSelection, &lastChordSelection, sizeof(CHORD_SELECTION)))
			changeToChord(&chordSelection);	
	}
	
	// remember the root note for this keyboard scan
	lastRootNoteSelection = chordSelection.rootNote;
}

////////////////////////////////////////////////////////////
//
// BLINK FIRMWARE VERSION
//
////////////////////////////////////////////////////////////
void showVersion()
{
	int i;
	for(i=0;i<VERSION_MAJOR;++i)
	{
		P_LED = 1;
		delay_s(1);
		P_LED = 0;
		delay_ms(250);
	}
	P_LED = 1; 
	delay_ms(10);	
	P_LED = 0;
	delay_ms(250);
	for(i=0;i<VERSION_MINOR;++i)
	{
		P_LED = 1;
		delay_s(1);
		P_LED = 0;
		delay_ms(250);
	}
	P_LED = 1; 
	delay_ms(10);	
	P_LED = 0;
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

	if(!P_MODE)
	{
		showVersion();
	}
	else
	{
		P_LED = 1;
		delay_ms(100);
		P_LED = 0;
	}
	
	// initialise MIDI comms
	init_usart();

	// initialise the notes array
	memset(playNotes,NO_NOTE,sizeof(playNotes));
	memset(droneNotes,NO_NOTE,sizeof(droneNotes));

	// load the reverse strum setting
	reverseStrum = (eeprom_read(EEPROM_ADDR_REV_STRUM) == 1);
	
	for(;;)
	{
		// and now just repeatedly
		// check for input
		pollIO();
	}
}



