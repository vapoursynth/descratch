/*
DeScratch - Scratches Removing Filter
Plugin for Avisynth 2.5
Copyright (c)2003-2016 Alexander G. Balakhnin aka Fizick
bag@hotmail.ru
http://avisynth.org.ru

This program is FREE software under GPL licence v2.

This plugin removes vertical scratches from digitized films.
*/

#include <avisynth.h>
#include <VapourSynth4.h>
#include <VSHelper4.h>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <cassert>

constexpr BYTE SD_NULL = 0;
constexpr BYTE SD_EXTREM = 1;
constexpr BYTE SD_TESTED = 2;
constexpr BYTE SD_GOOD = 4;
constexpr BYTE SD_REJECT = 8;

constexpr int MODE_NONE = 0;
constexpr int MODE_LOW = 1;
constexpr int MODE_HIGH = 2;
constexpr int MODE_ALL = 3;

struct DeScratchShared {
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

    BYTE *scratchdata = nullptr;
    BYTE *buf = nullptr;
    int buf_pitch;
    int width;
    int height;

    void DeScratch_pass(const BYTE *VS_RESTRICT srcp, ptrdiff_t src_pitch, const BYTE *VS_RESTRICT bluredp, ptrdiff_t blured_pitch,
        BYTE *VS_RESTRICT destp, ptrdiff_t dest_pitch, int row_sizep, int heightp, int hscale, int mindifp, int asym);
    ~DeScratchShared() {
        free(scratchdata);
        free(buf);
    }
};


class DeScratch : public GenericVideoFilter, private DeScratchShared {
    PClip blured_clip;

public:
    DeScratch(PClip _child, int _mindif, int _asym, int _maxgap, int _maxwidth,
        int _minlen, int _maxlen, float _maxangle, int _blurlen, int _keep, int _border,
        int _modeY, int _modeU, int _modeV, int _mindifUV, bool _mark, int _minwidth, int _wleft, int _wright, IScriptEnvironment *env);

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
};

//Here is the acutal constructor code used
DeScratch::DeScratch(PClip _child, int _mindif, int _asym, int _maxgap, int _maxwidth,
    int _minlen, int _maxlen, float _maxangle, int _blurlen, int _keep, int _border,
    int _modeY, int _modeU, int _modeV, int _mindifUV, bool _mark, int _minwidth, int _wleft, int _wright, IScriptEnvironment *env) :
    GenericVideoFilter(_child), DeScratchShared{ _mindif, _asym , _maxgap, _maxwidth,
        _minlen, _maxlen, _maxangle, _blurlen, _keep, _border,
        _modeY, _modeU, _modeV, _mindifUV, _mark, _minwidth, _wleft, _wright } {
    if (mindif <= 0)
        env->ThrowError("Descratch: mindif must be positive!");
    if (asym < 0)
        env->ThrowError("Descratch: asym must be not negative!");
    if (mindifUV < 0)
        env->ThrowError("Descratch: mindifUV must not be negative!");
    else if (mindifUV == 0)
        mindifUV = mindif; // v.0.5
    if ((maxgap < 0) || (maxgap > 255))
        env->ThrowError("Descratch: maxgap must be >=0 and <=256!");
    if (!(maxwidth % 2) || (maxwidth < 1) || (maxwidth > 15))
        env->ThrowError("Descratch: maxwidth must be odd from 1 to 15!");
    if ((minlen <= 0))
        env->ThrowError("Descratch: minlen must be > 0!");
    if ((maxlen <= 0))
        env->ThrowError("Descratch: maxlen must be > 0!");
    if ((maxangle < 0) || (maxangle > 90))
        env->ThrowError("Descratch: maxangle must be from 0 to 90!");
    if ((blurlen < 0) || (blurlen > 200))
        env->ThrowError("Descratch: blurlen must be from 0 to 200!");
    if ((keep < 0) || (keep > 100))
        env->ThrowError("Descratch: keep must be from 0 to 100!");
    if ((border < 0) || (border > 5))
        env->ThrowError("Descratch: border must be from 0 to 5!");
    if (!vi.IsYV12())
        env->ThrowError("Descratch: Video must be YV12!");
    if (modeY < 0 || modeY>3 || modeU < 0 || modeU>3 || modeV < 0 || modeV>3)
        env->ThrowError("Descratch: modeY, modeU, modeV must be from 0 to 3!");
    if (minwidth > maxwidth)
        env->ThrowError("Descratch: minwidth must be not above maxwidth!");
    if (!(minwidth % 2) || (minwidth < 1) || (minwidth > 15))
        env->ThrowError("Descratch: minwidth must be odd from 1 to 15!");

    width = vi.width;
    height = vi.height;
    buf_pitch = width + 16 - width % 16;

    // check working window limits
    if (wleft < 0)
        wleft = 0;
    wleft = wleft - wleft % 2;
    if (wright > width)
        wright = width;
    wright = wright - wright % 2;
    if (wleft >= wright)
        env->ThrowError("Descratch: must be: left < right <= width!");

    // create temporary array for scratches data
    scratchdata = (BYTE *)malloc(vi.height * vi.width);

    int down_height = (vi.height) / (1 + blurlen);
    if (down_height % 2) down_height -= 1;
    AVSValue down_args[3] = { child, width, down_height };
    PClip down_clip = env->Invoke("BilinearResize", AVSValue(down_args, 3)).AsClip();

    AVSValue blur_args[3] = { down_clip,  width, height };
    blured_clip = env->Invoke("BicubicResize", AVSValue(blur_args, 3)).AsClip();

    buf = (BYTE *)malloc(height * buf_pitch);
}

