#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>

namespace wowee::rendering {
// A light-space projection of ALL camera receivers. Extrusion along the light
// axis is implicit, so a caster behind/off screen remains eligible if it can
// shade any receiver. This is additional conservative culling, never a change
// to the stable shadow projection or to its finite upstream depth.
class ShadowReceiverHull {
    std::array<glm::vec2, 64> hull_{};
    std::array<glm::vec2, 32> inward_{};
    std::array<float,32> offsets_{};
    float supportPadding_ = 0.0f;
    unsigned count_ = 0;
    bool empty_ = false;
    const ShadowReceiverHull* additional_ = nullptr;
    glm::vec3 x_{}, y_{}, origin_{};
    static float cross(glm::vec2 a, glm::vec2 b) { return a.x*b.y-a.y*b.x; }
public:
    /// Fast repeated local-AABB query for one immutable model transform.
    ///
    /// WMO shadow submission tests many material ranges of the same placed
    /// building against the same receiver hull. The generic method rebuilds
    /// the model-to-hull projection for every range; this object resolves that
    /// projection once per instance/cascade and leaves only a few scalar MADs
    /// per hull edge in the inner loop. Reflection uses two hulls, so that less
    /// common path deliberately falls back to the generic exact method.
    class TransformedBoundsTester {
    public:
        TransformedBoundsTester() = default;
        TransformedBoundsTester(const ShadowReceiverHull& hull, const glm::mat4& model)
            : hull_(&hull), model_(model), count_(hull.count_), empty_(hull.empty_) {
            if (hull.additional_) return;
            fast_ = true;
            if (count_ < 3 || empty_) return;

            const glm::vec3 translation(model[3]);
            const glm::vec3 d = translation - hull.origin_;
            const glm::vec2 originProj{glm::dot(hull.x_, d), glm::dot(hull.y_, d)};
            std::array<glm::vec2, 3> axisProj{};
            for (unsigned axis = 0; axis < 3; ++axis) {
                const glm::vec3 worldAxis(model[axis]);
                axisProj[axis] = {glm::dot(hull.x_, worldAxis),
                                  glm::dot(hull.y_, worldAxis)};
            }
            for (unsigned i = 0; i < count_; ++i) {
                base_[i] = glm::dot(hull.inward_[i], originProj) - hull.offsets_[i];
                coeff_[i] = glm::vec3(glm::dot(hull.inward_[i], axisProj[0]),
                                      glm::dot(hull.inward_[i], axisProj[1]),
                                      glm::dot(hull.inward_[i], axisProj[2]));
            }
        }

        bool intersects(glm::vec3 low, glm::vec3 high) const {
            if (!hull_) return true;
            if (!fast_) return hull_->intersectsTransformedBounds(low, high, model_);
            if (empty_) return false;
            if (count_ < 3) return true;
            const glm::vec3 center = (low + high) * 0.5f;
            const glm::vec3 extent = (high - low) * 0.5f;
            for (unsigned axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(center[axis]) || !std::isfinite(extent[axis]) || extent[axis] < 0.0f)
                    return true;
            }
            for (unsigned i = 0; i < count_; ++i) {
                const float signedCenter = base_[i] + glm::dot(coeff_[i], center);
                const float support = glm::dot(glm::abs(coeff_[i]), extent);
                if (!std::isfinite(signedCenter) || !std::isfinite(support)) return true;
                if (signedCenter < -support - 2.0f - hull_->supportPadding_) return false;
            }
            return true;
        }

    private:
        const ShadowReceiverHull* hull_ = nullptr;
        glm::mat4 model_{1.0f};
        std::array<float, 32> base_{};
        std::array<glm::vec3, 32> coeff_{};
        unsigned count_ = 0;
        bool empty_ = false;
        bool fast_ = false;
    };

    [[nodiscard]] TransformedBoundsTester transformedBoundsTester(const glm::mat4& model) const {
        return TransformedBoundsTester(*this, model);
    }

