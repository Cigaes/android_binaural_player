//
//	SBaGen - Sequenced Binaural Beat Generator
//
//	(c) 1999-2007 Jim Peters <jim@uazu.net>.  All Rights Reserved.
//	For latest version see http://sbagen.sf.net/ or
//	http://uazu.net/sbagen/.  Released under the GNU GPL version 2.
//	Use at your own risk.
//
//	" This program is free software; you can redistribute it and/or modify
//	  it under the terms of the GNU General Public License as published by
//	  the Free Software Foundation, version 2.
//	  
//	  This program is distributed in the hope that it will be useful,
//	  but WITHOUT ANY WARRANTY; without even the implied warranty of
//	  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	  GNU General Public License for more details. "
//
//	See the file COPYING for details of this license.
//	
//	- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
//
//	Some code fragments in the Win32 audio handling are based on
//	code from PLIB (c) 2001 by Steve Baker, originally released
//	under the LGPL (slDSP.cxx and sl.h).  For the original source,
//	see the PLIB project: http://plib.sf.net
//
//	The code for the Mac audio output was based on code from the
//	FINK project's patches to ESounD, by Shawn Hsiao and Masanori
//	Sekino.  See: http://fink.sf.net

//	2010-07-18 - Nicolas George
//	Ported to Java Native Interface for Android:
//	Removed #ifdef soup.
//	Unconditionnally removed soundcard related code.
//	Hardcode Unix style functions, no fancy tty output.
//	Remove Ogg/MP3 decoders.
//	Remove most diagnosis.
//	Remove input mixing.
//	Always output to auxiliary funcion.
//	Never sync to clock.
//	Make most functions and variables static.
//	Remove immediate and preprogrammed modes.
//	Reject options in source file.
//	Free all mallocated structures.
//	Provide clean entry points.
//	Make parser and loop return an error instead of exit.
//	Return the error in a buffer.
//	Removed helpful diagnosis.

/*

API of the librarized version of sbagen.c, in rough calling order.
All fonctions that can fail return 0 in case of success and -1 in case of
error.

int sbagen_init(void);
-> Computes the sin table; can fail if malloc fails.

int sbagen_set_parameters(int rate, int prate, int fade, const char *roll);
-> Sets decoding parameters; can fail if roll is invalid.
	rate: sample rate; default: 44100; option -r
	prate: frequency recalculation, default: 10, option -R
	fade: fade time (ms), default: 60000, option -F
	roll: headphone roll-off compensation, option -c (see sbagen doc)

int sbagen_parse_seq(const char *seq);
-> Parses the sequence; can fail on syntax error or out of memory.
	seq: the text of the sequence, not the filename.

int sbagen_run(void);
-> Generates the waves; can fail on out of memory or if writeOut fails.

void sbagen_free_seq(void);
-> Frees the memory allocates by sbagen_parse_seq.

void
sbagen_exit(void);
-> Frees the sin table.

char *
sbagen_get_error(void);
-> Returns the error message; never fails.

static int writeOut(char *buf, int size);
-> To be implemented. Called by sbagen_run; can return -1 to fail.
	buf: samples; actually "short (*buf)[2]"
	size: size of buf in octets; divide by 4 (2 o/sample, 2 channels)

*/

#define VERSION "1.4.4"

// This should be built with one of the following target macros
// defined, which selects options for that platform, or else with some
// of the individual named flags #defined as listed later.
//
// Ogg and MP3 support is handled separately from the T_* macros.

#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>
typedef int64_t S64;

#include <sys/times.h>

typedef struct Channel Channel;
typedef struct Voice Voice;
typedef struct Period Period;
typedef struct NameDef NameDef;
typedef struct BlockDef BlockDef;
typedef unsigned char uchar;

static inline int t_per24(int t0, int t1) ;
static inline int t_per0(int t0, int t1) ;
static inline int t_mid(int t0, int t1) ;
static int init_sin_table(void) ;
static void * Alloc(size_t len) ;
static char * StrDup(char *str) ;
static int loop() ;
static int outChunk() ;
static void corrVal(int ) ;
static int readLine() ;
static char * getWord(void) ;
static void badSeq(void) ;
static int readSeq(const char *text) ;
static int correctPeriods();
static int setup_device(void) ;
static int readNameDef();
static int readTimeLine();
static int voicesEq(Voice *, Voice *);
static void error(char *fmt, ...) ;
static int readTime(char *, int *);
static int writeOut(char *, int);
static int sinc_interpolate(double *, int, int *);
static int handleOptions(char *p);
static int setupOptC(const char *spec) ;

#define N_CH 16			// Number of channels

struct Voice {
  int typ;			// Voice type: 0 off, 1 binaural, 2 pink noise, 3 bell, 4 spin,
   				//   5 mix, 6 mixspin, 7 mixbeat, -1 to -100 wave00 to wave99
  double amp;			// Amplitude level (0-4096 for 0-100%)
  double carr;			// Carrier freq (for binaural/bell), width (for spin)
  double res;			// Resonance freq (-ve or +ve) (for binaural/spin)
};

struct Channel {
  Voice v;			// Current voice setting (updated from current period)
  int typ;			// Current type: 0 off, 1 binaural, 2 pink noise, 3 bell, 4 spin,
   				//   5 mix, 6 mixspin, 7 mixbeat, -1 to -100 wave00 to wave99
  int amp, amp2;		// Current state, according to current type
  int inc1, off1;		//  ::  (for binaural tones, offset + increment into sine 
  int inc2, off2;		//  ::   table * 65536)
};

struct Period {
  Period *nxt, *prv;		// Next/prev in chain
  int tim;			// Start time (end time is ->nxt->tim)
  Voice v0[N_CH], v1[N_CH];	// Start and end voices
  int fi, fo;			// Temporary: Fade-in, fade-out modes
};

struct NameDef {
  NameDef *nxt;
  char *name;			// Name of definition
  BlockDef *blk;		// Non-zero for block definition
  Voice vv[N_CH];		// Voice-set for it (unless a block definition)
};

struct BlockDef {
  BlockDef *nxt;		// Next in chain
  char *lin;			// StrDup'd line
};

#define ST_AMP 0x7FFFF		// Amplitude of wave in sine-table
#define NS_ADJ 12		// Noise is generated internally with amplitude ST_AMP<<NS_ADJ
#define NS_DITHER 16		// How many bits right to shift the noise for dithering
#define NS_AMP (ST_AMP<<NS_ADJ)
#define ST_SIZ 16384		// Number of elements in sine-table (power of 2)
static int *sin_table;
#define AMP_DA(pc) (40.96 * (pc))	// Display value (%age) to ->amp value
#define AMP_AD(amp) ((amp) / 40.96)	// Amplitude value to display %age
static int *waves[100];		// Pointers are either 0 or point to a sin_table[]-style array of int

static Channel chan[N_CH];	// Current channel states
static int now;			// Current time (milliseconds from midnight)
static Period *per= 0;		// Current period
static NameDef *nlist;		// Full list of name definitions

