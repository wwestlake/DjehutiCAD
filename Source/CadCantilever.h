#pragma once
#include <string>
#include <vector>

namespace djehuti::cad::beam
{
// Euler-Bernoulli end-loaded rectangular cantilever. All inputs and outputs use coherent SI.
// breadth is parallel to the bending axis; depth is parallel to the transverse force.
struct Input
{
    double lengthM=0, breadthM=0, depthM=0, youngsModulusPa=0, forceN=0;
};
struct Station
{
    double xM=0, displacementM=0, slopeRad=0, momentNm=0;
    double extremeStressPa=0, extremeStrain=0;
};
struct Result
{
    bool valid=false;
    std::string error;
    double areaM2=0, secondMomentM4=0, extremeFibreM=0;
    double tipDisplacementM=0, reactionN=0, reactionMomentNm=0;
    double maximumStressPa=0, maximumStrain=0;
    std::vector<Station> stations;
};
Result solve(const Input&);
Station evaluate(const Input&, double xM);
}
