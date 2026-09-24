#!/usr/bin/env python3
"""Contracts for the 2.11 session/movement boundary hardening.

These checks deliberately read the production bodies. They guard the two
lifetime bugs this checkpoint closes: transient movement state surviving an
authoritative relocation, and session-owned callbacks/taxi recovery surviving
a full logout renderer rebuild.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
app = (root / "src/core/application.cpp").read_text()
world = (root / "src/core/world_entry_callback_handler.cpp").read_text()
cam = (root / "src/rendering/camera_controller.cpp").read_text()


def body(text: str, signature: str, next_signature: str) -> str:
    start = text.index(signature)
    end = text.index(next_signature, start)
    return text[start:end]

rebuild = body(app, "bool Application::rebuildSessionRenderer() {",
               "\nvoid Application::performLogoutToLogin() {")
for setter in (
    "setMeleeSwingCallback", "setRangedWeaponSwapCallback",
    "setLogoutCompleteCallback", "setKnockBackCallback",
    "setCameraShakeCallback", "setAutoFollowCallback",
    "setPlayerModelRebuildCallback", "setFaceCameraProvider",
):
    assert f"gameHandler->{setter}(nullptr);" in rebuild, setter

logout = body(app, "void Application::performLogoutToLogin() {",
              "\n// One frame of being in the world.")
disconnect = logout.index("gameHandler->disconnect();")
clear_taxi = logout.index("gameHandler->forceClearTaxiAndMovementState();")
assert disconnect < clear_taxi

relocate = body(world,
    "void WorldEntryCallbackHandler::applyAuthoritativeRelocation(",
    "\n// Sync teleported render position to server")
for token in (
    "renderer_.getCharacterPosition() = renderPos;",
    "cc->teleportTo(renderPos);",
    "cc->clearMovementInputs();",
    "cc->suppressMovementFor(movementSuppressSeconds);",
    "cc->suspendGravityFor(gravitySuspendSeconds);",
):
    assert token in relocate, token

setup = body(world, "void WorldEntryCallbackHandler::setupCallbacks() {",
             "\nvoid WorldEntryCallbackHandler::update(float deltaTime) {")
assert setup.count("applyAuthoritativeRelocation(renderPos") >= 3

reset = body(world, "void WorldEntryCallbackHandler::resetState() {", "\n}\n\n}}")
assert "taxiLandingReferenceZ_ = 0.0f;" in reset

# Online sessions use the same validated floor recovery, but only while the
# player is in-world and off taxis/transports. No GM command is involved.
assert "gameHandler_.getState() != game::WorldState::IN_WORLD" in setup
assert "gameHandler_.isOnTaxiFlight() || gameHandler_.isOnTransport()" in setup
assert "syncTeleportedPositionToServer(*safe);" in setup

transport = (root / "src/core/transport_callback_handler.cpp").read_text()
correction_start = transport.index("gameHandler_.setPlayerPositionCorrectionCallback")
correction_end = transport.index("// Taxi flight start callback", correction_start)
correction = transport[correction_start:correction_end]
assert "camera->teleportTo(renderPos);" in correction
assert "camera->suspendGravityFor(0.75f);" in correction
assert "ignored non-finite server position correction" in correction
assert "rejected non-finite world entry" in setup

teleport = body(cam, "void CameraController::teleportTo(const glm::vec3& pos) {",
                "\nbool CameraController::groundNotStreamedYet")
assert "rejected non-finite camera teleport" in teleport
for token in (
    "resetGroundRecovery();", "verticalVelocity = 0.0f;",
    "knockbackActive_ = false;", "knockbackHorizVel_ = glm::vec2(0.0f);",
    "jumpBufferTimer = 0.0f;", "coyoteTimer = 0.0f;",
    "airborneSeconds_ = 0.0f;", "movementSuppressTimer_ = 0.0f;",
    "gravitySuspendTimer_ = 0.0f;", "swimming = false;",
    "waterSampleGapSeconds_ = 0.0f;", "autoFollowTarget_ = nullptr;",
    "manualForwardWasDown_ = false;", "wasAscending_ = wasDescending_ = false;",
):
    assert token in teleport, token

print("PASS session/movement boundary: relocation resets transient motion, same-map entry uses one authoritative path, logout drops taxi recovery, renderer rebuild detaches in-game callbacks")
