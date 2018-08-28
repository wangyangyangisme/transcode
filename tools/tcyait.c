/*
 *  tcyait.c
 *
 *  Copyright (C) Allan Snider - February 2007
 *
 *  This file is part of transcode, a video stream processing tool
 *
 *  transcode is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  transcode is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 *	tcyait:
 *		Yet Another Inverse Telecine filter.
 *
 *	Usage:
 *		tcyait [-dnlotbwmEO] [arg...]
 *			-d		Print debug information to stdout
 *			-n		Do not drop frames, always de-interlace
 *			-k		No forced keep frames
 *			-l log		Input yait log file name
 *			-o ops		Output yait frame ops file name
 *			-t thresh	Interlace detection threshold (>1)
 *			-b blend	forced blend threshold (>thresh)
 *			-w size		Drop frame look ahead window (0-20)
 *			-m mode		Transcode blend method (0-5)
 *			-E thresh	Even pattern threhold [thresh]
 *			-O thresh	Odd pattern threhold [thresh]
 *			-N noise	minimum normalized delta, else considered noise
 *
 *		By default, reads "yait.log" and produces "yait.ops".
 *
 *	Description:
 *
 *		Read a yait log file (generated via -J yait=log), and analyze it to
 *	produce a yait frame operations file.  The frame operations file contains
 *	commands to the yait filter to drop, copy or save rows (to de-interlace),
 *	or blend frames.  This will convert from NTSC 29.97 to 23.976 fps.  The file
 *	generated is used as input for another transcode pass (-J yait=ops).
 */


#define	YAIT_VERSION		"v0.2"

#define	TRUE			1
#define	FALSE			0

/* program defaults */
#define	Y_LOG_FN		"yait.log"	/* log file read */
#define	Y_OPS_FN		"yait.ops"	/* frame operation file written */
#define	Y_THRESH		1.2		/* even/odd ratio to detect interlace */
#define	Y_DROPWIN_SIZE		15		/* drop frame look ahead window */
#define	Y_DEINT_MODE		3		/* default transcode de-interlace mode */

/* limits */
#define	Y_DEINT_MIN		0
#define	Y_DEINT_MAX		5
#define	Y_DROPWIN_MIN		0
#define	Y_DROPWIN_MAX		60

#define	Y_MTHRESH		1.02		/* minimum ratio allowing de-interlace */
#define	Y_NOISE			0.003		/* normalized delta too weak, noise */
#define	Y_SCENE_CHANGE		0.15		/* normalized delta > 15% of max delta */

/* force de-interlace */
#define	Y_FBLEND		1.6		/* if ratio is > this */
#define	Y_FWEIGHT		0.01		/* if over this weight */

/* frame operation flags */
#define	Y_OP_ODD		0x10
#define	Y_OP_EVEN		0x20
#define	Y_OP_PAT		0x30

#define	Y_OP_NOP		0x0
#define	Y_OP_SAVE		0x1
#define	Y_OP_COPY		0x2
#define	Y_OP_DROP		0x4
#define	Y_OP_DEINT		0x8

/* group flags */
#define	Y_HAS_DROP		1
#define	Y_BANK_DROP		2
#define	Y_WITHDRAW_DROP		3
#define	Y_BORROW_DROP		4
#define	Y_RETURN_DROP		5
#define	Y_FORCE_DEINT		6
#define	Y_FORCE_DROP		7
#define	Y_FORCE_KEEP		8

/* frame information */
typedef struct fi_t Fi;
struct fi_t
	{
	double	r;		/* even/odd delta ratio, filtered */
	double	ro;		/* ratio, original value */
	double	w;		/* statistical strength */
	double	nd;		/* normalized delta */
	int	fn;		/* frame number */
	int	ed;		/* even row delta */
	int	od;		/* odd row delta */
	int	gi;		/* group array index */
	int	ip;		/* telecine pattern */
	int	op;		/* frame operation, nop, save/copy row */
	int	drop;		/* frame is to be dropped */
	int	gf;		/* group flag */
	Fi	*next;
	};


/*
 *	globals:
 */

char *Prog;			/* argv[0] */
char *LogFn;			/* log file name, default "yait.log" */
char *OpsFn;			/* ops file name, default "yait.ops" */
double Thresh;			/* row delta ratio interlace detection threshold */
double EThresh;			/* even interlace detection threshold */
double OThresh;			/* odd interlace detection threshold */
double Blend;			/* force frame blending over this threshold */
double Noise;			/* minimum normalized delta */
int DropWin;			/* drop frame look ahead window */
int DeintMode;			/* transcode de-interlace mode, (1-5) */
int DebugFi;			/* dump debug frame info */
int NoDrops;			/* force de-interlace everywhere (non vfr) */
int NoKeeps;			/* Don't force keep frames (allows a/v sync drift) */

FILE *LogFp;			/* log file */
FILE *OpsFp;			/* ops file */

Fi *Fl;				/* frame list */
Fi **Fa;			/* frame array */
Fi **Ga;			/* group array */
int *Da;			/* drop count array */
int Nf;				/* number of frames */
int Ng;				/* number of elements in Ga */
int Nd;				/* number of elements in Da */
int Md;				/* max delta */

int Stat_nd;			/* total number of dropped frames */
int Stat_nb;			/* total number of blended frames */
int Stat_no;			/* total number of odd interlaced pairs */
int Stat_ne;			/* total number of even interlaced pairs */
int Stat_fd;			/* total number of forced drops */
int Stat_fk;			/* total number of forced keeps */


/*
 *	protos:
 */

static void yait_parse_args( int, char** );
static void yait_chkac( int* );
static void yait_usage( void );
static void yait_read_log( void );
static double yait_calc_ratio( int, int );
static void yait_find_ip( void );
static void yait_chk_ip( int );
static void yait_chk_pairs( int );
static void yait_chk_tuplets( int );
static int yait_find_odd( double, int, double*, int );
static int yait_find_even( double, int, double*, int );
static int yait_ffmin( int, int );
static int yait_ffmax( int, int );
static int yait_m5( int );
static void yait_mark_grp( int, int, double );
static void yait_find_drops( void );
static int yait_cnt_drops( int );
static int yait_extra_drop( int );
static int yait_missing_drop( int );
static void yait_keep_frame( int );
static int yait_get_hdrop( int, int* );
static void yait_ivtc_keep( int );
static void yait_ivtc_grps( void );
static int yait_scan_bk( int );
static int yait_scan_fw( int );
static void yait_drop_frame( int );
static int yait_ivtc_grp( int, int, int );
static double yait_tst_ip( int, int );
static void yait_deint( void );
static void yait_write_ops( void );
static char *yait_write_op( Fi* );
static void yait_fini( void );