template<int maxwidth>
static void  get_extrems_plane(const BYTE *VS_RESTRICT s, ptrdiff_t src_pitch, int row_size, int height, BYTE *VS_RESTRICT d, int mindif, int asym) {
    //d = scratchdata;

    constexpr int mw0 = (maxwidth + 1) / 2; // 7
    constexpr int mwp1 = mw0 + 1; // 8
    constexpr int mwm1 = mw0 - 1; // 6

    if (mindif > 0) { // black (low value) scratches

        for (int h = 0; h < height; h += 1) {
            for (int row = 0; row < mwp1; row += 1)
                d[row] = SD_NULL;
            for (int row = mwp1; row < row_size - mwp1; row += 1) {    // middle rows
                if ((s[row - mw0] - s[row] > mindif) && (s[row + mw0] - s[row] > mindif)
                    && (abs(s[row - mwp1] - s[row + mwp1]) <= asym)
                    && (s[row - mw0] - s[row - mwm1] + s[row + mw0] - s[row + mwm1] > s[row - mwp1] - s[row - mw0] + s[row + mwp1] - s[row + mw0]))
                    d[row] = SD_EXTREM;  // sharp extremum found
                else
                    d[row] = SD_NULL;
            }
            for (int row = row_size - mwp1; row < row_size; row += 1)
                d[row] = SD_NULL;

            s += src_pitch;
            d += row_size;
        }

    } else {    // white (high value) scratches

        for (int h = 0; h < height; h += 1) {
            for (int row = 0; row < mwp1; row += 1)
                d[row] = SD_NULL;
            for (int row = mwp1; row < row_size - mwp1; row += 1) {    // middle rows
                if ((s[row - mw0] - s[row] < mindif) && (s[row + mw0] - s[row] < mindif)
                    && (abs(s[row - mwp1] - s[row + mwp1]) <= asym)
                    && (s[row - mw0] - s[row - mwm1] + s[row + mw0] - s[row + mwm1] < s[row - mwp1] - s[row - mw0] + s[row + mwp1] - s[row + mw0]))
                    d[row] = SD_EXTREM;    // sharp extremum found
                else
                    d[row] = SD_NULL;
            }
            for (int row = row_size - mwp1; row < row_size; row += 1)
                d[row] = SD_NULL;

            s += src_pitch;
            d += row_size;
        }
    }

}

