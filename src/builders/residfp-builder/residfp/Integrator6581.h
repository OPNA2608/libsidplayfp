/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2020 Leandro Nini <drfiemost@users.sourceforge.net>
 * Copyright 2007-2010 Antti Lankila
 * Copyright 2004, 2010 Dag Lem <resid@nimrod.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef INTEGRATOR_H
#define INTEGRATOR_H

#include "InterpolatedLUT.h"

#include <stdint.h>
#include <cassert>

#include "siddefs-fp.h"

namespace reSIDfp
{

/**
 * Find output voltage in inverting integrator SID op-amp circuits, using a
 * single fixpoint iteration step.
 *
 * A circuit diagram of a MOS 6581 integrator is shown below.
 *
 *                    ---C---
 *                   |       |
 *     vi -----Rw-------[A>----- vo
 *          |      | vx
 *           --Rs--
 *
 * From Kirchoff's current law it follows that
 *
 *     IRw + IRs + ICr = 0
 *
 * Using the formula for current through a capacitor, i = C*dv/dt, we get
 *
 *     IRw + IRs + C*(vc - vc0)/dt = 0
 *     dt/C*(IRw + IRs) + vc - vc0 = 0
 *     vc = vc0 - n*(IRw(vi,vx) + IRs(vi,vx))
 *
 * which may be rewritten as the following iterative fixpoint function:
 *
 *     vc = vc0 - n*(IRw(vi,g(vc)) + IRs(vi,g(vc)))
 *
 * To accurately calculate the currents through Rs and Rw, we need to use
 * transistor models. Rs has a gate voltage of Vdd = 12V, and can be
 * assumed to always be in triode mode. For Rw, the situation is rather
 * more complex, as it turns out that this transistor will operate in
 * both subthreshold, triode, and saturation modes.
 *
 * The Shichman-Hodges transistor model routinely used in textbooks may
 * be written as follows:
 *
 *     Ids = 0                          , Vgst < 0               (subthreshold mode)
 *     Ids = K/2*W/L*(2*Vgst - Vds)*Vds , Vgst >= 0, Vds < Vgst  (triode mode)
 *     Ids = K/2*W/L*Vgst^2             , Vgst >= 0, Vds >= Vgst (saturation mode)
 *
 * where
 *     K   = u*Cox (transconductance coefficient)
 *     W/L = ratio between substrate width and length
 *     Vgst = Vg - Vs - Vt (overdrive voltage)
 *
 * This transistor model is also called the quadratic model.
 *
 * Note that the equation for the triode mode can be reformulated as
 * independent terms depending on Vgs and Vgd, respectively, by the
 * following substitution:
 *
 *     Vds = Vgst - (Vgst - Vds) = Vgst - Vgdt
 *
 *     Ids = K/2*W/L*(2*Vgst - Vds)*Vds
 *     = K/2*W/L*(2*Vgst - (Vgst - Vgdt)*(Vgst - Vgdt)
 *     = K/2*W/L*(Vgst + Vgdt)*(Vgst - Vgdt)
 *     = K/2*W/L*(Vgst^2 - Vgdt^2)
 *
 * This turns out to be a general equation which covers both the triode
 * and saturation modes (where the second term is 0 in saturation mode).
 * The equation is also symmetrical, i.e. it can calculate negative
 * currents without any change of parameters (since the terms for drain
 * and source are identical except for the sign).
 *
 * FIXME: Subthreshold as function of Vgs, Vgd.
 *
 *     Ids = I0*e^(Vgst/(n*VT))       , Vgst < 0               (subthreshold mode)
 *
 * The remaining problem with the textbook model is that the transition
 * from subthreshold the triode/saturation is not continuous.
 *
 * Realizing that the subthreshold and triode/saturation modes may both
 * be defined by independent (and equal) terms of Vgs and Vds,
 * respectively, the corresponding terms can be blended into (equal)
 * continuous functions suitable for table lookup.
 *
 * The EKV model (Enz, Krummenacher and Vittoz) essentially performs this
 * blending using an elegant mathematical formulation:
 *
 *     Ids = Is * (if - ir)
 *     Is = (2 * u*Cox * Ut^2)/k * W/L
 *     if = ln^2(1 + e^((k*(Vg - Vt) - Vs)/(2*Ut))
 *     ir = ln^2(1 + e^((k*(Vg - Vt) - Vd)/(2*Ut))
 *
 * For our purposes, the EKV model preserves two important properties
 * discussed above:
 *
 * - It consists of two independent terms, which can be represented by
 *   the same lookup table.
 * - It is symmetrical, i.e. it calculates current in both directions,
 *   facilitating a branch-free implementation.
 *
 * Rw in the circuit diagram above is a VCR (voltage controlled resistor),
 * as shown in the circuit diagram below.
 *
 *                      Vw
 *     
 *                      |
 *              Vdd     |
 *                 |---|
 *                _|_   |
 *              --    --| Vg
 *             |      __|__
 *             |      -----  Rw
 *             |      |   |
 *     vi ------------     -------- vo
 *
 *
 * In order to calculalate the current through the VCR, its gate voltage
 * must be determined.
 *
 * Assuming triode mode and applying Kirchoff's current law, we get the
 * following equation for Vg:
 *
 *     u*Cox/2*W/L*((Vddt - Vg)^2 - (Vddt - vi)^2 + (Vddt - Vg)^2 - (Vddt - Vw)^2) = 0
 *     2*(Vddt - Vg)^2 - (Vddt - vi)^2 - (Vddt - Vw)^2 = 0
 *     (Vddt - Vg) = sqrt(((Vddt - vi)^2 + (Vddt - Vw)^2)/2)
 *
 *     Vg = Vddt - sqrt(((Vddt - vi)^2 + (Vddt - Vw)^2)/2)
 */
class Integrator6581
{
private:
    const LUT* vcr_kVg;
    const LUT* vcr_n_Ids_term;
    const LUT* opamp_rev;

