#!/usr/bin/env python3
"""2.13 contracts for transport, death and session transition ownership.

The host suite cannot instantiate the PS4 renderers, so these checks guard the
production transition code itself: one transport-local passenger frame, hard
movement cleanup on death/teleport, and deliberate reconnect-vs-logout state
ownership.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
transport = (root / "src/game/transport_manager.cpp").read_text()
game = (root / "src/game/game_handler.cpp").read_text()
callbacks = (root / "src/game/game_handler_callbacks.cpp").read_text()
app = (root / "src/core/application.cpp").read_text()
local = (root / "src/game/local_gameplay.cpp").read_text()
header = (root / "include/game/game_handler.hpp").read_text()


def body(text: str, signature: str, next_signature: str) -> str:
    start = text.index(signature)
    end = text.index(next_signature, start)
    return text[start:end]

compose = body(transport,
    "glm::vec3 TransportManager::getPlayerWorldPosition",
    "\nglm::vec3 TransportManager::serverToTransportLocal")
assert "transport->transform * glm::vec4(localOffset, 1.0f)" in compose
assert "transport->position + localOffset" not in compose
assert "if (transport->isM2)" not in compose

wire = body(transport,
    "glm::vec3 TransportManager::serverToTransportLocal",
    "\nbool TransportManager::isPointOnTransportDeck")
assert "if (it != transports_.end()) return serverOffset;" in wire

boarding = body(game, "void GameHandler::updateM2TransportBoarding",
                "\n// ============================================================\n// Bank System")
assert "tr->invTransform * glm::vec4(playerRenderPos, 1.0f)" in boarding
assert "? playerCanonical - tr->position" not in boarding

# M2 riding converts the input vector into local space and composes the final
# position through the same TransportManager path as server attachments.
assert "glm::mat3(tr->invTransform) * walkRender" in app
assert "getPlayerWorldPosition(" in app

assert "void clearPendingPlayerTransportWorldTransfer();" in header
clear_pending = body(game,
    "void GameHandler::clearPendingPlayerTransportWorldTransfer()",
    "\nbool GameHandler::completePlayerTransportWorldTransfer")
for token in (
    "pendingPlayerTransportTransfer_ = false;",
    "pendingPlayerTransportGuid_ = 0;",
    "pendingPlayerTransportEntry_ = 0;",
    "pendingPlayerTransportMapId_ = 0xFFFFFFFFu;",
    "pendingPlayerTransportOffset_ = glm::vec3(0.0f);",
):
    assert token in clear_pending, token
complete = body(game, "bool GameHandler::completePlayerTransportWorldTransfer",
                "\n// Client-side transport boarding")
assert "clearPendingPlayerTransportWorldTransfer();" in complete
force = body(callbacks, "void GameHandler::forceClearTaxiAndMovementState()",
             "\nvoid GameHandler::setPosition")
assert "clearPendingPlayerTransportWorldTransfer();" in force

# disconnect remains the transient-reconnect boundary: it must not call the
# explicit hard-clear wrapper on its own.
disconnect = body(game, "void GameHandler::disconnect()", "\nvoid GameHandler::update(")
assert "forceClearTaxiAndMovementState" not in disconnect
assert "clearPendingPlayerTransportWorldTransfer" not in disconnect

travel_clear = body(local, "void clearLocalTravelMotion(LocalRealmPlayer& p)",
                    "\nvoid finishLocalTeleport")
for token in (
    "p.movementState = 0;", "p.falling = false;", "p.flight = {};",
    "p.transportEntry = 0;", "p.transportOffsetX = p.transportOffsetY = p.transportOffsetZ = 0;",
):
    assert token in travel_clear, token
assert local.count("clearLocalTravelMotion(*p);") >= 1
assert local.count("clearLocalTravelMotion(*target);") >= 1
assert local.count("localCaptureCorpse(*p);") >= 1
assert local.count("localCaptureCorpse(*target);") >= 1

print("PASS 2.13 transition matrix: local passenger frame, death travel cutoff, transfer cleanup, reconnect preservation")
