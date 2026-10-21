#include "CadCantilever.h"
#include <algorithm>
#include <cmath>

namespace djehuti::cad::beam
{
Station evaluate(const Input& b, double x)
{
    const double inertia=b.breadthM*std::pow(b.depthM,3)/12.0;
    Station s; s.xM=x;
    s.displacementM=b.forceN*x*x*(3*b.lengthM-x)/(6*b.youngsModulusPa*inertia);
    s.slopeRad=b.forceN*x*(2*b.lengthM-x)/(2*b.youngsModulusPa*inertia);
    s.momentNm=b.forceN*(b.lengthM-x);
    s.extremeStressPa=s.momentNm*(b.depthM/2)/inertia;
    s.extremeStrain=s.extremeStressPa/b.youngsModulusPa;
    return s;
}
Result solve(const Input& b)
{
    Result r;
    for(const double v:{b.lengthM,b.breadthM,b.depthM,b.youngsModulusPa})
        if(!std::isfinite(v)||v<=0){r.error="Beam dimensions and Young's modulus must be finite and positive.";return r;}
    if(!std::isfinite(b.forceN)||b.forceN==0){r.error="A finite, nonzero transverse end force is required.";return r;}
    // Conservative domain limits for this first beam backend; shear deformation and geometric nonlinearity
    // are deliberately absent. Reject rather than silently extrapolate beyond the declared assumptions.
    if(b.lengthM/std::max(b.breadthM,b.depthM)<10){r.error="Euler-Bernoulli requires length / largest section dimension >= 10.";return r;}
    r.areaM2=b.breadthM*b.depthM;
    r.secondMomentM4=b.breadthM*std::pow(b.depthM,3)/12;
    r.extremeFibreM=b.depthM/2;
    const auto tip=evaluate(b,b.lengthM), root=evaluate(b,0);
    r.tipDisplacementM=tip.displacementM;r.reactionN=-b.forceN;r.reactionMomentNm=-b.forceN*b.lengthM;
    r.maximumStressPa=std::abs(root.extremeStressPa);r.maximumStrain=r.maximumStressPa/b.youngsModulusPa;
    for(const double v:{r.areaM2,r.secondMomentM4,r.extremeFibreM,r.tipDisplacementM,r.reactionN,r.reactionMomentNm,r.maximumStressPa,r.maximumStrain})
        if(!std::isfinite(v)||v==0){r.error="Beam response is outside the supported numerical range.";return r;}
    if(std::abs(tip.displacementM)/b.lengthM>0.05||std::abs(tip.slopeRad)>0.1)
    {r.error="Small-deflection limit exceeded (tip/length <= 0.05 and tip slope <= 0.1 rad).";return r;}
    for(int i=0;i<=40;++i)r.stations.push_back(evaluate(b,b.lengthM*i/40));
    r.valid=true;return r;
}
}
