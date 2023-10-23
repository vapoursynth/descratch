/*
DeScratch - Scratches Removing Filter
Plugin for Avisynth 2.5
Copyright (c)2003-2016 Alexander G. Balakhnin aka Fizick
bag@hotmail.ru
http://avisynth.org.ru

This program is FREE software under GPL licence v2.

This plugin removes vertical scratches from digitized films.


         Version changes:

version 0.1,  December 22, 2003 - intitial beta (not public)
version 0.2,  December 31, 2003 - only sharp extrems detected, close gaps option
version 0.3,  July 13, 2004 - ported to native  Avisynth plugin,
	fast bluring by Avisynth resizing functions
version 0.4,  July 17, 2004 - added chroma processing (modeY, modeU, modeV parameters)
	mindif is absolute now,
	marked scratches value is not inverted but maximazed now
version 0.5,  July 31, 2004 - fixed read-write pointer bug,
	add mindifUV parameter, some code reorganization
version 0.6, August 23, 2004 - fixed byte overflow bug (clip output to 0-255)
version 0.7, November 15, 2004 -  added asymmetry check
Version 0.8, March 13, 2005 -  added maxlen parameter
Version 0.9, March 31, 2005 -  allow maxwidth=1
Version 0.9.0.0 September 09, 2006 - source licensed under GPL v2
Version 0.9.1, February 02, 2016 -  fixed scratch border
Version 1.0, February 5, 2016 - more maxwidth, other changes

  Current version limitation:

Maximum scratch width is odd from 1 to 11.
The plugin works only in YV12.

*/

#include <avisynth.h>
#include <cstdlib>
#include <algorithm>


#define SD_NULL 0
#define SD_EXTREM 1
#define SD_TESTED 2
#define SD_GOOD 4
#define SD_REJECT 8

#define MODE_NONE 0
#define MODE_LOW 1
#define MODE_HIGH 2
#define MODE_ALL 3


class DeScratch : public GenericVideoFilter {
  //  defines the name of your filter class.
  // This name is only used internally, and does not affect the name of your filter or similar.
  // This filter extends GenericVideoFilter, which incorporates basic functionality.
  // All functions present in the filter must also be present here.


	int mindif;
	int asym;
	int maxgap;
	int maxwidth;
	int minlen;
	int maxlen;
	float maxangle;
	int blurlen;
	int keep;
	int border;
	int modeY;
	int modeU;
	int modeV;
	int mindifUV;
	bool mark;
	int minwidth;
	int wleft;
	int wright;

	BYTE * scratchdata;
//	PClip down_clip;
    PClip blured_clip;

	BYTE *buf;
	int buf_pitch;
	int width;
	int height;

void DeScratch_pass(const BYTE * srcp, int src_pitch, const BYTE * bluredp,int blured_pitch,
			BYTE * destp, int dest_pitch, int row_sizep, int heightp, int hscale, int mindifp, int asym);

public:
  // This defines that these functions are present in your class.
  // These functions must be that same as those actually implemented.
  // Since the functions are "public" they are accessible to other classes.
  // Otherwise they can only be called from functions within the class itself.

	DeScratch(PClip _child, int _mindif, int _asym, int _maxgap, int _maxwidth,
		int _minlen, int _maxlen, float _maxangle, int _blurlen, int _keep, int _border,
		int _modeY, int _modeU, int _modeV, int _mindifUV, bool _mark, int _minwidth, int _wleft, int _wright, IScriptEnvironment* env);
  // This is the constructor. It does not return any value, and is always used,
  //  when an instance of the class is created.
  // Since there is no code in this, this is the definition.

  ~DeScratch();
  // The is the destructor definition. This is called when the filter is destroyed.

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
  // This is the function that AviSynth calls to get a given frame.
};

