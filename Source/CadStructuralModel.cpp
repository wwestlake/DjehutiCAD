#include "CadStructuralModel.h"
#include "CadCraftStock.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <set>

namespace djehuti::cad::structural
{

SectionProperties computeSection(const juce::String& profile, double widthMm, double thicknessMm, double outerDiameterMm, double innerDiameterMm)
{
    SectionProperties s;
    s.profile = profile;
    s.widthM = widthMm / 1000.0;
    s.thicknessM = thicknessMm / 1000.0;
    s.outerDiameterM = outerDiameterMm / 1000.0;
    s.innerDiameterM = innerDiameterMm / 1000.0;

    const double pi = juce::MathConstants<double>::pi;

    if (profile == "rectangular" || profile == "roundedStick")
    {
        const double w = s.widthM;
        const double t = s.thicknessM;
        s.areaM2 = w * t;
        s.iyM4 = (w * std::pow(t, 3)) / 12.0; // Bending across thickness (local y)
        s.izM4 = (t * std::pow(w, 3)) / 12.0; // Bending across width (local z)
        s.syM3 = (w * std::pow(t, 2)) / 6.0;
        s.szM3 = (t * std::pow(w, 2)) / 6.0;

        const double a = std::max(w, t);
        const double b = std::min(w, t);
        s.jM4 = a * std::pow(b, 3) * (1.0 / 3.0 - 0.21 * (b / a) * (1.0 - std::pow(b, 4) / (12.0 * std::pow(a, 4))));
        s.wtM3 = s.jM4 / (b > 1e-9 ? b : 1e-9);
    }
    else if (profile == "rod")
    {
        const double d = s.outerDiameterM;
        const double r = d / 2.0;
        s.areaM2 = pi * r * r;
        s.iyM4 = (pi * std::pow(d, 4)) / 64.0;
        s.izM4 = s.iyM4;
        s.jM4 = (pi * std::pow(d, 4)) / 32.0;
        s.syM3 = (pi * std::pow(d, 3)) / 32.0;
        s.szM3 = s.syM3;
        s.wtM3 = (pi * std::pow(d, 3)) / 16.0;
    }
    else if (profile == "tube")
    {
        const double doM = s.outerDiameterM;
        const double diM = s.innerDiameterM;
        s.areaM2 = (pi * (doM * doM - diM * diM)) / 4.0;
        s.iyM4 = (pi * (std::pow(doM, 4) - std::pow(diM, 4))) / 64.0;
        s.izM4 = s.iyM4;
        s.jM4 = (pi * (std::pow(doM, 4) - std::pow(diM, 4))) / 32.0;
        s.syM3 = (pi * (std::pow(doM, 4) - std::pow(diM, 4))) / (32.0 * (doM > 1e-9 ? doM : 1e-9));
        s.szM3 = s.syM3;
        s.wtM3 = (pi * (std::pow(doM, 4) - std::pow(diM, 4))) / (16.0 * (doM > 1e-9 ? doM : 1e-9));
    }
    return s;
}

namespace
{

struct Vec3
{
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double norm() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const
    {
        const double n = norm();
        return n > 1e-12 ? Vec3(x / n, y / n, z / n) : Vec3(0, 0, 0);
    }
};

Vec3 transformPoint(const gp_Trsf& trsf, const Vec3& p)
{
    gp_Pnt gp(p.x, p.y, p.z);
    gp.Transform(trsf);
    return {gp.X(), gp.Y(), gp.Z()};
}

Vec3 transformVector(const gp_Trsf& trsf, const Vec3& v)
{
    gp_Vec gv(v.x, v.y, v.z);
    gv.Transform(trsf);
    return {gv.X(), gv.Y(), gv.Z()};
}

} // namespace

bool extractModel(const CadDocument& doc, const CadAnalysisCase& analysisCase,
                  std::vector<Node>& nodes, std::vector<Member>& members,
                  juce::String& diagnostic, juce::StringArray* warnings)
{
    nodes.clear();
    members.clear();

    const auto& mechanics = doc.mechanicsData();
    std::set<juce::String> targetInstances;
    if (!analysisCase.parts.empty())
    {
        for (const auto& p : analysisCase.parts)
            if (p.instance.isNotEmpty()) targetInstances.insert(p.instance);
    }

    struct RawMember
    {
        juce::String instanceId;
        Vec3 startWorldM;
        Vec3 endWorldM;
        Vec3 dirXM, dirYM, dirZM;
        double lengthM = 0.0;
        SectionProperties section;
        double E = 0.0, G = 0.0, rho = 0.0;
        double tensileStrengthPa = 0.0, compressiveStrengthPa = 0.0, shearStrengthPa = 0.0;
        bool hasStrength = false;
    };
    std::vector<RawMember> rawMembers;

    for (const auto& inst : mechanics.instances)
    {
        if (!targetInstances.empty() && targetInstances.find(inst.id) == targetInstances.end())
            continue;

        const auto* comp = doc.componentFor(inst.featureId);
        if (comp == nullptr || !craft::isStock(*comp))
        {
            if (targetInstances.find(inst.id) != targetInstances.end())
            {
                diagnostic = "Instance " + inst.id + " is not a slender craft stock member.";
                return false;
            }
            continue;
        }

        const auto g = (*comp)["geometry"];
        const auto profile = g["profile"].toString();
        const double origLengthMm = g["lengthMm"];
        const double widthMm = g.getProperty("widthMm", 10.0);
        const double thicknessMm = g.getProperty("thicknessMm", 3.0);
        const double outerDiameterMm = g.getProperty("outerDiameterMm", 8.0);
        const double innerDiameterMm = g.getProperty("innerDiameterMm", 6.0);

        // Account for length cuts
        double startX = 0.0;
        double endX = origLengthMm;
        if (g.hasProperty("cuts") && g["cuts"].isArray())
        {
            for (const auto& cut : *g["cuts"].getArray())
            {
                const auto type = cut["type"].toString();
                if (type == "length")
                {
                    const double pos = cut["positionMm"];
                    const auto side = cut.getProperty("side", "end").toString();
                    if (side == "end") endX = std::min(endX, pos);
                    else if (side == "start") startX = std::max(startX, pos);
                }
            }
        }
        const double effLengthMm = endX - startX;
        if (effLengthMm < 1.0)
        {
            if (warnings) warnings->add("Stock " + inst.id + " has effective length < 1 mm after cutting.");
            continue;
        }

        // Centerline in local coordinates (mm)
        double locY = 0.0, locZ = 0.0;
        if (profile == "rectangular" || profile == "roundedStick")
        {
            locY = widthMm / 2.0;
            locZ = thicknessMm / 2.0;
        }
        const Vec3 locStart(startX, locY, locZ);
        const Vec3 locEnd(endX, locY, locZ);

        const gp_Trsf trsf = inst.placement.toTrsf();
        const Vec3 worldStartMm = transformPoint(trsf, locStart);
        const Vec3 worldEndMm = transformPoint(trsf, locEnd);

        // Convert to meters
        const Vec3 worldStartM = worldStartMm * 0.001;
        const Vec3 worldEndM = worldEndMm * 0.001;

        const Vec3 dirX = (worldEndM - worldStartM).normalized();
        const Vec3 dirY = transformVector(trsf, Vec3(0, 1, 0)).normalized();
        const Vec3 dirZ = dirX.cross(dirY).normalized();

        // Material properties
        double E = 9500e6; // Default to typical craft wood stick (9.5 GPa)
        double nu = 0.35;
        double rho = 550.0;
        double G = E / (2.0 * (1.0 + nu));
        double sigTensile = 80e6;
        double sigComp = 40e6;
        double tauShear = 8e6;
        bool hasStrength = true;

        if (const auto* assign = doc.materialsData().assignmentFor(inst.featureId))
        {
            if (const auto* mat = doc.materialsData().find(assign->materialId))
            {
                if (mat->youngsModulusMPa.has_value())
                {
                    E = *mat->youngsModulusMPa * 1e6;
                    nu = mat->poissonsRatio.value_or(0.35);
                    G = mat->shearModulusMPa.has_value() ? (*mat->shearModulusMPa * 1e6) : (E / (2.0 * (1.0 + nu)));
                }
                if (mat->densityKgPerM3.has_value()) rho = *mat->densityKgPerM3;
                if (mat->tensileStrengthMPa.has_value()) sigTensile = *mat->tensileStrengthMPa * 1e6;
                if (mat->compressiveStrengthMPa.has_value()) sigComp = *mat->compressiveStrengthMPa * 1e6;
                if (mat->shearStrengthMPa.has_value()) tauShear = *mat->shearStrengthMPa * 1e6;
                else if (mat->yieldStrengthMPa.has_value())
                {
                    sigTensile = *mat->yieldStrengthMPa * 1e6;
                    sigComp = sigTensile;
                    tauShear = sigTensile / std::sqrt(3.0);
                }
            }
        }

        RawMember rm;
        rm.instanceId = inst.id;
        rm.startWorldM = worldStartM;
        rm.endWorldM = worldEndM;
        rm.dirXM = dirX;
        rm.dirYM = dirY;
        rm.dirZM = dirZ;
        rm.lengthM = (worldEndM - worldStartM).norm();
        rm.section = computeSection(profile, widthMm, thicknessMm, outerDiameterMm, innerDiameterMm);
        rm.E = E; rm.G = G; rm.rho = rho;
        rm.tensileStrengthPa = sigTensile;
        rm.compressiveStrengthPa = sigComp;
        rm.shearStrengthPa = tauShear;
        rm.hasStrength = hasStrength;

        rawMembers.push_back(rm);
    }

    if (rawMembers.empty())
    {
        diagnostic = "No structural members found for analysis.";
        return false;
    }

    // 1. Identify valid bonded pairs
    const auto& joints = mechanics.bondedJoints;
    std::set<std::pair<juce::String, juce::String>> validBonds;
    for (const auto& j : joints)
    {
        if (j.status == "valid")
        {
            validBonds.insert({j.instanceA, j.instanceB});
            validBonds.insert({j.instanceB, j.instanceA});
        }
    }

    // 2. Identify interior split points along members where other bonded members attach or loads act
    std::vector<std::vector<double>> memberSplits(rawMembers.size());
    for (size_t i = 0; i < rawMembers.size(); ++i)
    {
        memberSplits[i].push_back(0.0);
        memberSplits[i].push_back(rawMembers[i].lengthM);
    }

    // Interior contact with bonded members
    for (size_t i = 0; i < rawMembers.size(); ++i)
    {
        const auto& rmA = rawMembers[i];
        for (size_t j = 0; j < rawMembers.size(); ++j)
        {
            if (i == j) continue;
            const auto& rmB = rawMembers[j];
            if (validBonds.count({rmA.instanceId, rmB.instanceId}) == 0) continue;

            const Vec3 ptsB[2] = { rmB.startWorldM, rmB.endWorldM };
            for (int k = 0; k < 2; ++k)
            {
                const Vec3& P = ptsB[k];
                const double s = (P - rmA.startWorldM).dot(rmA.dirXM);
                if (s > 0.005 && s < rmA.lengthM - 0.005)
                {
                    const Vec3 proj = rmA.startWorldM + rmA.dirXM * s;
                    if ((P - proj).norm() < 0.030) // within 30 mm
                    {
                        memberSplits[i].push_back(s);
                    }
                }
            }
        }

        // Midspan split for load on center of member
        for (const auto& load : analysisCase.loads)
        {
            if (load.target.instance == rmA.instanceId)
            {
                if (load.pointMm.has_value())
                {
                    const Vec3 P((*load.pointMm)[0] * 0.001, (*load.pointMm)[1] * 0.001, (*load.pointMm)[2] * 0.001);
                    const double s = (P - rmA.startWorldM).dot(rmA.dirXM);
                    if (s > 0.005 && s < rmA.lengthM - 0.005 && (P - (rmA.startWorldM + rmA.dirXM * s)).norm() < 0.030)
                        memberSplits[i].push_back(s);
                }
                else if (load.target.face.contains("center") || load.target.face.contains("midspan") || load.name.toLowerCase().contains("midspan"))
                {
                    memberSplits[i].push_back(rmA.lengthM * 0.5);
                }
            }
        }
    }

    // 3. Build sub-members
    struct SubMemberData
    {
        juce::String instanceId;
        Vec3 startWorldM, endWorldM;
        double lengthM;
        Vec3 dirXM, dirYM, dirZM;
        SectionProperties section;
        double E, G, rho;
        double tensileStrengthPa, compressiveStrengthPa, shearStrengthPa;
        bool hasStrength;
    };
    std::vector<SubMemberData> subMembers;

    for (size_t i = 0; i < rawMembers.size(); ++i)
    {
        const auto& rm = rawMembers[i];
        auto sList = memberSplits[i];
        std::sort(sList.begin(), sList.end());

        std::vector<double> uniqueS;
        for (double s : sList)
        {
            if (uniqueS.empty() || (s - uniqueS.back()) > 0.004)
                uniqueS.push_back(s);
        }
        if (uniqueS.empty() || uniqueS.front() > 0.004) uniqueS.insert(uniqueS.begin(), 0.0);
        if ((rm.lengthM - uniqueS.back()) > 0.004) uniqueS.push_back(rm.lengthM);

        for (size_t k = 0; k + 1 < uniqueS.size(); ++k)
        {
            const double s1 = uniqueS[k];
            const double s2 = uniqueS[k + 1];
            if (s2 - s1 < 0.001) continue;

            SubMemberData sm;
            sm.instanceId = rm.instanceId;
            sm.startWorldM = rm.startWorldM + rm.dirXM * s1;
            sm.endWorldM = rm.startWorldM + rm.dirXM * s2;
            sm.lengthM = s2 - s1;
            sm.dirXM = rm.dirXM; sm.dirYM = rm.dirYM; sm.dirZM = rm.dirZM;
            sm.section = rm.section;
            sm.E = rm.E; sm.G = rm.G; sm.rho = rm.rho;
            sm.tensileStrengthPa = rm.tensileStrengthPa;
            sm.compressiveStrengthPa = rm.compressiveStrengthPa;
            sm.shearStrengthPa = rm.shearStrengthPa;
            sm.hasStrength = rm.hasStrength;
            subMembers.push_back(sm);
        }
    }

    // 4. Snap endpoints of bonded members to corresponding contact points
    for (size_t i = 0; i < subMembers.size(); ++i)
    {
        auto& smA = subMembers[i];
        for (size_t j = 0; j < subMembers.size(); ++j)
        {
            if (i == j) continue;
            auto& smB = subMembers[j];
            if (validBonds.count({smA.instanceId, smB.instanceId}) == 0) continue;

            Vec3* ptsA[2] = { &smA.startWorldM, &smA.endWorldM };
            Vec3* ptsB[2] = { &smB.startWorldM, &smB.endWorldM };
            for (int a = 0; a < 2; ++a)
            {
                for (int b = 0; b < 2; ++b)
                {
                    if ((*ptsA[a] - *ptsB[b]).norm() < 0.020)
                    {
                        *ptsB[b] = *ptsA[a];
                    }
                }
            }
        }
    }

    const double mergeToleranceM = 0.005; // 5 mm clustering for connected joints

    auto findOrCreateNode = [&](const Vec3& pt, const juce::String& instId, bool allowMergeWithOther) -> int
    {
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const Vec3 np(nodes[i].positionM[0], nodes[i].positionM[1], nodes[i].positionM[2]);
            if ((np - pt).norm() <= mergeToleranceM)
            {
                bool canShare = false;
                for (const auto& existingInst : nodes[i].instanceIds)
                {
                    if (existingInst == instId) { canShare = true; break; }
                    if (allowMergeWithOther && validBonds.count({existingInst, instId}) > 0)
                    {
                        canShare = true;
                        break;
                    }
                }
                if (canShare || (joints.empty() && allowMergeWithOther))
                {
                    if (!nodes[i].instanceIds.contains(instId))
                        nodes[i].instanceIds.add(instId);
                    return (int) i;
                }
            }
        }
        Node n;
        n.id = (int) nodes.size();
        n.positionM = {pt.x, pt.y, pt.z};
        n.instanceIds.add(instId);
        nodes.push_back(n);
        return n.id;
    };

