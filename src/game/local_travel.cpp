#include "game/local_travel.hpp"
#include "core/logger.hpp"

#include <algorithm>
#include <cmath>

namespace wowee::game {
namespace {

float distance3(const LocalTaxiWaypoint& a, const LocalTaxiWaypoint& b) {
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

bool LocalTravelNetwork::setClientData(std::vector<LocalTaxiNode> nodes,
                                       std::vector<LocalTaxiPath> paths,
                                       std::vector<LocalTaxiWaypoint> waypoints,
                                       std::string& error) {
    if (nodes.empty()) {
        error = "no taxi nodes";
        return false;
    }
    if (paths.empty()) {
        error = "no taxi paths";
        return false;
    }

    std::sort(nodes.begin(), nodes.end(),
              [](const LocalTaxiNode& a, const LocalTaxiNode& b) { return a.id < b.id; });
    nodes.erase(std::unique(nodes.begin(), nodes.end(),
                            [](const LocalTaxiNode& a, const LocalTaxiNode& b) {
                                return a.id == b.id;
                            }),
                nodes.end());

    std::sort(paths.begin(), paths.end(),
              [](const LocalTaxiPath& a, const LocalTaxiPath& b) { return a.id < b.id; });
    paths.erase(std::unique(paths.begin(), paths.end(),
                            [](const LocalTaxiPath& a, const LocalTaxiPath& b) {
                                return a.id == b.id;
                            }),
                paths.end());

    // Group waypoints per path and precompute cumulative distance. Doing it
    // once here is what makes samplePath a binary search instead of a walk
    // over every waypoint of a route, which matters because a transport is
    // sampled every frame and a long flight path has hundreds of points.
    std::sort(waypoints.begin(), waypoints.end(),
              [](const LocalTaxiWaypoint& a, const LocalTaxiWaypoint& b) {
                  if (a.pathId != b.pathId) return a.pathId < b.pathId;
                  return a.index < b.index;
              });

    std::vector<PathGeometry> geometry;
    for (size_t i = 0; i < waypoints.size();) {
        const uint32_t pathId = waypoints[i].pathId;
        PathGeometry geo;
        geo.pathId = pathId;
        size_t j = i;
        while (j < waypoints.size() && waypoints[j].pathId == pathId) {
            geo.points.push_back(waypoints[j]);
            ++j;
        }
        i = j;

        geo.cumulative.assign(geo.points.size(), 0.0f);
        for (size_t k = 1; k < geo.points.size(); ++k) {
            // A path that crosses a map boundary (the Northrend boats do)
            // contributes no distance across the seam: the two halves are in
            // different coordinate spaces and subtracting them is meaningless.
            const float step = geo.points[k].mapId == geo.points[k - 1].mapId
                                   ? distance3(geo.points[k - 1], geo.points[k])
                                   : 0.0f;
            geo.cumulative[k] = geo.cumulative[k - 1] + step;
        }
        geo.length = geo.cumulative.empty() ? 0.0f : geo.cumulative.back();
        geometry.push_back(std::move(geo));
    }

    nodes_ = std::move(nodes);
    paths_ = std::move(paths);
    geometry_ = std::move(geometry);
    // Routes were validated against the previous geometry, so they have to be
    // re-checked rather than carried over.
    transports_.clear();
    transportSchedules_.clear();
    error.clear();
    return true;
}

const LocalTaxiNode* LocalTravelNetwork::node(uint32_t id) const {
    const auto it = std::lower_bound(nodes_.begin(), nodes_.end(), id,
                                     [](const LocalTaxiNode& n, uint32_t v) { return n.id < v; });
    if (it == nodes_.end() || it->id != id) return nullptr;
    return &*it;
}

const LocalTaxiPath* LocalTravelNetwork::path(uint32_t id) const {
    const auto it = std::lower_bound(paths_.begin(), paths_.end(), id,
                                     [](const LocalTaxiPath& p, uint32_t v) { return p.id < v; });
    if (it == paths_.end() || it->id != id) return nullptr;
    return &*it;
}

const LocalTravelNetwork::PathGeometry* LocalTravelNetwork::geometryFor(uint32_t pathId) const {
    const auto it = std::lower_bound(geometry_.begin(), geometry_.end(), pathId,
                                     [](const PathGeometry& g, uint32_t v) { return g.pathId < v; });
    if (it == geometry_.end() || it->pathId != pathId) return nullptr;
    return &*it;
}

const std::vector<LocalTaxiWaypoint>& LocalTravelNetwork::waypoints(uint32_t pathId) const {
    static const std::vector<LocalTaxiWaypoint> kEmpty;
    const PathGeometry* geo = geometryFor(pathId);
    return geo ? geo->points : kEmpty;
}

const LocalTaxiPath* LocalTravelNetwork::directPath(uint32_t fromNode, uint32_t toNode) const {
    if (fromNode == 0 || toNode == 0 || fromNode == toNode) return nullptr;
    for (const auto& p : paths_) {
        if (p.fromNode == fromNode && p.toNode == toNode) return &p;
    }
    return nullptr;
}

float LocalTravelNetwork::pathLength(uint32_t pathId) const {
    const PathGeometry* geo = geometryFor(pathId);
    return geo ? geo->length : 0.0f;
}

bool LocalTravelNetwork::samplePath(uint32_t pathId, float distance, uint32_t& mapId,
                                    float& x, float& y, float& z, float& orientation) const {
    const PathGeometry* geo = geometryFor(pathId);
    if (!geo || geo->points.empty()) return false;

    if (geo->points.size() == 1 || geo->length <= 0.0f) {
        const auto& p = geo->points.front();
        mapId = p.mapId;
        x = p.x; y = p.y; z = p.z;
        orientation = 0.0f;
        return true;
    }

    distance = std::clamp(distance, 0.0f, geo->length);
    // First waypoint whose cumulative distance is at or past the sample.
    const auto it = std::lower_bound(geo->cumulative.begin(), geo->cumulative.end(), distance);
    size_t hi = static_cast<size_t>(it - geo->cumulative.begin());
    if (hi == 0) hi = 1;
    if (hi >= geo->points.size()) hi = geo->points.size() - 1;
    const size_t lo = hi - 1;

    const auto& a = geo->points[lo];
    const auto& b = geo->points[hi];
    const float span = geo->cumulative[hi] - geo->cumulative[lo];
    const float t = span > 0.0f ? (distance - geo->cumulative[lo]) / span : 0.0f;

    // Across a map seam the two endpoints are in different coordinate spaces,
    // so interpolating between them would place the traveller nowhere. Snap to
    // whichever side of the seam the sample falls on instead.
    if (a.mapId != b.mapId) {
        const auto& pick = t < 0.5f ? a : b;
        mapId = pick.mapId;
        x = pick.x; y = pick.y; z = pick.z;
        orientation = 0.0f;
        return true;
    }

    mapId = a.mapId;
    x = a.x + (b.x - a.x) * t;
    y = a.y + (b.y - a.y) * t;
    z = a.z + (b.z - a.z) * t;
    orientation = std::atan2(b.y - a.y, b.x - a.x);
    return true;
}

const LocalTaxiNode* LocalTravelNetwork::flightNodeAt(uint32_t mapId, float x, float y,
                                                      float z) const {
    const LocalTaxiNode* best = nullptr;
    float bestDistSq = FlightMasterRadius * FlightMasterRadius;
    for (const auto& n : nodes_) {
        if (n.mapId != mapId || !n.flightNode()) continue;
        const float dx = n.x - x, dy = n.y - y, dz = n.z - z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= bestDistSq) {
            bestDistSq = distSq;
            best = &n;
        }
    }
    return best;
}

std::vector<uint32_t> LocalTravelNetwork::destinationsFrom(
    uint32_t fromNode, const std::vector<uint32_t>& knownNodes) const {
    std::vector<uint32_t> result;
    if (fromNode == 0) return result;
    for (const auto& p : paths_) {
        if (p.fromNode != fromNode) continue;
        if (std::find(knownNodes.begin(), knownNodes.end(), p.toNode) == knownNodes.end()) {
            continue;
        }
        // A route the client describes but has no waypoints for cannot be
        // flown, and offering it would sell a flight that goes nowhere.
        if (pathLength(p.id) <= 0.0f) continue;
        if (std::find(result.begin(), result.end(), p.toNode) == result.end()) {
            result.push_back(p.toNode);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

size_t LocalTravelNetwork::setTransportRoutes(const std::vector<LocalTransportRoute>& routes) {
    transports_.clear(); transportSchedules_.clear();
    for (const auto& route : routes) {
        TransportSchedule schedule;
        if (!route.entry || !route.displayId ||
            std::any_of(transports_.begin(), transports_.end(), [&](const auto& r){return r.entry == route.entry;}) ||
            !buildTransportSchedule(route, schedule)) {
            LOG_WARNING("[LOCAL_TRANSPORT] rejected entry=",route.entry," path=",route.pathId," name=",route.name);
            continue;
        }
        auto accepted=route; accepted.periodSeconds=static_cast<float>(schedule.duration);
        LOG_INFO("[LOCAL_TRANSPORT] directed loop entry=",route.entry," path=",route.pathId,
                 " seconds=",schedule.duration," spans=",schedule.spans.size()," dock waits=client DBC");
        transports_.push_back(std::move(accepted));
        transportSchedules_.push_back(std::move(schedule));
    }
    return transports_.size();
}

void LocalTravelNetwork::logClientTransportPaths() const {
    // Every taxi path whose two ends are both non-flight nodes. TaxiNodes.dbc
    // gives a flight node a mount display for at least one faction and a boat,
    // zeppelin or tram stop none, which is the same distinction the client
    // itself draws - so this is the client's own list of transport routes,
    // derived rather than assumed.
    //
    // It exists because the built-in route table carries taxi path ids taken
    // from an AzerothCore dump, and a wrong one silently yields no transport.
    // This prints what the player's own data actually says, including the dock
    // positions - which are the zeppelin towers and piers themselves - so the
    // table can be corrected against ground truth instead of guesswork.
    // The length alone was not enough to choose between two paths joining the
    // same two stops. The client's Orgrimmar-Grom'gol pair - 285 one way and
    // 301 the other - differ sixfold in length, and a length cannot say which
    // of them is the crossing and which is a stub. What can say it is the
    // geometry: a path that really runs between two stops begins at one of them
    // and ends at the other, and one that does not is not that route whatever
    // its TaxiPath row claims. So each candidate now also reports how many
    // waypoints it has, how many maps it touches, how far its first and last
    // waypoint sit from the two nodes it declares, and how long one leg would
    // take at the speed a transport actually moves - the last because a
    // nine-minute zeppelin is wrong on its face next to a ninety-second one.
    unsigned found = 0;
    for (const auto& path : paths_) {
        const LocalTaxiNode* from = node(path.fromNode);
        const LocalTaxiNode* to = node(path.toNode);
        if (!from || !to || from->flightNode() || to->flightNode()) continue;
        ++found;
        const PathGeometry* geo = geometryFor(path.id);
        const float length = pathLength(path.id);
        // Gap from each end of the geometry to the stop it claims. Measured only
        // when the two are on the same map: across a seam the coordinates are in
        // different spaces and a distance between them means nothing, so -1
        // reports "not comparable" rather than a number that looks like one.
        const auto gap = [](const LocalTaxiWaypoint* point, const LocalTaxiNode& stop) {
            if (!point || point->mapId != stop.mapId) return -1.0f;
            const float dx = point->x - stop.x, dy = point->y - stop.y, dz = point->z - stop.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
        const LocalTaxiWaypoint* first = geo && !geo->points.empty() ? &geo->points.front() : nullptr;
        const LocalTaxiWaypoint* last = geo && !geo->points.empty() ? &geo->points.back() : nullptr;
        unsigned seams = 0;
        if (geo) for (size_t i = 1; i < geo->points.size(); ++i)
            if (geo->points[i].mapId != geo->points[i - 1].mapId) ++seams;
        LOG_INFO("[LOCAL_TRANSPORT_SCAN] path=", path.id,
                 " length=", length,
                 " waypoints=", geo ? geo->points.size() : size_t(0),
                 " seams=", seams,
                 " legSeconds=", length / DefaultFlightSpeed,
                 " startGap=", gap(first, *from), " endGap=", gap(last, *to),
                 " from=\"", from->name, "\" map=", from->mapId,
                 " xyz=", from->x, ",", from->y, ",", from->z,
                 " to=\"", to->name, "\" map=", to->mapId,
                 " xyz=", to->x, ",", to->y, ",", to->z);
    }
    LOG_INFO("[LOCAL_TRANSPORT_SCAN] client describes ", found,
             " transport routes; the built-in table offers ", transports_.size());
}

float LocalTravelNetwork::pathEndpointGap(uint32_t pathId, uint32_t nodeId, bool pathStart) const {
    const PathGeometry* geo = geometryFor(pathId);
    const LocalTaxiNode* stop = node(nodeId);
    if (!geo || geo->points.empty() || !stop) return -1.0f;
    const LocalTaxiWaypoint& point = pathStart ? geo->points.front() : geo->points.back();
    // Across a map seam the two are in different coordinate spaces; subtracting
    // them would produce a number with no meaning, so say "cannot tell".
    if (point.mapId != stop->mapId) return -1.0f;
    const float dx = point.x - stop->x, dy = point.y - stop->y, dz = point.z - stop->z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

namespace {
// Symmetric acceleration / cruise / braking over the whole stop-to-stop leg,
// not separately at each spline knot. Distance is continuous at map seams.
double routeTimeAtDistance(double d,double length,double speed,double acceleration) {
    const double ramp=std::min(speed*speed/(2*acceleration),length*.5);
    const double peak=std::sqrt(2*acceleration*ramp), rampTime=peak/acceleration;
    const double duration=2*rampTime+(length-2*ramp)/std::max(peak,0.001);
    d=std::clamp(d,0.0,length);
    if (d<ramp) return std::sqrt(2*d/acceleration);
    if (d>length-ramp) return duration-std::sqrt(2*(length-d)/acceleration);
    return rampTime+(d-ramp)/std::max(peak,0.001);
}
double routeDistanceAtTime(double t,double length,double speed,double acceleration) {
    const double ramp=std::min(speed*speed/(2*acceleration),length*.5);
    const double peak=std::sqrt(2*acceleration*ramp), rampTime=peak/acceleration;
    const double duration=2*rampTime+(length-2*ramp)/std::max(peak,0.001);
    t=std::clamp(t,0.0,duration);
    if(t<rampTime) return .5*acceleration*t*t;
    if(t>duration-rampTime) return length-.5*acceleration*(duration-t)*(duration-t);
    return ramp+peak*(t-rampTime);
}
float cubic(float a,float b,float c,float d,float t) {
    return .5f*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t);
}
float cubicDerivative(float a,float b,float c,float d,float t) {
    return .5f*((-a+c)+2*(2*a-5*b+4*c-d)*t+3*(-a+3*b-3*c+d)*t*t);
}
LocalTaxiWaypoint extrapolate(const LocalTaxiWaypoint& a,const LocalTaxiWaypoint& b) {
    auto p=a; p.x=2*a.x-b.x;p.y=2*a.y-b.y;p.z=2*a.z-b.z;return p;
}
}
bool LocalTravelNetwork::buildTransportSchedule(const LocalTransportRoute& route,TransportSchedule& out) const {
    const auto& raw=waypoints(route.pathId);
    if(raw.size()<2 || !std::isfinite(route.speed) || !std::isfinite(route.acceleration) ||
       route.speed<=0 || route.speed>100 || route.acceleration<=0 || route.acceleration>20) return false;
    struct Knot { LocalTaxiWaypoint p; bool jump=false; };
    std::vector<Knot> knots;
    const bool authored=std::any_of(raw.begin(),raw.end(),[](const auto& p){return p.flags || p.delaySeconds;});
    // Taxi transport paths contain exterior spline control nodes and pairs of
    // portal control nodes. Neither kind is a place a hull should sail through.
    const size_t begin=authored && !(raw.front().flags&2) && !raw.front().delaySeconds?1:0;
    const size_t end=raw.size()-(authored && !(raw.back().flags&2) && !raw.back().delaySeconds?1:0);
    bool skip=false;
    for(size_t i=begin;i<end;++i) {
        const auto& p=raw[i];
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) return false;
        if(skip){skip=false;continue;}
        if((p.flags&1) || (i+1<end && p.mapId!=raw[i+1].mapId)) {
            if(!knots.empty())knots.back().jump=true;
            skip=true;continue;
        }
        knots.push_back({p,false});
    }
    if(knots.size()<2)return false;
    // The terminal-to-first transition closes the authored loop; it is never
    // a fabricated backwards return leg across an ocean or across a city.
    knots.back().jump=true;
    auto stop=[](const Knot& k){return (k.p.flags&2)!=0 || k.p.delaySeconds!=0;};
    const auto firstStop=std::find_if(knots.begin(),knots.end(),stop);
    const bool hasStops=firstStop!=knots.end();
    if(hasStops)std::rotate(knots.begin(),firstStop,knots.end());
    const size_t n=knots.size();
    std::vector<TransportSpan> curves(n);
    std::vector<float> length(n),fromStop(n),legLength(n);
    for(size_t i=0;i<n;++i) {
        auto& curve=curves[i];const size_t next=(i+1)%n,prev=(i+n-1)%n,after=(i+2)%n;
        curve.p1=knots[i].p;curve.p2=knots[next].p;
        if(knots[i].jump || curve.p1.mapId!=curve.p2.mapId)continue;
        curve.p0=knots[prev].jump?extrapolate(curve.p1,curve.p2):knots[prev].p;
        curve.p3=knots[next].jump?extrapolate(curve.p2,curve.p1):knots[after].p;
        auto last=curve.p1;
        for(size_t j=1;j<=16;++j) {
            const float u=float(j)/16;auto p=curve.p1;
            p.x=cubic(curve.p0.x,curve.p1.x,curve.p2.x,curve.p3.x,u);
            p.y=cubic(curve.p0.y,curve.p1.y,curve.p2.y,curve.p3.y,u);
            p.z=cubic(curve.p0.z,curve.p1.z,curve.p2.z,curve.p3.z,u);
            curve.arc[j]=curve.arc[j-1]+distance3(last,p);last=p;
        }
        length[i]=curve.arc[16];
        curve.heading=std::atan2(curve.p2.y-curve.p1.y,curve.p2.x-curve.p1.x);
    }
    if(hasStops)for(size_t i=0;i<n;++i)if(stop(knots[i])) {
        size_t j=i;float distance=0;
        do{fromStop[j]=distance;distance+=length[j];j=(j+1)%n;}while(j!=i&&!stop(knots[j]));
        size_t k=i;do{legLength[k]=distance;k=(k+1)%n;}while(k!=j);
    }
    out.entry=route.entry;out.speed=route.speed;out.acceleration=route.acceleration;
    double clock=0;
    for(size_t i=0;i<n;++i) {
        auto curve=curves[i];
        // Keep dock orientation aligned with the departure tangent. A zero
        // length knot inherits the first following geometric edge, not zero yaw.
        for(size_t j=0;j<n;++j)if(length[(i+j)%n]>0.001f){curve.heading=curves[(i+j)%n].heading;break;}
        if(knots[i].p.delaySeconds) {
            if(knots[i].p.delaySeconds>3600)return false;
            auto dock=curve;dock.p1=knots[i].p;dock.begin=clock;
            clock+=knots[i].p.delaySeconds;dock.end=clock;dock.docked=true;out.spans.push_back(dock);
        }
        if(length[i]<=0.001f)continue;
        curve.begin=clock;curve.distanceFromStop=fromStop[i];curve.legLength=legLength[i];curve.cruiseOnly=!hasStops;
        curve.legTime=hasStops?routeTimeAtDistance(fromStop[i],legLength[i],route.speed,route.acceleration):0;
        const double seconds=hasStops?
            routeTimeAtDistance(fromStop[i]+length[i],legLength[i],route.speed,route.acceleration)-curve.legTime:
            length[i]/route.speed;
        clock+=seconds;curve.end=clock;out.spans.push_back(curve);
    }
    out.duration=clock;
    return std::isfinite(clock)&&clock>0&&!out.spans.empty();
}
void LocalTravelNetwork::sampleTransports(double worldSeconds,std::vector<LocalTransportState>& out) const {
    out.clear();if(!std::isfinite(worldSeconds))return;
    out.reserve(transportSchedules_.size());
    for(const auto& schedule:transportSchedules_) {
        double phase=std::fmod(worldSeconds,schedule.duration);if(phase<0)phase+=schedule.duration;
        const auto it=std::upper_bound(schedule.spans.begin(),schedule.spans.end(),phase,
            [](double t,const TransportSpan& span){return t<span.end;});
        const auto& span=it==schedule.spans.end()?schedule.spans.back():*it;
        LocalTransportState state;state.entry=schedule.entry;state.mapId=span.p1.mapId;
        state.docked=span.docked;state.segment=span.p1.index;state.orientation=span.heading;
        state.x=span.p1.x;state.y=span.p1.y;state.z=span.p1.z;
        if(!span.docked) {
            const float distance=static_cast<float>(span.cruiseOnly?(phase-span.begin)*schedule.speed:
                routeDistanceAtTime(span.legTime+phase-span.begin,span.legLength,schedule.speed,schedule.acceleration)-span.distanceFromStop);
            const auto arc=std::lower_bound(span.arc,span.arc+17,distance);
            const int hi=std::clamp(int(arc-span.arc),1,16);
            const float delta=span.arc[hi]-span.arc[hi-1];
            const float u=(hi-1+(delta>0?std::clamp((distance-span.arc[hi-1])/delta,0.0f,1.0f):0))/16.0f;
            state.segmentFraction=u;
            state.x=cubic(span.p0.x,span.p1.x,span.p2.x,span.p3.x,u);
            state.y=cubic(span.p0.y,span.p1.y,span.p2.y,span.p3.y,u);
            state.z=cubic(span.p0.z,span.p1.z,span.p2.z,span.p3.z,u);
            const float dx=cubicDerivative(span.p0.x,span.p1.x,span.p2.x,span.p3.x,u);
            const float dy=cubicDerivative(span.p0.y,span.p1.y,span.p2.y,span.p3.y,u);
            if(dx*dx+dy*dy>0.000001f)state.orientation=std::atan2(dy,dx);
        }
        out.push_back(state);
    }
}

bool LocalTravelNetwork::beginFlight(uint32_t fromNode, uint32_t toNode,
                                     LocalFlightState& flight) const {
    const LocalTaxiPath* route = directPath(fromNode, toNode);
    if (!route) return false;
    const float length = pathLength(route->id);
    if (length <= 0.0f) return false;

    flight = LocalFlightState{};
    flight.active = true;
    flight.pathId = route->id;
    flight.destinationNode = toNode;
    flight.travelled = 0.0f;
    flight.totalLength = length;
    flight.speed = DefaultFlightSpeed;
    return true;
}

bool LocalTravelNetwork::advanceFlight(LocalFlightState& flight, float seconds, uint32_t& mapId,
                                       float& x, float& y, float& z, float& orientation) const {
    if (!flight.active) return false;
    if (seconds > 0.0f) {
        flight.travelled += flight.speed * seconds;
    }

    const bool arrived = flight.travelled >= flight.totalLength;
    if (arrived) flight.travelled = flight.totalLength;

    if (!samplePath(flight.pathId, flight.travelled, mapId, x, y, z, orientation)) {
        // The path went away underneath the flight (a content reload). End it
        // rather than leaving the player suspended on a route that is gone.
        flight.active = false;
        return false;
    }

    if (arrived) {
        // Land exactly on the node, not on the last waypoint: those differ by
        // a few yards and the difference is what decides whether the player is
        // standing at the flight master on arrival.
        const LocalTaxiNode* dest = node(flight.destinationNode);
        if (dest) {
            mapId = dest->mapId;
            x = dest->x;
            y = dest->y;
            z = dest->z;
        }
        flight.active = false;
        return false;
    }
    return true;
}

std::vector<LocalTransportRoute> LocalTravelNetwork::builtinTransportRoutes() {
    // GameObject template identities. The client supplies geometry and stop
    // delays; missing paths are rejected instead of mapped onto another route.
    std::vector<LocalTransportRoute> routes = {
        {175080,3031,285,"The Iron Eagle: Orgrimmar - Grom'gol"},
        {164871,3031,302,"The Thundercaller: Orgrimmar - Undercity"},
        {176495,3031,301,"The Purple Princess: Undercity - Grom'gol"},
        {181689,7546,737,"The Cloudkisser: Undercity - Howling Fjord"},
        {186238,7546,713,"The Mighty Wind: Orgrimmar - Borean Tundra"},
        {20808,3015,241,"The Maiden's Fancy: Ratchet - Booty Bay"},
        {176231,3015,292,"The Lady Mehley: Menethil - Theramore"},
        {176310,3015,295,"The Bravery: Menethil - Auberdine"},
        {176244,7087,293,"The Moonspray: Auberdine - Teldrassil"},
        {181646,7087,503,"Elune's Blessing: Auberdine - Exodar"},
        {181688,7446,659,"The Northspear: Menethil - Howling Fjord"},
    };
    // Map.dbc 3.3.5a transport spaces, matching the route endpoints above.
    // https://www.azerothcore.org/wiki/map
    constexpr uint32_t crewMaps[] = {589,591,590,610,613,593,584,588,582,586,612};
    for (size_t i=0; i<routes.size(); ++i) routes[i].crewMapId=crewMaps[i];
    return routes;
}

} // namespace wowee::game