static int *tmp_buf;		// Temporary buffer for 20-bit mix values
static short *out_buf;		// Output buffer
static int out_bsiz;		// Output buffer size (bytes)
static int out_blen;		// Output buffer length (samples) (1.0* or 0.5* out_bsiz)
static int out_bps;		// Output bytes per sample (2 or 4)
static int out_buf_ms;		// Time to output a buffer-ful in ms
static int out_buf_lo;		// Time to output a buffer-ful, fine-tuning in ms/0x10000
static int out_fd;		// Output file descriptor
static int out_rate= 44100;	// Sample rate
static int out_prate= 10;	// Rate of parameter change (for file and pipe output only)
static int fade_int= 60000;	// Fade interval (ms)
static const char *in_text;	// Input sequence text
static int in_lin;		// Current input line
static char buf[4096];		// Buffer for current line
static char buf_copy[4096];	// Used to keep unmodified copy of line
static char *lin;		// Input line (uses buf[])
static char *lin_copy;		// Copy of input line
static double spin_carr_max;	// Maximum 'carrier' value for spin (really max width in us)
static char error_message[256];	// Buffer for the error message

#define NS_BIT 10
//static int ns_tbl[1<<NS_BIT];
//static int ns_off= 0;

static int fast_tim0= -1;	// First time mentioned in the sequence file (for -q and -S option)
static int fast_tim1= -1;	// Last time mentioned in the sequence file (for -E option)
				//  output rate, with the multiplier indicated
static S64 byte_count= -1;	// Number of bytes left to output, or -1 if unlimited
static int tty_erase;		// Chars to erase from current line (for ESC[K emulation)

static int mix_flag= 0;		// Has 'mix/*' been used in the sequence?

static int opt_c;		// Number of -c option points provided (max 16)
static struct AmpAdj { 
   double freq, adj;
} ampadj[16];			// List of maximum 16 (freq,adj) pairs, freq-increasing order

//
//	Time-keeping functions
//

#define H24 (86400000)			// 24 hours
#define H12 (43200000)			// 12 hours

inline int t_per24(int t0, int t1) {		// Length of period starting at t0, ending at t1.
  int td= t1 - t0;				// NB for t0==t1 this gives 24 hours, *NOT 0*
  return td > 0 ? td : td + H24;
}
inline int t_per0(int t0, int t1) {		// Length of period starting at t0, ending at t1.
  int td= t1 - t0;				// NB for t0==t1 this gives 0 hours
  return td >= 0 ? td : td + H24;
}
inline int t_mid(int t0, int t1) {		// Midpoint of period from t0 to t1
  return ((t1 < t0) ? (H24 + t0 + t1) / 2 : (t0 + t1) / 2) % H24;
}

//
//	Handle an option string, disabled in the library
//

static int 
handleOptions(char *str0) {
   if(strcmp(str0, "-SE") == 0)
      return 0;
   error("Options not supported.\n");
   return -1;
}

//
//	Setup the ampadj[] array from the given -c spec-string
//

int
setupOptC(const char *spec) {
   const char *p= spec, *q;
   int a, b;
   
   while (1) {
      while (isspace(*p) || *p == ',') p++;
      if (!*p) break;

      if (opt_c >= sizeof(ampadj) / sizeof(ampadj[0])) {
	 error("Too many -c option frequencies; maxmimum is %d", 
	       sizeof(ampadj) / sizeof(ampadj[0]));
	 return -1;
      }

      ampadj[opt_c].freq= strtod(p, (char **)&q);
      if (p == q) goto bad;
      if (*q++ != '=') goto bad;
      ampadj[opt_c].adj= strtod(q, (char **)&p);
      if (p == q) goto bad;
      opt_c++;
   }

   // Sort the list
   for (a= 0; a<opt_c; a++)
      for (b= a+1; b<opt_c; b++) 
	 if (ampadj[a].freq > ampadj[b].freq) {
	    double tmp;
	    tmp= ampadj[a].freq; ampadj[a].freq= ampadj[b].freq; ampadj[b].freq= tmp;
	    tmp= ampadj[a].adj; ampadj[a].adj= ampadj[b].adj; ampadj[b].adj= tmp;
	 }
   return 0;
      
 bad:
   error("Bad -c option spec; expecting <freq>=<amp>[,<freq>=<amp>]...:\n  %s", spec);
   return -1;
}


static int
init_sin_table(void) {
  int a;
  int *arr= (int*)Alloc(ST_SIZ * sizeof(int));
  if(arr == NULL)
      return -1;
  for (a= 0; a<ST_SIZ; a++)
    arr[a]= (int)(ST_AMP * sin((a * 3.14159265358979323846 * 2) / ST_SIZ));
  sin_table= arr;
  return 0;
}

static void 
error(char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vsnprintf(error_message, sizeof(error_message), fmt, ap);
}

static void *
Alloc(size_t len) {
  void *p= calloc(1, len);
  if (!p) error("Out of memory");
  return p;
}

static char *
StrDup(char *str) {
  char *rv= strdup(str);
  if (!rv) error("Out of memory");
  return rv;
}

//
//	Simple random number generator.  Generates a repeating
//	sequence of 65536 odd numbers in the range -65535->65535.
//
//	Based on ZX Spectrum random number generator:
//	  seed= (seed+1) * 75 % 65537 - 1
//

#define RAND_MULT 75

static int seed= 2;

//inline int qrand() {
//  return (seed= seed * 75 % 131074) - 65535;
//}

//
//	Generate next sample for simulated pink noise, with same
//	scaling as the sin_table[].  This version uses an inlined
//	random number generator, and smooths the lower frequency bands
//	as well.
//

#define NS_BANDS 9
typedef struct Noise Noise;
struct Noise {
  int val;		// Current output value
  int inc;		// Increment
};
static Noise ntbl[NS_BANDS];
static int nt_off;
static int noise_buf[256];
static uchar noise_off= 0;

static inline int 
noise2() {
  int tot;
  int off= nt_off++;
  int cnt= 1;
  Noise *ns= ntbl;
  Noise *ns1= ntbl + NS_BANDS;

  tot= ((seed= seed * RAND_MULT % 131074) - 65535) * (NS_AMP / 65535 / (NS_BANDS + 1));

  while ((cnt & off) && ns < ns1) {
    int val= ((seed= seed * RAND_MULT % 131074) - 65535) * (NS_AMP / 65535 / (NS_BANDS + 1));
    tot += ns->val += ns->inc= (val - ns->val) / (cnt += cnt);
    ns++;
  }

  while (ns < ns1) {
    tot += (ns->val += ns->inc);
    ns++;
  }

  return noise_buf[noise_off++]= (tot >> NS_ADJ);
}

//	//
//	//	Generate next sample for simulated pink noise, scaled the same
//	//	as the sin_table[].  This version uses a library random number
//	//	generator, and no smoothing.
//	//
//	
//	inline double 
//	noise() {
//	  int tot= 0;
//	  int bit= ~0;
//	  int a;
//	  int off;
//	
//	  ns_tbl[ns_off]= (rand() - (RAND_MAX / 2)) / (NS_BIT + 1);
//	  off= ns_off;
//	  for (a= 0; a<=NS_BIT; a++, bit <<= 1) {
//	    off &= bit;
//	    tot += ns_tbl[off];
//	  }
//	  ns_off= (ns_off + 1) & ((1<<NS_BIT) - 1);
//	
//	  return tot * (ST_AMP / (RAND_MAX * 0.5));
//	}

//
//	Play loop
//