static void yait_debug_fi( void );
static char *yait_op( int op );
static char *yait_drop( Fi *f );
static char *yait_grp( int flg );


/*
 *	main:
 */

int
main( int argc, char **argv )
	{
	/* parse args */
	yait_parse_args( argc, argv );

	LogFp = fopen( LogFn, "r" );
	if( !LogFp )
		{
		perror( "fopen" );
		fprintf( stderr, "Cannot open YAIT delta log file (%s)\n", LogFn );
		exit( 1 );
		}

	OpsFp = fopen( OpsFn, "w" );
	if( !OpsFp )
		{
		perror( "fopen" );
		fprintf( stderr, "Cannot create YAIT frame ops file (%s)\n", OpsFn );
		exit( 1 );
		}

	/* read the log */
	yait_read_log();

	/* find interleave patterns */
	yait_find_ip();

	/* find drop frames */
	yait_find_drops();

	/* complete groups missing an interleave pattern */
	yait_ivtc_grps();

	/* let transcode de-interlace frames we missed */
	yait_deint();

	/* print frame ops file */
	yait_write_ops();

	if( DebugFi )
		yait_debug_fi();

	/* graceful exit */
	yait_fini();

	return( 0 );
	}


/*
 *	yait_parse_args:
 */

static void
yait_parse_args( int argc, char **argv )
	{
	int opt;
	char *p;

	LogFn = Y_LOG_FN;
	OpsFn = Y_OPS_FN;
	Thresh = Y_THRESH;
	EThresh = 0;
	OThresh = 0;
	Blend = Y_FBLEND;
	Noise = Y_NOISE;
	DeintMode = Y_DEINT_MODE;
	DropWin = Y_DROPWIN_SIZE;

	--argc;
	Prog = *argv++;
	while( (p = *argv) )
		{
		if( *p++ != '-' )
			break;
		while( (opt = *p++) )
			switch( opt )
				{
				case 'd':
					DebugFi = TRUE;
					break;

				case 'n':
					NoDrops = TRUE;
					break;

				case 'k':
					NoKeeps = TRUE;
					break;

				case 'l':
					yait_chkac( &argc );
					LogFn = *++argv;
					break;

				case 'o':
					yait_chkac( &argc );
					OpsFn = *++argv;
					break;

				case 't':
					yait_chkac( &argc );
					Thresh = atof( *++argv );
					break;

				case 'E':
					yait_chkac( &argc );
					EThresh = atof( *++argv );
					break;

				case 'O':
					yait_chkac( &argc );
					OThresh = atof( *++argv );
					break;

				case 'b':
					yait_chkac( &argc );
					Blend = atof( *++argv );
					break;

				case 'N':
					yait_chkac( &argc );
					Noise = atof( *++argv );
					break;

				case 'w':
					yait_chkac( &argc );
					DropWin = atoi( *++argv );
					break;

				case 'm':
					yait_chkac( &argc );
					DeintMode = atoi( *++argv );
					break;

				default:
					yait_usage();
					break;
				}
		--argc;
		argv++;
		}

	if( Thresh <= 1 )
		{
		printf( "Invalid threshold specified (%g).\n\n", Thresh );
		yait_usage();
		}

	if( Blend <= Thresh )
		{
		printf( "Invalid blend threshold specified (%g).\n\n", Blend );
		yait_usage();
		}

	if( DropWin<Y_DROPWIN_MIN || DropWin>Y_DROPWIN_MAX )
		{
		printf( "Invalid drop window size specified (%d).\n\n", DropWin );
		yait_usage();
		}

	if( DeintMode<Y_DEINT_MIN || DeintMode>Y_DEINT_MAX )
		{
		printf( "Invalid de-interlace method specified (%d).\n\n", DeintMode );
		yait_usage();
		}

	if( !EThresh )
		EThresh = Thresh;
	if( !OThresh )
		OThresh = Thresh;

	if( argc )
		yait_usage();
	}


/*
 *	yait_chkac:
 */

static void
yait_chkac( int *ac )
	{
	if( *ac < 1 )
		yait_usage();
	--*ac;
	}


/*
 *	yait_usage:
 */

static void
yait_usage( void )
	{
	printf( "Usage: %s [-dnklotbwmEON] [arg...] \n", Prog );
	printf( "\t-d\t\tPrint debug information to stdout.\n" );
	printf( "\t-n\t\tDo not drop frames, always de-interlace.\n" );
	printf( "\t-k\t\tNo forced keep frames.\n" );
	printf( "\t-l log\t\tInput yait log file name [%s].\n", Y_LOG_FN );
	printf( "\t-o ops\t\tOutput yait frame ops file name [%s].\n", Y_OPS_FN );
	printf( "\t-t thresh\tInterlace detection threshold (>1) [%g].\n", Y_THRESH );
	printf( "\t-b blend\tforced blend threshold (>thresh) [%g].\n", Y_FBLEND );
	printf( "\t-w size\t\tDrop frame look ahead window (0-20) [%d].\n", Y_DROPWIN_SIZE );
	printf( "\t-m mode\t\tTranscode blend method (0-5) [%d].\n", Y_DEINT_MODE );
	printf( "\t-E thresh\tEven pattern threshold [%g].\n", Y_THRESH );
	printf( "\t-O thresh\tOdd pattern threshold [%g].\n", Y_THRESH );
	printf( "\t-N noise\tMinimum normalized delta, else noise [%g].\n", Y_NOISE );
	printf( "\n" );

	exit( 1 );
	}


/*
 *	yait_read_log:
 */

