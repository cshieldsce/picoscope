#include "trigger.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"

/* Sub-sample linear interpolation helper 
 * returns a*(1-f)+b*f
 * static, returns uint16_t -> us prefix
 */
static inline uint16_t usLerpU16(uint16_t usA, uint16_t usB, uint32_t ulFrac_q16) {
    uint32_t ulFa = (1u << 16) - ulFrac_q16;
    return (uint16_t)(((uint32_t)usA * ulFa + (uint32_t)usB * ulFrac_q16 + 32768u) >> 16);
}

/* Read at fractional index using linear interpolation */
static inline uint16_t usReadLerpQ16(const uint16_t* pusSrc, uint32_t ulN, uint32_t ulS_q16) {
    uint32_t uxIndex = ulS_q16 >> 16;
    uint32_t ulFrac = ulS_q16 & 0xFFFFu;
    if (uxIndex >= ulN - 1) return pusSrc[ulN - 1];
    return usLerpU16(pusSrc[uxIndex], pusSrc[uxIndex + 1], ulFrac);
}

/* Decimate by resampling with fractional start/step (no averaging) */
static void vDecimateResampleLinear(const uint16_t* pusSrc, uint32_t ulSrcLen, uint32_t ulStart_q16, uint32_t ulSpan, uint16_t* pusDst, uint32_t ulDstLen) {
    if (!pusSrc || !pusDst || !ulSpan || !ulDstLen) return;
    uint32_t ulStep_q16 = (uint32_t)(((uint64_t)ulSpan << 16) / ulDstLen);
    uint32_t ulS = ulStart_q16 + (ulStep_q16 >> 1);
    for (uint32_t uxI = 0; uxI < ulDstLen; uxI++, ulS += ulStep_q16) {
        pusDst[uxI] = usReadLerpQ16(pusSrc, ulSrcLen, ulS);
    }
}

/* Trigger search constrained to a safe range
 * returns index (int) -> l prefix for local signed result
 */
static int lFindTrigger(const uint16_t* pusS, uint32_t ulBegin, uint32_t ulEnd, uint16_t usLevel, TriggerEdge_e eEdge, uint16_t usHyst, float* pfCross) {
    if (!pusS || ulEnd <= ulBegin + 1) return -1;
    uint16_t usLo = (usLevel > usHyst) ? (uint16_t)(usLevel - usHyst) : 0;
    uint16_t usHi = (uint16_t)((usLevel + usHyst <= 4095) ? (usLevel + usHyst) : 4095);
    for (uint32_t uxI = ulBegin + 1; uxI < ulEnd; uxI++) {
        uint16_t usA = pusS[uxI - 1], usB = pusS[uxI];
        bool bCrossed = (eEdge == TRIG_EDGE_RISING) ? (usA < usLo && usB >= usHi) : (usA > usHi && usB <= usLo);
        if (!bCrossed) continue;
        int32_t lDy = (int32_t)usB - (int32_t)usA;
        float fFrac = (lDy != 0) ? ((float)((int32_t)usLevel - (int32_t)usA) / (float)lDy) : 0.0f;
        if (fFrac < 0.0f) fFrac = 0.0f;
        if (fFrac > 1.0f) fFrac = 1.0f;
        if (pfCross) *pfCross = ((float)(int32_t)uxI - 1.0f) + fFrac;
        return (int)uxI;
    }
    return -1;
}

/* Initialize defaults -> void return, v prefix */
void vTriggerInitDefault(TriggerConfig_t* pxCfg) {
    if (!pxCfg) return;
    pxCfg->eMode          = TRIG_MODE_AUTO;
    pxCfg->eEdge          = TRIG_EDGE_RISING;
    pxCfg->uLevelCounts   = 2048;
    pxCfg->uHysteresis    = 50;
    pxCfg->fTimePerDivMs  = 10.0f;
    pxCfg->fPretriggerFrac= 0.30f;
    pxCfg->eSmoothing     = SMOOTH_MINMAX;
}

/* Build an output frame:
 * 1) Determine span (how many input samples map to DISPLAY_POINTS)
 * 2) Search for trigger (respecting pre-trigger fraction and staying within valid window)
 * 3) Compute fractional start (Q16) such that pretrigger fraction is honored
 * 4) Resample using decimate_resample_linear to produce dst_len points
 */
bool bTriggerBuildFrame(const uint16_t* pusSrc, uint32_t ulSrcLen, uint32_t ulFs_hz, const TriggerConfig_t* pxCfg, uint16_t* pusDst, uint32_t ulDstLen, TriggerResult_t* pxOut) {
    if (!pusSrc || !ulSrcLen || !pxCfg || !pusDst || !ulDstLen) return false;

    TriggerResult_t xRes = {0};
    xRes.iTriggerIndex = -1;

    uint32_t ulMax_step = (ulDstLen ? (ulSrcLen / ulDstLen) : 0);
    if (ulMax_step == 0) ulMax_step = 1;
    uint32_t ulStep = ulMax_step;
    if (ulStep > 1) ulStep--;
    uint32_t ulSpan = ulStep * ulDstLen;
    if (ulSpan == 0 || ulSpan > ulSrcLen) ulSpan = (ulSrcLen < ulDstLen ? ulSrcLen : ulDstLen);

    float fPre_frac = pxCfg->fPretriggerFrac;
    if (fPre_frac < 0.0f) fPre_frac = 0.0f;
    if (fPre_frac > 0.9f) fPre_frac = 0.9f;
    float fPre_f = fPre_frac * (float) ulSpan;
    uint32_t ulPre = (uint32_t)(fPre_f + 0.5f);

    uint32_t ulStart_max = (ulSrcLen > ulSpan ? (ulSrcLen - ulSpan) : 0u);
    uint32_t ulT_begin = (ulPre > 1u) ? ulPre : 1u;
    uint32_t ulT_end = ulSrcLen - 1u;
    uint32_t ulT_end_cap = ulStart_max + ulPre;
    if (ulT_end_cap < ulT_end) ulT_end = ulT_end_cap;

    float fT_fine = -1.0f;
    if (pxCfg->eMode != TRIG_MODE_NONE && ulT_begin < ulT_end) {
        int lT = lFindTrigger(pusSrc, ulT_begin, ulT_end, pxCfg->uLevelCounts, pxCfg->eEdge, pxCfg->uHysteresis, &fT_fine);
        if (lT >= 0) {
            xRes.iTriggerIndex = lT;
            xRes.bTriggered = true;
        }
    }

    float fStart_f = 0.0f;
    if (xRes.bTriggered && fT_fine >= 0.0f) {
        fStart_f = fT_fine - fPre_f;
        if (fStart_f < 0.0f) fStart_f = 0.0f;
        float fMax_start_f = (float) ulStart_max;
        if (fStart_f > fMax_start_f) fStart_f = fMax_start_f;
    } else {
        fStart_f = 0.0f;
    }

    uint32_t ulStart_q16 = (uint32_t) llroundf(fStart_f * 65536.0f);

    xRes.uStart = (uint32_t) (fStart_f + 0.5f);
    xRes.uLen   = ulSpan;
    xRes.uOutCount = ulDstLen;

    vDecimateResampleLinear(pusSrc, ulSrcLen, ulStart_q16, ulSpan, pusDst, ulDstLen);

    if (pxOut) *pxOut = xRes;
    return true;
}