static int
loop() {	
  int c, cnt;
  int now_lo= 0;			// Low-order 16 bits of 'now' (fractional)
  int err_lo= 0;
  int ms_inc;
  int r;

  if(setup_device() < 0)
      return -1;
  spin_carr_max= 127.0 / 1E-6 / out_rate;
  cnt= 1 + 1999 / out_buf_ms;	// Update every 2 seconds or so
  now= fast_tim0;
  byte_count= out_bps * (S64)(t_per0(now, fast_tim1) * 0.001 * out_rate);

  corrVal(0);		// Get into correct period
  
  while (1) {
    for (c= 0; c < cnt; c++) {
      corrVal(1);
      r = outChunk();
      if(r == 0)
	goto break2; /* all done */
      if(r < 0) {
	  free(tmp_buf);
	  free(out_buf);
	  return -1;
      }
      ms_inc= out_buf_ms;
      now_lo += out_buf_lo + err_lo;
      if (now_lo >= 0x10000) { ms_inc += now_lo >> 16; now_lo &= 0xFFFF; }
      now += ms_inc;
      if (now > H24) now -= H24;
    }
  }
break2:
  free(tmp_buf);
  free(out_buf);
  return 0;
}


//
//	Output a chunk of sound (a buffer-ful), then return
//
//	Note: Optimised for 16-bit output.  Eight-bit output is
//	slower, but then it probably won't have to run at as high a
//	sample rate.
//

static int rand0, rand1;

static int
outChunk() {
   int off= 0;

   while (off < out_blen) {
      int ns= noise2();		// Use same pink noise source for everything
      int tot1, tot2;		// Left and right channels
      int mix1, mix2;		// Incoming mix signals
      int val, a;
      Channel *ch;
      int *tab;

      mix1= tmp_buf[off];
      mix2= tmp_buf[off+1];

      // Do default mixing at 100% if no mix/* stuff is present
      if (!mix_flag) {
	 tot1= mix1 << 12;
	 tot2= mix2 << 12;
      } else {
	 tot1= tot2= 0;
      }
      
      ch= &chan[0];
      for (a= 0; a<N_CH; a++, ch++) switch (ch->typ) {
       case 0:
	  break;
       case 1:	// Binaural tones
	  ch->off1 += ch->inc1;
	  ch->off1 &= (ST_SIZ << 16) - 1;
	  tot1 += ch->amp * sin_table[ch->off1 >> 16];
	  ch->off2 += ch->inc2;
	  ch->off2 &= (ST_SIZ << 16) - 1;
	  tot2 += ch->amp2 * sin_table[ch->off2 >> 16];
	  break;
       case 2:	// Pink noise
	  val= ns * ch->amp;
	  tot1 += val;
	  tot2 += val;
	  break;
       case 3:	// Bell
	  if (ch->off2) {
	     ch->off1 += ch->inc1;
	     ch->off1 &= (ST_SIZ << 16) - 1;
	     val= ch->off2 * sin_table[ch->off1 >> 16];
	     tot1 += val; tot2 += val;
	     if (--ch->inc2 < 0) {
		ch->inc2= out_rate/20;
		ch->off2 -= 1 + ch->off2 / 12;	// Knock off 10% each 50 ms
	     }
	  }
	  break;
       case 4:	// Spinning pink noise
	  ch->off1 += ch->inc1;
	  ch->off1 &= (ST_SIZ << 16) - 1;
	  val= (ch->inc2 * sin_table[ch->off1 >> 16]) >> 24;
	  tot1 += ch->amp * noise_buf[(uchar)(noise_off+128+val)];
	  tot2 += ch->amp * noise_buf[(uchar)(noise_off+128-val)];
	  break;
       case 5:	// Mix level
	  tot1 += mix1 * ch->amp;
	  tot2 += mix2 * ch->amp;
	  break;
       default:	// Waveform-based binaural tones
	  tab= waves[-1 - ch->typ];
	  ch->off1 += ch->inc1;
	  ch->off1 &= (ST_SIZ << 16) - 1;
	  tot1 += ch->amp * tab[ch->off1 >> 16];
	  ch->off2 += ch->inc2;
	  ch->off2 &= (ST_SIZ << 16) - 1;
	  tot2 += ch->amp * tab[ch->off2 >> 16];
	  break;
      }


      // // Add pink noise as dithering
      // tot1 += (ns >> NS_DITHER) + 0x8000;
      // tot2 += (ns >> NS_DITHER) + 0x8000;
      
      // // Add white noise as dithering
      // tot1 += (seed >> 1) + 0x8000;
      // tot2 += (seed >> 1) + 0x8000;

      // White noise dither; you could also try (rand0-rand1) for a
      // dither with more high frequencies
      rand0= rand1; 
      rand1= (rand0 * 0x660D + 0xF35F) & 0xFFFF;
      if (tot1 <= 0x7FFF0000) tot1 += rand0;
      if (tot2 <= 0x7FFF0000) tot2 += rand0;

      out_buf[off++]= tot1 >> 16;
      out_buf[off++]= tot2 >> 16;
  }

  // Check and update the byte count if necessary
  if (byte_count > 0) {
    if (byte_count <= out_bsiz) {
      if(writeOut((char*)out_buf, byte_count) < 0)
	return -1;
      return 0;		// All done
    }
    else {
      if(writeOut((char*)out_buf, out_bsiz) < 0)
	return -1;
      byte_count -= out_bsiz;
    }
  }
  else {
    if(writeOut((char*)out_buf, out_bsiz) < 0)
      return -1;
  }
  return 1;
} 

//
//	Calculate amplitude adjustment factor for frequency 'freq'
//

static double 
ampAdjust(double freq) {
   int a;
   struct AmpAdj *p0, *p1;

   if (!opt_c) return 1.0;
   if (freq <= ampadj[0].freq) return ampadj[0].adj;
   if (freq >= ampadj[opt_c-1].freq) return ampadj[opt_c-1].adj;

   for (a= 1; a<opt_c; a++) 
      if (freq < ampadj[a].freq) 
	 break;
   
   p0= &ampadj[a-1];
   p1= &ampadj[a];
      
   return p0->adj + (p1->adj - p0->adj) * (freq - p0->freq) / (p1->freq - p0->freq);
}
   

//
//	Correct channel values and types according to current period,
//	and current time
//