static void
yait_read_log( void )
	{
	Fi **fa, *pf, *f;
	int fn, ed, od;
	int s, n;

	s = 0;
	pf = NULL;
	for( Nf=0; ; Nf++ )
		{
		n = fscanf( LogFp, "%d: e: %d, o: %d\n", &fn, &ed, &od );
		if( n != 3 )
			break;

		/* starting frame number */
		if( !Nf )
			s = fn;

		if( (fn-s) != Nf )
			{
			printf( "Broken log file, line %d\n", Nf );
			exit( 1 );
			}

		f = (Fi*) malloc( sizeof(Fi) );
		if( !f )
			{
			perror( "malloc" );
			exit( 1 );
			}

		memset( (void*) f, 0, sizeof(Fi) );
		if( !Fl )
			Fl = f;
		if( pf )
			pf->next = f;
		pf = f;

		f->r = yait_calc_ratio( ed, od );
		f->ro = f->r;
		f->fn = fn;
		f->ed = ed;
		f->od = od;
		f->ip = -1;
		}

	if( !Fl )
		{
		fprintf( stderr, "Invalid log file.\n" );
		exit( 1 );
		}

	/* number of 5 frame groups */
	Nd = Nf / 5;

	Fa = (Fi**) malloc( (Nf+1) * sizeof(Fi*) );
	Ga = (Fi**) malloc( (Nf+1) * sizeof(Fi*) );
	Da = (int*) malloc( Nd * sizeof(int) );
	if( !Fa || !Ga || !Da )
		{
		perror( "malloc" );
		exit( 1 );
		}

	fa = Fa;
	for( f=Fl; f; f=f->next )
		*fa++ = f;
	*fa = NULL;
	}


/*
 *	yait_calc_ratio:
 *		Compute a ratio between even/odd row deltas.  A high ratio indicates an
 *	interlace present.  Use the sign of the ratio to indicate even row (<0), or odd
 *	row (>0) correlation.
 *
 *		If the magnitude of the ratio is > 1.1, this is usually enough to
 *	indicate interlacing.  A value around 1.0 indicates no row correlation at
 *	all.
 *
 * 		Assigning the ratios in this manner result in the following patterns
 * 	present for interlaced material.  Assume 0 for fabs(r)<thresh, else +/- 1:
 *
 * 	An odd interlace pattern (for a five frame group) would appear as:
 *
 *			frame:  1	2	3	4	5
 *			even:	a	a	b	c	d
 *			odd:	a	b	c	c	d
 *
 *			ratio:	0	-1	0	1	0
 *
 * 	If we detect this pattern, we assign the following frame operations:
 *
 *			frame:  1	2	3	4	5
 *			even:	a	a	b	c	d
 *			odd:	a	b	c	c	d
 *
 *			ratio:	0	-1	0	1	0
 *			op:		osd	oc
 *
 * 		osd = save odd rows and drop the frame
 * 		oc  = copy in saved odd rows
 *
 * 	This results with:
 *
 *			frame:  1	|2|	3	4	5
 *			even:	a	|a|	b	c	d
 *			odd:	a	|b|-->	b	c	d
 *                                     drop
 *
 *	For even interlace patterns, the signs are reversed, or simply:
 *
 *			ratio:	0	1	0	-1	0
 *					esd	ec
 *
 *	The entire approach of this tool depends on these specific ratio patterns
 *	to be present, and should be for 2:3 pulldown.  Lots of complications arise
 *	around still and abrupt scene changes.  Again, it might be useful for the
 *	filter to produce a combing co-efficient as well as the delta information.
 *
 *	Side note:
 *		For yuv, deltas based only on luminance yeilded much stronger
 *		interlace patterns, however, I suppose there are (rare) cases where
 *		chroma could be the only indicator, so chroma is included in the
 *		delta calculation, even though it results in weaker ratios.
 */

static double
yait_calc_ratio( int ed, int od )
	{
	double r;

	r = 1;

	/* compute ratio, >1 odd, <-1 even */
	if( !ed && !od )
		/* duplicate frame */
		r = 0;

	if( ed && !od )
		r = 100;

	if( !ed && od )
		r = -100;

	if( ed && od )
		{
		r = (double) ed / (double) od;

		if( r < 1 )
			r = -1.0 / r;

		if( r > 100 )
			r = 100;
		if( r < -100 )
			r = -100;
		}

	return( r );
	}


/*
 *	yait_find_ip:
 *		- Mark isolated duplicate frames to be hard dropped.
 *		- Create the group array which is used to processes interleave
 *		  patterns without duplicate frames present.
 *		- Find the maximum frame delta value.  This is used to normalize
 *		  frame deltas to filter out weak frames (noise which may cause
 *		  erroneous interleave patterns to be detected).
 *		- Detect local interleave patterns.
 */

static void
yait_find_ip( void )
	{
	Fi *f;
	double w;
	int m, p, i;

	/* mark obvious drop frames */
	if( !NoDrops )
		for( i=1; i<Nf-1; i++ )
			{
			f = Fa[i];
			if( f->r )
				continue;

			if( !Fa[i-1]->r && !Fa[i+1]->r )
				continue;

			f->drop = TRUE;
			}

	/* create group array, ommiting drops */
	Ng = 0;
	for( i=0; i<Nf; i++ )
		{
		f = Fa[i];
		if( f->drop )
			continue;

		f->gi = Ng;
		Ga[Ng++] = f;
		}
	Ga[Ng] = NULL;

	/* find max row delta */
	m = 0;
	for( i=0; i<Nf; i++ )
		{
		f = Fa[i];
		if( f->ed > m )
			m = f->ed;
		if( f->od > m )
			m = f->od;
		}

	Md = m;
	if( !Md )
		{
		fprintf( stderr, "All empty frames?\n" );
		exit( 1 );
		}

	/* compute normalized row deltas and */
	/* filter out weak r values (noise) */
	for( i=0; i<Ng; i++ )
		{
		f = Ga[i];
		f->nd = (f->ed + f->od) / (double) Md;
		if( f->nd < Noise )
			f->r = 1;
		}

	/* adjust for incomplete interleave patterns */
	/* (indexing Fa[0,..,i+6]) */
	for( i=0; i<Ng-6; i++ )
		yait_chk_ip( i );

	/* find interleave patterns */
	for( i=0; i<Ng; i++ )
		{
		f = Ga[i];
		if( f->op & Y_OP_COPY )
			{
			/* finish this group before looking for another pattern */
			i++;
			continue;
			}

		p = yait_find_odd( OThresh, i, &w, 4 );
		if( p != -1 )
			{
			yait_mark_grp( p, i, w );
			continue;
			}

		p = yait_find_even( EThresh, i, &w, 4 );
		if( p != -1 )
			yait_mark_grp( p+10, i, w );
		}
	}