//Here is the acutal constructor code used
DeScratch::DeScratch(PClip _child, int _mindif, int _asym, int _maxgap, int _maxwidth,
					 int _minlen, int _maxlen, float _maxangle, int _blurlen, int _keep, int _border,
					 int _modeY, int _modeU, int _modeV, int _mindifUV, bool _mark, int _minwidth, int _wleft, int _wright, IScriptEnvironment* env) :
	GenericVideoFilter(_child), mindif(_mindif), asym(_asym), maxgap(_maxgap), maxwidth(_maxwidth),
		minlen(_minlen), maxlen(_maxlen), maxangle(_maxangle), blurlen(_blurlen), keep(_keep), border(_border),
		modeY(_modeY), modeU(_modeU), modeV(_modeV), mindifUV(_mindifUV), mark(_mark), minwidth(_minwidth), wleft(_wleft), wright(_wright)
	{
  // This is the implementation of the constructor.
  // The child clip (source clip) is inherited by the GenericVideoFilter,
  //  where the following variables gets defined:

        if ( mindif<=0 )
           env->ThrowError("Descratch: mindif must be positive!");
        if ( asym<0 )
           env->ThrowError("Descratch: asym must be not negative!");
        if ( mindifUV<0 )
           env->ThrowError("Descratch: mindifUV must not be negative!");
		else if ( mindifUV == 0 )
			mindifUV = mindif; // v.0.5
        if ( (maxgap<0) || (maxgap>255))
           env->ThrowError("Descratch: maxgap must be >=0 and <=256!");
        if ( !(maxwidth % 2) || (maxwidth < 1) || (maxwidth >15))
           env->ThrowError("Descratch: maxwidth must be odd from 1 to 15!"); // v.1.0
        if ( (minlen<=0))
           env->ThrowError("Descratch: minlen must be > 0!");
        if ( (maxlen<=0))
           env->ThrowError("Descratch: maxlen must be > 0!");
        if ( (maxangle<0) || (maxangle>90) )
           env->ThrowError("Descratch: maxangle must be from 0 to 90!");
        if ( (blurlen<0) || (blurlen>200) )
           env->ThrowError("Descratch: blurlen must be from 0 to 200!"); // v1.0
        if ( (keep<0) || (keep>100) )
           env->ThrowError("Descratch: keep must be from 0 to 100!");
        if ( (border<0) || (border>5) )
           env->ThrowError("Descratch: border must be from 0 to 5!");
		if (!vi.IsYV12())
			env->ThrowError("Descratch: Video must be YV12!");
		if ( modeY<0 || modeY>3 || modeU<0 || modeU>3 || modeV<0 || modeV>3 )
			env->ThrowError("Descratch: modeY, modeU, modeV must be from 0 to 3!");
        if ( minwidth > maxwidth )
           env->ThrowError("Descratch: minwidth must be not above maxwidth!");
        if ( !(minwidth %2) || (minwidth < 1) || (minwidth >15))
           env->ThrowError("Descratch: minwidth must be odd from 1 to 15!"); // v.1.0


		width = vi.width;
		height= vi.height;
		buf_pitch = width + 16 - width%16;

		// check working window limits - v.1.0
		if (wleft < 0)
			wleft = 0;
		wleft = wleft - wleft%2;
		if (wright > width)
			wright = width;
		wright = wright - wright%2;
		if (wleft >= wright)
			env->ThrowError("Descratch: must be: left < right <= width!");

		// create temporary array for scratches data
	scratchdata = (BYTE *)malloc(vi.height*vi.width);

   int down_height = (vi.height)/(1+blurlen);
   if (down_height%2) down_height -=1;
 //  int height = vi.height;
 //  int width = vi.width;
      AVSValue down_args[3] = { child, width, down_height};
      PClip down_clip = env->Invoke("BilinearResize", AVSValue(down_args,3)).AsClip();

      AVSValue blur_args[3] = { down_clip,  width, height};
      blured_clip = env->Invoke("BicubicResize", AVSValue(blur_args,3)).AsClip();

	  buf = (BYTE *)malloc(height*buf_pitch);

}

// This is where any actual destructor code used goes
DeScratch::~DeScratch() {
  // This is where you can deallocate any memory you might have used.
       free(scratchdata);
	   free(buf);
}


