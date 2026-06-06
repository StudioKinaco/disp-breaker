/*
    DispBreaker.cpp

    Disp Breaker effect — for each enabled direction (Left, Right, Top, Bottom),
    scan the matte layer along that axis to find the first fully-opaque pixel,
    take the input layer's color at the position 1px BEFORE that opaque pixel
    (i.e., 1px further toward the edge), then flood that color into all pixels
    on the OUTSIDE of the opaque position, blending into the working output
    with the chosen blend mode.

    Intended to be applied to an adjustment layer so the "input" the effect
    sees is the composited image of layers below.

    SmartFX + Multi-Frame Rendering enabled. Handles 8/16/32 bit color.
*/

#include "DispBreaker.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

// ===========================================================================
//  Per-(frame, direction, line-index) hash for possibility gating.
//
//  Deterministic — same (frameIdx, dir, line) always returns the same 0..99.
//  MFR-safe: no shared state, pure function.
// ===========================================================================
enum {
    DIR_LEFT   = 1,
    DIR_RIGHT  = 2,
    DIR_TOP    = 3,
    DIR_BOTTOM = 4,
};

static inline std::uint32_t LineHash(A_long frameIdx, A_long dir, A_long line)
{
    std::uint32_t h = (std::uint32_t)frameIdx * 0x9E3779B1u;
    h ^= (std::uint32_t)dir   * 0x85EBCA77u;
    h += (std::uint32_t)line  * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// poss is 0..100; returns true if the line should appear this frame.
static inline bool LinePasses(A_long frameIdx, A_long dir, A_long line, PF_FpShort poss)
{
    if (poss >= 100.0f) return true;
    if (poss <= 0.0f)   return false;
    return (LineHash(frameIdx, dir, line) % 10000u) < (std::uint32_t)(poss * 100.0f);
}

// ===========================================================================
//  Depth traits
// ===========================================================================

template <typename Pixel> struct DepthTraits;

template <> struct DepthTraits<PF_Pixel8> {
    using Channel = A_u_char;
    static constexpr Channel kMax = PF_MAX_CHAN8;            // 255
    static inline bool fully_opaque(Channel a) { return a >= kMax; }
};

template <> struct DepthTraits<PF_Pixel16> {
    using Channel = A_u_short;
    static constexpr Channel kMax = PF_MAX_CHAN16;           // 32768
    static inline bool fully_opaque(Channel a) { return a >= kMax; }
};

template <> struct DepthTraits<PF_PixelFloat> {
    using Channel = PF_FpShort;
    static inline bool fully_opaque(PF_FpShort a) { return a >= 1.0f; }
};

// ===========================================================================
//  Blend math
// ===========================================================================

static inline A_u_char BlendChan8(A_u_char dst, A_u_char src, A_long mode) {
    const A_long d = dst, s = src;
    A_long r;
    switch (mode) {
        case BLEND_MULTIPLY:
            r = (d * s + (PF_MAX_CHAN8 / 2)) / PF_MAX_CHAN8;
            break;
        case BLEND_SCREEN:
            r = PF_MAX_CHAN8 - ((PF_MAX_CHAN8 - d) * (PF_MAX_CHAN8 - s) + (PF_MAX_CHAN8 / 2)) / PF_MAX_CHAN8;
            break;
        case BLEND_ADD:
            r = d + s;
            if (r > PF_MAX_CHAN8) r = PF_MAX_CHAN8;
            break;
        case BLEND_LIGHTEN:
            r = (d > s) ? d : s;
            break;
        case BLEND_DARKEN:
            r = (d < s) ? d : s;
            break;
        case BLEND_DIVIDE:
            // dst / src normalized. src==0 → fully bright (mirrors AE behavior).
            r = (s == 0) ? PF_MAX_CHAN8 : (d * PF_MAX_CHAN8 + (s / 2)) / s;
            if (r > PF_MAX_CHAN8) r = PF_MAX_CHAN8;
            break;
        case BLEND_EXCLUSION:
            // d + s - 2*d*s/max
            r = d + s - 2 * (d * s + (PF_MAX_CHAN8 / 2)) / PF_MAX_CHAN8;
            if (r < 0) r = 0;
            if (r > PF_MAX_CHAN8) r = PF_MAX_CHAN8;
            break;
        case BLEND_NORMAL:
        default:
            r = s;
            break;
    }
    return (A_u_char)r;
}

static inline A_u_short BlendChan16(A_u_short dst, A_u_short src, A_long mode) {
    const A_long d = dst, s = src;
    A_long r;
    switch (mode) {
        case BLEND_MULTIPLY:
            r = (d * s + (PF_MAX_CHAN16 / 2)) / PF_MAX_CHAN16;
            break;
        case BLEND_SCREEN:
            r = PF_MAX_CHAN16 - ((PF_MAX_CHAN16 - d) * (PF_MAX_CHAN16 - s) + (PF_MAX_CHAN16 / 2)) / PF_MAX_CHAN16;
            break;
        case BLEND_ADD:
            r = d + s;
            if (r > PF_MAX_CHAN16) r = PF_MAX_CHAN16;
            break;
        case BLEND_LIGHTEN:
            r = (d > s) ? d : s;
            break;
        case BLEND_DARKEN:
            r = (d < s) ? d : s;
            break;
        case BLEND_DIVIDE:
            r = (s == 0) ? PF_MAX_CHAN16 : (d * PF_MAX_CHAN16 + (s / 2)) / s;
            if (r > PF_MAX_CHAN16) r = PF_MAX_CHAN16;
            break;
        case BLEND_EXCLUSION:
            r = d + s - 2 * (d * s + (PF_MAX_CHAN16 / 2)) / PF_MAX_CHAN16;
            if (r < 0) r = 0;
            if (r > PF_MAX_CHAN16) r = PF_MAX_CHAN16;
            break;
        case BLEND_NORMAL:
        default:
            r = s;
            break;
    }
    return (A_u_short)r;
}

static inline PF_FpShort BlendChanF(PF_FpShort dst, PF_FpShort src, A_long mode) {
    switch (mode) {
        case BLEND_MULTIPLY:  return dst * src;
        case BLEND_SCREEN:    return 1.0f - (1.0f - dst) * (1.0f - src);
        case BLEND_ADD:       return dst + src;     // float: no clamp (allow over-range)
        case BLEND_LIGHTEN:   return (dst > src) ? dst : src;
        case BLEND_DARKEN:    return (dst < src) ? dst : src;
        case BLEND_DIVIDE:    return (src == 0.0f) ? 1.0f : (dst / src);
        case BLEND_EXCLUSION: return dst + src - 2.0f * dst * src;
        case BLEND_NORMAL:
        default:              return src;
    }
}

// ---- Premult / unpremult helpers (channel is 0..max) ---------------------
static inline A_long Unpremult8 (A_long c, A_long a) {
    if (a <= 0) return 0;
    A_long r = (c * PF_MAX_CHAN8 + a / 2) / a;
    return (r > PF_MAX_CHAN8) ? PF_MAX_CHAN8 : r;
}
static inline A_long Premult8   (A_long c, A_long a) {
    A_long r = (c * a + PF_MAX_CHAN8 / 2) / PF_MAX_CHAN8;
    return (r > PF_MAX_CHAN8) ? PF_MAX_CHAN8 : r;
}
static inline A_long Unpremult16(A_long c, A_long a) {
    if (a <= 0) return 0;
    A_long r = (c * PF_MAX_CHAN16 + a / 2) / a;
    return (r > PF_MAX_CHAN16) ? PF_MAX_CHAN16 : r;
}
static inline A_long Premult16  (A_long c, A_long a) {
    A_long r = (c * a + PF_MAX_CHAN16 / 2) / PF_MAX_CHAN16;
    return (r > PF_MAX_CHAN16) ? PF_MAX_CHAN16 : r;
}
static inline PF_FpShort UnpremultF(PF_FpShort c, PF_FpShort a) { return (a <= 0.0f) ? 0.0f : (c / a); }
static inline PF_FpShort PremultF  (PF_FpShort c, PF_FpShort a) { return c * a; }

// ---- Unpremultiply a sampled pixel to straight RGB (alpha preserved) ----
template <typename Pixel> static inline Pixel UnpremultRGB(const Pixel &p);

template <> inline PF_Pixel8 UnpremultRGB<PF_Pixel8>(const PF_Pixel8 &p) {
    PF_Pixel8 r;
    r.alpha = p.alpha;
    r.red   = (A_u_char)Unpremult8(p.red,   p.alpha);
    r.green = (A_u_char)Unpremult8(p.green, p.alpha);
    r.blue  = (A_u_char)Unpremult8(p.blue,  p.alpha);
    return r;
}
template <> inline PF_Pixel16 UnpremultRGB<PF_Pixel16>(const PF_Pixel16 &p) {
    PF_Pixel16 r;
    r.alpha = p.alpha;
    r.red   = (A_u_short)Unpremult16(p.red,   p.alpha);
    r.green = (A_u_short)Unpremult16(p.green, p.alpha);
    r.blue  = (A_u_short)Unpremult16(p.blue,  p.alpha);
    return r;
}
template <> inline PF_PixelFloat UnpremultRGB<PF_PixelFloat>(const PF_PixelFloat &p) {
    PF_PixelFloat r;
    r.alpha = p.alpha;
    r.red   = UnpremultF(p.red,   p.alpha);
    r.green = UnpremultF(p.green, p.alpha);
    r.blue  = UnpremultF(p.blue,  p.alpha);
    return r;
}

// ---- BlendPixel: dst is premultiplied, srcStraight is straight RGB.
//                  Result keeps dst.alpha and stores premultiplied RGB. -----
static inline void BlendPixel(PF_Pixel8 &out, const PF_Pixel8 &dst, const PF_Pixel8 &srcStraight, A_long mode) {
    const A_long da = dst.alpha;
    out.alpha = (A_u_char)da;
    if (mode == BLEND_NORMAL) {
        out.red   = (A_u_char)Premult8(srcStraight.red,   da);
        out.green = (A_u_char)Premult8(srcStraight.green, da);
        out.blue  = (A_u_char)Premult8(srcStraight.blue,  da);
        return;
    }
    const A_long dr = Unpremult8(dst.red,   da);
    const A_long dg = Unpremult8(dst.green, da);
    const A_long db = Unpremult8(dst.blue,  da);
    const A_long rr = BlendChan8((A_u_char)dr, srcStraight.red,   mode);
    const A_long rg = BlendChan8((A_u_char)dg, srcStraight.green, mode);
    const A_long rb = BlendChan8((A_u_char)db, srcStraight.blue,  mode);
    out.red   = (A_u_char)Premult8(rr, da);
    out.green = (A_u_char)Premult8(rg, da);
    out.blue  = (A_u_char)Premult8(rb, da);
}

static inline void BlendPixel(PF_Pixel16 &out, const PF_Pixel16 &dst, const PF_Pixel16 &srcStraight, A_long mode) {
    const A_long da = dst.alpha;
    out.alpha = (A_u_short)da;
    if (mode == BLEND_NORMAL) {
        out.red   = (A_u_short)Premult16(srcStraight.red,   da);
        out.green = (A_u_short)Premult16(srcStraight.green, da);
        out.blue  = (A_u_short)Premult16(srcStraight.blue,  da);
        return;
    }
    const A_long dr = Unpremult16(dst.red,   da);
    const A_long dg = Unpremult16(dst.green, da);
    const A_long db = Unpremult16(dst.blue,  da);
    const A_long rr = BlendChan16((A_u_short)dr, srcStraight.red,   mode);
    const A_long rg = BlendChan16((A_u_short)dg, srcStraight.green, mode);
    const A_long rb = BlendChan16((A_u_short)db, srcStraight.blue,  mode);
    out.red   = (A_u_short)Premult16(rr, da);
    out.green = (A_u_short)Premult16(rg, da);
    out.blue  = (A_u_short)Premult16(rb, da);
}

static inline void BlendPixel(PF_PixelFloat &out, const PF_PixelFloat &dst, const PF_PixelFloat &srcStraight, A_long mode) {
    const PF_FpShort da = dst.alpha;
    out.alpha = da;
    if (mode == BLEND_NORMAL) {
        out.red   = PremultF(srcStraight.red,   da);
        out.green = PremultF(srcStraight.green, da);
        out.blue  = PremultF(srcStraight.blue,  da);
        return;
    }
    const PF_FpShort dr = UnpremultF(dst.red,   da);
    const PF_FpShort dg = UnpremultF(dst.green, da);
    const PF_FpShort db = UnpremultF(dst.blue,  da);
    out.red   = PremultF(BlendChanF(dr, srcStraight.red,   mode), da);
    out.green = PremultF(BlendChanF(dg, srcStraight.green, mode), da);
    out.blue  = PremultF(BlendChanF(db, srcStraight.blue,  mode), da);
}

// ===========================================================================
//  Templated core processor
// ===========================================================================
//
//  inputW : checked-out input world covering [0..in_data->width, 0..in_data->height)
//  matteW : checked-out matte world covering same range (or NULL if no matte layer)
//  outputW: AE-allocated output world for the requested output_request.rect
//  offX/Y : the original output_request.rect.left/top — offset of output(0,0)
//           in input-world coordinates.
//
//  Order is fixed Left → Right → Top → Bottom; each pass blends into the
//  evolving output. Source color is always sampled from the ORIGINAL input
//  (never from the partially-blended output).
//
template <typename Pixel>
static PF_Err ProcessTyped(
    DispBreakerInfo     *infoP,
    PF_EffectWorld      *inputW,
    PF_EffectWorld      *matteW,
    PF_EffectWorld      *outputW,
    A_long              offX,
    A_long              offY,
    A_long              matteOriginX,    // layer-coord of matteW(0,0)
    A_long              matteOriginY)
{
    using Tr = DepthTraits<Pixel>;

    auto inputRow = [&](A_long y) -> const Pixel* {
        return reinterpret_cast<const Pixel*>(
            reinterpret_cast<const char*>(inputW->data) + (size_t)y * (size_t)inputW->rowbytes);
    };
    auto outRow = [&](A_long y) -> Pixel* {
        return reinterpret_cast<Pixel*>(
            reinterpret_cast<char*>(outputW->data) + (size_t)y * (size_t)outputW->rowbytes);
    };

    const A_long outW = outputW->width;
    const A_long outH = outputW->height;
    const A_long inW  = inputW->width;
    const A_long inH  = inputW->height;

    // Matte bounds in layer coords. If no matte (matteW==NULL or empty), set to empty rect.
    const A_long matteW_W = (matteW && matteW->data) ? matteW->width  : 0;
    const A_long matteW_H = (matteW && matteW->data) ? matteW->height : 0;
    const A_long matteLeft   = matteOriginX;
    const A_long matteTop    = matteOriginY;
    const A_long matteRight  = matteOriginX + matteW_W;   // exclusive
    const A_long matteBottom = matteOriginY + matteW_H;   // exclusive

    // Layer-coord matte sample. Returns false (treated as not-opaque) when outside matte's world.
    auto matteOpaqueAt = [&](A_long gx, A_long gy) -> bool {
        if (!matteW || !matteW->data) return false;
        if (gx < matteLeft || gx >= matteRight)  return false;
        if (gy < matteTop  || gy >= matteBottom) return false;
        const A_long my = gy - matteTop;
        const A_long mx = gx - matteLeft;
        const Pixel *row = reinterpret_cast<const Pixel*>(
            reinterpret_cast<const char*>(matteW->data) + (size_t)my * (size_t)matteW->rowbytes);
        return Tr::fully_opaque(row[mx].alpha);
    };

    // ---- 1) Seed output with the input pixels under our output rect.
    for (A_long y = 0; y < outH; ++y) {
        const A_long gy = y + offY;
        if (gy < 0 || gy >= inH) {
            std::memset(outRow(y), 0, (size_t)outputW->rowbytes);
            continue;
        }
        const Pixel *src = inputRow(gy);
        Pixel *dst = outRow(y);
        for (A_long x = 0; x < outW; ++x) {
            const A_long gx = x + offX;
            if (gx < 0 || gx >= inW) {
                std::memset(&dst[x], 0, sizeof(Pixel));
            } else {
                dst[x] = src[gx];
            }
        }
    }

    // No matte (or empty matte world) → passthrough.
    if (!matteW || !matteW->data || matteW_W == 0 || matteW_H == 0) return PF_Err_NONE;

    const A_long mode = infoP->blendMode;

    // Returns the input pixel UNPREMULTIPLIED (straight RGB) so BlendPixel can
    // composite it correctly into a premultiplied output buffer.
    auto sampleInput = [&](A_long gx, A_long gy, Pixel &outPix) -> bool {
        if (gx < 0 || gx >= inW || gy < 0 || gy >= inH) return false;
        outPix = UnpremultRGB<Pixel>(inputRow(gy)[gx]);
        return true;
    };

    // ----- Left -----
    if (infoP->enableLeft && infoP->possLeft > 0.0f) {
        for (A_long y = 0; y < outH; ++y) {
            const A_long gy = y + offY;
            if (gy < matteTop || gy >= matteBottom) continue;       // row outside matte → no fill
            if (!LinePasses(infoP->frameIndex, DIR_LEFT, gy, infoP->possLeft)) continue;
            A_long firstOp = -1;
            for (A_long gx = matteLeft; gx < matteRight; ++gx) {
                if (matteOpaqueAt(gx, gy)) { firstOp = gx; break; }
            }
            if (firstOp <= 0) continue;
            Pixel srcPix;
            if (!sampleInput(firstOp - 1, gy, srcPix)) continue;
            Pixel *orow = outRow(y);
            const A_long localEnd = std::min<A_long>(outW, firstOp - offX);   // exclusive
            for (A_long x = std::max<A_long>(0, -offX); x < localEnd; ++x) {
                Pixel cur = orow[x];
                BlendPixel(orow[x], cur, srcPix, mode);
            }
        }
    }

    // ----- Right -----
    if (infoP->enableRight && infoP->possRight > 0.0f) {
        for (A_long y = 0; y < outH; ++y) {
            const A_long gy = y + offY;
            if (gy < matteTop || gy >= matteBottom) continue;
            if (!LinePasses(infoP->frameIndex, DIR_RIGHT, gy, infoP->possRight)) continue;
            A_long firstOp = -1;
            for (A_long gx = matteRight - 1; gx >= matteLeft; --gx) {
                if (matteOpaqueAt(gx, gy)) { firstOp = gx; break; }
            }
            if (firstOp < 0 || firstOp >= inW - 1) continue;
            Pixel srcPix;
            if (!sampleInput(firstOp + 1, gy, srcPix)) continue;
            Pixel *orow = outRow(y);
            const A_long localStart = std::max<A_long>(0, (firstOp + 1) - offX);
            const A_long localEnd   = std::min<A_long>(outW, inW - offX);
            for (A_long x = localStart; x < localEnd; ++x) {
                Pixel cur = orow[x];
                BlendPixel(orow[x], cur, srcPix, mode);
            }
        }
    }

    // ----- Top -----
    if (infoP->enableTop && infoP->possTop > 0.0f) {
        for (A_long x = 0; x < outW; ++x) {
            const A_long gx = x + offX;
            if (gx < matteLeft || gx >= matteRight) continue;
            if (!LinePasses(infoP->frameIndex, DIR_TOP, gx, infoP->possTop)) continue;
            A_long firstOp = -1;
            for (A_long gy = matteTop; gy < matteBottom; ++gy) {
                if (matteOpaqueAt(gx, gy)) { firstOp = gy; break; }
            }
            if (firstOp <= 0) continue;
            Pixel srcPix;
            if (!sampleInput(gx, firstOp - 1, srcPix)) continue;
            const A_long localEnd = std::min<A_long>(outH, firstOp - offY);
            for (A_long y = std::max<A_long>(0, -offY); y < localEnd; ++y) {
                Pixel *orow = outRow(y);
                Pixel cur = orow[x];
                BlendPixel(orow[x], cur, srcPix, mode);
            }
        }
    }

    // ----- Bottom -----
    if (infoP->enableBottom && infoP->possBottom > 0.0f) {
        for (A_long x = 0; x < outW; ++x) {
            const A_long gx = x + offX;
            if (gx < matteLeft || gx >= matteRight) continue;
            if (!LinePasses(infoP->frameIndex, DIR_BOTTOM, gx, infoP->possBottom)) continue;
            A_long firstOp = -1;
            for (A_long gy = matteBottom - 1; gy >= matteTop; --gy) {
                if (matteOpaqueAt(gx, gy)) { firstOp = gy; break; }
            }
            if (firstOp < 0 || firstOp >= inH - 1) continue;
            Pixel srcPix;
            if (!sampleInput(gx, firstOp + 1, srcPix)) continue;
            const A_long localStart = std::max<A_long>(0, (firstOp + 1) - offY);
            const A_long localEnd   = std::min<A_long>(outH, inH - offY);
            for (A_long y = localStart; y < localEnd; ++y) {
                Pixel *orow = outRow(y);
                Pixel cur = orow[x];
                BlendPixel(orow[x], cur, srcPix, mode);
            }
        }
    }

    return PF_Err_NONE;
}

// ===========================================================================
//  AE command handlers
// ===========================================================================

static PF_Err
About(
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output)
{
    PF_SPRINTF(out_data->return_msg,
               "%s, v%d.%d\r%s",
               NAME, MAJOR_VERSION, MINOR_VERSION, DESCRIPTION);
    return PF_Err_NONE;
}

static PF_Err
GlobalSetup(
    PF_InData       *in_dataP,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output)
{
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION,
                                      STAGE_VERSION, BUILD_VERSION);

    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE |
                           PF_OutFlag_PIX_INDEPENDENT  |
                           PF_OutFlag_NON_PARAM_VARY;        // per-frame deterministic possibility hash → must re-render on time change

    out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE      |
                           PF_OutFlag2_SUPPORTS_SMART_RENDER  |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

    return PF_Err_NONE;
}