/*
 *	yait_chk_ip:
 *		Two cases to look for.  An isolated pair of high r's, and an
 *	isolated tuplet of high r's.  These can be caused by interlacing over
 *	still and abrupt scene changes.
 */

static void
yait_chk_ip( int n )
	{
	if( !NoDrops )
		yait_chk_pairs( n );

	yait_chk_tuplets( n );
	}


/*
 *	yait_chk_pairs:
 *		Look for patterns of the type:
 *			i:      0  1  2  3  4  5
 *			odd:	0  0 -1  1  0  0
 *			even:	0  0  1 -1  0  0
 *
 *	If detected, force the drop of the (single) interlaced frame.
 *	De-interlacing would just incur a redundant copy operation.
 */

static void
yait_chk_pairs( int n )
	{
	Fi *fa[6];
	double ra[6];
	int i;

	for( i=0; i<6; i++ )
		{
		fa[i] = Ga[n+i];
		ra[i] = fabs( fa[i]->r );
		}

	for( i=2; i<4; i++ )
		if( ra[i] < Thresh )
			return;

	/* adjacent frames to the tuplet must be <thresh */
	if( ra[1]>Thresh || ra[4]>Thresh )
		return;

	/* we only need one edge frame to be <thresh */
	if( ra[0]>Thresh && ra[5]>Thresh )
		return;

	if( fa[2]->r>0 && fa[3]->r>0 )
		return;

	if( fa[2]->r<0 && fa[3]->r<0 )
		return;

	/* two isolated high r values of opposite sign */
	/* drop the interlaced frame, erase the pattern */
	fa[2]->r = 1;
	fa[3]->r = 1;

	fa[2]->drop = TRUE;
	}


/*
 *	yait_chk_tuplets:
 *		Look for patterns of the type:
 *			i:      0  1  2   3    4  5  6
 *			odd:	0  0 -1  +/-2  1  0  0
 *			even:	0  0  1  +/-2 -1  0  0
 *
 *	and complete to:
 *
 *			odd:	0  0 -1   0    1  0  0
 *			even:	0  0  1   0   -1  0  0
 */

static void
yait_chk_tuplets( int n )
	{
	Fi *fa[7];
	double ra[7];
	int i;

	for( i=0; i<7; i++ )
		{
		fa[i] = Ga[n+i];
		ra[i] = fabs( fa[i]->r );
		}

	for( i=2; i<5; i++ )
		if( ra[i] < Thresh )
			return;

	/* adjacent frames to the tuplet must be <thresh */
	if( ra[1]>Thresh || ra[5]>Thresh )
		return;

	/* we only need one edge frame to be <thresh */
	if( ra[0]>Thresh && ra[6]>Thresh )
		return;

	if( fa[2]->r>0 && fa[4]->r>0 )
		return;

	if( fa[2]->r<0 && fa[4]->r<0 )
		return;

	/* isolated tuplet of high r values of opposite sign */
	if( ra[3]>ra[2] || ra[3]>ra[4] )
		fa[3]->r = 1;
	}


/*
 *	yait_find_odd:
 */

static int
yait_find_odd( double thresh, int n, double *w, int win )
	{
	double re, ro;
	int me, mo;
	int p;

	/* find max even/odd correlations */
	/* (r<0 - even, r>0 - odd) */
	me = yait_ffmin( n, win );
	mo = yait_ffmax( n, win );

	p = -1;
	if( yait_m5(mo-2) == yait_m5(me) )
		{
		re = fabs( Ga[me]->r );
		ro = fabs( Ga[mo]->r );
		if( re>thresh && ro>thresh )
			{
			p = yait_m5( mo - 4 );
			if( w )
				*w = re + ro;
			}
		}

	return( p );
	}


/*
 *	yait_find_even:
 */

static int
yait_find_even( double thresh, int n, double *w, int win )
	{
	double re, ro;
	int me, mo;
	int p;

	me = yait_ffmin( n, win );
	mo = yait_ffmax( n, win );

	p = -1;
	if( yait_m5(me-2) == yait_m5(mo) )
		{
		re = fabs( Ga[me]->r );
		ro = fabs( Ga[mo]->r );
		if( re>thresh && ro>thresh )
			{
			p = yait_m5( me - 4 );
			if( w )
				*w = re + ro;
			}
		}

	return( p );
	}


/*
 *	yait_ffmin:
 */

static int
yait_ffmin( int n, int w )
	{
	Fi *f;
	int m, i;
	double r;

	r = 0;
	m = 0;
	for( i=n; i<n+w; i++ )
		{
		if( i < 0 )
			break;

		f = Ga[i];
		if( !f )
			break;

		if( f->r < r )
			{
			r = f->r;
			m = i;
			}
		}

	return( m );
	}


/*
 *	yait_ffmax:
 */

static int
yait_ffmax( int n, int w )
	{
	Fi *f;
	int m, i;
	double r;

	r = 0;
	m = 0;
	for( i=n; i<n+w; i++ )
		{
		if( i < 0 )
			break;

		f = Ga[i];
		if( !f )
			break;

		if( f->r > r )
			{
			r = f->r;
			m = i;
			}
		}

	return( m );
	}


/*
 *	yait_m5:
 */

static int
yait_m5( int n )
	{
	while( n < 0 )
		n += 5;
	return( n % 5 );
	}


/*
 *	yait_mark_grp:
 *		Try to catch the situation where a progressive frame is missing
 *	between interlace groups.  This will cause an erroneous (opposite) ip
 *	pattern to be detected.  The first sequence shown below is a normal (odd)
 *	telecine pattern.  The second shows what happens when a progressive frame
 *	is missing.  We want to reject the even pattern detected.  Therefore, if
 *	we find an identical pattern at n+4 we keep it.  If not, we reject if an
 *	opposite pattern follows at n+2 of greater weight.
 *
 *		n:  0   1   2   3   4   0   1   2   3   4
 *		r:  0  -1   0   1   0   0  -1   0   1   0
 *                     odd                 odd
 *
 *		n:  0   1   2   3   4   1   2   3   4
 *		r:  0  -1   0   1   0  -1   0   1   0
 *                     odd     even    odd
 */