    for (size_t i = 0; i < subMembers.size(); ++i)
    {
        const auto& sm = subMembers[i];
        const int n1 = findOrCreateNode(sm.startWorldM, sm.instanceId, true);
        const int n2 = findOrCreateNode(sm.endWorldM, sm.instanceId, true);

        Member m;
        m.id = (int) members.size();
        m.instanceId = sm.instanceId;
        m.node1 = n1;
        m.node2 = n2;
        m.lengthM = sm.lengthM;
        m.dirX = {sm.dirXM.x, sm.dirXM.y, sm.dirXM.z};
        m.dirY = {sm.dirYM.x, sm.dirYM.y, sm.dirYM.z};
        m.dirZ = {sm.dirZM.x, sm.dirZM.y, sm.dirZM.z};
        m.section = sm.section;
        m.youngsModulusPa = sm.E;
        m.shearModulusPa = sm.G;
        m.densityKgPerM3 = sm.rho;
        m.tensileStrengthPa = sm.tensileStrengthPa;
        m.compressiveStrengthPa = sm.compressiveStrengthPa;
        m.shearStrengthPa = sm.shearStrengthPa;
        m.hasStrengthLimits = sm.hasStrength;

        members.push_back(m);
    }

    // Apply Supports
    for (const auto& sup : analysisCase.supports)
    {
        const juce::String targetInst = sup.target.instance;
        int targetNode = -1;

        const RawMember* targetRaw = nullptr;
        for (const auto& rm : rawMembers)
        {
            if (rm.instanceId == targetInst) { targetRaw = &rm; break; }
        }

        if (targetRaw != nullptr)
        {
            Vec3 targetPos = targetRaw->startWorldM;
            if (sup.target.face.contains("end") || sup.name.toLowerCase().contains("right") || sup.name.toLowerCase().contains("end"))
                targetPos = targetRaw->endWorldM;

            double minDist = 1e9;
            for (const auto& nd : nodes)
            {
                if (nd.instanceIds.contains(targetInst))
                {
                    const Vec3 np(nd.positionM[0], nd.positionM[1], nd.positionM[2]);
                    const double d = (np - targetPos).norm();
                    if (d < minDist) { minDist = d; targetNode = nd.id; }
                }
            }
        }

        if (targetNode >= 0 && targetNode < (int) nodes.size())
        {
            auto& n = nodes[targetNode];
            for (const auto& dof : sup.fixed)
            {
                if (dof == "ux") n.fixedDOFs[0] = true;
                else if (dof == "uy") n.fixedDOFs[1] = true;
                else if (dof == "uz") n.fixedDOFs[2] = true;
                else if (dof == "rx") n.fixedDOFs[3] = true;
                else if (dof == "ry") n.fixedDOFs[4] = true;
                else if (dof == "rz") n.fixedDOFs[5] = true;
            }
        }
    }