    float Vddt_Vw_2;
    mutable float vx;
    mutable float vc;

    const float kVddt;
    const float n_snake;

public:
    Integrator6581(const LUT* vcr_kVg, const LUT* vcr_n_Ids_term,
               const LUT* opamp_rev, float kVddt, float n_snake) :
        vcr_kVg(vcr_kVg),
        vcr_n_Ids_term(vcr_n_Ids_term),
        opamp_rev(opamp_rev),
        Vddt_Vw_2(0.f),
        vx(0.f),
        vc(0.f),
        kVddt(kVddt),
        n_snake(n_snake) {}

    void setVw(float Vw) { Vddt_Vw_2 = (kVddt - Vw) * (kVddt - Vw); }

    float solve(float vi) const;
};

} // namespace reSIDfp

#if RESID_INLINING || defined(INTEGRATOR_CPP)

namespace reSIDfp
{

RESID_INLINE
float Integrator6581::solve(float vi) const
{
    // Make sure Vgst>0 so we're not in subthreshold mode
    assert(vx < kVddt);

    // Check that transistor is actually in triode mode
    // Vds < Vgs - Vth
    assert(vi < kVddt);

    // "Snake" voltages for triode mode calculation.
    const float Vgst = kVddt - vx;
    const float Vgdt = kVddt - vi;

    const float Vgst_2 = Vgst * Vgst;
    const float Vgdt_2 = Vgdt * Vgdt;

    // "Snake" current, scaled by (1/m)*m*2^16*m*2^16 = m*2^32
    const float n_I_snake = n_snake * (Vgst_2 - Vgdt_2);

    // VCR gate voltage.       // Scaled by m*2^16
    // Vg = Vddt - sqrt(((Vddt - Vw)^2 + Vgdt^2)/2)
    const float kVg = vcr_kVg->output(((Vddt_Vw_2 + Vgdt_2) / 2.f) / 65536.f);

    // VCR voltages for EKV model table lookup.
    const float Vgs = (vx < kVg) ? kVg - vx : 0.;
    assert(Vgs < (1 << 16));
    const float Vgd = (vi < kVg) ? kVg - vi : 0.;
    assert(Vgd < (1 << 16));

    // VCR current, scaled by m*2^16
    const float n_I_vcr = vcr_n_Ids_term->output(Vgs) - vcr_n_Ids_term->output(Vgd);

    // Change in capacitor charge.
    vc += (n_I_snake / 65536.f) + n_I_vcr;

    // vx = g(vc)
    const float tmp = (vc / 2.f) + (1 << 15);
    assert(tmp < (1 << 16));
    vx = opamp_rev->output(tmp);

    // Return vo.
    return vx - vc;
}

} // namespace reSIDfp

#endif

#endif