static PF_Err
ParamsSetup(
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output)
{
    PF_Err      err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER("Matte Layer", PF_LayerDefault_NONE, MATTE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Stretch Left",   TRUE,  0, LEFT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Left Possibility",   0, 100, 0, 100, 100, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, LEFT_POSS_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Stretch Right",  FALSE, 0, RIGHT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Right Possibility",  0, 100, 0, 100, 100, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, RIGHT_POSS_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Stretch Top",    FALSE, 0, TOP_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Top Possibility",    0, 100, 0, 100, 100, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, TOP_POSS_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Stretch Bottom", FALSE, 0, BOTTOM_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Bottom Possibility", 0, 100, 0, 100, 100, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, BOTTOM_POSS_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Blend Mode",
                 BLEND_NUM_MODES,
                 BLEND_NORMAL,
                 "Normal|Multiply|Screen|Add|Lighten|Darken|Divide|Exclusion",
                 BLEND_DISK_ID);

    out_data->num_params = DISPBREAKER_NUM_PARAMS;
    return err;
}

// Stored across PreRender → SmartRender via pre_render_data handle.
typedef struct PreRenderData {
    DispBreakerInfo info;
    A_long          offX;            // original output_request.rect.left  (in layer coords)
    A_long          offY;
    A_Boolean       matte_present;
    // Layer-space rect of the matte world. matte_worldP(0,0) == layer(matteRect.left, matteRect.top).
    PF_LRect        matteRect;
} PreRenderData;