    // Apply Loads
    for (const auto& load : analysisCase.loads)
    {
        const juce::String targetInst = load.target.instance;
        int targetNode = -1;

        if (load.pointMm.has_value())
        {
            const Vec3 p((*load.pointMm)[0] * 0.001, (*load.pointMm)[1] * 0.001, (*load.pointMm)[2] * 0.001);
            double minDist = 1e9;
            for (const auto& nd : nodes)
            {
                if (nd.instanceIds.contains(targetInst) || targetInst.isEmpty())
                {
                    const Vec3 np(nd.positionM[0], nd.positionM[1], nd.positionM[2]);
                    const double d = (np - p).norm();
                    if (d < minDist) { minDist = d; targetNode = nd.id; }
                }
            }
        }

        if (targetNode < 0)
        {
            const RawMember* targetRaw = nullptr;
            for (const auto& rm : rawMembers)
            {
                if (rm.instanceId == targetInst) { targetRaw = &rm; break; }
            }
            if (targetRaw != nullptr)
            {
                Vec3 targetPos = targetRaw->startWorldM;
                if (load.target.face.contains("end"))
                    targetPos = targetRaw->endWorldM;
                else if (load.target.face.contains("center") || load.target.face.contains("midspan") || load.name.toLowerCase().contains("midspan"))
                    targetPos = (targetRaw->startWorldM + targetRaw->endWorldM) * 0.5;

                double minDist = 1e9;
                for (const auto& nd : nodes)
                {
                    if (nd.instanceIds.contains(targetInst))
                    {
                        const Vec3 np(nd.positionM[0], nd.positionM[1], nd.positionM[2]);
                        const double d = (np - targetPos).norm();
                        if (d < minDist) { minDist = d; targetNode = nd.id; }
                    }
                }
            }
        }

        if (targetNode >= 0 && targetNode < (int) nodes.size())
        {
            const double mag = load.magnitude;
            const auto dir = load.direction;
            if (load.type == "force")
            {
                nodes[targetNode].appliedLoad[0] += mag * dir[0];
                nodes[targetNode].appliedLoad[1] += mag * dir[1];
                nodes[targetNode].appliedLoad[2] += mag * dir[2];
            }
            else if (load.type == "moment")
            {
                nodes[targetNode].appliedLoad[3] += mag * dir[0];
                nodes[targetNode].appliedLoad[4] += mag * dir[1];
                nodes[targetNode].appliedLoad[5] += mag * dir[2];
            }
        }
    }

