#pragma once
#include "rendering/glue_camera.hpp"

namespace wowee::rendering::cinematic {
// M2SplineKey is stored as p0, p1, p2. For a cinematic segment i -> i+1,
// p1[i] and p2[i+1] are the start/end spline handles. The existing M2 loader
// names those arrays InTangents and OutTangents respectively. Those names
// must not be interpreted as the usual modelling-tool handle directions.
// Reference: WebWowViewerCpp animate.h, interpolateHermite (p1.inTan,
// p2.outTan). Keep this camera-specific convention separate from other tracks.
template<class T>
T sample(const pipeline::M2AnimationTrack& track, float timeMs,
         const std::vector<T>& values, const std::vector<T>& firstHandles,
         const std::vector<T>& secondHandles, const T& fallback) {
    return glue::sample(track, 0, timeMs, values, secondHandles, firstHandles, fallback);
}

// Stabilized flyovers retain the asset's key positions and nonuniform key times.
// Reconstruct C1 derivatives from adjacent secants instead of feeding the raw
// camera handle payload into the skeleton's cubic evaluator. Each component
// stays inside its two endpoint values, eliminating handle-induced reversals.
// This is deliberately a comfort interpolation, not a claim of retail timing
// between keys. Skeleton, glue-scene and roll track sampling is unchanged.
inline double limitedSlope(double a, double b, double ha, double hb) {
    if (a == 0 || b == 0 || std::signbit(a) != std::signbit(b)) return 0;
    const double w1=2*hb+ha, w2=hb+2*ha;
    return (w1+w2)/(w1/a+w2/b);
}
inline double endpointSlope(double d0, double d1, double h0, double h1) {
    double m=((2*h0+h1)*d0-h0*d1)/(h0+h1);
    if (m==0 || d0==0 || std::signbit(m)!=std::signbit(d0)) return 0;
    if (std::signbit(d0)!=std::signbit(d1) && std::abs(m)>3*std::abs(d0)) m=3*d0;
    return m;
}
inline glm::vec3 stabilizedPosition(const pipeline::M2AnimationTrack& track, float ms) {
    if (track.sequences.empty() || !std::isfinite(ms)) return {};
    const auto& keys=track.sequences.front();
    const auto& t=keys.timestamps; const auto& v=keys.vec3Values;
    const size_t count=std::min(t.size(),v.size());
    if (!count) return {};
    if (count==1 || ms<=t.front()) return glue::finite(v.front()) ? v.front() : glm::vec3(0);
    if (double(ms)>=t[count-1]) return glue::finite(v[count-1]) ? v[count-1] : glm::vec3(0);
    auto it=std::upper_bound(t.begin(),t.begin()+count,ms);
    const size_t i=size_t(it-t.begin())-1, j=i+1;
    if (!glue::finite(v[i]) || !glue::finite(v[j])) return {};
    const double h=double(t[j])-t[i];
    if (!(h>0)) return v[i];
    const double u=std::clamp((double(ms)-t[i])/h,0.0,1.0);
    if (track.interpolationType==0) return v[i];
    if (track.interpolationType==1 || count==2) return glm::mix(v[i],v[j],float(u));
    glm::vec3 out{};
    for (int c=0;c<3;++c) {
        auto secant=[&](size_t k) { double dt=double(t[k+1])-t[k];
            return dt>0 ? (double(v[k+1][c])-v[k][c])/dt : 0.0; };
        const double d=secant(i);
        double m0=d,m1=d;
        if (i>0) {
            const double hp=double(t[i])-t[i-1];
            m0=hp>0 ? limitedSlope(secant(i-1),d,hp,h) : 0;
        } else if (count>2) {
            const double hn=double(t[2])-t[1];
            m0=hn>0 ? endpointSlope(d,secant(1),h,hn) : d;
        }
        if (j+1<count) {
            const double hn=double(t[j+1])-t[j];
            m1=hn>0 ? limitedSlope(d,secant(j),h,hn) : 0;
        } else if (i>0) {
            const double hp=double(t[i])-t[i-1];
            m1=hp>0 ? endpointSlope(d,secant(i-1),h,hp) : d;
        }
        const double u2=u*u,u3=u2*u;
        const double result=(2*u3-3*u2+1)*v[i][c]+(u3-2*u2+u)*h*m0+
                            (-2*u3+3*u2)*v[j][c]+(u3-u2)*h*m1;
        out[c]=float(std::clamp(result,std::min(double(v[i][c]),double(v[j][c])),
                                      std::max(double(v[i][c]),double(v[j][c]))));
    }
    return glue::finite(out) ? out : v[i];
}
} // namespace wowee::rendering::cinematic
