#pragma once

// Local travel network: flight masters, taxi routes and world transports for
// the standalone/LAN realm.
//
// Where the data comes from, and why it is split this way:
//
//  * The route geometry is the player's own client data. TaxiNodes.dbc,
//    TaxiPath.dbc and TaxiPathNode.dbc already ship in every 3.3.5a client and
//    MovementHandler already parses them for online play, so the local realm
//    reuses the same rows rather than inventing a parallel network. Nothing
//    here is fabricated: a route the client does not describe does not exist.
//
//  * Which NPC is a flight master is server data. AzerothCore stores it in
//    creature_template.npcflag, which the catalog importer now carries through
//    as LocalNpcDefinition::npcFlags. When that is missing - the shipped
//    catalog predates the field - the network falls back to proximity: a taxi
//    node is a physical location, and the NPC standing on it is its flight
//    master. That is how the two data sets relate in the first place.
//
//  * Which world transports exist is also server data (gameobject_template
//    type 15 plus its taxi path). The catalog carries it when the user
//    re-imports; otherwise a small built-in table of the classic routes is
//    used. Every built-in entry is validated against the client's own
//    TaxiPath data before it is accepted, so a wrong guess produces no
//    transport rather than a ghost ship on a route that does not exist.
//
// This is a bounded travel simulation, not a claim of retail-identical flight
// paths, prices or schedules.

#include <cstdint>
#include <string>
#include <vector>

namespace wowee::game {

// One row of TaxiNodes.dbc, as the local realm needs it.
struct LocalTaxiNode {
    uint32_t id = 0;
    uint32_t mapId = 0;
    // Server coordinates, matching LocalRealmPlayer.
    float x = 0, y = 0, z = 0;
    std::string name;
    // A node with no mount display for either faction is not flown to: it is
    // a boat, zeppelin or tram stop. The client uses the same distinction.
    uint32_t mountAlliance = 0, mountHorde = 0;
    bool flightNode() const { return mountAlliance != 0 || mountHorde != 0; }
};

// One row of TaxiPath.dbc: a directed route between two nodes.
struct LocalTaxiPath {
    uint32_t id = 0;
    uint32_t fromNode = 0, toNode = 0;
    uint32_t cost = 0;   // copper
};

// One waypoint of TaxiPathNode.dbc.
struct LocalTaxiWaypoint {
    uint32_t pathId = 0;
    uint32_t index = 0;
    uint32_t mapId = 0;
    float x = 0, y = 0, z = 0;
    uint32_t flags = 0, delaySeconds = 0, arrivalEvent = 0, departureEvent = 0;
};

// A world transport (zeppelin, ship, tram) that runs a taxi path on a loop.
struct LocalTransportRoute {
    uint32_t entry = 0;          // gameobject entry, for provenance and saves
    uint32_t displayId = 0;      // GameObjectDisplayInfo id of the hull
    uint32_t pathId = 0;         // TaxiPath the hull follows
    std::string name;
    // Seconds for one full loop of the path. Derived from the path length and
    // a speed when the catalog does not state it.
    float periodSeconds = 0;
    // How long the transport waits at each end before setting off again.
    float dockSeconds = 20.0f; // legacy catalog field; DBC delays take precedence
    float speed = 32.0f;
    float acceleration = 1.0f;
    uint32_t crewMapId = 0; // Map.dbc transport-local spawn space; zero means no crew data.
};

// A transport's position at a moment in world time, plus whether it is docked.
struct LocalTransportState {
    uint32_t entry = 0;
    uint32_t mapId = 0;
    float x = 0, y = 0, z = 0;
    float orientation = 0;
    bool docked = false;
    // Index of the waypoint the hull has most recently passed, so a passenger
    // can be re-seated after a reconnect without replaying the whole path.
    uint32_t segment = 0;
    float segmentFraction = 0;
};

// A player's flight in progress.
struct LocalFlightState {
    bool active = false;
    uint32_t pathId = 0;
    uint32_t destinationNode = 0;
    // Distance already covered along the path, in world units.
    float travelled = 0;
    float totalLength = 0;
    float speed = 32.0f;   // world units per second
};

class LocalTravelNetwork {
public:
    // Flight speed used when a route does not state one. 32 u/s is the client's
    // ordinary taxi speed; it is a constant here rather than a tuned value so a
    // route's duration follows from its real length.
    static constexpr float DefaultFlightSpeed = 32.0f;
    // A flight master is the NPC standing on its node. Retail places them
    // within a few yards; the tolerance is generous enough for the ones on a
    // platform or behind a rail, and small enough that an unrelated NPC in the
    // same building does not qualify.
    static constexpr float FlightMasterRadius = 12.0f;
    // UNIT_NPC_FLAG_FLIGHTMASTER. Only meaningful once the catalog carries
    // npcflag; older catalogs report zero for every NPC.
    static constexpr uint32_t NpcFlagFlightMaster = 8192u;

    LocalTravelNetwork() = default;

    // Replace the network with client DBC rows. Waypoints may arrive in any
    // order; they are sorted per path. Returns false and leaves the previous
    // network untouched if the rows are unusable.
    bool setClientData(std::vector<LocalTaxiNode> nodes,
                       std::vector<LocalTaxiPath> paths,
                       std::vector<LocalTaxiWaypoint> waypoints,
                       std::string& error);