static PF_Err
PreRender(
    PF_InData           *in_dataP,
    PF_OutData          *out_dataP,
    PF_PreRenderExtra   *extraP)
{
    PF_Err err = PF_Err_NONE;
    PF_CheckoutResult   in_result, matte_result;
    PF_ParamDef         leftP, rightP, topP, bottomP, blendP;
    PF_ParamDef         leftPoss, rightPoss, topPoss, bottomPoss;

    AEFX_CLR_STRUCT(in_result);
    AEFX_CLR_STRUCT(matte_result);
    AEFX_CLR_STRUCT(leftP);
    AEFX_CLR_STRUCT(rightP);
    AEFX_CLR_STRUCT(topP);
    AEFX_CLR_STRUCT(bottomP);
    AEFX_CLR_STRUCT(blendP);
    AEFX_CLR_STRUCT(leftPoss);
    AEFX_CLR_STRUCT(rightPoss);
    AEFX_CLR_STRUCT(topPoss);
    AEFX_CLR_STRUCT(bottomPoss);

    AEFX_SuiteScoper<PF_HandleSuite1> handleSuite =
        AEFX_SuiteScoper<PF_HandleSuite1>(in_dataP, kPFHandleSuite,
                                          kPFHandleSuiteVersion1, out_dataP);

    PF_Handle dataH = handleSuite->host_new_handle(sizeof(PreRenderData));
    if (!dataH) return PF_Err_OUT_OF_MEMORY;

    PreRenderData *pdP = reinterpret_cast<PreRenderData*>(handleSuite->host_lock_handle(dataH));
    if (!pdP) {
        handleSuite->host_dispose_handle(dataH);
        return PF_Err_OUT_OF_MEMORY;
    }

    AEFX_CLR_STRUCT(*pdP);
    extraP->output->pre_render_data = dataH;

    // ---- Snapshot non-layer params.
    ERR(PF_CHECKOUT_PARAM(in_dataP, DISPBREAKER_LEFT,        in_dataP->current_time, in_dataP->time_step, in_dataP->time_scale, &leftP));
    ERR(PF_CHECKOUT_PARAM(in_dataP, DISPBREAKER_LEFT_POSS,   in_dataP->current_time, in_dataP->time_step, in_dataP->time_scale, &leftPoss));
    ERR(PF_CHECKOUT_PARAM(in_dataP, DISPBREAKER_RIGHT,       in_dataP->current_time, in_dataP->time_step, in_dataP->time_scale, &rightP));
    ERR(PF_CHECKOUT_PARAM(in_dataP, DISPBREAKER_RIGHT_POSS,  in_dataP->current_time, in_dataP->time_step, in_dataP->time_scale, &rightPoss));
    ERR(PF_CHECKOUT_PARAM(in_dataP, DISPBREAKER_TOP,         in_dataP->current_time, in_dataP->time_step, in_dataP->time_scale, &topP));
    ERR(PF_CHECKOUT_PARAM(in_dataP, DISPBREAKER_TOP_POSS,    in_dataP->current_time, in_dataP->time_step, in_dataP->time_scale, &topPoss));
    ERR(PF_CHECKOUT_PARAM(in_dataP, DISPBREAKER_BOTTOM,      in_dataP->current_time, in_dataP->time_step, in_dataP->time_scale, &bottomP));
    ERR(PF_CHECKOUT_PARAM(in_dataP, DISPBREAKER_BOTTOM_POSS, in_dataP->current_time, in_dataP->time_step, in_dataP->time_scale, &bottomPoss));
    ERR(PF_CHECKOUT_PARAM(in_dataP, DISPBREAKER_BLEND,       in_dataP->current_time, in_dataP->time_step, in_dataP->time_scale, &blendP));

    if (!err) {
        pdP->info.enableLeft   = leftP.u.bd.value;
        pdP->info.enableRight  = rightP.u.bd.value;
        pdP->info.enableTop    = topP.u.bd.value;
        pdP->info.enableBottom = bottomP.u.bd.value;
        pdP->info.blendMode    = blendP.u.pd.value;
        pdP->info.possLeft     = (PF_FpShort)leftPoss.u.fs_d.value;
        pdP->info.possRight    = (PF_FpShort)rightPoss.u.fs_d.value;
        pdP->info.possTop      = (PF_FpShort)topPoss.u.fs_d.value;
        pdP->info.possBottom   = (PF_FpShort)bottomPoss.u.fs_d.value;
        pdP->info.frameIndex   = (in_dataP->time_step != 0) ? (in_dataP->current_time / in_dataP->time_step) : in_dataP->current_time;
    }

    // Save the original requested output rect offset.
    pdP->offX = extraP->input->output_request.rect.left;
    pdP->offY = extraP->input->output_request.rect.top;

    // ---- Build the EXPANDED request for the input layer so we can scan
    //      a full row (for L/R) and full column (for T/B). Easiest correct
    //      thing: ask for the entire layer extent.
    PF_RenderRequest req = extraP->input->output_request;
    req.rect.left   = 0;
    req.rect.top    = 0;
    req.rect.right  = in_dataP->width;
    req.rect.bottom = in_dataP->height;
    req.preserve_rgb_of_zero_alpha = TRUE;
    req.field = PF_Field_FRAME;

    // ---- Input layer (full extent).
    ERR(extraP->cb->checkout_layer(in_dataP->effect_ref,
                                   DISPBREAKER_INPUT,
                                   DISPBREAKER_INPUT,
                                   &req,
                                   in_dataP->current_time,
                                   in_dataP->time_step,
                                   in_dataP->time_scale,
                                   &in_result));

    if (!err) {
        // result_rect: exactly what AE asked for (must be ⊆ request).
        extraP->output->result_rect = extraP->input->output_request.rect;

        // max_result_rect: must contain result_rect. Start there and union in
        // the input layer's max bounds only if it is a valid non-degenerate
        // rect (when the underlying layer is fully transparent, AE can return
        // an empty/inverted max_result_rect — using that directly is rejected).
        extraP->output->max_result_rect = extraP->output->result_rect;
        if (in_result.max_result_rect.right  > in_result.max_result_rect.left &&
            in_result.max_result_rect.bottom > in_result.max_result_rect.top)
        {
            UnionLRect(&in_result.max_result_rect, &extraP->output->max_result_rect);
        }
    }

    // ---- Matte layer (also full extent; safe to call even if user picked None,
    //      AE returns a result whose ref is empty in that case).
    PF_Err matte_err = extraP->cb->checkout_layer(in_dataP->effect_ref,
                                                  DISPBREAKER_MATTE,
                                                  DISPBREAKER_MATTE,
                                                  &req,
                                                  in_dataP->current_time,
                                                  in_dataP->time_step,
                                                  in_dataP->time_scale,
                                                  &matte_result);
    if (matte_err == PF_Err_NONE) {
        // Heuristic: if AE returned a non-empty result rect we treat the
        // matte as present. We confirm by world-pointer presence in SmartRender.
        pdP->matte_present = !(matte_result.result_rect.right  == matte_result.result_rect.left ||
                               matte_result.result_rect.bottom == matte_result.result_rect.top);
        pdP->matteRect = matte_result.result_rect;
    }

    handleSuite->host_unlock_handle(dataH);
    return err;
}