//
//
void  get_extrems_plane(const BYTE * s, int src_pitch, int row_size, int height, BYTE *scratchdata, int mindif, int asym, int maxwidth)
{
  BYTE *d;
  d = scratchdata;
  int row, h;

  if (mindif>0)
  { // black (low value) scratches

    for (h = 0; h < height; h+=1)
	{
		switch (maxwidth)
		{
			case 1:// added in v 0.9
			  for (row = 0; row < 2; row += 1)
      			 d[row] = SD_NULL;
			  for (row = 2; row < row_size-2; row += 1)
			  {    // middle rows
      				if ( (s[row-1]-s[row] > mindif) && (s[row+1]-s[row] > mindif)
     					&& (abs(s[row-2]-s[row+2]) <= asym)  // added in v.0.7
     					&& (s[row-1]-s[row]+s[row+1]-s[row] > s[row-2]-s[row-1]+s[row+2]-s[row+1]) ) // changed v1.0
      					d[row] = SD_EXTREM;  // sharp extremum found
      				else
						d[row] = SD_NULL;
			  }
			  for (row = row_size-2; row < row_size; row += 1)
      			 d[row] = SD_NULL;
			  break;

			case 3:
			default:
			  for (row = 0; row < 3; row += 1)
      			 d[row] = SD_NULL;
			  for (row = 3; row < row_size-3; row += 1)
			  {    // middle rows
      				if ( (s[row-2]-s[row] > mindif) && (s[row+2]-s[row] > mindif)
     					&& (abs(s[row-3]-s[row+3]) <= asym)  // added in v.0.7
     					&& (s[row-2]-s[row-1]+s[row+2]-s[row+1] > s[row-3]-s[row-2]+s[row+3]-s[row+2]) ) // changed v1.0
      					d[row] = SD_EXTREM;  // sharp extremum found
      				else
						d[row] = SD_NULL;
			  }
			  for (row = row_size-3; row < row_size; row += 1)
      			 d[row] = SD_NULL;
			  break;

			case 5: // v1.0
			  for (row = 0; row < 4; row += 1)
      			 d[row] = SD_NULL;
			  for (row = 4; row < row_size-4; row += 1)
			  {    // middle rows
      				if ( (s[row-3]-s[row] > mindif) && (s[row+3]-s[row] > mindif)
     					&& (abs(s[row-4]-s[row+4]) <= asym)
     					&& (s[row-3]-s[row-2]+s[row+3]-s[row+2] > s[row-4]-s[row-3]+s[row+4]-s[row+3]) )
      					d[row] = SD_EXTREM;  // sharp extremum found
      				else
						d[row] = SD_NULL;
			  }
			  for (row = row_size-4; row < row_size; row += 1)
      			 d[row] = SD_NULL;
			  break;

			case 7: // v1.0
			  for (row = 0; row < 5; row += 1)
      			 d[row] = SD_NULL;
			  for (row = 5; row < row_size-5; row += 1)
			  {    // middle rows
      				if ( (s[row-4]-s[row] > mindif) && (s[row+4]-s[row] > mindif)
     					&& (abs(s[row-5]-s[row+5]) <= asym)
     					&& (s[row-4]-s[row-3]+s[row+4]-s[row+3] > s[row-5]-s[row-4]+s[row+5]-s[row+4]) )
      					d[row] = SD_EXTREM;  // sharp extremum found
      				else
						d[row] = SD_NULL;
			  }
			  for (row = row_size-5; row < row_size; row += 1)
      			 d[row] = SD_NULL;
			  break;

			case 9: // v1.0
			  for (row = 0; row < 6; row += 1)
      			 d[row] = SD_NULL;
			  for (row = 6; row < row_size-6; row += 1)
			  {    // middle rows
      				if ( (s[row-5]-s[row] > mindif) && (s[row+5]-s[row] > mindif)
     					&& (abs(s[row-6]-s[row+6]) <= asym)
     					&& (s[row-5]-s[row-4]+s[row+5]-s[row+4] > s[row-6]-s[row-5]+s[row+6]-s[row+5]) )
      					d[row] = SD_EXTREM;  // sharp extremum found
      				else
						d[row] = SD_NULL;
			  }
			  for (row = row_size-6; row < row_size; row += 1)
      			 d[row] = SD_NULL;
			  break;

			case 11: // v1.0
			  for (row = 0; row < 7; row += 1)
      			 d[row] = SD_NULL;
			  for (row = 7; row < row_size-7; row += 1)
			  {    // middle rows
      				if ( (s[row-6]-s[row] > mindif) && (s[row+6]-s[row] > mindif)
     					&& (abs(s[row-7]-s[row+7]) <= asym)
     					&& (s[row-6]-s[row-5]+s[row+6]-s[row+5] > s[row-7]-s[row-6]+s[row+7]-s[row+6]) )
      					d[row] = SD_EXTREM;  // sharp extremum found
      				else
						d[row] = SD_NULL;
			  }
			  for (row = row_size-7; row < row_size; row += 1)
      			 d[row] = SD_NULL;
			  break;

			case 13: // v1.0
			  for (row = 0; row < 8; row += 1)
      			 d[row] = SD_NULL;
			  for (row = 8; row < row_size-8; row += 1)
			  {    // middle rows
      				if ( (s[row-7]-s[row] > mindif) && (s[row+7]-s[row] > mindif)
     					&& (abs(s[row-8]-s[row+8]) <= asym)
     					&& (s[row-7]-s[row-6]+s[row+7]-s[row+6] > s[row-8]-s[row-7]+s[row+8]-s[row+7]) )
      					d[row] = SD_EXTREM;  // sharp extremum found
      				else
						d[row] = SD_NULL;
			  }
			  for (row = row_size-8; row < row_size; row += 1)
      			 d[row] = SD_NULL;

			  break;

			case 15: // v1.0
			  for (row = 0; row < 9; row += 1)
      			 d[row] = SD_NULL;
			  for (row = 9; row < row_size-9; row += 1)
			  {    // middle rows
      				if ( (s[row-8]-s[row] > mindif) && (s[row+8]-s[row] > mindif)
     					&& (abs(s[row-9]-s[row+9]) <= asym)
     					&& (s[row-8]-s[row-7]+s[row+8]-s[row+7] > s[row-9]-s[row-8]+s[row+9]-s[row+8]) )
      					d[row] = SD_EXTREM;  // sharp extremum found
      				else
						d[row] = SD_NULL;
			  }
			  for (row = row_size-9; row < row_size; row += 1)
      			 d[row] = SD_NULL;

		}

		s += src_pitch;
		d += row_size;
    }

  }
  else
  {    // white (high value) scratches

    for (h = 0; h < height; h+=1)
	{
		switch (maxwidth)
		{
			case 1: // added in v 0.9
				for (row = 0; row < 2; row += 1)
      				d[row] = SD_NULL;
				for (row = 2; row < row_size-2; row += 1)
				{    // middle rows
      				if ( (s[row-1]-s[row] < mindif) && (s[row+1]-s[row] < mindif)
     					&& (abs(s[row-2]-s[row+2]) <= asym)  // added in v.0.7
      					&& (s[row-1]-s[row]+s[row+1]-s[row] < s[row-2]-s[row-1]+s[row+2]-s[row+1]))
      					d[row] = SD_EXTREM;    // sharp extremum found
      				else
						d[row] = SD_NULL;
				}
				for (row = row_size-2; row < row_size; row += 1)
      				d[row] = SD_NULL;
				break;

			case 3:
			default:
				for (row = 0; row < 3; row += 1)
      				d[row] = SD_NULL;
				for (row = 3; row < row_size-3; row += 1)
				{    // middle rows
      				if ( (s[row-2]-s[row] < mindif) && (s[row+2]-s[row] < mindif)
     					&& (abs(s[row-3]-s[row+3]) <= asym)  // added in v.0.7
      					&& (s[row-2]-s[row-1]+s[row+2]-s[row+1] < s[row-3]-s[row-2]+s[row+3]-s[row+2]))
      					d[row] = SD_EXTREM;    // sharp extremum found
      				else
						d[row] = SD_NULL;
				}
				for (row = row_size-3; row < row_size; row += 1)
      				d[row] = SD_NULL;
				break;

			case 5: // v1.0
				for (row = 0; row < 4; row += 1)
      				d[row] = SD_NULL;
				for (row = 4; row < row_size-4; row += 1)
				{    // middle rows
      				if ( (s[row-3]-s[row] < mindif) && (s[row+3]-s[row] < mindif)
     					&& (abs(s[row-4]-s[row+4]) <= asym)  // added in v.0.7
      					&& (s[row-3]-s[row-2]+s[row+3]-s[row+2] < s[row-4]-s[row-3]+s[row+4]-s[row+3]))
      					d[row] = SD_EXTREM;    // sharp extremum found
      				else
						d[row] = SD_NULL;
				}
				for (row = row_size-4; row < row_size; row += 1)
      				d[row] = SD_NULL;
				break;

			case 7:
				for (row = 0; row < 5; row += 1)
      				d[row] = SD_NULL;
				for (row = 5; row < row_size-5; row += 1)
				{    // middle rows
      				if ( (s[row-4]-s[row] < mindif) && (s[row+4]-s[row] < mindif)
     					&& (abs(s[row-5]-s[row+5]) <= asym)
      					&& (s[row-4]-s[row-3]+s[row+4]-s[row+3] < s[row-5]-s[row-4]+s[row+5]-s[row+4]))
      					d[row] = SD_EXTREM;    // sharp extremum found
      				else
						d[row] = SD_NULL;
				}
				for (row = row_size-5; row < row_size; row += 1)
      				d[row] = SD_NULL;
				break;

			case 9:
				for (row = 0; row < 6; row += 1)
      				d[row] = SD_NULL;
				for (row = 6; row < row_size-6; row += 1)
				{    // middle rows
      				if ( (s[row-5]-s[row] < mindif) && (s[row+5]-s[row] < mindif)
     					&& (abs(s[row-6]-s[row+6]) <= asym)
      					&& (s[row-5]-s[row-4]+s[row+5]-s[row+4] < s[row-6]-s[row-5]+s[row+6]-s[row+5]))
      					d[row] = SD_EXTREM;    // sharp extremum found
      				else
						d[row] = SD_NULL;
				}
				for (row = row_size-6; row < row_size; row += 1)
      				d[row] = SD_NULL;
				break;

			case 11:
				for (row = 0; row < 7; row += 1)
      				d[row] = SD_NULL;
				for (row = 7; row < row_size-7; row += 1)
				{    // middle rows
      				if ( (s[row-6]-s[row] < mindif) && (s[row+6]-s[row] < mindif)
     					&& (abs(s[row-7]-s[row+7]) <= asym)
      					&& (s[row-6]-s[row-5]+s[row+6]-s[row+5] < s[row-7]-s[row-6]+s[row+7]-s[row+6]))
      					d[row] = SD_EXTREM;    // sharp extremum found
      				else
						d[row] = SD_NULL;
				}
				for (row = row_size-7; row < row_size; row += 1)
      				d[row] = SD_NULL;
				break;

			case 13:
				for (row = 0; row < 8; row += 1)
      				d[row] = SD_NULL;
				for (row = 8; row < row_size-8; row += 1)
				{    // middle rows
      				if ( (s[row-7]-s[row] < mindif) && (s[row+7]-s[row] < mindif)
     					&& (abs(s[row-8]-s[row+8]) <= asym)
      					&& (s[row-7]-s[row-6]+s[row+7]-s[row+6] < s[row-8]-s[row-7]+s[row+8]-s[row+7]))
      					d[row] = SD_EXTREM;    // sharp extremum found
      				else
						d[row] = SD_NULL;
				}
				for (row = row_size-8; row < row_size; row += 1)
      				d[row] = SD_NULL;
				break;

			case 15:
				for (row = 0; row < 9; row += 1)
      				d[row] = SD_NULL;
				for (row = 9; row < row_size-9; row += 1)
				{    // middle rows
      				if ( (s[row-8]-s[row] < mindif) && (s[row+8]-s[row] < mindif)
     					&& (abs(s[row-9]-s[row+9]) <= asym)
      					&& (s[row-8]-s[row-7]+s[row+8]-s[row+7] < s[row-9]-s[row-8]+s[row+9]-s[row+8]))
      					d[row] = SD_EXTREM;    // sharp extremum found
      				else
						d[row] = SD_NULL;
				}
				for (row = row_size-9; row < row_size; row += 1)
      				d[row] = SD_NULL;
		}
      s += src_pitch;
      d += row_size;
    }
  }

}
//
void  remove_min_extrems_plane(const BYTE * s, int src_pitch, int row_size, int height, BYTE *scratchdata, int mindif, int asym, int minwidth)
{
  BYTE *d;
  d = scratchdata;
  int row, h;
  int removewidth;

  if (minwidth <= 1)
    return;
  else
    removewidth = minwidth - 2;


  if (mindif>0)
  { // black (low value) scratches

    for (h = 0; h < height; h+=1)
	{
		switch (removewidth)
		{
		    case 0:
			default:
			break;

			case 1:
			  for (row = 2; row < row_size-2; row += 1)
			  {    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-1]-s[row] > mindif) && (s[row+1]-s[row] > mindif)
     					&& (abs(s[row-2]-s[row+2]) <= asym)  // added in v.0.7
     					&& (s[row-1]-s[row]+s[row+1]-s[row] > s[row-2]-s[row-1]+s[row+2]-s[row+1]) ) // changed v1.0
						d[row] = SD_NULL;
			  }
			  break;

			case 3:
			  for (row = 3; row < row_size-3; row += 1)
			  {    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-2]-s[row] > mindif) && (s[row+2]-s[row] > mindif)
     					&& (abs(s[row-3]-s[row+3]) <= asym)  // added in v.0.7
     					&& (s[row-2]-s[row-1]+s[row+2]-s[row+1] > s[row-3]-s[row-2]+s[row+3]-s[row+2]) ) // changed v1.0
						d[row] = SD_NULL;
			  }
			  break;

			case 5:
			  for (row = 4; row < row_size-4; row += 1)
			  {    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-3]-s[row] > mindif) && (s[row+3]-s[row] > mindif)
     					&& (abs(s[row-4]-s[row+4]) <= asym)
     					&& (s[row-3]-s[row-2]+s[row+3]-s[row+2] > s[row-4]-s[row-3]+s[row+4]-s[row+3]) )
						d[row] = SD_NULL;
			  }
			  break;

			case 7: // v1.0
			  for (row = 5; row < row_size-5; row += 1)
			  {    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-4]-s[row] > mindif) && (s[row+4]-s[row] > mindif)
     					&& (abs(s[row-5]-s[row+5]) <= asym)
     					&& (s[row-4]-s[row-3]+s[row+4]-s[row+3] > s[row-5]-s[row-4]+s[row+5]-s[row+4]) )
						d[row] = SD_NULL;
			  }
			  break;

			case 9: // v1.0
			  for (row = 6; row < row_size-6; row += 1)
			  {    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-5]-s[row] > mindif) && (s[row+5]-s[row] > mindif)
     					&& (abs(s[row-6]-s[row+6]) <= asym)
     					&& (s[row-5]-s[row-4]+s[row+5]-s[row+4] > s[row-6]-s[row-5]+s[row+6]-s[row+5]) )
						d[row] = SD_NULL;
			  }
			  break;

			case 11: // v1.0
			  for (row = 7; row < row_size-7; row += 1)
			  {    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-6]-s[row] > mindif) && (s[row+6]-s[row] > mindif)
     					&& (abs(s[row-7]-s[row+7]) <= asym)
     					&& (s[row-6]-s[row-5]+s[row+6]-s[row+5] > s[row-7]-s[row-6]+s[row+7]-s[row+6]) )
						d[row] = SD_NULL;
			  }
			  break;

			case 13: // v1.0
			  for (row = 8; row < row_size-8; row += 1)
			  {    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-7]-s[row] > mindif) && (s[row+7]-s[row] > mindif)
     					&& (abs(s[row-8]-s[row+8]) <= asym)
     					&& (s[row-7]-s[row-6]+s[row+7]-s[row+6] > s[row-8]-s[row-7]+s[row+8]-s[row+7]) )
						d[row] = SD_NULL;
			  }
			  break;

			case 15: // v1.0
			  for (row = 9; row < row_size-9; row += 1)
			  {    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-8]-s[row] > mindif) && (s[row+8]-s[row] > mindif)
     					&& (abs(s[row-9]-s[row+9]) <= asym)
     					&& (s[row-8]-s[row-7]+s[row+8]-s[row+7] > s[row-9]-s[row-8]+s[row+9]-s[row+8]) )
						d[row] = SD_NULL;
			  }

		}

		s += src_pitch;
		d += row_size;
    }

  }
  else
  {    // white (high value) scratches

    for (h = 0; h < height; h+=1)
	{
		switch (removewidth)
		{
			case 0:
			default:
			break;

			case 1:
				for (row = 2; row < row_size-2; row += 1)
				{    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-1]-s[row] < mindif) && (s[row+1]-s[row] < mindif)
     					&& (abs(s[row-2]-s[row+2]) <= asym)  // added in v.0.7
      					&& (s[row-1]-s[row]+s[row+1]-s[row] < s[row-2]-s[row-1]+s[row+2]-s[row+1]))
						d[row] = SD_NULL;
				}
			break;

			case 3:
				for (row = 3; row < row_size-3; row += 1)
				{    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-2]-s[row] < mindif) && (s[row+2]-s[row] < mindif)
     					&& (abs(s[row-3]-s[row+3]) <= asym)  // added in v.0.7
      					&& (s[row-2]-s[row-1]+s[row+2]-s[row+1] < s[row-3]-s[row-2]+s[row+3]-s[row+2]))
						d[row] = SD_NULL;
				}
			break;


			case 5: // v1.0
				for (row = 4; row < row_size-4; row += 1)
				{    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-3]-s[row] < mindif) && (s[row+3]-s[row] < mindif)
     					&& (abs(s[row-4]-s[row+4]) <= asym)  // added in v.0.7
      					&& (s[row-3]-s[row-2]+s[row+3]-s[row+2] < s[row-4]-s[row-3]+s[row+4]-s[row+3]))
						d[row] = SD_NULL;
				}
				break;

			case 7:
				for (row = 5; row < row_size-5; row += 1)
				{    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-4]-s[row] < mindif) && (s[row+4]-s[row] < mindif)
     					&& (abs(s[row-5]-s[row+5]) <= asym)
      					&& (s[row-4]-s[row-3]+s[row+4]-s[row+3] < s[row-5]-s[row-4]+s[row+5]-s[row+4]))
						d[row] = SD_NULL;
				}
				break;

			case 9:
				for (row = 6; row < row_size-6; row += 1)
				{    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-5]-s[row] < mindif) && (s[row+5]-s[row] < mindif)
     					&& (abs(s[row-6]-s[row+6]) <= asym)
      					&& (s[row-5]-s[row-4]+s[row+5]-s[row+4] < s[row-6]-s[row-5]+s[row+6]-s[row+5]))
						d[row] = SD_NULL;
				}
				break;

			case 11:
				for (row = 7; row < row_size-7; row += 1)
				{    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-6]-s[row] < mindif) && (s[row+6]-s[row] < mindif)
     					&& (abs(s[row-7]-s[row+7]) <= asym)
      					&& (s[row-6]-s[row-5]+s[row+6]-s[row+5] < s[row-7]-s[row-6]+s[row+7]-s[row+6]))
						d[row] = SD_NULL;
				}
				break;

			case 13:
				for (row = 8; row < row_size-8; row += 1)
				{    // middle rows
      				if ( d[row] == SD_EXTREM && (s[row-7]-s[row] < mindif) && (s[row+7]-s[row] < mindif)
     					&& (abs(s[row-8]-s[row+8]) <= asym)
      					&& (s[row-7]-s[row-6]+s[row+7]-s[row+6] < s[row-8]-s[row-7]+s[row+8]-s[row+7]))
						d[row] = SD_NULL;
				}
		}
      s += src_pitch;
      d += row_size;
    }
  }

}