    // Apply Gravity / self-weight
    if (analysisCase.gravity.has_value())
    {
        const auto g = *analysisCase.gravity;
        for (const auto& m : members)
        {
            const double mass = m.densityKgPerM3 * m.section.areaM2 * m.lengthM;
            const double fx = (mass * g[0]) / 2.0;
            const double fy = (mass * g[1]) / 2.0;
            const double fz = (mass * g[2]) / 2.0;

            nodes[m.node1].appliedLoad[0] += fx;
            nodes[m.node1].appliedLoad[1] += fy;
            nodes[m.node1].appliedLoad[2] += fz;

            nodes[m.node2].appliedLoad[0] += fx;
            nodes[m.node2].appliedLoad[1] += fy;
            nodes[m.node2].appliedLoad[2] += fz;
        }
    }

    return true;
}

StructuralResult solve(const std::vector<Node>& nodes, const std::vector<Member>& members,
                       const StructuralOptions& options)
{
    StructuralResult res;
    const int numNodes = (int) nodes.size();
    if (numNodes == 0 || members.empty())
    {
        res.status = "error";
        res.diagnostic = "Model contains no nodes or members.";
        return res;
    }

    const int numDofs = numNodes * 6;
    std::vector<double> K(numDofs * numDofs, 0.0);
    std::vector<double> F(numDofs, 0.0);

    for (int i = 0; i < numNodes; ++i)
    {
        for (int d = 0; d < 6; ++d)
        {
            F[i * 6 + d] = nodes[i].appliedLoad[d];
            if (d < 3) res.totalAppliedForceN[d] += nodes[i].appliedLoad[d];
        }
    }

    // Assemble member stiffness matrices
    for (const auto& m : members)
    {
        const double L = m.lengthM;
        if (L < 1e-9) continue;

        const double E = m.youngsModulusPa;
        const double G = m.shearModulusPa;
        const double A = m.section.areaM2;
        const double Iy = m.section.iyM4;
        const double Iz = m.section.izM4;
        const double J = m.section.jM4;

        // Local 12x12 stiffness matrix
        double kl[12][12] = {};

        const double ea_l = (E * A) / L;
        const double gj_l = (G * J) / L;

        // Axial
        kl[0][0] =  ea_l; kl[0][6] = -ea_l;
        kl[6][0] = -ea_l; kl[6][6] =  ea_l;

        // Torsion
        kl[3][3] =  gj_l; kl[3][9] = -gj_l;
        kl[9][3] = -gj_l; kl[9][9] =  gj_l;

        // Bending in local xy (shear y, moment z)
        const double ky1 = (12.0 * E * Iz) / std::pow(L, 3);
        const double ky2 = (6.0 * E * Iz) / std::pow(L, 2);
        const double ky3 = (4.0 * E * Iz) / L;
        const double ky4 = (2.0 * E * Iz) / L;

        kl[1][1] =  ky1; kl[1][5] =  ky2; kl[1][7] = -ky1; kl[1][11] =  ky2;
        kl[5][1] =  ky2; kl[5][5] =  ky3; kl[5][7] = -ky2; kl[5][11] =  ky4;
        kl[7][1] = -ky1; kl[7][5] = -ky2; kl[7][7] =  ky1; kl[7][11] = -ky2;
        kl[11][1] = ky2; kl[11][5] = ky4; kl[11][7] = -ky2; kl[11][11] = ky3;

        // Bending in local xz (shear z, moment y)
        const double kz1 = (12.0 * E * Iy) / std::pow(L, 3);
        const double kz2 = (6.0 * E * Iy) / std::pow(L, 2);
        const double kz3 = (4.0 * E * Iy) / L;
        const double kz4 = (2.0 * E * Iy) / L;

        kl[2][2] =  kz1; kl[2][4] = -kz2; kl[2][8] = -kz1; kl[2][10] = -kz2;
        kl[4][2] = -kz2; kl[4][4] =  kz3; kl[4][8] =  kz2; kl[4][10] =  kz4;
        kl[8][2] = -kz1; kl[8][4] =  kz2; kl[8][8] =  kz1; kl[8][10] =  kz2;
        kl[10][2] = -kz2; kl[10][4] = kz4; kl[10][8] = kz2; kl[10][10] = kz3;

        // Rotation matrix R (3x3): rows are local unit vectors in world space
        double R[3][3] = {
            {m.dirX[0], m.dirX[1], m.dirX[2]},
            {m.dirY[0], m.dirY[1], m.dirY[2]},
            {m.dirZ[0], m.dirZ[1], m.dirZ[2]}
        };

        // Transform 12x12: T = diag(R, R, R, R)
        // kg = T^T * kl * T
        double kg[12][12] = {};
        for (int blkRow = 0; blkRow < 4; ++blkRow)
        {
            for (int blkCol = 0; blkCol < 4; ++blkCol)
            {
                // Multiply R^T * kl_blk * R
                double tmp[3][3] = {};
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        for (int k = 0; k < 3; ++k)
                            tmp[r][c] += R[k][r] * kl[blkRow * 3 + k][blkCol * 3 + c];

                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        for (int k = 0; k < 3; ++k)
                            kg[blkRow * 3 + r][blkCol * 3 + c] += tmp[r][k] * R[k][c];
            }
        }

        // Map to global DOFs
        const int dofMap[12] = {
            m.node1 * 6 + 0, m.node1 * 6 + 1, m.node1 * 6 + 2, m.node1 * 6 + 3, m.node1 * 6 + 4, m.node1 * 6 + 5,
            m.node2 * 6 + 0, m.node2 * 6 + 1, m.node2 * 6 + 2, m.node2 * 6 + 3, m.node2 * 6 + 4, m.node2 * 6 + 5
        };

        for (int r = 0; r < 12; ++r)
        {
            for (int c = 0; c < 12; ++c)
            {
                K[dofMap[r] * numDofs + dofMap[c]] += kg[r][c];
            }
        }
    }

    const auto K_orig = K;

    // Apply boundary conditions
    int restrainedCount = 0;
    for (int i = 0; i < numNodes; ++i)
    {
        for (int d = 0; d < 6; ++d)
        {
            if (nodes[i].fixedDOFs[d])
            {
                ++restrainedCount;
                const int dof = i * 6 + d;
                for (int c = 0; c < numDofs; ++c)
                {
                    K[dof * numDofs + c] = 0.0;
                    K[c * numDofs + dof] = 0.0;
                }
                K[dof * numDofs + dof] = 1.0;
                F[dof] = 0.0;
            }
        }
    }

    if (restrainedCount < 3)
    {
        res.status = "unsupported";
        res.diagnostic = "Structure has insufficient supports (less than 3 restrained DOFs). Rigid body motion expected.";
        res.nodeResults.resize(numNodes);
        res.memberResults.resize(members.size());
        return res;
    }

    // Solve system K * U = F using LU decomposition with partial pivoting
    std::vector<double> A = K;
    std::vector<double> b = F;
    std::vector<int> p(numDofs);
    for (int i = 0; i < numDofs; ++i) p[i] = i;

    double maxDiag = 0.0;
    for (int i = 0; i < numDofs; ++i) maxDiag = std::max(maxDiag, std::abs(A[i * numDofs + i]));

    for (int k = 0; k < numDofs; ++k)
    {
        int maxRow = k;
        double maxVal = std::abs(A[k * numDofs + k]);
        for (int i = k + 1; i < numDofs; ++i)
        {
            const double val = std::abs(A[i * numDofs + k]);
            if (val > maxVal) { maxVal = val; maxRow = i; }
        }

        if (maxVal < 1e-12 * (maxDiag > 1e-6 ? maxDiag : 1.0))
        {
            res.status = "singular";
            res.diagnostic = "Singular matrix detected: model is unconstrained or has rigid-body mechanisms. Ensure supports adequately restrain all degrees of freedom.";
            res.nodeResults.resize(numNodes);
            res.memberResults.resize(members.size());
            return res;
        }

        if (maxRow != k)
        {
            std::swap(p[k], p[maxRow]);
            for (int j = 0; j < numDofs; ++j) std::swap(A[k * numDofs + j], A[maxRow * numDofs + j]);
            std::swap(b[k], b[maxRow]);
        }

        for (int i = k + 1; i < numDofs; ++i)
        {
            A[i * numDofs + k] /= A[k * numDofs + k];
            for (int j = k + 1; j < numDofs; ++j)
                A[i * numDofs + j] -= A[i * numDofs + k] * A[k * numDofs + j];
            b[i] -= A[i * numDofs + k] * b[k];
        }
    }

    // Back-substitution
    std::vector<double> U(numDofs, 0.0);
    for (int i = numDofs - 1; i >= 0; --i)
    {
        double sum = b[i];
        for (int j = i + 1; j < numDofs; ++j) sum -= A[i * numDofs + j] * U[j];
        U[i] = sum / A[i * numDofs + i];
    }

    // Reaction forces: R = K_orig * U - F_applied
    std::vector<double> R(numDofs, 0.0);
    for (int i = 0; i < numDofs; ++i)
    {
        double ku = 0.0;
        for (int j = 0; j < numDofs; ++j) ku += K_orig[i * numDofs + j] * U[j];
        const int nodeIdx = i / 6;
        const int dofIdx = i % 6;
        R[i] = ku - nodes[nodeIdx].appliedLoad[dofIdx];
    }

    // Node results
    res.nodeResults.resize(numNodes);
    for (int i = 0; i < numNodes; ++i)
    {
        NodeResult nr;
        nr.nodeId = i;
        nr.displacementM = {U[i * 6 + 0], U[i * 6 + 1], U[i * 6 + 2]};
        nr.rotationRad   = {U[i * 6 + 3], U[i * 6 + 4], U[i * 6 + 5]};
        nr.reactionForceN   = {R[i * 6 + 0], R[i * 6 + 1], R[i * 6 + 2]};
        nr.reactionMomentNm = {R[i * 6 + 3], R[i * 6 + 4], R[i * 6 + 5]};
        nr.displacementMagnitudeM = std::sqrt(U[i*6+0]*U[i*6+0] + U[i*6+1]*U[i*6+1] + U[i*6+2]*U[i*6+2]);
        res.maxDeflectionM = std::max(res.maxDeflectionM, nr.displacementMagnitudeM);

        if (nodes[i].fixedDOFs[0]) res.totalReactionForceN[0] += nr.reactionForceN[0];
        if (nodes[i].fixedDOFs[1]) res.totalReactionForceN[1] += nr.reactionForceN[1];
        if (nodes[i].fixedDOFs[2]) res.totalReactionForceN[2] += nr.reactionForceN[2];

        res.nodeResults[i] = nr;
    }

    // Member results and station calculation
    res.memberResults.resize(members.size());
    for (size_t mi = 0; mi < members.size(); ++mi)
    {
        const auto& m = members[mi];
        MemberResult mr;
        mr.memberId = m.id;
        mr.instanceId = m.instanceId;

        const double L = m.lengthM;
        const double E = m.youngsModulusPa;
        const double G = m.shearModulusPa;
        const double A = m.section.areaM2;
        const double Iy = m.section.iyM4;
        const double Iz = m.section.izM4;
        const double J = m.section.jM4;

        // Global element displacement vector
        double u_glob[12] = {
            U[m.node1 * 6 + 0], U[m.node1 * 6 + 1], U[m.node1 * 6 + 2], U[m.node1 * 6 + 3], U[m.node1 * 6 + 4], U[m.node1 * 6 + 5],
            U[m.node2 * 6 + 0], U[m.node2 * 6 + 1], U[m.node2 * 6 + 2], U[m.node2 * 6 + 3], U[m.node2 * 6 + 4], U[m.node2 * 6 + 5]
        };

        // Transform to local: u_loc = T * u_glob
        double R[3][3] = {
            {m.dirX[0], m.dirX[1], m.dirX[2]},
            {m.dirY[0], m.dirY[1], m.dirY[2]},
            {m.dirZ[0], m.dirZ[1], m.dirZ[2]}
        };
        double u_loc[12] = {};
        for (int blk = 0; blk < 4; ++blk)
        {
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    u_loc[blk * 3 + r] += R[r][c] * u_glob[blk * 3 + c];
        }

        // Local internal forces
        const double ea_l = (E * A) / L;
        const double gj_l = (G * J) / L;
        const double ky1 = (12.0 * E * Iz) / std::pow(L, 3);
        const double ky2 = (6.0 * E * Iz) / std::pow(L, 2);
        const double ky3 = (4.0 * E * Iz) / L;
        const double ky4 = (2.0 * E * Iz) / L;

        const double kz1 = (12.0 * E * Iy) / std::pow(L, 3);
        const double kz2 = (6.0 * E * Iy) / std::pow(L, 2);
        const double kz3 = (4.0 * E * Iy) / L;
        const double kz4 = (2.0 * E * Iy) / L;

        const double f0 =  ea_l * u_loc[0] - ea_l * u_loc[6];
        const double f6 = -ea_l * u_loc[0] + ea_l * u_loc[6];

        const double f1 =  ky1 * u_loc[1] + ky2 * u_loc[5] - ky1 * u_loc[7] + ky2 * u_loc[11];
        const double f5 =  ky2 * u_loc[1] + ky3 * u_loc[5] - ky2 * u_loc[7] + ky4 * u_loc[11];
        const double f7 = -ky1 * u_loc[1] - ky2 * u_loc[5] + ky1 * u_loc[7] - ky2 * u_loc[11];
        const double f11=  ky2 * u_loc[1] + ky4 * u_loc[5] - ky2 * u_loc[7] + ky3 * u_loc[11];

        const double f2 =  kz1 * u_loc[2] - kz2 * u_loc[4] - kz1 * u_loc[8] - kz2 * u_loc[10];
        const double f4 = -kz2 * u_loc[2] + kz3 * u_loc[4] + kz2 * u_loc[8] + kz4 * u_loc[10];
        const double f8 = -kz1 * u_loc[2] + kz2 * u_loc[4] + kz1 * u_loc[8] + kz2 * u_loc[10];
        const double f10= -kz2 * u_loc[2] + kz4 * u_loc[4] + kz2 * u_loc[8] + kz3 * u_loc[10];

        const double f3 =  gj_l * u_loc[3] - gj_l * u_loc[9];
        const double f9 = -gj_l * u_loc[3] + gj_l * u_loc[9];

        mr.axialForceN = (f6 - f0) / 2.0;
        mr.shearForceYM = std::max(std::abs(f1), std::abs(f7));
        mr.shearForceZM = std::max(std::abs(f2), std::abs(f8));
        mr.torsionNm = std::max(std::abs(f3), std::abs(f9));
        mr.bendingMomentYNm = std::max(std::abs(f4), std::abs(f10));
        mr.bendingMomentZNm = std::max(std::abs(f5), std::abs(f11));

        mr.maxAxialForceN = std::abs(mr.axialForceN);
        mr.maxShearForceN = std::sqrt(mr.shearForceYM * mr.shearForceYM + mr.shearForceZM * mr.shearForceZM);
        mr.maxBendingMomentNm = std::sqrt(mr.bendingMomentYNm * mr.bendingMomentYNm + mr.bendingMomentZNm * mr.bendingMomentZNm);
        mr.maxTorsionNm = mr.torsionNm;

        // Stress
        const double normalStressAxial = A > 1e-12 ? (mr.maxAxialForceN / A) : 0.0;
        const double normalStressBendingY = m.section.syM3 > 1e-12 ? (mr.bendingMomentYNm / m.section.syM3) : 0.0;
        const double normalStressBendingZ = m.section.szM3 > 1e-12 ? (mr.bendingMomentZNm / m.section.szM3) : 0.0;
        mr.maxNormalStressPa = normalStressAxial + normalStressBendingY + normalStressBendingZ;

        const double shearStressTorsion = m.section.wtM3 > 1e-12 ? (mr.maxTorsionNm / m.section.wtM3) : 0.0;
        const double shearStressDirect = A > 1e-12 ? (mr.maxShearForceN / A) : 0.0;
        mr.maxShearStressPa = shearStressTorsion + shearStressDirect;

        const double limitStress = m.tensileStrengthPa > 1e3 ? m.tensileStrengthPa : (m.youngsModulusPa * 0.002);
        mr.utilizationRatio = mr.maxNormalStressPa / limitStress;

        res.maxNormalStressPa = std::max(res.maxNormalStressPa, mr.maxNormalStressPa);
        res.maxShearStressPa = std::max(res.maxShearStressPa, mr.maxShearStressPa);
        res.maxUtilizationRatio = std::max(res.maxUtilizationRatio, mr.utilizationRatio);

        // Stations along member (Hermite cubics)
        const int numStations = 10;
        const Vec3 startP(nodes[m.node1].positionM[0], nodes[m.node1].positionM[1], nodes[m.node1].positionM[2]);
        const Vec3 endP(nodes[m.node2].positionM[0], nodes[m.node2].positionM[1], nodes[m.node2].positionM[2]);

        for (int si = 0; si <= numStations; ++si)
        {
            const double xi = (double) si / (double) numStations;
            const double x = xi * L;

            // Hermite basis functions
            const double n1 = 1.0 - 3.0 * xi * xi + 2.0 * std::pow(xi, 3);
            const double n2 = L * (xi - 2.0 * xi * xi + std::pow(xi, 3));
            const double n3 = 3.0 * xi * xi - 2.0 * std::pow(xi, 3);
            const double n4 = L * (-xi * xi + std::pow(xi, 3));

            const double ux = (1.0 - xi) * u_loc[0] + xi * u_loc[6];
            const double vy = n1 * u_loc[1] + n2 * u_loc[5] + n3 * u_loc[7] + n4 * u_loc[11];
            const double vz = n1 * u_loc[2] - n2 * u_loc[4] + n3 * u_loc[8] - n4 * u_loc[10];

            // Transform local displacement to global: u_world = R^T * [ux, vy, vz]
            Vec3 u_world;
            u_world.x = R[0][0] * ux + R[1][0] * vy + R[2][0] * vz;
            u_world.y = R[0][1] * ux + R[1][1] * vy + R[2][1] * vz;
            u_world.z = R[0][2] * ux + R[1][2] * vy + R[2][2] * vz;

            const Vec3 posUndeformed = startP + (endP - startP) * xi;

            Station st;
            st.xM = x;
            st.worldPosM = {posUndeformed.x, posUndeformed.y, posUndeformed.z};
            st.displacementM = {u_world.x, u_world.y, u_world.z};
            st.normalStressPa = mr.maxNormalStressPa; // Station approximate envelope
            mr.stations.push_back(st);
        }

        res.memberResults[mi] = mr;
    }

    res.solved = true;
    res.status = "completed";
    res.overlays = generateOverlays(nodes, members, res, options);

    return res;
}