// removewidth = minwidth - 2;
template<int removewidth>
static void  remove_min_extrems_plane(const BYTE *VS_RESTRICT s, ptrdiff_t src_pitch, int row_size, int height, BYTE *VS_RESTRICT d, int mindif, int asym) {
    constexpr int rw0 = (removewidth + 1) / 2;
    constexpr int rwp1 = rw0 + 1;
    constexpr int rwm1 = rw0 - 1;

    if (mindif > 0) { // black (low value) scratches

        for (int h = 0; h < height; h += 1) {
            for (int row = rwp1; row < row_size - rwp1; row += 1) {
                if (d[row] == SD_EXTREM && (s[row - rw0] - s[row] > mindif) && (s[row + rw0] - s[row] > mindif)
                    && (abs(s[row - rwp1] - s[row + rwp1]) <= asym)
                    && (s[row - rw0] - s[row - rwm1] + s[row + rw0] - s[row + rwm1] > s[row - rwp1] - s[row - rw0] + s[row + rwp1] - s[row + rw0]))
                    d[row] = SD_NULL;
            }

            s += src_pitch;
            d += row_size;
        }

    } else {    // white (high value) scratches

        for (int h = 0; h < height; h += 1) {
            for (int row = rwp1; row < row_size - rwp1; row += 1) {
                if (d[row] == SD_EXTREM && (s[row - rw0] - s[row] < mindif) && (s[row + rw0] - s[row] < mindif)
                    && (abs(s[row - rwp1] - s[row + rwp1]) <= asym)
                    && (s[row - rw0] - s[row - rwm1] + s[row + rw0] - s[row + rwm1] < s[row - rwp1] - s[row - rw0] + s[row + rwp1] - s[row + rw0]))
                    d[row] = SD_NULL;
            }

            s += src_pitch;
            d += row_size;
        }
    }
}

static void  close_gaps(BYTE *VS_RESTRICT d, int rows, int height, int maxgap) {
    for (int h = maxgap; h < height; h++) {
        for (int r = 0; r < rows; r++) {
            int rh = r + h * rows;
            if (d[rh] == SD_EXTREM) {       // found first point of candidate
                for (int j = 1; j < maxgap; j++)
                    d[rh - j * rows] = SD_EXTREM;    // expand to previous lines in range
            }
        }
    }
}

// fixme, weird variable scoping but probably not worth the effort to improve further
static void  test_scratches(BYTE *VS_RESTRICT d, int rows, int height, int maxwidth, int minlens, int maxlens, float maxangle) {
    int rhcnew = 0;
    int len = 0;
    BYTE maskold, masknew;

    for (int h = 0; h < height; h += 1) {
        for (int r = 2; r < rows - 2; r += 1) {
            int rh = r + h * rows;
            if (d[rh] == SD_EXTREM) {       // found first point of candidate

                for (int pass = 1; pass <= 2; pass += 1) {	// two passes

                    if (pass == 1) {             // First pass - test
                        maskold = SD_EXTREM;
                        masknew = SD_TESTED;
                    } else {                      // Second pass - decision
                        maskold = SD_TESTED;  // repeat last cycle, but convert SD_TESTED to masknew
                        if (len >= minlens && len <= maxlens) {
                            masknew = SD_GOOD;
                        }  // Good scratch found!
                        else {
                            masknew = SD_REJECT;
                        }   // Bad scratch, reject
                    }

                    int rhc = rh + 1;             // centered to scratch for maxwidth=3

                    for (len = 0; len < height - h; len += 1) {     // cycle along scratch
                        int nrow = 0;  // number good points in row
                        if (maxwidth >= 3) {
                            if (d[rhc - 2] == maskold) {
                                d[rhc - 2] = masknew;
                                rhcnew = rhc - 2;
                                nrow = nrow + 1;
                            }
                            if (d[rhc + 2] == maskold) {
                                d[rhc + 2] = masknew;
                                rhcnew = rhc + 2;
                                nrow = nrow + 1;
                            }
                        }
                        if (d[rhc - 1] == maskold) {
                            d[rhc - 1] = masknew;
                            rhcnew = rhc - 1;
                            nrow = nrow + 1;
                        }
                        if (d[rhc + 1] == maskold) {
                            d[rhc + 1] = masknew;
                            rhcnew = rhc + 1;
                            nrow = nrow + 1;
                        }
                        if (d[rhc] == maskold) {
                            d[rhc] = masknew;
                            rhcnew = rhc;
                            nrow = nrow + 1;
                        }
                        // end of points tests, check result for row:
                        if ((nrow > 0) && (maxwidth + len * maxangle / 57 > abs(rhcnew % rows - r)))// check gap, and angle
                        {
                            rhc = rhcnew + rows;           // new center for next row test
                        } else {    // if no points or big angle, it is end of scratch, break  cycle
                            break;
                        }
                    }
                }
            }
        }
    }
}