static void 
corrVal(int running) {
   int a;
   int t0= per->tim;
   int t1= per->nxt->tim;
   Channel *ch;
   Voice *v0, *v1, *vv;
   double rat0, rat1;
   int trigger= 0;
   
   // Move to the correct period
   while ((now >= t0) ^ (now >= t1) ^ (t1 > t0)) {
      per= per->nxt;
      t0= per->tim;
      t1= per->nxt->tim;
      if (running) {
	 if (tty_erase) {
	    fprintf(stderr, "%*s\r", tty_erase, ""); 
	    tty_erase= 0;
	 }
      }
      trigger= 1;		// Trigger bells or whatever
   }
   
   // Run through to calculate voice settings for current time
   rat1= t_per0(t0, now) / (double)t_per24(t0, t1);
   rat0= 1 - rat1;
   for (a= 0; a<N_CH; a++) {
      ch= &chan[a];
      v0= &per->v0[a];
      v1= &per->v1[a];
      vv= &ch->v;
      
      if (vv->typ != v0->typ) {
	 switch (vv->typ= ch->typ= v0->typ) {
	  case 1:
	     ch->off1= ch->off2= 0; break;
	  case 2:
	     break;
	  case 3:
	     ch->off1= ch->off2= 0; break;
	  case 4:
	     ch->off1= ch->off2= 0; break;
	  case 5:
	     break;
	  default:
	     ch->off1= ch->off2= 0; break;
	 }
      }
      
      // Setup vv->*
      switch (vv->typ) {
       case 1:
	  vv->amp= rat0 * v0->amp + rat1 * v1->amp;
	  vv->carr= rat0 * v0->carr + rat1 * v1->carr;
	  vv->res= rat0 * v0->res + rat1 * v1->res;
	  break;
       case 2:
	  vv->amp= rat0 * v0->amp + rat1 * v1->amp;
	  break;
       case 3:
	  vv->amp= v0->amp;		// No need to slide, as bell only rings briefly
	  vv->carr= v0->carr;
	  break;
       case 4:
	  vv->amp= rat0 * v0->amp + rat1 * v1->amp;
	  vv->carr= rat0 * v0->carr + rat1 * v1->carr;
	  vv->res= rat0 * v0->res + rat1 * v1->res;
	  if (vv->carr > spin_carr_max) vv->carr= spin_carr_max; // Clipping sweep width
	  if (vv->carr < -spin_carr_max) vv->carr= -spin_carr_max;
	  break;
       case 5:
	  vv->amp= rat0 * v0->amp + rat1 * v1->amp;
	  break;
       default:		// Waveform based binaural
	  vv->amp= rat0 * v0->amp + rat1 * v1->amp;
	  vv->carr= rat0 * v0->carr + rat1 * v1->carr;
	  vv->res= rat0 * v0->res + rat1 * v1->res;
	  break;
      }
   }
   
   // Check and limit amplitudes if -c option in use
   if (opt_c) {
      double tot_beat= 0, tot_other= 0;
      for (a= 0; a<N_CH; a++) {
	 vv= &chan[a].v;
	 if (vv->typ == 1) {
	    double adj1= ampAdjust(vv->carr + vv->res/2);
	    double adj2= ampAdjust(vv->carr - vv->res/2);
	    if (adj2 > adj1) adj1= adj2;
	    tot_beat += vv->amp * adj1;
	 } else if (vv->typ) {
	    tot_other += vv->amp;
	 }
      }
      if (tot_beat + tot_other > 4096) {
	 double adj_beat= (tot_beat > 4096) ? 4096 / tot_beat : 1.0;
	 double adj_other= (4096 - tot_beat * adj_beat) / tot_other;
	 for (a= 0; a<N_CH; a++) {
	    vv= &chan[a].v;
	    if (vv->typ == 1)
	       vv->amp *= adj_beat;
	    else if (vv->typ) 	
	       vv->amp *= adj_other;
	 }
      }
   }
   
   // Setup Channel data from Voice data
   for (a= 0; a<N_CH; a++) {
      ch= &chan[a];
      vv= &ch->v;
      
      // Setup ch->* from vv->*
      switch (vv->typ) {
	 double freq1, freq2;
       case 1:
	  freq1= vv->carr + vv->res/2;
	  freq2= vv->carr - vv->res/2;
	  if (opt_c) {
	     ch->amp= vv->amp * ampAdjust(freq1);
	     ch->amp2= vv->amp * ampAdjust(freq2);
	  } else 
	     ch->amp= ch->amp2= (int)vv->amp;
	  ch->inc1= (int)(freq1 / out_rate * ST_SIZ * 65536);
	  ch->inc2= (int)(freq2 / out_rate * ST_SIZ * 65536);
	  break;
       case 2:
	  ch->amp= (int)vv->amp;
	  break;
       case 3:
	  ch->amp= (int)vv->amp;
	  ch->inc1= (int)(vv->carr / out_rate * ST_SIZ * 65536);
	  if (trigger) {		// Trigger the bell only on entering the period
	     ch->off2= ch->amp;
	     ch->inc2= out_rate/20;
	  }
	  break;
       case 4:
	  ch->amp= (int)vv->amp;
	  ch->inc1= (int)(vv->res / out_rate * ST_SIZ * 65536);
	  ch->inc2= (int)(vv->carr * 1E-6 * out_rate * (1<<24) / ST_AMP);
	  break;
       case 5:
	  ch->amp= (int)vv->amp;
	  break;
       default:		// Waveform based binaural
	  ch->amp= (int)vv->amp;
	  ch->inc1= (int)((vv->carr + vv->res/2) / out_rate * ST_SIZ * 65536);
	  ch->inc2= (int)((vv->carr - vv->res/2) / out_rate * ST_SIZ * 65536);
	  if (ch->inc1 > ch->inc2) 
	     ch->inc2= -ch->inc2;
	  else 
	     ch->inc1= -ch->inc1;
	  break;
      }
   }
}       
      
//
//	Setup audio device
//

static int
setup_device(void) {

  // Handle output to files and pipes
  out_fd= 1;		// stdout
  out_blen= out_rate * 2 / out_prate;		// 10 fragments a second by default
  while (out_blen & (out_blen-1)) out_blen &= out_blen-1;		// Make power of two
  out_bsiz= out_blen * 2;
  out_bps= 4;
  out_buf= (short*)Alloc(out_blen * sizeof(short));
  if(out_buf == NULL)
      return -1;
  out_buf_lo= (int)(0x10000 * 1000.0 * 0.5 * out_blen / out_rate);
  out_buf_ms= out_buf_lo >> 16;
  out_buf_lo &= 0xFFFF;
  tmp_buf= (int*)Alloc(out_blen * sizeof(int));
  if(tmp_buf == NULL)
      return -1;
  return 0;
}

//
//	Read a line, discarding blank lines and comments.  Rets:
//	Another line?  Comments starting with '##' are displayed on
//	stderr.
//   

static int 
readLine() {
   char *p;
   char *endl;
   size_t llin;
   
   while (1) {
      lin= buf;
      endl = strchr(in_text, '\n');
      llin = endl == NULL ? strlen(in_text) : endl + 1 - in_text;
      if(llin == 0)
	 return 0; /* EOF */
      if(llin > sizeof(buf) - 1)
	 llin = sizeof(buf) - 1;
      memcpy(buf, in_text, llin);
      buf[llin] = 0;
      in_text += llin;
      
      in_lin++;
      
      while (isspace(*lin)) lin++;
      p= strchr(lin, '#');
      p= p ? p : strchr(lin, 0);
      while (p > lin && isspace(p[-1])) p--;
      if (p != lin) break;
   }
   *p= 0;
   lin_copy= buf_copy;
   strcpy(lin_copy, lin);
   return 1;
}

//
//	Get next word at '*lin', moving lin onwards, or return 0
//

static char *
getWord() {
  char *rv, *end;
  while (isspace(*lin)) lin++;
  if (!*lin) return 0;

  rv= lin;
  while (*lin && !isspace(*lin)) lin++;
  end= lin;
  if (*lin) lin++;
  *end= 0;

  return rv;
}

//
//	Bad sequence file
//

