/*
    Copyright (C) 2013 Stephane Poinsart <s@poinsart.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * VotAR.cpp : native image parsing functions
 *
 * Small amount of vocabulary to understand this file :
 *  - mark = when the algorithm detect a choice, i will call that a mark. a mark has a x and y coordinates, and a pattern rotation.
 *  - "pr" = pattern rotation, a number from 0 to 4 to designate a choice a voter made, the whole pattern being what the symbol looks like.
 *  - a square is 1/4th of a pattern
 *
 * File overview :
 *  - simple_analyze : first "android agnostic" entry point
 *  	- generateWorkingImage : allocate a "work" version for the photo that we can be messy with, and does some non-vital pre-processing (pixel averaging, edge detection)
 *  	- findAllPatterns : iterate through most pixels of the photo
 *  		- findOnePattern : given a pixel, iterate through the 4 rotations and 4 squares, and return a pr if it match
 *  			- compare a pixel with a reference color, send back a difference value that determine if the color is unlikely to match
 *
 */


#include <string.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <math.h>
#include <utility>
#include <endian.h>
#include <include/android.hpp>
#include <stdlib.h>
#include <time.h>

#include "count-simple.hpp"
#include "common.hpp"

#define  LOG_TAG    "nativeAnalyze"
#define  Log_i(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  Log_w(...)  __android_log_print(ANDROID_LOG_WARN,LOG_TAG,__VA_ARGS__)
#define  Log_e(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