static void  mark_scratches_plane(BYTE *VS_RESTRICT dest_data, ptrdiff_t dest_pitch, ptrdiff_t row_size, int height, BYTE *VS_RESTRICT scratchdata, BYTE mask, BYTE value) {
    for (int h = 0; h < height; h++) {
        for (int row = 0; row < row_size; row++) {
            if (scratchdata[row] == mask)
                dest_data[row] = value;
        }
        dest_data += dest_pitch;
        scratchdata += row_size;
    }
}

static void remove_scratches_plane(const BYTE *VS_RESTRICT src_data, ptrdiff_t src_pitch, BYTE *VS_RESTRICT dest_data, ptrdiff_t dest_pitch,
    const BYTE *VS_RESTRICT blured_data, ptrdiff_t blured_pitch, int row_size, int height, BYTE *VS_RESTRICT d,
    int mindif1, int maxwidth, int keep100, int border) {
    int rad = maxwidth / 2;  // 3/2=1
    int keep256 = (keep100 * 256) / 100; // to norm 256
    int div2rad2 = (256 * 256) / (2 * rad + 2); // to div by 2*rad+2, replace division by mult and shift

    for (int h = 0; h < height; h += 1) {
        int left = 0; // v.0.9.1
        for (int row = rad + border + 2; row < row_size - rad - border - 2; row += 1) {

            if (!!(d[row] & SD_GOOD) && !(d[row - 1] & SD_GOOD))         // the scratch left
                left = row;                                           // memo
            if (left != 0 && !!(d[row] & SD_GOOD) && !(d[row + 1] & SD_GOOD)) {        // the scratch right
                int rowc = (left + row) / 2;                                // the scratch center

                for (int i = -rad; i <= rad; i += 1) {          // in scratch
                    int newdata1 = ((keep256 * (src_data[rowc + i] + blured_data[rowc - rad - border - 1] - blured_data[rowc + i])) + (256 - keep256) * src_data[rowc - rad - border - 1]) / 256;
                    int newdata2 = ((keep256 * (src_data[rowc + i] + blured_data[rowc + rad + border + 1] - blured_data[rowc + i])) + (256 - keep256) * src_data[rowc + rad + border + 1]) / 256;
                    int newdata = ((newdata1 * (rad - i + 1) + newdata2 * (rad + i + 1)) * div2rad2) / (256 * 256); // weighted left and right
                    dest_data[rowc + i] = std::min(255, std::max(0, newdata));
                }
                for (int i = -rad - border; i < -rad; i += 1) {         // at left border
                    int newdata = src_data[rowc + i] + blured_data[rowc - rad - border - 1] - blured_data[rowc + i];
                    newdata = (keep256 * newdata + (256 - keep256) * src_data[rowc - rad - border - 1]) / 256;
                    dest_data[rowc + i] = std::min(255, std::max(0, newdata));
                }
                for (int i = rad + 1; i <= rad + border; i += 1) {         // at right border
                    int newdata = src_data[rowc + i] + blured_data[rowc + rad + border + 1] - blured_data[rowc + i];
                    newdata = (keep256 * newdata + (256 - keep256) * src_data[rowc + rad + border + 1]) / 256;
                    dest_data[rowc + i] = std::min(255, std::max(0, newdata));
                }
                left = 0;
            }
        }
        src_data += src_pitch;
        dest_data += dest_pitch;
        blured_data += blured_pitch;
        d += row_size;
    }

}