static PF_Err
SmartRender(
    PF_InData               *in_data,
    PF_OutData              *out_data,
    PF_SmartRenderExtra     *extraP)
{
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;

    PF_EffectWorld *input_worldP  = NULL;
    PF_EffectWorld *matte_worldP  = NULL;
    PF_EffectWorld *output_worldP = NULL;

    AEFX_SuiteScoper<PF_HandleSuite1> handleSuite =
        AEFX_SuiteScoper<PF_HandleSuite1>(in_data, kPFHandleSuite,
                                          kPFHandleSuiteVersion1, out_data);

    PreRenderData *pdP = reinterpret_cast<PreRenderData*>(
        handleSuite->host_lock_handle(reinterpret_cast<PF_Handle>(extraP->input->pre_render_data)));

    if (!pdP) return PF_Err_BAD_CALLBACK_PARAM;

    ERR(extraP->cb->checkout_layer_pixels(in_data->effect_ref, DISPBREAKER_INPUT, &input_worldP));
    ERR(extraP->cb->checkout_output(in_data->effect_ref, &output_worldP));

    if (pdP->matte_present) {
        // We don't bail if this fails — just treat as no matte.
        PF_Err mErr = extraP->cb->checkout_layer_pixels(in_data->effect_ref, DISPBREAKER_MATTE, &matte_worldP);
        if (mErr != PF_Err_NONE) {
            matte_worldP = NULL;
        }
    }

    if (!err && input_worldP && output_worldP) {

        PF_PixelFormat fmt = PF_PixelFormat_INVALID;

        AEFX_SuiteScoper<PF_WorldSuite2> wsP =
            AEFX_SuiteScoper<PF_WorldSuite2>(in_data, kPFWorldSuite,
                                             kPFWorldSuiteVersion2, out_data);
        ERR(wsP->PF_GetPixelFormat(output_worldP, &fmt));

        // Confirm matte is in the same format. If not, drop it (v1 keeps things simple).
        if (!err && matte_worldP) {
            PF_PixelFormat mfmt = PF_PixelFormat_INVALID;
            wsP->PF_GetPixelFormat(matte_worldP, &mfmt);
            if (mfmt != fmt) matte_worldP = NULL;
        }

        if (!err) {
            const A_long mOriginX = pdP->matteRect.left;
            const A_long mOriginY = pdP->matteRect.top;
            switch (fmt) {
                case PF_PixelFormat_ARGB32:
                    err = ProcessTyped<PF_Pixel8>(&pdP->info, input_worldP, matte_worldP,
                                                  output_worldP, pdP->offX, pdP->offY,
                                                  mOriginX, mOriginY);
                    break;
                case PF_PixelFormat_ARGB64:
                    err = ProcessTyped<PF_Pixel16>(&pdP->info, input_worldP, matte_worldP,
                                                   output_worldP, pdP->offX, pdP->offY,
                                                   mOriginX, mOriginY);
                    break;
                case PF_PixelFormat_ARGB128:
                    err = ProcessTyped<PF_PixelFloat>(&pdP->info, input_worldP, matte_worldP,
                                                      output_worldP, pdP->offX, pdP->offY,
                                                      mOriginX, mOriginY);
                    break;
                default:
                    err = PF_Err_BAD_CALLBACK_PARAM;
                    break;
            }
        }
    }

    if (input_worldP) {
        ERR2(extraP->cb->checkin_layer_pixels(in_data->effect_ref, DISPBREAKER_INPUT));
    }
    if (matte_worldP) {
        ERR2(extraP->cb->checkin_layer_pixels(in_data->effect_ref, DISPBREAKER_MATTE));
    }

    handleSuite->host_unlock_handle(reinterpret_cast<PF_Handle>(extraP->input->pre_render_data));
    return err;
}