//
//
void  close_gaps(BYTE *scratchdata, int rows, int height, int maxgap)
{
  BYTE *d;
  d = scratchdata;    // copy pointer
  int r, h, j;
  long rh;

  for (h = maxgap; h < height; h+=1)
  {
     for (r = 0; r < rows; r += 1)
	 {
     	rh = r+h*rows;
        if (d[rh] == SD_EXTREM)
		{       // found first point of candidate
        	for (j=1; j<maxgap; j+=1)
			{
        		d[rh-j*rows] = SD_EXTREM;    // expand to previous lines in range
        	}
     	}
     }
   }
}
//
//
//
void  test_scratches(BYTE *scratchdata, int rows, int height, int maxwidth,int minlens, int maxlens, float maxangle)
{
  BYTE *d;
  d = scratchdata;    // copy pointer
  int r, h;
  long rh, rhc, rhcnew;
  int nrow, ntotal;
  int len;
  int maskold, masknew, pass;

  len = 0;
  rhcnew = 0;



  for (h = 0; h < height; h+=1)
  {
     for (r = 2; r < rows-2; r += 1)
	 {
        rh=r+h*rows;
        if (d[rh]==SD_EXTREM)
		{       // found first point of candidate

      	    for (pass =1; pass <=2; pass +=1)
			{	// two passes

      	        if ( pass==1)
				{             // First pass - test
      	   			maskold = SD_EXTREM;
      	   			masknew = SD_TESTED;
      	        }
      	        else
				{                      // Second pass - decision
      	   			maskold = SD_TESTED;  // repeat last cycle, but convert SD_TESTED to masknew
      	          if (len>=minlens && len<=maxlens)
      	             { masknew = SD_GOOD; }  // Good scratch found!
      	          else { masknew = SD_REJECT; }   // Bad scratch, reject
      	        }

      	   		ntotal=0;              // total number of good points in candidate
      	   		rhc = rh+1;             // centered to scratch for maxwidth=3

      	   	  for (len=0; len< height-h; len+=1)
			  {     // cycle along scratch
      	   	         nrow =0;  // number good points in row
					 if (maxwidth >= 3) // added in v.0.9
					 {
      	   				 if(d[rhc-2] ==maskold)
						 {
      	   					  d[rhc-2]=masknew;
      	   					  rhcnew = rhc-2;
      	   					  nrow = nrow+1;
						 }
      	   				 if(d[rhc+2] ==maskold)
						 {
      	   					  d[rhc+2]=masknew;
      	   					  rhcnew = rhc+2;
      	   					  nrow = nrow+1;
						 }
					 }
      	   	         if(d[rhc-1] ==maskold)
					 {
      	   	              d[rhc-1]=masknew;
      	   	              rhcnew = rhc-1;
      	   	              nrow = nrow+1;
					 }
      	   	         if(d[rhc+1] ==maskold)
					 {
      	   	              d[rhc+1]=masknew;
      	   	              rhcnew = rhc+1;
      	   	              nrow = nrow+1;
					 }
      	   	         if(d[rhc] ==maskold)
					 {
      	   	              d[rhc]=masknew;
      	   	              rhcnew = rhc;
      	   	              nrow = nrow+1;
					 }
      	   	         // end of points tests, check result for row:
      	   	         if ( (nrow>0) && (maxwidth+len*maxangle/57 > abs(rhcnew%rows-r)) )// check gap, and angle
					 {
      	   	             ntotal=ntotal+nrow;
      	   	             rhc = rhcnew+rows;           // new center for next row test
      	   	         }
					 else
					 {    // if no points or big angle, it is end of scratch, break  cycle
      	   	             break;
					 }
			  } // end for len
      	     }	// end for pass
      	 } // end if d
      }	//end for r
   }  //end for h

}