void DeScratchShared::DeScratch_pass(const BYTE *VS_RESTRICT srcp, ptrdiff_t src_pitch, const BYTE *VS_RESTRICT bluredp, ptrdiff_t blured_pitch,
    BYTE *VS_RESTRICT destp, ptrdiff_t dest_pitch, int row_sizep, int heightp, int hscale, int mindifp, int asym) {

    if (row_sizep < maxwidth + 3)
        return;

    switch (maxwidth) {
    case 1: get_extrems_plane<1>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
    case 3: get_extrems_plane<3>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
    case 5: get_extrems_plane<5>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
    case 7: get_extrems_plane<7>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
    case 9: get_extrems_plane<9>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
    case 11: get_extrems_plane<11>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
    case 13: get_extrems_plane<13>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
    case 15: get_extrems_plane<15>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
    default:
        assert(false);
    }

    if (minwidth > 1) {
        int removewidth = minwidth - 2;
        switch (removewidth) {
        case 1: remove_min_extrems_plane<1>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
        case 3: remove_min_extrems_plane<3>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
        case 5: remove_min_extrems_plane<5>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
        case 7: remove_min_extrems_plane<7>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
        case 9: remove_min_extrems_plane<9>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
        case 11: remove_min_extrems_plane<11>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
        case 13: remove_min_extrems_plane<13>(bluredp, blured_pitch, row_sizep, heightp, scratchdata, mindifp, asym); break;
        default:
            assert(false);
        }
    }
    close_gaps(scratchdata, row_sizep, heightp, maxgap / hscale);
    test_scratches(scratchdata, row_sizep, heightp, maxwidth, minlen / hscale, maxlen / hscale, maxangle);

    if (mark) {
        mark_scratches_plane(destp, dest_pitch, row_sizep, heightp, scratchdata, SD_GOOD, (mindifp > 0) ? 0 : 255);
        mark_scratches_plane(destp, dest_pitch, row_sizep, heightp, scratchdata, SD_REJECT, 127);
    } else {
        remove_scratches_plane(srcp, src_pitch, destp, dest_pitch, bluredp, blured_pitch,
            row_sizep, heightp, scratchdata, mindifp, maxwidth, keep, border);
    }
}

PVideoFrame __stdcall DeScratch::GetFrame(int ndest, IScriptEnvironment *env) {
    PVideoFrame src = child->GetFrame(ndest, env);
    PVideoFrame blured = blured_clip->GetFrame(ndest, env);
    PVideoFrame dest = env->NewVideoFrame(vi);

    auto ProcessPlane = [&src, &blured, &dest, this, env](int plane, int mode, int mindif) {
        const BYTE *bluredp = blured->GetReadPtr(plane);
        int blured_pitch = blured->GetPitch(plane);
        BYTE *destp = dest->GetWritePtr(plane);
        int dest_pitch = dest->GetPitch(plane);
        const BYTE *srcp = src->GetReadPtr(plane);
        int src_pitch = src->GetPitch(plane);
        int row_size = src->GetRowSize(plane);
        int heightp = src->GetHeight(plane);
        int wleftp = wleft * row_size / width;
        int wrightp = wright * row_size / width;

        if (mode == MODE_ALL) {
            env->BitBlt(buf, buf_pitch, srcp, src_pitch, row_size, heightp);
            DeScratch_pass(srcp + wleftp, src_pitch, bluredp + wleftp, blured_pitch, buf + wleftp, buf_pitch, wrightp - wleftp, heightp, height / heightp, mindif, asym);
            env->BitBlt(destp, dest_pitch, buf, buf_pitch, row_size, heightp);
            DeScratch_pass(buf + wleftp, buf_pitch, bluredp + wleftp, blured_pitch, destp + wleftp, dest_pitch, wrightp - wleftp, heightp, (height / heightp), (-mindif), asym);
        } else {
            env->BitBlt(destp, dest_pitch, srcp, src_pitch, row_size, heightp);
            if (mode == MODE_LOW || mode == MODE_HIGH) {
                int sign = (mode == MODE_LOW) ? 1 : -1;
                DeScratch_pass(srcp + wleftp, src_pitch, bluredp + wleftp, blured_pitch, destp + wleftp, dest_pitch, wrightp - wleftp, heightp, height / heightp, sign * mindif, asym);
            }
        }
        };

    ProcessPlane(PLANAR_Y, modeY, mindif);
    ProcessPlane(PLANAR_U, modeU, mindifUV);
    ProcessPlane(PLANAR_V, modeV, mindifUV);

    return dest;
}