static void 
badSeq(void) {
  error("Bad sequence file content at line: %d\n  %s", in_lin, lin_copy);
}

//
//	Read a list of sequence files, and generate a list of Period
//	structures
//

static int
readSeq(const char *text) {
   // Setup a 'now' value to use for NOW in the sequence file
   int start= 1;
   now= 0;
   
   in_text = text;
   in_lin= 0;
   
   while (readLine()) {
      char *p= lin;

      // Blank lines
      if (!*p) continue;
      
      // Look for options
      if (*p == '-') {
	 if (!start) {
	    error("Options are only permitted at start of sequence file:\n  %s", p);
	    return -1;
	 }
	 if(handleOptions(p) < 0)
	     return -1;
	 continue;
      }

      // Check to see if it fits the form of <name>:<white-space>
      start= 0;
      if (!isalpha(*p)) 
	 p= 0;
      else {
	 while (isalnum(*p) || *p == '_' || *p == '-') p++;
	 if (*p++ != ':' || !isspace(*p)) 
	    p= 0;
      }
      
      if (p) {
	 if(readNameDef() < 0)
	     return -1;
      } else {
	 if(readTimeLine() < 0)
	     return -1;
      }
   }
   return 0;
}


//
//	Fill in all the correct information for the Periods, assuming
//	they have just been loaded using readTimeLine()
//


static int
correctPeriods() {
  // Get times all correct
  {
    Period *pp= per;
    do {
      if (pp->fi == -2) {
	pp->tim= pp->nxt->tim;
	pp->fi= -1;
      }

      pp= pp->nxt;
    } while (pp != per);
  }

  // Make sure that the transitional periods each have enough time
  {
    Period *pp= per;
    do {
      if (pp->fi == -1) {
	int per= t_per0(pp->tim, pp->nxt->tim);
	if (per < fade_int) {
	  int adj= (fade_int - per) / 2, adj0, adj1;
	  adj0= t_per0(pp->prv->tim, pp->tim);
	  adj0= (adj < adj0) ? adj : adj0;
	  adj1= t_per0(pp->nxt->tim, pp->nxt->nxt->tim);
	  adj1= (adj < adj1) ? adj : adj1;
	  pp->tim= (pp->tim - adj0 + H24) % H24;
	  pp->nxt->tim= (pp->nxt->tim + adj1) % H24;
	}
      }

      pp= pp->nxt;
    } while (pp != per);
  }

  // Fill in all the voice arrays, and sort out details of
  // transitional periods
  {
    Period *pp= per;
    do {
      if (pp->fi < 0) {
	int fo, fi;
	int a;
	int midpt= 0;

	Period *qq= (Period*)Alloc(sizeof(*qq));
	if(qq == NULL)
	    return -1;
	qq->prv= pp; qq->nxt= pp->nxt;
	qq->prv->nxt= qq->nxt->prv= qq;

	qq->tim= t_mid(pp->tim, qq->nxt->tim);

	memcpy(pp->v0, pp->prv->v1, sizeof(pp->v0));
	memcpy(qq->v1, qq->nxt->v0, sizeof(qq->v1));

	// Special handling for bells
	for (a= 0; a<N_CH; a++) {
	  if (pp->v0[a].typ == 3 && pp->fi != -3)
	    pp->v0[a].typ= 0;

	  if (qq->v1[a].typ == 3 && pp->fi == -3)
	    qq->v1[a].typ= 0;
	}
	      
	fo= pp->prv->fo;
	fi= qq->nxt->fi;

	// Special handling for -> slides:
	//   always slide, and stretch slide if possible
	if (pp->fi == -3) {
	  fo= fi= 2;		// Force slides for ->
	  for (a= 0; a<N_CH; a++) {
	    Voice *vp= &pp->v0[a];
	    Voice *vq= &qq->v1[a];
	    if (vp->typ == 0 && vq->typ != 0 && vq->typ != 3) {
	      memcpy(vp, vq, sizeof(*vp)); vp->amp= 0;
	    }
	    else if (vp->typ != 0 && vq->typ == 0) {
	      memcpy(vq, vp, sizeof(*vq)); vq->amp= 0;
	    }
	  }
	}

	memcpy(pp->v1, pp->v0, sizeof(pp->v1));
	memcpy(qq->v0, qq->v1, sizeof(qq->v0));

	for (a= 0; a<N_CH; a++) {
	  Voice *vp= &pp->v1[a];
	  Voice *vq= &qq->v0[a];
	  if ((fo == 0 || fi == 0) ||		// Fade in/out to silence
	      (vp->typ != vq->typ) ||		// Different types
	      ((fo == 1 || fi == 1) &&		// Fade thru, but different pitches
	       (vp->typ == 1 || vp->typ < 0) && 
	       (vp->carr != vq->carr || vp->res != vq->res))
	      ) {
	    vp->amp= vq->amp= 0;		// To silence
	    midpt= 1;				// Definitely need the mid-point

	    if (vq->typ == 3) {	 		// Special handling for bells
	      vq->amp= qq->v1[a].amp; 
	      qq->nxt->v0[a].typ= qq->nxt->v1[a].typ= 0;
	    }
	  }
	  else if (vp->typ == 3) {		// Else smooth transition - for bells not so smooth
	    qq->v0[a].typ= qq->v1[a].typ= 0;
	  }
	  else {				// Else smooth transition
	    vp->amp= vq->amp= (vp->amp + vq->amp) / 2;
	    if (vp->typ == 1 || vp->typ == 4 || vp->typ < 0) {
	      vp->carr= vq->carr= (vp->carr + vq->carr) / 2;
	      vp->res= vq->res= (vp->res + vq->res) / 2;
	    }
	  }
	}

	// If we don't really need the mid-point, then get rid of it
	if (!midpt) {
	  memcpy(pp->v1, qq->v1, sizeof(pp->v1));
	  qq->prv->nxt= qq->nxt;
	  qq->nxt->prv= qq->prv;
	  free(qq);
	}
	else pp= qq;
      }

      pp= pp->nxt;
    } while (pp != per);
  }

  // Clear out zero length sections, and duplicate sections
  {
    Period *pp;
    while (per != per->nxt) {
      pp= per;
      do {
	if (voicesEq(pp->v0, pp->v1) &&
	    voicesEq(pp->v0, pp->nxt->v0) &&
	    voicesEq(pp->v0, pp->nxt->v1))
	  pp->nxt->tim= pp->tim;

	if (pp->tim == pp->nxt->tim) {
	  if (per == pp) per= per->prv;
	  pp->prv->nxt= pp->nxt;
	  pp->nxt->prv= pp->prv;
	  free(pp);
	  pp= 0;
	  break;
	}
	pp= pp->nxt;
      } while (pp != per);
      if (pp) break;
    }
  }

  // Make sure that the total is 24 hours only (not more !)
  if (per->nxt != per) {
    int tot= 0;
    Period *pp= per;
    
    do {
      tot += t_per0(pp->tim, pp->nxt->tim);
      pp= pp->nxt;
    } while (pp != per);

    if (tot > H24) {
      error("Total time is greater than 24 hours.");
      return -1;
    }
  }
  return 0;
}