// ===========================================================================
//  Entry points
// ===========================================================================

extern "C" DllExport
PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr        inPtr,
    PF_PluginDataCB2        inPluginDataCallBackPtr,
    SPBasicSuite            *inSPBasicSuitePtr,
    const char              *inHostName,
    const char              *inHostVersion)
{
    PF_Err result = PF_Err_INVALID_CALLBACK;

    result = PF_REGISTER_EFFECT_EXT2(
        inPtr,
        inPluginDataCallBackPtr,
        "Disp Breaker",               // Name (shows in Effects menu)
        "USR Disp Breaker",           // Match Name (must be unique)
        "STUDIO KINACO",              // Category
        AE_RESERVED_INFO,
        "EffectMain",                 // Entry point
        "https://example.com/dispbreaker");

    return result;
}

PF_Err
EffectMain(
    PF_Cmd          cmd,
    PF_InData       *in_dataP,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output,
    void            *extra)
{
    PF_Err err = PF_Err_NONE;

    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                err = About(in_dataP, out_data, params, output);
                break;
            case PF_Cmd_GLOBAL_SETUP:
                err = GlobalSetup(in_dataP, out_data, params, output);
                break;
            case PF_Cmd_PARAMS_SETUP:
                err = ParamsSetup(in_dataP, out_data, params, output);
                break;
            case PF_Cmd_SMART_PRE_RENDER:
                err = PreRender(in_dataP, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra));
                break;
            case PF_Cmd_SMART_RENDER:
                err = SmartRender(in_dataP, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra));
                break;
            default:
                break;
        }
    } catch (PF_Err &thrown_err) {
        err = thrown_err;
    }
    return err;
}