    bool loaded() const { return !nodes_.empty() && !paths_.empty(); }

    const std::vector<LocalTaxiNode>& nodes() const { return nodes_; }
    const std::vector<LocalTaxiPath>& paths() const { return paths_; }
    const std::vector<LocalTaxiWaypoint>& waypoints(uint32_t pathId) const;

    const LocalTaxiNode* node(uint32_t id) const;
    const LocalTaxiPath* path(uint32_t id) const;

    // The direct route between two nodes, or nullptr when the client data has
    // none. Taxi paths are directed, so the reverse leg is a different row.
    const LocalTaxiPath* directPath(uint32_t fromNode, uint32_t toNode) const;

    // Total length of a path in world units, or 0 when it has no waypoints.
    float pathLength(uint32_t pathId) const;

    // Position along a path at `distance` world units from its start. Returns
    // false when the path is unknown or empty.
    bool samplePath(uint32_t pathId, float distance, uint32_t& mapId,
                    float& x, float& y, float& z, float& orientation) const;

    // The taxi node an NPC at this position serves, or nullptr. Only flight
    // nodes qualify: a boat stop has no flight master.
    const LocalTaxiNode* flightNodeAt(uint32_t mapId, float x, float y, float z) const;

    // Print every transport route the player's own client describes - a taxi
    // path whose two ends are both boat/zeppelin stops rather than flight
    // nodes. Diagnostic only; it changes nothing.
    void logClientTransportPaths() const;

    // How far the first (or last) waypoint of a path sits from a stop, or -1
    // when the two are on different maps and the distance would be meaningless.
    //
    // This is what tells a route from a stub. Two paths can join the same pair
    // of stops in TaxiPath.dbc and only one of them actually run between them;
    // the one whose geometry starts and ends at the piers is the crossing, and
    // the other is a fragment whose row says more than its waypoints do. See
    // setTransportRoutes, which refuses to sail a hull down a path that does
    // not reach its own declared stops.
    float pathEndpointGap(uint32_t pathId, uint32_t nodeId, bool pathStart) const;

    // Nodes reachable from `fromNode` by a single direct route, restricted to
    // the nodes the player has already discovered. Retail lets a player fly to
    // any known node via connecting hops; this keeps it to routes the client
    // actually describes, so no invented geometry is ever flown.
    std::vector<uint32_t> destinationsFrom(uint32_t fromNode,
                                           const std::vector<uint32_t>& knownNodes) const;

    // --- Transports ---------------------------------------------------------

    // Install transport routes. Each is checked against the loaded client data:
    // a route whose path has no waypoints is rejected, so a wrong entry in the
    // built-in fallback table simply does not spawn. Returns how many were
    // accepted.
    size_t setTransportRoutes(const std::vector<LocalTransportRoute>& routes);
    const std::vector<LocalTransportRoute>& transportRoutes() const { return transports_; }

    // The routes shipped as a fallback for a catalog that carries none. These
    // are the classic ship and zeppelin lines; see the note at the top of this
    // header about their provenance.
    static std::vector<LocalTransportRoute> builtinTransportRoutes();

    // Where every transport is at `worldSeconds`. Deterministic: the same time
    // gives the same result on every console, which is what lets a LAN guest
    // predict a hull between host snapshots instead of jittering behind it.
    void sampleTransports(double worldSeconds,
                          std::vector<LocalTransportState>& out) const;

    // Advance a flight. Returns true while the flight is still running; sets
    // the player's position through the out parameters either way, so the
    // final call lands the player exactly on the destination node.
    bool advanceFlight(LocalFlightState& flight, float seconds, uint32_t& mapId,
                       float& x, float& y, float& z, float& orientation) const;

    // Start a flight along a direct route. Returns false when no such route
    // exists in the client data.
    bool beginFlight(uint32_t fromNode, uint32_t toNode, LocalFlightState& flight) const;

private:
    std::vector<LocalTaxiNode> nodes_;
    std::vector<LocalTaxiPath> paths_;
    // Waypoints grouped by path id, each group sorted by index, with the
    // cumulative distance from the path start precomputed so a sample is a
    // binary search rather than a walk.
    struct PathGeometry {
        uint32_t pathId = 0;
        std::vector<LocalTaxiWaypoint> points;
        std::vector<float> cumulative;   // size == points.size(), [0] == 0
        float length = 0;
    };
    std::vector<PathGeometry> geometry_;
    std::vector<LocalTransportRoute> transports_;

    struct TransportSpan {
        LocalTaxiWaypoint p0, p1, p2, p3;
        float arc[17]{};
        double begin = 0, end = 0, legTime = 0;
        float distanceFromStop = 0, legLength = 0, heading = 0;
        bool docked = false, cruiseOnly = false;
    };
    struct TransportSchedule {
        uint32_t entry = 0;
        double duration = 0;
        float speed = 32, acceleration = 1;
        std::vector<TransportSpan> spans;
    };
    std::vector<TransportSchedule> transportSchedules_;
    bool buildTransportSchedule(const LocalTransportRoute&, TransportSchedule&) const;
    const PathGeometry* geometryFor(uint32_t pathId) const;
};

} // namespace wowee::game