static int 
voicesEq(Voice *v0, Voice *v1) {
  int a= N_CH;

  while (a-- > 0) {
    if (v0->typ != v1->typ) return 0;
    switch (v0->typ) {
     case 1:
     case 4:
     default:
       if (v0->amp != v1->amp ||
	   v0->carr != v1->carr ||
	   v0->res != v1->res)
	 return 0;
       break;
     case 2:
     case 5:
       if (v0->amp != v1->amp)
	 return 0;
       break;
     case 3:
       if (v0->amp != v1->amp ||
	   v0->carr != v1->carr)
	 return 0;
       break;
    }
    v0++; v1++;
  }
  return 1;
}

static NameDef *
free_namedef(NameDef *n)
{
    NameDef *nn;
    BlockDef *b, *bn;

    if(n == NULL)
	return NULL;
    free(n->name);
    for(b = n->blk; b != NULL; b = bn) {
	bn = b->nxt;
	free(b->lin);
	free(b);
    }
    nn = n->nxt;
    free(n);
    return nn;
}

//
//	Read a name definition
//

static int
readNameDef() {
  char *p, *q;
  NameDef *nd;
  int ch;

  if (!(p= getWord())) {
      badSeq();
      return -1;
  }

  q= strchr(p, 0) - 1;
  if (*q != ':') {
      badSeq();
      return -1;
  }
  *q= 0;
  for (q= p; *q; q++) {
    if (!isalnum(*q) && *q != '-' && *q != '_') {
      error("Bad name \"%s\" in definition, line %d:\n  %s", p, in_lin, lin_copy);
      return -1;
    }
  }

  // Waveform definition ?
  if (0 == memcmp(p, "wave", 4) && 
      isdigit(p[4]) &&
      isdigit(p[5]) &&
      !p[6]) {
     int ii= (p[4] - '0') * 10 + (p[5] - '0');
     int siz= ST_SIZ * sizeof(int);
     int *arr= (int*)Alloc(siz);
     if(arr == NULL)
	 return -1;
     double *dp0= (double*)arr;
     double *dp1= (double*)(siz + (char*)arr);
     double *dp= dp0;
     double dmax= 0, dmin= 1;
     int np;

     if (waves[ii]) {
	free(arr);
	error("Waveform %02d already defined, line %d:\n  %s",
	      ii, in_lin, lin_copy);
	return -1;
     }
     waves[ii]= arr;
     
     while ((p= getWord())) {
	double dd;
	char dmy;
	if (1 != sscanf(p, "%lf %c", &dd, &dmy)) {
	   free(arr);
	   error("Expecting floating-point numbers on this waveform "
		 "definition line, line %d:\n  %s",
		 in_lin, lin_copy);
	   return -1;
	}
	if (dp >= dp1) {
	   free(arr);
	   error("Too many samples on line (maximum %d), line %d:\n  %s",
		 dp1-dp0, in_lin, lin_copy);
	   return -1;
	}
	*dp++= dd;
	if (dmax < dmin) dmin= dmax= dd;
	else {
	   if (dd > dmax) dmax= dd;
	   if (dd < dmin) dmin= dd;
	}
     }
     dp1= dp;
     np= dp1 - dp0;
     if (np < 2) {
	free(arr);
	error("Expecting at least two samples in the waveform, line %d:\n  %s",
	      in_lin, lin_copy);
	return -1;
     }

     // Adjust to range 0-1
     for (dp= dp0; dp < dp1; dp++)
	*dp= (*dp - dmin) / (dmax - dmin);

     if(sinc_interpolate(dp0, np, arr) < 0)
	 return -1;
     
     return 0;
  } 

  // Must be block or tone-set, then, so put into a NameDef
  nd= (NameDef*)Alloc(sizeof(NameDef));
  if(nd == NULL)
      return -1;
  nd->name= StrDup(p);
  if(nd->name == NULL)
      return -1;

  // Block definition ?
  if (*lin == '{') {
    BlockDef *bd, **prvp;
    if (!(p= getWord()) || 
	0 != strcmp(p, "{") || 
	0 != (p= getWord())) {
      free_namedef(nd);
      badSeq();
      return -1;
    }

    prvp= &nd->blk;
    
    while (readLine()) {
      if (*lin == '}') {
	if (!(p= getWord()) || 
	    0 != strcmp(p, "}") || 
	    0 != (p= getWord())) {
	  free_namedef(nd);
	  badSeq();
	  return -1;
	}
	if (!nd->blk) {
	    free_namedef(nd);
	    error("Empty blocks not permitted, line %d:\n  %s", in_lin, lin_copy);
	    return -1;
	}
	nd->nxt= nlist; nlist= nd;
	return 0;
      }
      
      if (*lin != '+') {
	free_namedef(nd);
	error("All lines in the block must have relative time, line %d:\n  %s",
	      in_lin, lin_copy);
	return -1;
      }
      
      bd= (BlockDef*) Alloc(sizeof(*bd));
      if(bd == NULL)
	  return -1;
      *prvp= bd; prvp= &bd->nxt;
      bd->lin= StrDup(lin);
      if(bd->lin == NULL)
	  return -1;
    }
    
    // Hit EOF before }
    free_namedef(nd);
    error("End-of-file within block definition (missing '}')");
    return -1;
  }

  // Normal line-definition
  for (ch= 0; ch < N_CH && (p= getWord()); ch++) {
    char dmy;
    double amp, carr, res;
    int wave;

    // Interpret word into Voice nd->vv[ch]
    if (0 == strcmp(p, "-")) continue;
    if (1 == sscanf(p, "pink/%lf %c", &amp, &dmy)) {
       nd->vv[ch].typ= 2;
       nd->vv[ch].amp= AMP_DA(amp);
       continue;
    }
    if (2 == sscanf(p, "bell%lf/%lf %c", &carr, &amp, &dmy)) {
       nd->vv[ch].typ= 3;
       nd->vv[ch].carr= carr;
       nd->vv[ch].amp= AMP_DA(amp);
       continue;
    }
    if (1 == sscanf(p, "mix/%lf %c", &amp, &dmy)) {
       nd->vv[ch].typ= 5;
       nd->vv[ch].amp= AMP_DA(amp);
       mix_flag= 1;
       continue;
    }
    if (4 == sscanf(p, "wave%d:%lf%lf/%lf %c", &wave, &carr, &res, &amp, &dmy)) {
       if (wave < 0 || wave >= 100) {
	  free_namedef(nd);
	  error("Only wave00 to wave99 is permitted at line: %d\n  %s", in_lin, lin_copy);
	  return -1;
       }
       if (!waves[wave]) {
	  free_namedef(nd);
	  error("Waveform %02d has not been defined, line: %d\n  %s", wave, in_lin, lin_copy);
	  return -1;
       }
       nd->vv[ch].typ= -1-wave;
       nd->vv[ch].carr= carr;
       nd->vv[ch].res= res;
       nd->vv[ch].amp= AMP_DA(amp);	
       continue;
    }
    if (3 == sscanf(p, "%lf%lf/%lf %c", &carr, &res, &amp, &dmy)) {
      nd->vv[ch].typ= 1;
      nd->vv[ch].carr= carr;
      nd->vv[ch].res= res;
      nd->vv[ch].amp= AMP_DA(amp);	
      continue;
    }
    if (2 == sscanf(p, "%lf/%lf %c", &carr, &amp, &dmy)) {
      nd->vv[ch].typ= 1;
      nd->vv[ch].carr= carr;
      nd->vv[ch].res= 0;
      nd->vv[ch].amp= AMP_DA(amp);	
      continue;
    }
    if (3 == sscanf(p, "spin:%lf%lf/%lf %c", &carr, &res, &amp, &dmy)) {
      nd->vv[ch].typ= 4;
      nd->vv[ch].carr= carr;
      nd->vv[ch].res= res;
      nd->vv[ch].amp= AMP_DA(amp);	
      continue;
    }
    free_namedef(nd);
    badSeq();
    return -1;
  }
  nd->nxt= nlist; nlist= nd;
  return 0;
}  