AVSValue __cdecl Create_DeScratch(AVSValue args, void *user_data, IScriptEnvironment *env) {

    return new DeScratch(args[0].AsClip(), // the 0th parameter is the source clip
        args[1].AsInt(5), //mindif
        args[2].AsInt(10), //asym
        args[3].AsInt(2), //maxgap
        args[4].AsInt(3), //scratch maxwidth
        args[5].AsInt(100), //minlen
        args[6].AsInt(2048), //maxlen
        args[7].AsFloatf(5.0f), //maxangle
        args[8].AsInt(15), //blurlen
        args[9].AsInt(100), //keep
        args[10].AsInt(2), //border
        args[11].AsInt(1), //modeY
        args[12].AsInt(0), //modeU
        args[13].AsInt(0), //modeV
        args[14].AsInt(0), //mindifUV
        args[15].AsBool(false), //mark
        args[16].AsInt(1), //mindwidth
        args[17].AsInt(0), // window left (inclusive)
        args[18].AsInt(4096), // window right (exclusive)
        env);
}

const AVS_Linkage *AVS_linkage = 0;
extern "C" __declspec(dllexport) const char *__stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage *const vectors) {
    AVS_linkage = vectors;
    env->AddFunction("descratch", "c[mindif]i[asym]i[maxgap]i[maxwidth]i[minlen]i[maxlen]i[maxangle]f[blurlen]i[keep]i[border]i[modeY]i[modeU]i[modeV]i[mindifUV]i[mark]b[minwidth]i[left]i[right]i", Create_DeScratch, 0);
    return "DeScratch";
}

////////////////
// VapourSynth support starts here

struct DeScratchVSData : public DeScratchShared {
    VSNode *node;
    VSNode *blured_clip;
};

static const VSFrame *VS_CC deScratchGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    DeScratchVSData *d = (DeScratchVSData *)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        vsapi->requestFrameFilter(n, d->blured_clip, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrame *blured = vsapi->getFrameFilter(n, d->blured_clip, frameCtx);
        VSFrame *dest = vsapi->newVideoFrame(vsapi->getVideoFrameFormat(src), d->width, d->height, src, core);

        auto ProcessPlane = [src, blured, dest, vsapi, d](int plane, int mode, int mindif) {
            const BYTE *bluredp = vsapi->getReadPtr(blured, plane);
            ptrdiff_t blured_pitch = vsapi->getStride(blured, plane);
            BYTE *destp = vsapi->getWritePtr(dest, plane);
            ptrdiff_t dest_pitch = vsapi->getStride(dest, plane);
            const BYTE *srcp = vsapi->getReadPtr(src, plane);
            ptrdiff_t src_pitch = vsapi->getStride(src, plane);
            int row_size = vsapi->getFrameWidth(src, plane);
            int heightp = vsapi->getFrameHeight(src, plane);
            int wleftp = d->wleft * row_size / d->width;
            int wrightp = d->wright * row_size / d->width;

            if (mode == MODE_ALL) {
                vsh::bitblt(d->buf, d->buf_pitch, srcp, src_pitch, row_size, heightp);
                d->DeScratch_pass(srcp + wleftp, src_pitch, bluredp + wleftp, blured_pitch, d->buf + wleftp, d->buf_pitch, wrightp - wleftp, heightp, d->height / heightp, mindif, d->asym);
                vsh::bitblt(destp, dest_pitch, d->buf, d->buf_pitch, row_size, heightp);
                d->DeScratch_pass(d->buf + wleftp, d->buf_pitch, bluredp + wleftp, blured_pitch, destp + wleftp, dest_pitch, wrightp - wleftp, heightp, (d->height / heightp), (-mindif), d->asym);
            } else {
                vsh::bitblt(destp, dest_pitch, srcp, src_pitch, row_size, heightp);
                if (mode == MODE_LOW || mode == MODE_HIGH) {
                    int sign = (mode == MODE_LOW) ? 1 : -1;
                    d->DeScratch_pass(srcp + wleftp, src_pitch, bluredp + wleftp, blured_pitch, destp + wleftp, dest_pitch, wrightp - wleftp, heightp, d->height / heightp, sign * mindif, d->asym);
                }
            }
            };

        ProcessPlane(0, d->modeY, d->mindif);
        ProcessPlane(1, d->modeU, d->mindifUV);
        ProcessPlane(2, d->modeV, d->mindifUV);

        return dest;
    }

    return nullptr;
}