//
//
//
void  mark_scratches_plane(BYTE * dest_data, int dest_pitch, int row_size, int height, BYTE *scratchdata, int mask, int value)
{
 int row, h;

    for (h = 0; h < height; h+=1)
	{
      for (row = 0; row < row_size; row += 1)
	  {
      	   if (scratchdata[row]==mask) dest_data[row] = value;
      }
      dest_data += dest_pitch;
      scratchdata += row_size;
    }
}

//
//
//
void remove_scratches_plane(const BYTE * src_data, int src_pitch, BYTE * dest_data, int dest_pitch,
	  const BYTE * blured_data, int blured_pitch, int row_size, int height, BYTE *scratchdata,
	  int mindif1, int maxwidth, int keep100, int border)
{
  BYTE *d;
  d = scratchdata;
//  int h, row;
//  int background,blured_background;
  int i;
  int rad = maxwidth/2;  // 3/2=1
  int left, rowc;


  int keep256 = (keep100*256)/100; // to norm 256

  int div2rad2 = (256*256)/(2*rad+2); // to div by 2*rad+2, replace division by mult and shift


    for (int h=0; h<height; h+=1)
	{
		left = 0; // v.0.9.1
       for (int row=rad+border+2; row<row_size-rad-border-2; row+=1)
	   {

       	   if (!!(d[row]&SD_GOOD) && !(d[row-1]&SD_GOOD) )         // the scratch left
       	       left = row;                                           // memo
       	   if (left!=0 && !!(d[row]&SD_GOOD) && !(d[row+1]&SD_GOOD) )
		   {        // the scratch right
       	   	rowc = (left+row)/2;                                // the scratch center

       	           for (i=-rad; i<=rad; i +=1)
				   {          // in scratch
					int newdata1 = ((keep256*(src_data[rowc+i] + blured_data[rowc-rad-border-1] - blured_data[rowc+i])) + (256-keep256)*src_data[rowc-rad-border-1])/256;
					int newdata2 = ((keep256*(src_data[rowc+i] + blured_data[rowc+rad+border+1] - blured_data[rowc+i])) + (256-keep256)*src_data[rowc+rad+border+1])/256;
					int newdata = ((newdata1*(rad-i+1) + newdata2*(rad+i+1))*div2rad2)/(256*256); // weighted left and right - v1.1
       	           	 dest_data[rowc+i] = std::min(255,std::max(0, newdata)); // clipped in v.0.6
       	           }
       	           for (i=-rad-border; i<-rad; i +=1)
				   {         // at left border
					int newdata = src_data[rowc+i] + blured_data[rowc-rad-border-1] - blured_data[rowc+i]; // fix v.0.9.1
					newdata = (keep256*newdata + (256-keep256)*src_data[rowc-rad-border-1])/256; //v1.1
       	           	 dest_data[rowc+i] = std::min(255,std::max(0,newdata)); // clipped in v.0.6
       	           }
       	           for (i=rad+1; i<=rad+border; i +=1)
				   {         // at right border
					int newdata = src_data[rowc+i] + blured_data[rowc+rad+border+1] - blured_data[rowc+i]; // fix v.0.9.1
					newdata = (keep256*newdata + (256-keep256)*src_data[rowc+rad+border+1])/256; // v.1.1
       	           	 dest_data[rowc+i] = std::min(255,std::max(0,newdata)); // clipped in v.0.6
       	           }
			left=0; // v.0.9.1
       	   }
       }
       src_data += src_pitch;
       dest_data += dest_pitch;
       blured_data += blured_pitch;
       d += row_size;
    }

}