//
//	Bad time
//

static void 
badTime(char *tim) {
  error("Badly constructed time \"%s\", line %d:\n  %s", tim, in_lin, lin_copy);
}

//
//	Read a time-line of either type
//

static int
readTimeLine() {
  char *p, *tim_p;
  int nn;
  int fo, fi;
  Period *pp;
  NameDef *nd;
  static int last_abs_time= -1;
  int tim, rtim = 0;

  if (!(p= getWord())) {
      badSeq();
      return -1;
  }
  tim_p= p;
  
  // Read the time represented
  tim= -1;
  if (0 == memcmp(p, "NOW", 3)) {
    last_abs_time= tim= now;
    p += 3;
  }

  while (*p) {
    if (*p == '+') {
      if (tim < 0) {
	if (last_abs_time < 0) {
	  error("Relative time without previous absolute time, line %d:\n  %s", in_lin, lin_copy);
	  return -1;
	}
	tim= last_abs_time;
      }
      p++;
    }
    else if (tim != -1) {
	badTime(tim_p);
	return -1;
    }

    if (0 == (nn= readTime(p, &rtim))) {
	badTime(tim_p);
	return -1;
    }
    p += nn;

    if (tim == -1) 
      last_abs_time= tim= rtim;
    else 
      tim= (tim + rtim) % H24;
  }

  if (fast_tim0 < 0) fast_tim0= tim;		// First time
  fast_tim1= tim;				// Last time
      
  if (!(p= getWord())) {
      badSeq();
      return -1;
  }
      
  fi= fo= 1;
  if (!isalpha(*p)) {
    switch (p[0]) {
     case '<': fi= 0; break;
     case '-': fi= 1; break;
     case '=': fi= 2; break;
     default: badSeq(); return -1;
    }
    switch (p[1]) {
     case '>': fo= 0; break;
     case '-': fo= 1; break;
     case '=': fo= 2; break;
     default: badSeq(); return -1;
    }
    if (p[2]) {
	badSeq();
	return -1;
    }

    if (!(p= getWord())) {
	badSeq();
	return -1;
    }
  }
      
  for (nd= nlist; nd && 0 != strcmp(p, nd->name); nd= nd->nxt) ;
  if (!nd) {
      error("Name \"%s\" not defined, line %d:\n  %s", p, in_lin, lin_copy);
      return -1;
  }

  // Check for block name-def
  if (nd->blk) {
    BlockDef *bd= nd->blk;
    char *prep= StrDup(tim_p);		// Put this at the start of each line
    if(prep == NULL)
	return -1;

    while (bd) {
      lin= buf; lin_copy= buf_copy;
      sprintf(lin, "%s%s", prep, bd->lin);
      strcpy(lin_copy, lin);
      if(readTimeLine() < 0) {		// This may recurse, and that's why we're StrDuping the string
	  free(prep);
	  return -1;
      }
      bd= bd->nxt;
    }
    free(prep);
    return 0;
  }
      
  // Normal name-def
  pp= (Period*)Alloc(sizeof(*pp));
  if(pp == NULL)
      return -1;
  pp->tim= tim;
  pp->fi= fi;
  pp->fo= fo;
      
  memcpy(pp->v0, nd->vv, N_CH * sizeof(Voice));
  memcpy(pp->v1, nd->vv, N_CH * sizeof(Voice));

  if (!per)
    per= pp->nxt= pp->prv= pp;
  else {
    pp->nxt= per; pp->prv= per->prv;
    pp->prv->nxt= pp->nxt->prv= pp;
  }

  // Automatically add a transitional period
  pp= (Period*)Alloc(sizeof(*pp));
  if(pp == NULL)
      return -1;
  pp->fi= -2;		// Unspecified transition
  pp->nxt= per; pp->prv= per->prv;
  pp->prv->nxt= pp->nxt->prv= pp;

  if (0 != (p= getWord())) {
    if (0 != strcmp(p, "->")) {
	badSeq();
	return -1;
    }
    pp->fi= -3;		// Special '->' transition
    pp->tim= tim;
  }
  return 0;
}

static int
readTime(char *p, int *timp) {		// Rets chars consumed, or 0 error
  int nn, hh, mm, ss;

  if (3 > sscanf(p, "%2d:%2d:%2d%n", &hh, &mm, &ss, &nn)) {
    ss= 0;
    if (2 > sscanf(p, "%2d:%2d%n", &hh, &mm, &nn)) return 0;
  }

  if (hh < 0 || hh >= 24 ||
      mm < 0 || mm >= 60 ||
      ss < 0 || ss >= 60) return 0;

  *timp= ((hh * 60 + mm) * 60 + ss) * 1000;
  return nn;
}

//
//	Takes a set of points and repeats them twice, inverting the
//	second set, and then interpolates them using a periodic sinc
//	function (see http://www-ccrma.stanford.edu/~jos/resample/)
//	and writes them to arr[] in the same format as the sin_table[].
//

static int sinc_interpolate(double *dp, int np, int *arr) {
   double *sinc;	// Temporary sinc-table
   double *out;		// Temporary output table
   int a, b;
   double dmax, dmin;
   double adj, off;

   // Generate a modified periodic sin(x)/x function to be used for
   // each of the points.  Really this should be sin(x)/x modified
   // by the sum of an endless series.  However, this doesn't
   // converge very quickly, so to save time I'm approximating this
   // series by 1-4*t*t where t ranges from 0 to 0.5 over the first
   // half of the periodic cycle.  If you do the maths, this is at
   // most 5% out.  This will have to do - it's smooth, and I don't
   // know enough maths to make this series converge quicker.
   sinc= (double *)Alloc(ST_SIZ * sizeof(double));
   if(sinc == NULL)
       return -1;
   sinc[0]= 1.0;
   for (a= ST_SIZ/2; a>0; a--) {
      double tt= a * 1.0 / ST_SIZ;
      double t2= tt*tt;
      double adj= 1 - 4 * t2;
      double xx= 2 * np * 3.14159265358979323846 * tt;
      double vv= adj * sin(xx) / xx;
      sinc[a]= vv;
      sinc[ST_SIZ-a]= vv;
   }
   
   // Build waveform into buffer
   out= (double *)Alloc(ST_SIZ * sizeof(double));
   if(out == NULL) {
       free(sinc);
       return -1;
   }
   for (b= 0; b<np; b++) {
      int off= b * ST_SIZ / np / 2;
      double val= dp[b];
      for (a= 0; a<ST_SIZ; a++) {
	 out[(a + off)&(ST_SIZ-1)] += sinc[a] * val;
	 out[(a + off + ST_SIZ/2)&(ST_SIZ-1)] -= sinc[a] * val;
      }
   }

   // Look for maximum for normalization
   dmax= dmin= 0;
   for (a= 0; a<ST_SIZ; a++) {
      if (out[a] > dmax) dmax= out[a];
      if (out[a] < dmin) dmin= out[a];
   }

   // Write out to output buffer
   off= -0.5 * (dmax + dmin);
   adj= ST_AMP / ((dmax - dmin) / 2);
   for (a= 0; a<ST_SIZ; a++)
      arr[a]= (int)((out[a] + off) * adj);

   free(sinc);
   free(out);
   return 0;
}