extern "C" {


// total difference allowed
// easy-to-adjust value
// more = more stuff detected but also more false positives
// less = less stuff detected but also less false positives
#define AVERAGE_DIFFERENCE_ALLOWED			(0xB8)


// used in the color matching formula to determine how bad it is
// for each amount of difference in saturation
#define SATURATION_NOMINATOR				(0x400)
#define SATURATION_V_OFFSET					(0x05)
#define SATURATION_H_OFFSET					(0x01)


// used in the color matching formula to determine how bad it s
// for each amount of hue difference from the reference color
#define HUEDIFF_LINEAR_MULT					(0x03)

//#define ALGO_STATS

#ifdef ALGO_STATS
int algo_stats_hues[4];
int algo_stats_sat[4];
int algo_stats_mindiff, algo_stats_minpr;
#endif


// the following variables are computed based on image width
int pixelsteptocenter,	// to get the 4 colors in a mark, we look sideways from the center of the mark. This variable define how far the specific pixel we use will be
	burnradius;			// when we have a mark, we burn pixel of the "working image" around it to make sure we don't detect false positive with 2 marks very close to each other. this is how much we burn.


// will not work on first/last column/work but that's ok : those are not useful pixels
// we just skip some at the beginning / end just to make sure we don't SIGSEGV
// on reading outer pixels
// we need to average each component separately to avoid overflowing on others
// will only work if inpixels != outpixels because the average method need stable neighbors
void average33(unsigned int *inpixels, unsigned int *outpixels,  int width, int height) {
	int endx=width-1,
		endy=height-1;
	for (int y=1; y<endy; y++) {
		for (int x=1; x<endx; x++) {
			int i=x+y*width;
			outpixels[i]= (
					((((inpixels[i-1-width] & 0x00FF0000) + (inpixels[i-width] & 0x00FF0000) + (inpixels[i+1-width] & 0x00FF0000)	// Row -1, component 1
					+ (inpixels[i-1]       & 0x00FF0000) + (inpixels[i]       & 0x00FF0000) + (inpixels[i+1]       & 0x00FF0000)	// Row  0, component 1
					+ (inpixels[i-1+width] & 0x00FF0000) + (inpixels[i+width] & 0x00FF0000) + (inpixels[i+1+width] & 0x00FF0000))	// Row +1, component 1
					/ 9) & 0x00FF0000)
				|
					(
					(((inpixels[i-1-width] & 0x0000FF00) + (inpixels[i-width] & 0x0000FF00) + (inpixels[i+1-width] & 0x0000FF00)	// Row -1, component 2
					+ (inpixels[i-1]       & 0x0000FF00) + (inpixels[i]       & 0x0000FF00) + (inpixels[i+1]       & 0x0000FF00)	// Row  0, component 2
					+ (inpixels[i-1+width] & 0x0000FF00) + (inpixels[i+width] & 0x0000FF00) + (inpixels[i+1+width] & 0x0000FF00))	// Row +1, component 2
					/ 9) & 0x0000FF00)
				|
					(
					(((inpixels[i-1-width] & 0x000000FF) + (inpixels[i-width] & 0x000000FF) + (inpixels[i+1-width] & 0x000000FF)	// Row -1, component 3
					+ (inpixels[i-1]       & 0x000000FF) + (inpixels[i]       & 0x000000FF) + (inpixels[i+1]       & 0x000000FF)	// Row  0, component 3
					+ (inpixels[i-1+width] & 0x000000FF) + (inpixels[i+width] & 0x000000FF) + (inpixels[i+1+width] & 0x000000FF))	// Row +1, component 3
					/ 9) & 0x000000FF))
				& 0x00FFFFFF;
		;
		}
	}
}



// combine many operation to get an easier to work on image :
// - allocate memory for a new image
// - average pixels in 3x3 squares into the new "working image"
// - return the resulting image
unsigned int *generateWorkingImage(unsigned int *inpixels, int width, int height) {
	int pixelcount = width * height;
	unsigned int *workpixels = (unsigned int*) malloc(sizeof(unsigned int) * pixelcount);
	if (!workpixels) {
		Log_e("Failed to allocate %d bytes as a work image",pixelcount);
		return workpixels;
	}
	benchmarkElapsed("malloc workpixels");

	average33(inpixels, workpixels, width, height);
	benchmarkElapsed("average33");
	return workpixels;
}


// simple function to put a dot on an image position
// we use it to draw on the display image, or to burn pixels on the working one
void markPixel(unsigned int *pixels, int width, int height, int x, int y, unsigned int color, int size) {
	// make sure no memory overflow in this loop (the mark can be close to top/bottom edge and radius bigger than PIXEL_STEP_TO_CENTER)

	int jstart=max(y-size,0), // crop top
		jend=min(y+size, height), // crop bot
		istart=max(x-size,0), // crop left
		iend=min(x+size, width); // crop right

	// no vertical bleed
	for (int j=jstart; j<jend; j++)
		//no horizontal bleed
		for (int i=istart; i<iend; i++)
			pixels[i+j*width]=color;
}



/**
 * determine how much the color a specific photo pixel differ from a given reference color (on of the 4)
 *
 * It's called for almost every pixel * every pattern
 *
 * We have a different test for each color :
 * Given a particular color, we determine which component should be strong and which component should be weak.
 * i.e. yellow = red and green, so r and g should be strong, b should be weak
 *  - We discard immediately if we get the complete opposite of a weak / strong assumption
 *  - In other cases, we use the shape of an inverse function to get a meaningful value of how different the color looks
 *  - We then use the saturation to ponder the difference : a big hue difference on a low saturation pixel is not extremely significant
 *  - We then re-add saturation as a positive element in the formula because if a pixel has really low saturation, it's less likely part of our colorful voting paper
 *
 *  - c = int with each byte as a color component (r,g,b,0)
 *  - cindex = color index 0 to 3 as explained bellow
 */
inline int checkSquare(unsigned int c, unsigned int cindex) {
	// cindex = reference colors components clockwise
	//		  R    G    B
	//		{0x00,0xFF,0x00},		0 -> green
	//		{0xFF,0xFF,0x00},		1 -> yellow
	//		{0x00,0xFF,0xFF},		2 -> cyan
	//		{0xFF,0x00,0xFF},		3 -> magenta

	// maths : https://www.desmos.com/calculator/b0g1wkqyju

	// color components
	int		r = (int)(c & 0x000000FF),
			g = (int)((c >> 8) & 0x000000FF),
			b = (int)((c >> 16) & 0x000000FF);


	// total match value accounting for hue difference and saturation
	int diff=0;

	// relative saturation, allow negative values (if we have the opposite hue)
	int sat=0;
	// arbitrary value of a hue difference
	int huediff;


	switch (cindex) {
	case 2 :
		// yellow,
		// r and  g are strong, b is weak


		// first, outcast anything where b is not the weakest color
		// which means dead square (completely wrong hue) and we stop here
		//
		// to check saturation, we calculate stronger - weaker (and get an absolute value 0 <= sat <= 0xFF
		//
		// to check for more moderate hue difference
		// 0     n      n'    255
		// .. b .... g .... r ..
		//    r-g/g-b/sat        (with r-b being saturation)
		//
		//     or
		// .. b .... r .... g ..
		//
		// what we want with hue check is : are g and r close together or one of them is close to b ?
		// raw huediff = 0 : very close
		if (b>=r || b>=g)
			return 0x400;

		// we assume one of the strong component > weak to calculate a relative saturation
		// if it's not the case, we have a "negative" saturation, which is mean it has a completely reversed hue
		if (r>g) {
			sat=r-b;
			huediff=(r-g) * HUEDIFF_LINEAR_MULT/(g-b+1);
		} else {
			sat=g-b;
			huediff=(g-r) * HUEDIFF_LINEAR_MULT/(r-b+1);
		}
		huediff=huediff*0x100/sat;

		break;
	case 1 :
		// cyan,
		// g and b are strong, r is weak
		if (r>=g || r>=b)
			return 0x400;

		if (g>b) {
			sat=g-r;
			huediff=(g-b) * HUEDIFF_LINEAR_MULT/(b-r);
		} else {
			sat=b-r;
			huediff=(b-g) * HUEDIFF_LINEAR_MULT/(g-r);
		}
		huediff=huediff*0x100/sat;

		break;
	case 3 :
		// magenta,
		// r and b are strong, g is weak
		if (g>=r || g>=b)
			return 0x400;

		if (r>b) {
			sat=r-g;
			huediff=(r-b) * HUEDIFF_LINEAR_MULT/(b-g);
		} else {
			sat=b-g;
			huediff=(b-r) * HUEDIFF_LINEAR_MULT/(r-g);
		}
		huediff=huediff*0x100/sat;

		break;
	case 0 :
		// green,
		// r and b are weak, g is strong
		if (g<=r || g<=b)
			return 0x400;

		if (r>b) {
			sat=g-b;
			huediff=(r-b) * HUEDIFF_LINEAR_MULT/(g-r);
		} else {
			sat=g-r;
			huediff=(b-r) * HUEDIFF_LINEAR_MULT/(g-b);
		}

		huediff=huediff*0x100/sat;

		break;
	}
	diff+=huediff;
#ifdef ALGO_STATS
	algo_stats_hues[cindex]=huediff;
#endif

	// adjusted 1/sat curve :
	// extremely low saturation = dead square
	// very low sat = penalize much
	// low, medium sat = penalize very little (to still work in dark rooms)
	// high sat = don't penalize, might give a small bonus
	if (sat>=0) {
		// a quite soft exponential curve for low values : x * x / 0x80
		diff+=SATURATION_NOMINATOR/(sat+SATURATION_H_OFFSET)-SATURATION_V_OFFSET;
	} else {
		diff+=AVERAGE_DIFFERENCE_ALLOWED*2;
	}
#ifdef ALGO_STATS
	if (sat>0) {
		algo_stats_sat[cindex]=SATURATION_NOMINATOR/(sat+SATURATION_H_OFFSET)-SATURATION_V_OFFSET;
	} else {
		algo_stats_sat[cindex]=AVERAGE_DIFFERENCE_ALLOWED*2;
	}
//	Log_i("localdiff %d: %d",cindex,diff);
#endif

	return diff;
}


/**
 * given a particular pixel position on the photo, we check if it match a pattern rotation
 * return the pr, or -1 if it does not match
 *
 * bruteforce every square for every rotation, not optimal but adequate
 */
int findOnePattern(unsigned int *inpixels, unsigned int *workpixels, int width, int height, int x, int y) {
	unsigned int uc[4]; // unshifted colors
	// Green, Yellow, Cyan, Magenta
	unsigned int ct=x+width*y;
	const unsigned int hstep=pixelsteptocenter;
	unsigned int vstep=width*hstep;

	// we use x/y as the center of this pattern, each table cell match a sub-square
	//  0 | 1
	// ---X---
	//  3 | 2
	uc[0]=workpixels[ct-vstep-hstep]; // top left
	uc[1]=workpixels[ct-vstep+hstep]; // top right
	uc[2]=workpixels[ct+vstep+hstep]; // bot right
	uc[3]=workpixels[ct+vstep-hstep]; // bot left




	// the fun continues
	// pr is the pattern rotation, we loop on the 4 possible rotations of the first pattern
	//  pr=0         pr=1         pr=2          pr=3
	//  0 | 1        1 | 2        2 | 3         3 | 0
	// -------  ->  -------  ->  --------  ->  -------
	//  3 | 2        0 | 3        1 | 0         2 | 1
	for (int pr=0; pr<4; pr++) {
		int diff=0;
		// for each sub-square of this pattern rotation, add up the difference with the reference color
#ifdef ALGO_STATS
		algo_stats_mindiff=0xFFFFFF;
		algo_stats_minpr=0;
		algo_stats_hues[0]=0; algo_stats_hues[1]=0; algo_stats_hues[2]=0; algo_stats_hues[3]=0;
		algo_stats_sat[0]=0; algo_stats_sat[1]=0; algo_stats_sat[2]=0; algo_stats_sat[3]=0;
#endif
		for (int i=0; i<4; i++) {
			// check every square
			// pr=0
			//  G |            | C          |             |
			// -------  ->  -------  ->  --------  ->  -------
			//    |            |            | Y         M |
			//
			// pr=1
			//  M |            | G          |             |
			// -------  ->  -------  ->  --------  ->  -------
			//    |            |            | C         Y |
			//
			// ...
			diff+=checkSquare(uc[(pr+i)%4], i);
#ifndef ALGO_STATS
			// if the color difference for all the subsquares is too big, it's over for this pattern rotation so don't waste time
			if (diff>AVERAGE_DIFFERENCE_ALLOWED+0x20)
				break;
#endif

		}
#ifdef ALGO_STATS
		if (diff<algo_stats_mindiff) {
			algo_stats_mindiff=diff;
			algo_stats_minpr=pr;
		}
		if (diff-40<=AVERAGE_DIFFERENCE_ALLOWED) {
			Log_i("Square pr:%d | d:%d | hues:%d,%d,%d,%d | sat:%d,%d,%d,%d | pos: %d,%d",algo_stats_minpr,algo_stats_mindiff*100/AVERAGE_DIFFERENCE_ALLOWED,
						algo_stats_hues[0]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_hues[1]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_hues[2]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_hues[3]*100/AVERAGE_DIFFERENCE_ALLOWED,
						algo_stats_sat[0]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_sat[1]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_sat[2]*100/AVERAGE_DIFFERENCE_ALLOWED,algo_stats_sat[3]*100/AVERAGE_DIFFERENCE_ALLOWED,
						x,y);
		}
#endif

		//
		if (diff<=AVERAGE_DIFFERENCE_ALLOWED) {
			return pr;
		}
	}
	return -1;
}


// compute squared color distance from color vector
// we compare 2 pixels this way for edge detection
inline int colorDiff(unsigned int c1, unsigned int c2) {
	int		r1 = (int)(c1 & 0x000000FF),
			g1 = (int)((c1 >> 8) & 0x000000FF),
			b1 = (int)((c1 >> 16) & 0x000000FF);
	int		r2 = (int)(c2 & 0x000000FF),
			g2 = (int)((c2 >> 8) & 0x000000FF),
			b2 = (int)((c2 >> 16) & 0x000000FF);
	return (r1-r2)*(r1-r2)+(g1-g2)*(g1-g2)+(b1-b2)*(b1-b2);
}


#define COLORDIFF_STEP	1
#define COLORDIFF_ALLOWED_DELTA 0x24*0x24

// if 2 nearby pixels have a significant color difference, we assume it's an edge and we burn it.
inline void burnIfEdge(unsigned int *inpixels, unsigned int *workpixels, int width, int height, int i, int j) {
	if (colorDiff(workpixels[i-COLORDIFF_STEP+j*width], workpixels[i+COLORDIFF_STEP+j*width])>COLORDIFF_ALLOWED_DELTA
			|| colorDiff(workpixels[i+(j-COLORDIFF_STEP)*width], workpixels[i+(j+COLORDIFF_STEP)*width])>COLORDIFF_ALLOWED_DELTA ) {
		workpixels[i+j*width]|=0xFF000000;
	}
}

// the fun part start here... iterate through most pixels to pass to the pattern comparison function
void findAllPatterns(unsigned int *inpixels, unsigned int *workpixels, unsigned int width, unsigned int height, int (&mark)[MAX_MARK_COUNT][3], int &markcount, int (&prcount)[4]) {
	//          i=
	//        5 7 9
	// j=5 -> + . .     . + .    . . +    . . .
	// j=7 -> . . .  -> . . . -> . . . -> + . .
	// j=9 -> . . .     . . .    . . .    . . .

	// the exploration coordinates is based on the center, so we cant start at 0 because we are going to look left.
	// we add "pixelsteptocenter" as a border around the area
	// and then we add 1 extra pixel because average33 and burnIfEdges need their own margins
	int startingx=pixelsteptocenter+max(1,COLORDIFF_STEP),
		startingy=pixelsteptocenter+max(1,COLORDIFF_STEP),
		endingx=width-pixelsteptocenter-max(1,COLORDIFF_STEP),
		endingy=height-pixelsteptocenter-max(1,COLORDIFF_STEP);

	unsigned int matchcolors[4]={
			0x0000FF00,
			0x00FF00FF,
			0x0000FFFF,
			0x00FFFF00
	};

	markcount=0;
	for (int j=startingy; j<endingy; j+=2) {
		for (int i=startingx; i<endingx; i+=2) {
			// basic edge detection to burn pixels on an edge
			burnIfEdge(inpixels, workpixels, width, height, i, j);
		}
	}
	for (int j=startingy; j<endingy; j+=2) {
		for (int i=startingx; i<endingx; i+=2) {
			// skip burned pixels
			if (	   (workpixels[(i-pixelsteptocenter)+width*(j-pixelsteptocenter)] & 0xFF000000)
					|| (workpixels[(i+pixelsteptocenter)+width*(j-pixelsteptocenter)] & 0xFF000000)
					|| (workpixels[(i+pixelsteptocenter)+width*(j+pixelsteptocenter)] & 0xFF000000)
					|| (workpixels[(i-pixelsteptocenter)+width*(j+pixelsteptocenter)] & 0xFF000000)
				)
				continue;
			int pr=findOnePattern(inpixels, workpixels, width, height, i,j);
			if (pr>=0) {
				prcount[pr]++;

				// this mark is just for display
				markPixel(inpixels,width, height, i, j,matchcolors[pr],burnradius);

				// also burn the workpixels to make sure we do not count the same square 2 times
				markPixel(workpixels,width, height, i, j,0xFF000000,burnradius);

				// this will be part of the function return values (converted to json)
				mark[markcount][0]=i;
				mark[markcount][1]=j;
				mark[markcount][2]=pr;
				(markcount)++;
				if (markcount>=MAX_MARK_COUNT) {
					Log_w("unlikely event : pattern count match limit reached, stopping before the image is completely processed");
					goto SkipFindAllPaternsLoop;
				}
			}
		}
	}
	SkipFindAllPaternsLoop:
	Log_i("found patterns... 1: %d | 2: %d | 3: %d | 4: %d ", prcount[0], prcount[1], prcount[2], prcount[3]);
}


/**
 * Get the image and deliver the results of the vote by some image processing
 * This is the only function that should be called directly from this file.
 *
 * All outputs arrays are preallocated and static, dont malloc them or overwrite their reference
 *
 * INPUTS :
 *	- inpixel : raw array of pixels in the photo, from top left to bottom right, each pixel is coded as 4 bytes: R,G,B,0. For this version, it's displayed in the app, so you can draw on it.
 *	- width : photo width
 *	- height : photo height
 *
 * OUTPUTS :
 *	- mark : array that contains up to MAX_MARK_COUNT vote results. Each vote result is coded as an array of 3 int :
 *     - x coordinate
 *     - y coordinate
 *     - pattern rotation (0=A, 1=B, 2=C, 3=...)
 *  - markcount : total number of marks found (number of people who voted and were detected)
 *  - prcount : number of vote for each choice in an array with 4 int (0=A, 1=B, 2=C, 3=...)
 */
bool simple_analyze(unsigned int *inpixels, unsigned int width, unsigned int height, int (&mark)[MAX_MARK_COUNT][3], int &markcount, int (&prcount)[4]) {
	/////////////////////////////
	// set some offsets that will be used latter
	// if it's a high res photo, we assume larger pixel distances in the analysis
	unsigned int *workpixels;
	int maxdim=max(width, height);

	// 22 pixels at 8mp, 20 at 5mp
	// it needs to be that large to protect against double-counting
	burnradius=8+maxdim/256;
	// 8 pixels at 8mp, 7 pixels at 5mp
	// large step helps on blurry pictures but too large miss small patterns or double-count some overlapped patterns
	pixelsteptocenter=2+maxdim/512;
	Log_i("step: %d, burn radius: %d", pixelsteptocenter, burnradius);


	benchmarkElapsed("various initialization stuff");

	// allocate and prepare the image
	workpixels=generateWorkingImage(inpixels, width, height);
	if (!workpixels)
		return false;

	publish_progress(2);

	// match everything
	findAllPatterns(inpixels, workpixels, width, height, mark, markcount, prcount);

	publish_progress(3);

	benchmarkElapsed("findAllPatterns");
	free(workpixels);
	return true;
}

}