    template <size_t N>
    void build(const std::array<glm::vec3, N>& corners, const glm::mat4& light,
               float supportPadding = 0.0f, size_t pointCount = N) {
        count_ = 0;
        empty_ = false;
        additional_ = nullptr;
        if (pointCount < 3 || pointCount > N) return;
        if (!(supportPadding >= 0.0f) || !std::isfinite(supportPadding)) return;
        supportPadding_ = supportPadding;
        x_ = glm::vec3(light[0][0], light[1][0], light[2][0]);
        y_ = glm::vec3(light[0][1], light[1][1], light[2][1]);
        const float nx=glm::length(x_), ny=glm::length(y_);
        if (!(nx>0.0f && ny>0.0f) || !std::isfinite(nx+ny)) return;
        x_/=nx; y_/=ny; origin_=corners[0];
        std::array<glm::vec2,N> p{};
        for(size_t i=0;i<pointCount;++i) {
            const auto d=corners[i]-origin_;
            p[i]={glm::dot(x_,d),glm::dot(y_,d)};
            if(!std::isfinite(p[i].x+p[i].y)) return;
        }
        std::sort(p.begin(),p.begin()+pointCount,[](auto a,auto b){return a.x<b.x || (a.x==b.x && a.y<b.y);});
        unsigned n=0;
        for(size_t i=0;i<pointCount;++i) {
            const auto v=p[i];
            while(n>=2 && cross(hull_[n-1]-hull_[n-2],v-hull_[n-1])<=0) --n;
            if (n >= hull_.size()) return;
            hull_[n++]=v;
        }
        const unsigned lower=n;
        for(int i=static_cast<int>(pointCount)-2;i>=0;--i) {
            const auto v=p[i];
            while(n>lower && cross(hull_[n-1]-hull_[n-2],v-hull_[n-1])<=0) --n;
            if (n >= hull_.size()) return;
            hull_[n++]=v;
        }
        if(n>3 && n-1 <= inward_.size()) {
            count_=n-1;
            for(unsigned i=0;i<count_;++i) {
                const auto edge=hull_[(i+1)%count_]-hull_[i];
                const float length=glm::length(edge);
                if(!(length>0.0f) || !std::isfinite(length)) { count_=0; return; }
                inward_[i]=glm::vec2(-edge.y,edge.x)/length;
                offsets_[i]=glm::dot(inward_[i],hull_[i]);
            }
        }
    }
    // Intersect the actual camera clip volume with this finite cascade
    // before projecting receivers along the light. The camera far plane is
    // 30,000 yards; receivers outside the atlas depth range cannot sample it
    // and used to enlarge the hull to nearly every loaded caster.
    // The bounded intersection has at most 20 vertices (12 planes). Use
    // double precision for plane triples at large world coordinates.
    void buildClipped(const glm::mat4& cameraClip, const glm::mat4& light,
                      float supportPadding = 0.0f) {
        count_ = 0;
        empty_ = false;
        additional_ = nullptr;
        if (!(supportPadding >= 0.0f) || !std::isfinite(supportPadding)) return;
        std::array<glm::dvec4,12> planes{};
        for (unsigned volume=0; volume<2; ++volume) {
            const glm::dmat4 matrix(volume ? light : cameraClip);
            std::array<glm::dvec4,4> row{};
            for (unsigned i=0;i<4;++i)
                row[i] = {matrix[0][i],matrix[1][i],matrix[2][i],matrix[3][i]};
            const std::array<glm::dvec4,6> extracted{
                row[3]+row[0],row[3]-row[0],row[3]+row[1],row[3]-row[1],
                row[2],row[3]-row[2]}; // Vulkan 0 <= z <= w
            for (unsigned i=0;i<6;++i) {
                auto plane=extracted[i];
                const double length=glm::length(glm::dvec3(plane));
                if (!(length > 1e-12) || !std::isfinite(length) || !std::isfinite(plane.w)) return;
                plane/=length;
                // Include numeric error, scene projection jitter and normal
                // bias. XY also includes every shadow reconstruction tap.
                plane.w += 2.0 + ((volume && i<4) ? supportPadding : 0.0);
                planes[volume*6+i]=plane;
            }
        }
        std::array<glm::vec3,220> points{};
        size_t pointCount=0;
        for(unsigned a=0;a<10;++a) for(unsigned b=a+1;b<11;++b) for(unsigned c=b+1;c<12;++c) {
            const auto na=glm::dvec3(planes[a]),nb=glm::dvec3(planes[b]),nc=glm::dvec3(planes[c]);
            const auto bc=glm::cross(nb,nc);
            const double det=glm::dot(na,bc);
            if(std::abs(det)<1e-10) continue;
            const auto point=(-planes[a].w*bc-planes[b].w*glm::cross(nc,na)
                              -planes[c].w*glm::cross(na,nb))/det;
            if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)) return;
            bool inside=true;
            for(const auto& plane:planes) {
                if(glm::dot(glm::dvec3(plane),point)+plane.w < -1e-5) { inside=false;break; }
            }
            if(inside) points[pointCount++]=glm::vec3(point);
        }
        if(pointCount==0) { empty_=true; return; }
        build(points,light,supportPadding,pointCount);
    }
    // The reflected camera is a separate receiver set. Keeping two convex
    // hulls retains the gap between them, unlike one oversized convex union.
    // The caller owns the additional hull for this query's lifetime.
    void includeAdditional(const ShadowReceiverHull* additional) {
        additional_ = additional == this ? nullptr : additional;
    }
    bool intersects(glm::vec3 center,float radius) const {
        if (additional_ && additional_->intersects(center,radius)) return true;
        if (empty_) return false;
        if(count_<3 || !(radius>=0) || !std::isfinite(radius)) return true;
        const auto d=center-origin_;
        const glm::vec2 p{glm::dot(x_,d),glm::dot(y_,d)};
        // Float error at distant far-plane corners and the scene's subpixel
        // projection jitter must not make a silhouette caster disappear.
        // Sampling support is separate from numeric/jitter tolerance. The
        // far atlas has half the near resolution and therefore needs its
        // own world-space PCF margin, including diagonal filter footprints.
        const float padded=radius+2.0f+supportPadding_;
        for(unsigned i=0;i<count_;++i) {
            if(glm::dot(inward_[i],p)-offsets_[i] < -padded) return false;
        }
        return true;
    }
    // Exact projected support of an axis-aligned world box. Sphere bounds
    // greatly overestimate long buildings; this keeps those groups/ranges
    // eligible only when their box can project onto a receiver. Invalid
    // bounds fail open, just like the sphere query.
    bool intersectsBounds(glm::vec3 low, glm::vec3 high) const {
        if (additional_ && additional_->intersectsBounds(low,high)) return true;
        if (empty_) return false;
        if (count_ < 3) return true;
        const auto center = (low + high) * 0.5f;
        const auto extent = (high - low) * 0.5f;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!std::isfinite(center[axis]) || !std::isfinite(extent[axis]) || extent[axis] < 0.0f)
                return true;
        const auto d = center - origin_;
        const glm::vec2 projected{glm::dot(x_, d), glm::dot(y_, d)};
        for (unsigned i = 0; i < count_; ++i) {
            const glm::vec3 normal = inward_[i].x * x_ + inward_[i].y * y_;
            const float support = glm::dot(glm::abs(normal), extent) + 2.0f + supportPadding_;
            if (glm::dot(inward_[i], projected) - offsets_[i] < -support) return false;
        }
        return true;
    }
    // An affine model may rotate, scale or shear its local box. Project its
    // three half-axes directly instead of constructing an oversized world
    // sphere or transforming all eight corners for every draw range.
    bool intersectsTransformedBounds(glm::vec3 low, glm::vec3 high, const glm::mat4& model) const {
        if (additional_ && additional_->intersectsTransformedBounds(low,high,model)) return true;
        if (empty_) return false;
        if (count_ < 3) return true;
        const auto extent = (high - low) * 0.5f;
        const glm::vec3 center = glm::vec3(model * glm::vec4((low + high) * 0.5f, 1.0f));
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!std::isfinite(center[axis]) || !std::isfinite(extent[axis]) || extent[axis] < 0.0f)
                return true;
        const auto d = center - origin_;
        const glm::vec2 projected{glm::dot(x_, d), glm::dot(y_, d)};
        for (unsigned i = 0; i < count_; ++i) {
            const glm::vec3 normal = inward_[i].x * x_ + inward_[i].y * y_;
            const float support = std::abs(glm::dot(normal, glm::vec3(model[0]))) * extent.x
                + std::abs(glm::dot(normal, glm::vec3(model[1]))) * extent.y
                + std::abs(glm::dot(normal, glm::vec3(model[2]))) * extent.z;
            if (!std::isfinite(support)) return true;
            if (glm::dot(inward_[i], projected) - offsets_[i] < -support - 2.0f - supportPadding_)
                return false;
        }
        return true;
    }
    unsigned size() const { return count_; }
};
}