static void
yait_mark_grp( int p, int n, double w )
	{
	Fi *f;
	double nw;
	int np, t, i;

	if( n%5 != (p+2)%5 )
		return;

	/* only overwrite an existing pattern if weight is greater */
	f = Ga[n];
	if( w <= f->w )
		return;

	/* check for same pattern at n+4 */
	if( p < 10 )
		np = yait_find_odd( OThresh, n+4, NULL, 5 );
	else
		np = yait_find_even( EThresh, n+4, NULL, 5 );
	if( np < 0 )
		{
		/* no pattern at n+4, reject if opposite ip at n+2 */
		if( p < 10 )
			np = yait_find_even( EThresh, n+2, &nw, 5 );
		else
			np = yait_find_odd( OThresh, n+2, &nw, 5 );

		if( np>=0 && nw>w )
			return;
		}

	/* erase previous pattern */
	if( n > 1 )
		{
		Ga[n-1]->op = 0;
		Ga[n-2]->op = 0;
		}

	/* this frame and next are interlaced */
	t = (p < 10) ? Y_OP_ODD : Y_OP_EVEN;
	f->op = t | Y_OP_SAVE | Y_OP_DROP;
	f = Ga[n+1];
	f->op = t | Y_OP_COPY;

	/* assume 1 progressive on either side of the tuplet */
	for( i=n-1; i<n+4; i++ )
		{
		if( i<0 || i>=Ng )
			continue;

		f = Ga[i];
		f->ip = p;
		f->w = w;
		}
	}


/*
 *	yait_find_drops:
 *		For every group of 5 frames, make sure we drop a frame.  Allow up to a
 *	DropWin (default 15) group lookahead to make up for extra or missing drops.  (The
 *	duplicated frames generated by --hard_fps can be quite early or late in the sequence).
 *	If a group requires a drop, but none exists, mark the group as requiring de-interlacing.
 *	Finally, consequetive marked groups inherit surrounding interleave patterns.
 *
 *	Each group will receive one of the following flags:
 *
 * 		Y_HAS_DROP		- group has a single drop frame
 * 		Y_BANK_DROP		- extra drop, can be used forward
 * 		Y_WITHDRAW_DROP		- missing drop, use banked drop from behind
 * 		Y_RETURN_DROP		- extra drop, can be used behind
 * 		Y_BORROW_DROP		- missing drop, use future extra drop
 * 		Y_FORCE_DEINT		- force de-interlacing, (produces a drop)
 * 		Y_FORCE_DROP		- missing drop, no extras and no interleave found
 * 		Y_FORCE_KEEP		- extra drop, no consumer so have to keep it
 *
 *	For any flags other than FORCE, no action is required.  Eeach group already has
 *	an available frame to drop, whether a marked duplicate, or a locally detected
 *	interleave pattern (which produces a drop).
 *
 *	For Y_FORCE_DEINT, assemble consecutive groups of this type and try to inherit
 *	adjacent interleave patterns.  If no pattern is available, mark them as
 *	Y_FORCE_DROP.
 */

static void
yait_find_drops( void )
	{
	Fi *f;
	int d, l;

	/* populate drop counts */
	for( d=0; d<Nd; d++ )
		Da[d] = yait_cnt_drops( d*5 );

	/* balance drop counts restricted by window size */
	for( d=0; d<Nd; d++ )
		{
		f = Fa[d*5];

		/* this is what we want to see */
		if( Da[d] == 1 )
			{
			if( !f->gf )
				f->gf = Y_HAS_DROP;
			continue;
			}

		/* group is missing a drop? */
		if( !Da[d] )
			{
			/* look ahead for an extra drop */
			l = yait_extra_drop( d );
			if( l )
				{
				/* found one */
				Da[d]++;
				f->gf = Y_BORROW_DROP;

				--Da[l];
				Fa[l*5]->gf = Y_RETURN_DROP;
				continue;
				}

			/* no extra drops exist, mark for de-interlacing */
			f->gf = Y_FORCE_DEINT;
			continue;
			}

		/* we have too many drops */
		while( Da[d] > 1 )
			{
			--Da[d];

			/* look ahead for a missing drop */
			l = yait_missing_drop( d );
			if( l )
				{
				/* found one */
				f->gf = Y_BANK_DROP;

				Da[l]++;
				Fa[l*5]->gf = Y_WITHDRAW_DROP;
				continue;
				}

			/* we have to keep a drop */
			if( !NoKeeps )
				{
				f->gf = Y_FORCE_KEEP;
				yait_keep_frame( d*5 );

				Stat_fk++;
				}
			}
		}
	}


/*
 *	yait_cnt_drops:
 */

static int
yait_cnt_drops( int n )
	{
	Fi *f;
	int d, i;

	d = 0;
	for( i=n; i<n+5 && i<Nf; i++ )
		{
		f = Fa[i];
		if( f->drop || f->op&Y_OP_DROP )
			d++;
		}

	return( d );
	}


/*
 *	yait_extra_drop:
 *		Scan DropWin groups ahead for an extra drop.
 */

static int
yait_extra_drop( int d )
	{
	int l, w;

	for( w=0; w<DropWin; w++ )
		{
		l = d + w + 1;
		if( l >= Nd )
			return( 0 );

		if( Da[l] > 1 )
			return( l );
		}

	return( 0 );
	}


/*
 *	yait_missing_drop:
 *		Scan DropWin groups ahead for a missing drop.
 */

static int
yait_missing_drop( int d )
	{
	int l, w;

	for( w=0; w<DropWin; w++ )
		{
		l = d + w + 1;
		if( l >= Nd )
			return( 0 );

		if( !Da[l] )
			return( l );
		}

	return( 0 );
	}


/*
 *	yait_keep_frame:
 *		Multiple drops exist.  Pick the best frame to keep.  This can be difficult,
 *	as we do not want to keep a duplicate of an interlaced frame.  First, try to find
 *	a hard dropped frame which does not follow an interlace.  If one can be found, then
 *	simply negate the drop flag.  If we are duplicating an interlace, alter the frame
 *	operations for the group to produce a non-interlaced duplicate.
 */