void DeScratch::DeScratch_pass (const BYTE * srcp, int src_pitch, const BYTE * bluredp,int blured_pitch,
			BYTE * destp, int dest_pitch, int row_sizep, int heightp,  int hscale, int mindifp, int asym)
{
//		pass for current plane and current sign
  int markvalue;

    if (row_sizep < maxwidth +3)
    return;  //v.1.0

	get_extrems_plane(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym, maxwidth);
	if (minwidth>1) remove_min_extrems_plane(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym, minwidth);
	close_gaps(scratchdata, row_sizep, heightp, maxgap/hscale);
	test_scratches(scratchdata, row_sizep, heightp, maxwidth, minlen/hscale, maxlen/hscale, maxangle);

	if (mark)
	{
       if (mindifp >0) markvalue=0;
       else markvalue=255;
	   mark_scratches_plane(destp, dest_pitch, row_sizep, heightp, scratchdata, SD_GOOD, markvalue);
       markvalue=127;
       mark_scratches_plane(destp, dest_pitch, row_sizep, heightp, scratchdata, SD_REJECT, markvalue);
	}
	else
	{
		remove_scratches_plane(srcp, src_pitch,	destp, dest_pitch, bluredp, blured_pitch,
			row_sizep, heightp,	scratchdata,  mindifp, maxwidth, keep, border);
	}
}