static void VS_CC deScratchFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    DeScratchVSData *d = (DeScratchVSData *)instanceData;
    vsapi->freeNode(d->node);
    vsapi->freeNode(d->blured_clip);
    delete d;
}

#define RETERROR(x) do { vsapi->mapSetError(out, (x)); 	vsapi->freeNode(d->node); vsapi->freeNode(d->blured_clip); return; } while (0)

static void VS_CC deScratchCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<DeScratchVSData> d(new DeScratchVSData());
    int err;
    d->mindif = vsapi->mapGetIntSaturated(in, "mindif", 0, &err);
    if (err)
        d->mindif = 5;
    d->asym = vsapi->mapGetIntSaturated(in, "asym", 0, &err);
    if (err)
        d->asym = 10;
    d->maxgap = vsapi->mapGetIntSaturated(in, "maxgap", 0, &err);
    if (err)
        d->maxgap = 2;
    d->maxwidth = vsapi->mapGetIntSaturated(in, "maxwidth", 0, &err);
    if (err)
        d->maxwidth = 3;
    d->minlen = vsapi->mapGetIntSaturated(in, "minlen", 0, &err);
    if (err)
        d->minlen = 100;
    d->maxlen = vsapi->mapGetIntSaturated(in, "maxlen", 0, &err);
    if (err)
        d->maxlen = 2048;
    d->maxangle = vsapi->mapGetFloatSaturated(in, "maxangle", 0, &err);
    if (err)
        d->maxangle = 5.0f;
    d->blurlen = vsapi->mapGetIntSaturated(in, "blurlen", 0, &err);
    if (err)
        d->blurlen = 15;
    d->keep = vsapi->mapGetIntSaturated(in, "keep", 0, &err);
    if (err)
        d->keep = 100;
    d->border = vsapi->mapGetIntSaturated(in, "border", 0, &err);
    if (err)
        d->border = 2;
    d->modeY = vsapi->mapGetIntSaturated(in, "modeY", 0, &err);
    if (err)
        d->modeY = 1;
    d->modeU = vsapi->mapGetIntSaturated(in, "modeU", 0, &err);
    d->modeV = vsapi->mapGetIntSaturated(in, "modeV", 0, &err);
    d->mindifUV = vsapi->mapGetIntSaturated(in, "mindifUV", 0, &err);
    d->mark = !!vsapi->mapGetIntSaturated(in, "mark", 0, &err);
    d->minwidth = vsapi->mapGetIntSaturated(in, "minwidth", 0, &err);
    if (err)
        d->minwidth = 1;
    d->wleft = vsapi->mapGetIntSaturated(in, "left", 0, &err);
    d->wright = vsapi->mapGetIntSaturated(in, "right", 0, &err);
    if (err)
        d->wright = 4096;

    if (d->mindif <= 0)
        RETERROR("Descratch: mindif must be positive!");
    if (d->asym < 0)
        RETERROR("Descratch: asym must be not negative!");
    if (d->mindifUV < 0)
        RETERROR("Descratch: mindifUV must not be negative!");
    else if (d->mindifUV == 0)
        d->mindifUV = d->mindif;
    if ((d->maxgap < 0) || (d->maxgap > 255))
        RETERROR("Descratch: maxgap must be >=0 and <=256!");
    if (!(d->maxwidth % 2) || (d->maxwidth < 1) || (d->maxwidth > 15))
        RETERROR("Descratch: maxwidth must be odd from 1 to 15!"); // v.1.0
    if ((d->minlen <= 0))
        RETERROR("Descratch: minlen must be > 0!");
    if ((d->maxlen <= 0))
        RETERROR("Descratch: maxlen must be > 0!");
    if ((d->maxangle < 0) || (d->maxangle > 90))
        RETERROR("Descratch: maxangle must be from 0 to 90!");
    if ((d->blurlen < 0) || (d->blurlen > 200))
        RETERROR("Descratch: blurlen must be from 0 to 200!"); // v1.0
    if ((d->keep < 0) || (d->keep > 100))
        RETERROR("Descratch: keep must be from 0 to 100!");
    if ((d->border < 0) || (d->border > 5))
        RETERROR("Descratch: border must be from 0 to 5!");
    if (d->modeY < 0 || d->modeY>3 || d->modeU < 0 || d->modeU>3 || d->modeV < 0 || d->modeV>3)
        RETERROR("Descratch: modeY, modeU, modeV must be from 0 to 3!");
    if (d->minwidth > d->maxwidth)
        RETERROR("Descratch: minwidth must be not above maxwidth!");
    if (!(d->minwidth % 2) || (d->minwidth < 1) || (d->minwidth > 15))
        RETERROR("Descratch: minwidth must be odd from 1 to 15!"); // v.1.0

    d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const VSVideoInfo *vi = vsapi->getVideoInfo(d->node);

    if (!vsh::isConstantVideoFormat(vi) || (vsapi->queryVideoFormatID(vi->format.colorFamily, vi->format.sampleType, vi->format.bitsPerSample, vi->format.subSamplingW, vi->format.subSamplingH, core) != pfYUV420P8))
        RETERROR("Descratch: Video must be constant format YV12!");

    d->width = vi->width;
    d->height = vi->height;
    d->buf_pitch = d->width + 16 - d->width % 16;

    // check working window limits
    if (d->wleft < 0)
        d->wleft = 0;
    d->wleft = d->wleft - d->wleft % 2;
    if (d->wright > d->width)
        d->wright = d->width;
    d->wright = d->wright - d->wright % 2;
    if (d->wleft >= d->wright)
        RETERROR("Descratch: must be: left < right <= width!");

    int down_height = (vi->height) / (1 + d->blurlen);
    if (down_height % 2) down_height -= 1;

    VSMap *args1 = vsapi->createMap();
    vsapi->mapSetNode(args1, "clip", d->node, maAppend);
    vsapi->mapSetInt(args1, "width", d->width, maAppend);
    vsapi->mapSetInt(args1, "height", down_height, maAppend);
    VSMap *args2 = vsapi->invoke(vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, core), "Bilinear", args1);
    vsapi->freeMap(args1);
    vsapi->mapSetInt(args2, "width", d->width, maAppend);
    vsapi->mapSetInt(args2, "height", d->height, maAppend);
    VSMap *result = vsapi->invoke(vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, core), "Bicubic", args2);
    vsapi->freeMap(args2);
    d->blured_clip = vsapi->mapGetNode(result, "clip", 0, nullptr);
    vsapi->freeMap(result);

    d->scratchdata = (BYTE *)malloc(vi->height * vi->width);
    d->buf = (BYTE *)malloc(d->height * d->buf_pitch);

    VSFilterDependency deps[] = { {d->node, rpStrictSpatial}, {d->blured_clip, rpStrictSpatial} }; /* Depending the the request patterns you may want to change this */
    vsapi->createVideoFilter(out, "DeScratch", vi, deScratchGetFrame, deScratchFree, fmParallelRequests, deps, 2, d.release(), core);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.vapoursynth.descratch", "descratch", "DeScratch for Vapoursynth and friends", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("DeScratch", "clip:vnode;mindif:int:opt;asym:int:opt;maxgap:int:opt;maxwidth:int:opt;minlen:int:opt;maxlen:int:opt;maxangle:float:opt;blurlen:int:opt;keep:int:opt;border:int:opt;modeY:int:opt;modeU:int:opt;modeV:int:opt;mindifUV:int:opt;mark:int:opt;minwidth:int:opt;left:int:opt;right:int:opt;", "clip:vnode;", deScratchCreate, nullptr, plugin);
}