static void
yait_keep_frame( int n )
	{
	Fi *f;
	int da[6], bd, d, i;

	d = yait_get_hdrop( n, da );

	if( !d )
		{
		/* no hard drop frames were found, so ... */
		/* two interlace drops exist, keep one, but blend it */
		for( i=n; i<n+5 && i<Nf; i++ )
			{
			f = Fa[i];
			if( f->op & Y_OP_DROP )
				{
				f->op &= ~Y_OP_DROP;
				f->op |= Y_OP_DEINT;
				return;
				}
			}

		/* sanity check */
		f = Fa[n];
		fprintf( stderr, "No drop frame can be found, frame: %d\n", f->fn );
		exit( 1 );
		}

	/* try to use a drop frame that isn't an interlace duplicate */
	bd = -1;
	for( i=0; i<5; i++ )
		{
		d = da[i];
		if( !d )
			/* can't access before Fa[0] */
			continue;

		if( d < 0 )
			/* end of drop list */
			break;

		f = Fa[d-1];
		if( f->drop )
			/* two dups in a row */
			f = Fa[d-2];

		if( !f->op )
			{
			/* good */
			f = Fa[d];
			f->drop = FALSE;
			return;
			}

		if( f->op & Y_OP_COPY )
			bd = d;
		}

	/* keeping a duplicate of an interlace, try to use one which duplicates the */
	/* second of an interlace pair, as that is cleaner to deal with */
	/* bd (best drop) was set earlier in the loop if such a frame was found */
	if( bd < 0 )
		bd = da[0];

	yait_ivtc_keep( bd );
	}


/*
 *	yait_get_hdrop:
 *		Populate an index array of the hard dropped frames, and return
 *	the count of how many were found.
 */

static int
yait_get_hdrop( int n, int *da )
	{
	Fi *f;
	int d, i;

	d = 0;
	for( i=n; i<n+5 && i<Nf; i++ )
		{
		f = Fa[i];
		if( f->drop )
			{
			*da++ = i;
			d++;
			}
		}
	*da = -1;

	return( d );
	}


/*
 *	yait_ivtc_keep
 *		Depending upon the position of the DROP in the pattern, alter the
 *	frame ops to generate a non-interlaced frame, and keep it.
 *
 *	Case 1:
 *		If the duplicated frame is the second of the interlaced pair, then
 *		simply repeat the row copy operation and keep the frame.
 *
 *		Original (odd pattern):
 *				 	sd	c	 	 
 *			even:	2	2	3	3	4
 *			odd:	2	3	4	4	4
 *					drop		DROP
 *
 *		    yeilds (bad keep frame):
 *			even:	2		3	3	4
 *			odd:	2		3	4	4
 *							KEEP
 *		Revised:
 *				 	sd	c	c	 
 *			even:	2	2	3	3	4
 *			odd:	2	3	4	4	4
 *					drop		DROP
 *		    yeilds:
 *			even:	2		3	3	4
 *			odd:	2		3	3	4
 *							KEEP
 *
 *	Case 2:
 *		If the duplicated frame copies the first of the interlaced pair, more
 *		work must be done:
 *
 *		Original (odd pattern):
 *				 	sd		c	 
 *			even:	2	2	2	3	4
 *			odd:	2	3	3	4	4
 *					drop	DROP
 *
 *		    yeilds (bad keep frame):
 *			even:	2		2	3	4
 *			odd:	2		3	3	4
 *						KEEP
 *		Revised:
 *				s	c	sd	c	 
 *			even:	2	2	2	3	4
 *			odd:	2	3	3	4	4
 *						drop
 *		    yeilds:
 *			even:	2	2		3	4
 *			odd:	2	2		3	4
 *					(keep)
 */

static void
yait_ivtc_keep( int d )
	{
	Fi *fd, *fp;
	int t;

	fd = Fa[d];
	fp = Fa[d-1];
	if( fp->drop )
		fp = Fa[d-2];

	if( fp->op & Y_OP_COPY )
		{
		/* case 1 */
		fd->op = fp->op;
		fd->drop = FALSE;
		return;
		}

	/* case 2 */
	if( d < 2 )
		{
		/* can't access before Fa[0] */
		/* (unlikely we would see this the first two frames of a film) */
		fd->drop = FALSE;
		return;
		}

	fd->op = fp->op;
	fd->drop = FALSE;

	t = fp->op & Y_OP_PAT;
	fp->op = t | Y_OP_COPY;
	fp = Fa[d-2];
	fp->op = t | Y_OP_SAVE;
	}


/*
 *	yait_ivtc_grps:
 *		For each group missing an interleave pattern, scan backward and forward
 *	for an adjacent pattern.  Consider hard dropped frames as barriers.  If two
 *	different patterns exist, test the pattern against the original r values to find
 *	the best match.  For consecutive (forced) interleave groups, use the previously
 *	found pattern values, until the forward scan value is used, which is then
 *	propagated to the rest of the sequence.  (This avoids an O(n^2) search).
 *
 *		If no pattern can be found, force a drop of a frame in the group.
 *
 *	TODO:
 *		I should really be detecting scene changes as well, and consider them
 *		barriers.
 */

static void
yait_ivtc_grps( void )
	{
	Fi *f;
	int pb, pf, fg;
	int p, n;

	/* process by groups of 5 */
	fg = TRUE;
	pb = -1;
	pf = -1;
	for( n=0; n<Nf; n+=5 )
		{
		f = Fa[n];
		if( f->gf != Y_FORCE_DEINT )
			{
			fg = TRUE;
			continue;
			}

		if( fg )
			{
			/* this is the first group of a sequence, scan */
			fg = FALSE;
			pb = yait_scan_bk( n );
			pf = yait_scan_fw( n );
			}

		if( pb<0 && pf<0 )
			{
			/* no pattern exists */
			f->gf = Y_FORCE_DROP;
			yait_drop_frame( n );
			continue;
			}

		/* deinterlace the group with one of the given patterns */
		/* if the pattern used is forward, keep it from now on */
		p = yait_ivtc_grp( n, pb, pf );
		if( p < 0 )
			{
			/* no pattern will match */
			f->gf = Y_FORCE_DROP;
			yait_drop_frame( n );
			continue;
			}

		if( p == pf )
			pb = -1;
		}
	}