std::vector<CadOverlayLine> generateOverlays(const std::vector<Node>& nodes,
                                             const std::vector<Member>& members,
                                             const StructuralResult& result,
                                             const StructuralOptions& options)
{
    std::vector<CadOverlayLine> lines;

    const double scale = options.visualDeformationScale;

    // 1. Undeformed centerlines (subtle translucent grey/cyan)
    if (options.showUndeformed)
    {
        for (const auto& m : members)
        {
            CadOverlayLine line;
            line.a = { (float)(nodes[m.node1].positionM[0] * 1000.0),
                       (float)(nodes[m.node1].positionM[1] * 1000.0),
                       (float)(nodes[m.node1].positionM[2] * 1000.0) };
            line.b = { (float)(nodes[m.node2].positionM[0] * 1000.0),
                       (float)(nodes[m.node2].positionM[1] * 1000.0),
                       (float)(nodes[m.node2].positionM[2] * 1000.0) };
            line.argb = 0x80808080;
            lines.push_back(line);
        }
    }

    // 2. Deformed shape (station segments colored by stress or deflection)
    if (options.showDeformed && result.solved)
    {
        for (const auto& mr : result.memberResults)
        {
            if (mr.stations.size() < 2) continue;

            const double util = juce::jlimit(0.0, 1.0, mr.utilizationRatio);
            juce::Colour c = options.colorByStress
                ? juce::Colour::fromFloatRGBA((float)juce::jmin(1.0, 2.0 * util), (float)juce::jmin(1.0, 2.0 * (1.0 - util)), 0.2f, 1.0f)
                : juce::Colour(0xffff66dd);

            for (size_t s = 0; s + 1 < mr.stations.size(); ++s)
            {
                const auto& s1 = mr.stations[s];
                const auto& s2 = mr.stations[s + 1];

                CadOverlayLine line;
                line.a = { (float)((s1.worldPosM[0] + scale * s1.displacementM[0]) * 1000.0),
                           (float)((s1.worldPosM[1] + scale * s1.displacementM[1]) * 1000.0),
                           (float)((s1.worldPosM[2] + scale * s1.displacementM[2]) * 1000.0) };
                line.b = { (float)((s2.worldPosM[0] + scale * s2.displacementM[0]) * 1000.0),
                           (float)((s2.worldPosM[1] + scale * s2.displacementM[1]) * 1000.0),
                           (float)((s2.worldPosM[2] + scale * s2.displacementM[2]) * 1000.0) };
                line.argb = c.getARGB();
                lines.push_back(line);
            }
        }
    }

    // 3. Support glyphs & Load arrows
    if (options.showLoadsAndSupports)
    {
        for (const auto& n : nodes)
        {
            const float px = (float)(n.positionM[0] * 1000.0);
            const float py = (float)(n.positionM[1] * 1000.0);
            const float pz = (float)(n.positionM[2] * 1000.0);

            // Support: small cross marker (cyan)
            if (n.fixedDOFs[0] || n.fixedDOFs[1] || n.fixedDOFs[2])
            {
                const float s = 6.0f;
                lines.push_back({ {px - s, py, pz}, {px + s, py, pz}, 0xff44dddd });
                lines.push_back({ {px, py - s, pz}, {px, py + s, pz}, 0xff44dddd });
                lines.push_back({ {px, py, pz - s}, {px, py, pz + s}, 0xff44dddd });
            }

            // Load arrow (orange)
            const double fx = n.appliedLoad[0];
            const double fy = n.appliedLoad[1];
            const double fz = n.appliedLoad[2];
            const double fmag = std::sqrt(fx * fx + fy * fy + fz * fz);
            if (fmag > 1e-6)
            {
                const float arrowLen = 25.0f;
                const float dx = (float)(fx / fmag);
                const float dy = (float)(fy / fmag);
                const float dz = (float)(fz / fmag);

                const std::array<float, 3> tip = { px, py, pz };
                const std::array<float, 3> tail = { px - dx * arrowLen, py - dy * arrowLen, pz - dz * arrowLen };
                lines.push_back({ tail, tip, 0xffffaa22 });
            }
        }
    }

    return lines;
}