// END //

/* NG: Conversion to a library: entry points */

int
sbagen_init(void)
{
    if(sin_table == NULL)
	if(init_sin_table() < 0)
	    return -1;
    return 0;
}

/*
 * rate: sample rate; default: 44100; option -r
 * prate: frequency recalculation, default: 10, option -R
 * fade: fade time (ms), default: 60000, option -F
 * roll: headphone roll-off compensation, option -c
 */
int
sbagen_set_parameters(int rate, int prate, int fade, const char *roll)
{
    if(rate != 0)
	out_rate = rate;
    if(prate != 0)
	out_prate = prate;
    if(fade != 0)
	fade_int = fade;
    if(roll != NULL)
	if(setupOptC(roll) < 0)
	    return -1;
    return 0;
}

void
sbagen_exit(void)
{
    free(sin_table);
    sin_table = NULL;
}

int
sbagen_parse_seq(const char *seq)
{
    int r = 0;

    if(readSeq(seq) < 0) {
	r = -1;
    } else {
	if(correctPeriods() < 0)
	    r = -1;
    }
    while(nlist != NULL)
	nlist = free_namedef(nlist);
    return r;
}

void
sbagen_free_seq(void)
{
    Period *pn;
    unsigned i;

    if(per != NULL) {
	if(per->prv != NULL)
	    per->prv->nxt = NULL;
	for(; per != NULL; per = pn) {
	    pn = per->nxt;
	    free(per);
	}
    }
    for(i = 0; i < sizeof(waves) / sizeof(*waves); i++)
	free(waves[i]);
}

int
sbagen_run(void)
{
   return(loop());
}

char *
sbagen_get_error(void)
{
    error_message[sizeof(error_message) - 1] = 0;
    return error_message;
}

#ifdef BUILD_JNI

#include <assert.h>
#include <jni.h>
#include "tmp/sbagen.h"

static void
die(JNIEnv *env, char c)
{
    jclass e;
    char *cl;

    if((*env)->ExceptionOccurred(env))
	return;
    cl =
	c == 'M' ? "java/lang/OutOfMemoryError" :
	c == 'A' ? "java/lang/IllegalArgumentException" :
	NULL;
    e = (*env)->FindClass(env, cl);
    (*env)->ThrowNew(env, e, sbagen_get_error());
}

void
Java_org_cigaes_binaural_1player_Binaural_1decoder_sbagen_1init(
    JNIEnv *env, jobject self)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    if(sbagen_init() < 0) {
	die(env, 'M');
	return;
    }
}

void
Java_org_cigaes_binaural_1player_Binaural_1decoder_sbagen_1set_1parameters(
    JNIEnv *env, jobject self,
    jint rate, jint prate, jint fade, jstring jroll)
{
    const char *roll;
    int r;

    if(jroll == NULL) {
	roll = NULL;
    } else {
	if((roll = (*env)->GetStringUTFChars(env, jroll, NULL)) == NULL)
	    return;
    }
    r = sbagen_set_parameters(rate, prate, fade, roll);
    if(roll != NULL)
	(*env)->ReleaseStringUTFChars(env, jroll, roll);
    if(r < 0)
	die(env, 'A');
}

void
Java_org_cigaes_binaural_1player_Binaural_1decoder_sbagen_1exit(
    JNIEnv *env, jobject self)
{
    sbagen_exit();
}

void
Java_org_cigaes_binaural_1player_Binaural_1decoder_sbagen_1parse_1seq(
    JNIEnv *env, jobject self, jstring jseq)
{
    const char *seq;
    int r;

    if((seq = (*env)->GetStringUTFChars(env, jseq, NULL)) == NULL)
	return;
    r = sbagen_parse_seq(seq);
    (*env)->ReleaseStringUTFChars(env, jseq, seq);
    if(r < 0)
	die(env, 'A');
}

void
Java_org_cigaes_binaural_1player_Binaural_1decoder_sbagen_1free_1seq(
    JNIEnv *env, jobject self)
{
    sbagen_free_seq();
}

static JNIEnv *output_env;
static jobject output_self;
static jmethodID output_method;

static int
writeOut(char *buf, int siz)
{
    jshortArray *a;
    int r = 0;

    siz /= 2;
    if((a = (*output_env)->NewShortArray(output_env, siz)) == NULL)
	return(-1);
    (*output_env)->SetShortArrayRegion(output_env, a, 0, siz, (jshort *)buf);
    (*output_env)->CallVoidMethod(output_env, output_self, output_method, a);
    if((*output_env)->ExceptionOccurred(output_env))
	r = -1;
    (*output_env)->DeleteLocalRef(output_env, a);
    return(r);
}

void
Java_org_cigaes_binaural_1player_Binaural_1decoder_sbagen_1run(
    JNIEnv *env, jobject self)
{
    jclass class;

    output_env = env;
    output_self = self;
    class = (*env)->GetObjectClass(env, self);
    output_method = (*env)->GetMethodID(env, class, "out", "([S)V");
    assert(output_method != NULL);
    if(sbagen_run() < 0)
	die(env, 'A');
}

#elif BUILD_STANDALONE_TEST

static int
writeOut(char *buf, int siz) {
    int rv;

    while (-1 != (rv= write(out_fd, buf, siz))) {
	if (0 == (siz -= rv)) return 0;
	buf += rv;
    }
    error("Output error");
    return(-1);
}

int 
main(int argc, char **argv)
{
    int i, l;
    FILE *f;
    char *buf;

    if(sbagen_init() < 0) {
	fprintf(stderr, "Error: %s\n", sbagen_get_error());
	exit(1);
    }
    if(sbagen_set_parameters(0, 0, 0, NULL) < 0) {
	fprintf(stderr, "Error: %s\n", sbagen_get_error());
	exit(1);
    }
    for(i = 1; i < argc; i++) {
	if((f = fopen(argv[i], "r")) == NULL) {
	    perror(argv[i]);
	    exit(1);
	}
	fseek(f, 0, SEEK_END);
	l = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(l + 1);
	l = fread(buf, 1, l, f);
	buf[l] = 0;
	fclose(f);
	if(sbagen_parse_seq(buf) < 0) {
	    sbagen_free_seq();
	    sbagen_exit();
	    free(buf);
	    fprintf(stderr, "Error: %s\n", sbagen_get_error());
	    exit(1);
	}
	free(buf);
    }
    if(sbagen_run() < 0) {
	sbagen_free_seq();
	sbagen_exit();
	fprintf(stderr, "Error: %s\n", sbagen_get_error());
	exit(1);
    }
    sbagen_free_seq();
    sbagen_exit();

    return 0;
}

#else

# error Please define the build mode.

#endif
