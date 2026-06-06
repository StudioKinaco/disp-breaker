#pragma once
#ifndef DISP_BREAKER_H
#define DISP_BREAKER_H

#include "AEConfig.h"
#include "entry.h"
#include "AEFX_SuiteHelper.h"
#include "PrSDKAESupport.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_Macros.h"
#include "AEGP_SuiteHandler.h"
#include "Param_Utils.h"
#include "Smart_Utils.h"

#ifdef AE_OS_WIN
    #include <Windows.h>
#endif

#define DESCRIPTION  "\nDisp Breaker v1.0\rFlood the edge color past the matte's first fully-opaque pixel toward each enabled layer edge."

#define NAME            "Disp Breaker"
#define MAJOR_VERSION   1
#define MINOR_VERSION   0
#define BUG_VERSION     0
#define STAGE_VERSION   PF_Stage_DEVELOP
#define BUILD_VERSION   1

enum {
    DISPBREAKER_INPUT = 0,
    DISPBREAKER_MATTE,
    DISPBREAKER_LEFT,
    DISPBREAKER_LEFT_POSS,
    DISPBREAKER_RIGHT,
    DISPBREAKER_RIGHT_POSS,
    DISPBREAKER_TOP,
    DISPBREAKER_TOP_POSS,
    DISPBREAKER_BOTTOM,
    DISPBREAKER_BOTTOM_POSS,
    DISPBREAKER_BLEND,
    DISPBREAKER_NUM_PARAMS
};

enum {
    MATTE_DISK_ID = 1,
    LEFT_DISK_ID,
    LEFT_POSS_DISK_ID,
    RIGHT_DISK_ID,
    RIGHT_POSS_DISK_ID,
    TOP_DISK_ID,
    TOP_POSS_DISK_ID,
    BOTTOM_DISK_ID,
    BOTTOM_POSS_DISK_ID,
    BLEND_DISK_ID
};

enum {
    BLEND_NORMAL = 1,
    BLEND_MULTIPLY,
    BLEND_SCREEN,
    BLEND_ADD,
    BLEND_LIGHTEN,
    BLEND_DARKEN,
    BLEND_DIVIDE,
    BLEND_EXCLUSION,
    BLEND_NUM_MODES = 8
};

typedef struct DispBreakerInfo {
    A_long      enableLeft;
    A_long      enableRight;
    A_long      enableTop;
    A_long      enableBottom;
    A_long      blendMode;
    PF_FpShort  possLeft;       // 0..100
    PF_FpShort  possRight;
    PF_FpShort  possTop;
    PF_FpShort  possBottom;
    A_long      frameIndex;     // for deterministic per-frame hashing
} DispBreakerInfo;

extern "C" {

    DllExport
    PF_Err
    EffectMain(
        PF_Cmd          cmd,
        PF_InData       *in_data,
        PF_OutData      *out_data,
        PF_ParamDef     *params[],
        PF_LayerDef     *output,
        void            *extra);

}

#endif // EDGE_STRETCH_H