StructuralResult evaluateCase(const CadDocument& doc, const CadAnalysisCase& analysisCase,
                              const StructuralOptions& options)
{
    std::vector<Node> nodes;
    std::vector<Member> members;
    juce::String diag;
    if (!extractModel(doc, analysisCase, nodes, members, diag))
    {
        StructuralResult r;
        r.status = "error";
        r.diagnostic = diag;
        return r;
    }
    return solve(nodes, members, options);
}

juce::var StructuralResult::toVar() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("solved", solved);
    obj->setProperty("status", status);
    obj->setProperty("diagnostic", diagnostic);
    obj->setProperty("maxDeflectionMm", maxDeflectionM * 1000.0);
    obj->setProperty("maxNormalStressMPa", maxNormalStressPa / 1e6);
    obj->setProperty("maxShearStressMPa", maxShearStressPa / 1e6);
    obj->setProperty("maxUtilizationRatio", maxUtilizationRatio);

    auto* rf = new juce::DynamicObject();
    rf->setProperty("x", totalReactionForceN[0]);
    rf->setProperty("y", totalReactionForceN[1]);
    rf->setProperty("z", totalReactionForceN[2]);
    obj->setProperty("totalReactionForceN", juce::var(rf));

    auto* af = new juce::DynamicObject();
    af->setProperty("x", totalAppliedForceN[0]);
    af->setProperty("y", totalAppliedForceN[1]);
    af->setProperty("z", totalAppliedForceN[2]);
    obj->setProperty("totalAppliedForceN", juce::var(af));

    juce::Array<juce::var> mList;
    for (const auto& mr : memberResults)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty("memberId", mr.memberId);
        mo->setProperty("instanceId", mr.instanceId);
        mo->setProperty("axialForceN", mr.axialForceN);
        mo->setProperty("shearForceN", mr.maxShearForceN);
        mo->setProperty("bendingMomentNm", mr.maxBendingMomentNm);
        mo->setProperty("torsionNm", mr.maxTorsionNm);
        mo->setProperty("normalStressMPa", mr.maxNormalStressPa / 1e6);
        mo->setProperty("shearStressMPa", mr.maxShearStressPa / 1e6);
        mo->setProperty("utilizationRatio", mr.utilizationRatio);
        mList.add(juce::var(mo));
    }
    obj->setProperty("members", mList);

    juce::Array<juce::var> nList;
    for (const auto& nr : nodeResults)
    {
        auto* no = new juce::DynamicObject();
        no->setProperty("nodeId", nr.nodeId);
        no->setProperty("displacementMm", juce::Array<juce::var>{nr.displacementM[0] * 1000.0, nr.displacementM[1] * 1000.0, nr.displacementM[2] * 1000.0});
        no->setProperty("rotationRad", juce::Array<juce::var>{nr.rotationRad[0], nr.rotationRad[1], nr.rotationRad[2]});
        no->setProperty("reactionForceN", juce::Array<juce::var>{nr.reactionForceN[0], nr.reactionForceN[1], nr.reactionForceN[2]});
        no->setProperty("reactionMomentNm", juce::Array<juce::var>{nr.reactionMomentNm[0], nr.reactionMomentNm[1], nr.reactionMomentNm[2]});
        nList.add(juce::var(no));
    }
    obj->setProperty("nodes", nList);

    return juce::var(obj);
}

} // namespace djehuti::cad::structural
