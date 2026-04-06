DeScratch - Scratch Removing Filter
===================================

Plugin for VapourSynth and Avisynth+
Copyright (C)2003-2016 Alexander G. Balakhnin aka Fizick (http://avisynth.org.ru)
Modernization and VapourSynth support by Fredrik Mellbin

Purpose
-------
This plugin removes vertical scratches from films. Also it can be used for removing horizontal noise lines such as drop-outs from analog VHS captures (after image rotation).
How it works
The plugin firstly detects scratches, then removes them.
It uses spatial information only from the current frame.
I created it for restoration of my old 8 mm films, maybe it will be useful to somebody else.

Scratch detection
-----------------
Apply some vertical blur to frame copy, for suppression of image thin structure, inclined lines and noise.
Search for local extremes of luma in every row with luma difference criterion for scratches with width not above a max.
Put these extremes in some map (frame). Search and deactivate extrems for width below a min (optional).
Optionally close vertical gaps in extrems by vertical expanding of extreme points.
Test the extremes map with length and angle criterions, so select real long scratches only.

Scratch removal
---------------
Scratches may be either partially transparent, smooth (with image details), or opaque (with no details or almost destroyed).
In the first case, plugin can subtract smooth (blurred) part of luma scratches variation from original image. Therefore, image details are kept.
In the second case, plugin replaces scratched pixels by mean luma values from some neighbours pixels (in same row).
We have also intermediate case by setting some percent of detail to keep.
In all cases, some nearest neighbours pixels may be also partially changed for smooth transition.

See full documentation in doc folder for usage examples.