/*
 *	yait_scan_bk:
 */

static int
yait_scan_bk( int n )
	{
	Fi *f;
	int i;

	for( i=n-1; i>=0; --i )
		{
		f = Fa[i];
		if( !f )
			return( -1 );

		if( f->drop )
			return( -1 );

		if( f->ip != -1 )
			return( f->ip );
		}

	return( -1 );
	}


/*
 *	yait_scan_fw:
 */

static int
yait_scan_fw( int n )
	{
	Fi *f;
	int i;

	for( i=n+5; i<Nf; i++ )
		{
		f = Fa[i];

		if( f->drop )
			return( -1 );

		if( f->ip != -1 )
			return( f->ip );
		}

	return( -1 );
	}


/*
 *	yait_drop_frame:
 *		Choose a frame to drop.  We want the frame with the highest fabs(r) value,
 *	as it is likely an interlaced frame.  Do not use a frame which follows an assigned
 *	ip pattern, (it is the trailing element of a tuplet).  If no r values exceed the
 *	threshold, choose the frame with the minimum delta.
 *
 *		Frame:	0   1   2   3   4   |   5   6   7   8   9  
 *		Ratio:	0   0   0  -1   0   |	1   0   0   0   0
 *		Op:		   sd	c   |
 *				      group boundary
 *
 *	In the above example, the first frame of the second group (5) may have the highest
 *	ratio value, but is the worst choice because it is part of the detected pattern and
 *	is a unique progressive frame.
 */

static void
yait_drop_frame( int n )
	{
	Fi *f;
	double mr, r;
	int md, d;
	int fr, fd;
	int i;

	mr = 0;
	md = 0;
	fr = n;
	fd = n;

	for( i=n; i<n+5 && i<Nf-1; i++ )
		{
		if( !i )
			/* can't access before Fa[0] */
			continue;

		if( Fa[i-1]->drop || Fa[i+1]->drop )
			/* avoid two consequetive drops */
			continue;

		if( Fa[i-1]->op & Y_OP_PAT )
			/* trailing tuplet element */
			continue;

		f = Fa[i];

		r = fabs( f->ro );
		if( r > mr )
			{
			mr = r;
			fr = i;
			}

		d = f->ed + f->od;
		if( !md || d<md )
			{
			md = d;
			fd = i;
			}
		}

	Fa[ (mr>Thresh)?fr:fd ]->drop = TRUE;
	Stat_fd++;
	}


/*
 *	yait_ivtc_grp:
 *		We need to de-interlace this group.  Given are two potential patterns.
 *	If both are valid, test both and keep the one with the best r value matches.
 *	For the pattern used, mark the group, set the frame ops accordingly, and return
 *	it as the function value.
 */

static int
yait_ivtc_grp( int n, int p1, int p2 )
	{
	Fi *f;
	double thresh, m1, m2;
	int p, t, i;

	m1 = (p1 < 0) ? -1 : yait_tst_ip(n,p1);
	m2 = (p2 < 0) ? -1 : yait_tst_ip(n,p2);

	/* yait_tst_ip() returns the sum of two ratios */
	/* we want both ratios > Y_MTHRESH */
	thresh = Y_MTHRESH * 2;
	if( !NoDrops && m1<thresh && m2<thresh )
		/* neither pattern matches, force a drop instead */
		return( -1 );

	p = (m1 > m2) ? p1 : p2;

	/* sanity check */
	if( p < 0 )
		{
		f = Fa[n];
		fprintf( stderr, "Impossible interlace pattern computed (%d), frame: %d\n",
			p, f->fn );
		exit( 1 );
		}

	/* we have a pattern, mark group */
	for( i=n; i<n+5 && i<Nf; i++ )
		{
		f = Fa[i];
		if( f->drop )
			{
			fprintf( stderr,
				"De-interlace, horribly confused now, frame: %d.\n", f->fn );
			exit( 1 );
			}
		f->ip = p;
		}

	f = Fa[n];
	n = f->gi;

	/* sanity check */
	if( Ga[n] != f )
		{
		fprintf( stderr, "Lost our frame in the group array, frame: %d\n", f->fn );
		exit( 1 );
		}

	t = (p < 10) ? Y_OP_ODD : Y_OP_EVEN;
	for( i=n; i<n+5 && i<Ng-1; i++ )
		{
		if( i%5 == (p+2)%5 )
			{
			f = Ga[i];
			f->op = t | Y_OP_SAVE | Y_OP_DROP;

			/* don't overwrite an existing frame drop */
			f = Ga[i+1];
			if( !(f->op&Y_OP_DROP) )
				f->op = t | Y_OP_COPY;

			break;
			}
		}

	return( p );
	}


/*
 *	yait_tst_ip:
 */

static double
yait_tst_ip( int n, int p )
	{
	double rs, r;
	int s, i;

	s = (p < 10) ? 1 : -1;
	rs = 0;

	n = Fa[n]->gi;
	for( i=n; i<n+5 && i<Ng-2; i++ )
		{
		if( i%5 != (p+2)%5 )
			continue;

		/* strong pattern would have r[i]<-thresh and r[i+2]>thresh */
		r = s * Ga[i]->ro;
		if( r < 0 )
			rs += fabs( r );

		r = s * Ga[i+2]->ro;
		if( r > 0 )
			rs += r;

		break;
		}

	return( rs );
	}


/*
 *	yait_deint:
 *		For non 3/2 telecine patterns, we may have let interlaced frames
 *	through.  Tell transcode to de-interlace (blend) these.  This is the case for
 *	any frame having a high ratio with no interlace pattern detected.
 *
 *	TODO:
 *		This was an afterthought.  Perhaps we can avoid a 32detect pass on
 *	the video by performing this, although it is difficult to detect out of
 *	pattern interlace frames solely on row delta information.  Perhaps we should
 *	have built 32detect into the log generation, and added an extra flag field if
 *	we thought the frame was interlaced.  This also would help when trying to
 *	assign ambiguous ip patterns.
 */