//
//  Frame computing function *************************************************************
//

PVideoFrame __stdcall DeScratch::GetFrame (int ndest, IScriptEnvironment* env)
{
   // Request frame 'n' from the child (source) clip.
	PVideoFrame src = child->GetFrame(ndest, env);
	PVideoFrame dest = env->NewVideoFrame(vi);
//	PVideoFrame tmp = env->NewVideoFrame(vi);

	//   Get  blured frame
	 PVideoFrame blured = blured_clip->GetFrame(ndest, env);

	 int sign;
	int plane;

	plane = PLANAR_Y;
	const BYTE * bluredp = blured->GetReadPtr(plane);
	int blured_pitch = blured->GetPitch(plane);
    BYTE * destp = dest->GetWritePtr(plane);
    int dest_pitch = dest->GetPitch(plane);
    const BYTE * srcp = src->GetReadPtr(plane);
    int src_pitch = src->GetPitch(plane);
    int row_size = src->GetRowSize(plane);
    int heightp = src->GetHeight(plane);
	int wleftp=wleft*row_size/width;
	int wrightp=wright*row_size/width;

	// remove scratches  for every plane and sign independently
	if (modeY == MODE_ALL)
	{
		env->BitBlt(buf, buf_pitch, srcp, src_pitch, row_size, heightp);
		DeScratch_pass (srcp+wleftp, src_pitch, bluredp+wleftp, blured_pitch, buf+wleftp, buf_pitch, wrightp-wleftp, heightp, height/heightp, mindif, asym);
		env->BitBlt( destp, dest_pitch, buf, buf_pitch, row_size, heightp);
		DeScratch_pass (buf+wleftp, buf_pitch, bluredp+wleftp, blured_pitch, destp+wleftp, dest_pitch, wrightp-wleftp, heightp, (height/heightp), (-mindif), asym);
	}
	else
	{
		env->BitBlt(destp, dest_pitch, srcp, src_pitch, row_size, heightp);
		if (modeY == MODE_LOW || modeY == MODE_HIGH)
		{
			sign = (modeY == MODE_LOW) ? 1 : -1;
			DeScratch_pass (srcp+wleftp, src_pitch, bluredp+wleftp, blured_pitch, destp+wleftp, dest_pitch, wrightp-wleftp, heightp, height/heightp, sign*mindif, asym);
		}
	}


	plane = PLANAR_U;
	bluredp = blured->GetReadPtr(plane);
	blured_pitch = blured->GetPitch(plane);
    destp = dest->GetWritePtr(plane);
    dest_pitch = dest->GetPitch(plane);
    srcp = src->GetReadPtr(plane);
    src_pitch = src->GetPitch(plane);
    row_size = src->GetRowSize(plane);
    heightp = src->GetHeight(plane);
	wleftp=wleft*row_size/width;
	wrightp=wright*row_size/width;

	if (modeU == MODE_ALL)
	{
		env->BitBlt(buf, buf_pitch, srcp, src_pitch, row_size, heightp);
		DeScratch_pass (srcp+wleftp, src_pitch, bluredp+wleftp, blured_pitch, buf+wleftp, buf_pitch, wrightp-wleftp, heightp, height/heightp, mindifUV, asym);
		env->BitBlt( destp, dest_pitch, buf, buf_pitch, row_size, heightp);
		DeScratch_pass (buf+wleftp, buf_pitch, bluredp+wleftp, blured_pitch, destp+wleftp, dest_pitch, wrightp-wleftp, heightp, height/heightp, -mindifUV, asym);
	}
	else
	{
		env->BitBlt(destp, dest_pitch, srcp, src_pitch, row_size, heightp);
		if (modeU == MODE_LOW || modeU == MODE_HIGH)
		{
			sign = (modeU == MODE_LOW) ? 1 : -1;
			DeScratch_pass (srcp+wleftp, src_pitch, bluredp+wleftp, blured_pitch, destp+wleftp, dest_pitch, wrightp-wleftp, heightp, height/heightp, sign*mindifUV, asym);
		}
	}


	plane = PLANAR_V;
	bluredp = blured->GetReadPtr(plane);
	blured_pitch = blured->GetPitch(plane);
    destp = dest->GetWritePtr(plane);
    dest_pitch = dest->GetPitch(plane);
    srcp = src->GetReadPtr(plane);
    src_pitch = src->GetPitch(plane);
    row_size = src->GetRowSize(plane);
    heightp = src->GetHeight(plane);
	wleftp=wleft*row_size/width;
	wrightp=wright*row_size/width;

	if (modeV == MODE_ALL)
	{
		env->BitBlt(buf, buf_pitch, srcp, src_pitch, row_size, heightp);
		DeScratch_pass (srcp+wleftp, src_pitch, bluredp+wleftp, blured_pitch, buf+wleftp, buf_pitch, wrightp-wleftp, heightp, height/heightp, mindifUV, asym);
		env->BitBlt( destp, dest_pitch, buf, buf_pitch, row_size, heightp);
		DeScratch_pass (buf+wleftp, buf_pitch, bluredp+wleftp, blured_pitch, destp+wleftp, dest_pitch, wrightp-wleftp, heightp, height/heightp, -mindifUV, asym);
	}
	else
	{
		env->BitBlt(destp, dest_pitch, srcp, src_pitch, row_size, heightp);
		if (modeV == MODE_LOW || modeV == MODE_HIGH)
		{
			sign = (modeV == MODE_LOW) ? 1 : -1;
			DeScratch_pass (srcp+wleftp, src_pitch, bluredp+wleftp, blured_pitch, destp+wleftp, dest_pitch, wrightp-wleftp, heightp, height/heightp, sign*mindifUV, asym);
		}
	}

  return dest;    // return computed frame
}