static void
yait_deint( void )
	{
	Fi *fp, *fn, *f;
	int i;

	for( i=1; i<Ng-1; i++ )
		{
		fp = Ga[i-1];
		f = Ga[i];
		fn = Ga[i+1];

		if( f->op&Y_OP_PAT || f->drop )
			/* already being de-interlaced or dropped */
			continue;

		if( fp->op & Y_OP_PAT )
			/* trailing element of a tuplet */
			continue;

		if( fabs(f->r) < Blend )
			/* it isn't interlaced (we think) */
			continue;

		if( f->nd < Y_FWEIGHT )
			/* delta is too weak, interlace is likely not visible */
			continue;

		if( fp->nd>Y_SCENE_CHANGE || f->nd>Y_SCENE_CHANGE )
			/* can't make a decision over scene changes */
			continue;

		/* this frame is interlaced with no operation assigned */
		f->op = Y_OP_DEINT;

		/* if the next frame ratio < thresh, it is similar, unless */
		/* a scene change, therefore interlaced as well */
		if( fabs(fn->r)<Thresh && fn->nd<Y_SCENE_CHANGE )
			if( !(fn->op&Y_OP_PAT) && !fn->drop )
				fn->op = Y_OP_DEINT;

		/* if the next frame(s) are duplicates of this, mark them */
		/* for blending as well, as the may eventually be force kept */
		while( f->next && !f->next->gi )
			{
			f = f->next;
			f->op = Y_OP_DEINT;
			}
		}
	}


/*
 *	yait_write_ops:
 */

static void
yait_write_ops( void )
	{
	Fi *f;

	for( f=Fl; f; f=f->next )
		fprintf( OpsFp, "%d: %s\n", f->fn, yait_write_op(f) );
	}


/*
 *	yait_write_op:
 */

static char*
yait_write_op( Fi *f )
	{
	static char buf[10];
	char *p;
	int op;

	p = buf;
	if( f->drop )
		{
		*p++ = 'd';
		*p = 0;
		Stat_nd++;
		return( buf );
		}

	op = f->op;
	if( op & Y_OP_ODD )
		*p++ = 'o';
	if( op & Y_OP_EVEN )
		*p++ = 'e';
	if( op & Y_OP_SAVE )
		*p++ = 's';
	if( op & Y_OP_COPY )
		*p++ = 'c';
	if( op & Y_OP_DROP )
		{
		*p++ = 'd';
		Stat_nd++;
		if( op & Y_OP_ODD )
			Stat_no++;
		else
			Stat_ne++;
		}
	if( op & Y_OP_DEINT )
		{
		*p++ = '0' + DeintMode;
		Stat_nb++;
		}
	*p = 0;

	return( buf );
	}


/*
 *	yait_fini:
 *		Free up allocations.
 */

static void
yait_fini( void )
	{
	int i;

	for( i=0; i<Nf; i++ )
		free( Fa[i] );

	free( Fa );
	free( Ga );
	free( Da );
	}


/*
 *	Output debug information to stdout
 */

static void
yait_debug_fi( void )
	{
	Fi *f;
	int i;

	printf( "Options:\n" );
	printf( "\tLog file: %s\n", LogFn );
	printf( "\tOps file: %s\n", OpsFn );
	printf( "\tEven Threshold: %g\n", EThresh );
	printf( "\tOdd Threshold: %g\n", OThresh );
	printf( "\tBlend threshold: %g\n", Blend );
	printf( "\tDrop window size: %d\n", DropWin );
	printf( "\tDe-interlace mode: %d\n\n", DeintMode );

	printf( "Stats:\n" );
	printf( "\tTotal number of frames: %d\n", Nf );
	printf( "\tNumber of frames divided by 5: %d\n", Nf/5 );
	printf( "\tTotal dropped frames: %d\n", Stat_nd );
	printf( "\tTotal blended frames: %d\n", Stat_nb );
	printf( "\tTotal odd interlaced pairs: %d\n", Stat_no );
	printf( "\tTotal even interlaced pairs: %d\n", Stat_ne );
	printf( "\tNumber of forced frame drops: %d\n", Stat_fd );
	printf( "\tNumber of forced frame keeps: %d\n\n", Stat_fk );
	printf( "\tMax row delta: %d\n\n", Md );

	i = 0;
	for( f=Fl; f; f=f->next, i++ )
		{
		if( i && !(i%5) )
			printf( "\n" );

		printf( "Frame %6d: e: %8d, o: %8d, r: %7.3f, ro: %7.3f, w: %8.4f, "
			"ip: %2d, gi: %6d, op: %-4s d: %s   %s\n",
				f->fn, f->ed, f->od, f->r, f->ro, f->w, f->ip, f->gi,
				yait_op(f->op), yait_drop(f), yait_grp(f->gf) );
		}
	}

static char*
yait_op( int op )
	{
	static char buf[10];
	char *p;

	p = buf;
	*p = 0;
	if( !op )
		return( buf );

	if( op & Y_OP_ODD )
		*p++ = 'o';
	if( op & Y_OP_EVEN )
		*p++ = 'e';
	if( op & Y_OP_SAVE )
		*p++ = 's';
	if( op & Y_OP_COPY )
		*p++ = 'c';
	if( op & Y_OP_DROP )
		*p++ = 'd';
	if( op & Y_OP_DEINT )
		*p++ = '0' + DeintMode;
	*p = 0;

	return( buf );
	}

static char*
yait_drop( Fi *f )
	{
	if( f->drop )
		return( "DROP" );

	if( f->op&Y_OP_ODD && f->op&Y_OP_DROP )
		return( "odd " );

	if( f->op&Y_OP_EVEN && f->op&Y_OP_DROP )
		return( "even" );

	return( "    " );
	}

static char*
yait_grp( int flg )
	{
	switch( flg )
		{
		case Y_HAS_DROP:
			return( "has drop" );
		case Y_BANK_DROP:
			return( "bank" );
		case Y_WITHDRAW_DROP:
			return( "withdraw" );
		case Y_BORROW_DROP:
			return( "borrow" );
		case Y_RETURN_DROP:
			return( "return" );
		case Y_FORCE_DEINT:
			return( "force deint" );
		case Y_FORCE_DROP:
			return( "force drop" );
		case Y_FORCE_KEEP:
			return( "force keep" );
		}
	return( "" );
	}