//
//  Clear data on exit function *******************************************************
//

//
// This is the function that created the filter, when the filter has been called.
// This can be used for simple parameter checking, so it is possible to create different filters,
// based on the arguments recieved.

AVSValue __cdecl Create_DeScratch(AVSValue args, void* user_data, IScriptEnvironment* env) {


    return new DeScratch(args[0].AsClip(), // the 0th parameter is the source clip
		 args[1].AsInt(5), //mindif
		 args[2].AsInt(10), //asym  -  added in v.0.7
		 args[3].AsInt(2), //maxgap
		 args[4].AsInt(3), //scratch maxwidth
		 args[5].AsInt(100), //minlen
		 args[6].AsInt(2048), //maxlen
		 args[7].AsFloat(5.0), //maxangle
		 args[8].AsInt(15), //blurlen
		 args[9].AsInt(100), //keep
		 args[10].AsInt(2), //border
		 args[11].AsInt(1), //modeY
		 args[12].AsInt(0), //modeU
		 args[13].AsInt(0), //modeV
		 args[14].AsInt(0), //mindifUV
		 args[15].AsBool(false), //mark
		 args[16].AsInt(1), //mindwidth
		 args[17].AsInt(0), // window left (inclusive) - v.1.0
		 args[18].AsInt(4096), // window right (exclusive) - v.1.0
		 env);
    // Calls the constructor with the arguments provied.
}


// The following function is the function that actually registers the filter in AviSynth
// It is called automatically, when the plugin is loaded to see which functions this filter contains.

const AVS_Linkage *AVS_linkage = 0;
extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;
    env->AddFunction("descratch", "c[mindif]i[asym]i[maxgap]i[maxwidth]i[minlen]i[maxlen]i[maxangle]f[blurlen]i[keep]i[border]i[modeY]i[modeU]i[modeV]i[mindifUV]i[mark]b[minwidth]i[left]i[right]i", Create_DeScratch, 0);
    // The AddFunction has the following paramters:
    // AddFunction(Filtername , Arguments, Function to call,0);

    // Arguments is a string that defines the types and optional names of the arguments for you filter.
    // c - Video Clip
    // i - Integer number
    // f - Float number
    // s - String
    // b - boolean

	 // The word inside the [ ] lets you used named parameters in your script

    return "DeScratch";
    // A freeform name of the plugin.
}

