#include "PlayerMovementSystem.hpp"

#include "AnimationState.hpp"
#include "AnimationType.hpp"
#include "CharacterConstants.hpp"
#include "CharacterKinematics.hpp"
#include "CollisionSystem.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "MotionSystem.hpp"
#include "Motor.hpp"
#include "PlayerInputState.hpp"
#include "PlayerModes.hpp"
#include "PlayerMovementState.hpp"
#include "Speed.hpp"
#include "Tilemap.hpp"
#include "TileMath.hpp"
#include "Transform.hpp"

#include <algorithm>
#include <cmath>

namespace PlayerMovementSystem
{
void UpdateFacing(
    Facing& facing, PlayerInputState& input, int moveDx, int moveDy, glm::vec2 normalizedDir)
{
    bool xActive = (moveDx != 0);
    bool yActive = (moveDy != 0);
    bool xRising = xActive && !input.prevAxisXActive;
    bool yRising = yActive && !input.prevAxisYActive;
    bool facingX = (facing.dir == Direction::LEFT || facing.dir == Direction::RIGHT);

    if (xRising && !yRising)
    {
        facing.dir = (moveDx > 0) ? Direction::RIGHT : Direction::LEFT;
    }
    else if (yRising && !xRising)
    {
        facing.dir = (moveDy > 0) ? Direction::DOWN : Direction::UP;
    }
    else if (xRising && yRising)
    {
        if (std::abs(normalizedDir.x) > std::abs(normalizedDir.y))
        {
            facing.dir = (moveDx > 0) ? Direction::RIGHT : Direction::LEFT;
        }
        else
        {
            facing.dir = (moveDy > 0) ? Direction::DOWN : Direction::UP;
        }
    }
    else if (facingX && xActive)
    {
        facing.dir = (moveDx > 0) ? Direction::RIGHT : Direction::LEFT;
    }
    else if (!facingX && yActive)
    {
        facing.dir = (moveDy > 0) ? Direction::DOWN : Direction::UP;
    }
    else if (xActive)
    {
        facing.dir = (moveDx > 0) ? Direction::RIGHT : Direction::LEFT;
    }
    else if (yActive)
    {
        facing.dir = (moveDy > 0) ? Direction::DOWN : Direction::UP;
    }

    input.prevAxisXActive = xActive;
    input.prevAxisYActive = yActive;
}

void UpdateAnimation(AnimationState& anim,
                     const PlayerModes& modes,
                     const Motor& motor,
                     float dt,
                     float animationSpeed)
{
    anim.animationTime += dt;

    // Frame cadence scales with actual speed: legs slow during accel/decel ramps and
    // quicken at sprint. The base walk speed is the 1.0x cadence reference.
    constexpr float ANIM_REF_SPEED = CharacterConstants::PLAYER_BASE_SPEED;
    float speed = glm::length(motor.velocity);
    float animScale = std::clamp(ANIM_REF_SPEED / std::max(speed, 1.0f), 0.4f, 2.5f);
    float animSpeed = animationSpeed * animScale;

    if (anim.animationTime >= animSpeed)
    {
        anim.animationTime = std::fmod(anim.animationTime, animSpeed);

        if (modes.animationType == AnimationType::IDLE)
        {
            CharacterKinematics::ResetAnimation(anim);
        }
        else
        {
            CharacterKinematics::AdvanceWalkAnimation(anim);
        }
    }
}

void Stop(AnimationState& anim, PlayerInputState& input, PlayerModes& modes, Motor& motor)
{
    input.isMoving = false;
    modes.animationType = AnimationType::IDLE;
    anim.currentFrame = 0;
    anim.walkSequenceIndex = 0;
    anim.animationTime = 0.0f;
    MotionSystem::Reset(motor);
}

glm::vec2 CurrentTileCenter(glm::vec2 position, float tileSize)
{
    if (tileSize <= 0.0f)
    {
        return glm::vec2(0.0f);
    }

    constexpr float EPS = 0.001f;  // Small epsilon to handle edge cases

    const int tileX = TileMath::TileIndex(position.x, tileSize);
    const int tileY = TileMath::AnchorTileRow(position.y, tileSize, EPS);
    return TileMath::TileFeetCenter(tileX, tileY, tileSize);
}

void Step(Transform& xf,
          Motor& motor,
          Facing& facing,
          AnimationState& anim,
          Elevation& elev,
          PlayerModes& modes,
          PlayerInputState& input,
          PlayerMovementState& movement,
          const Speed& speed,
          const Hitbox& hitbox,
          glm::vec2 direction,
          float dt,
          const Tilemap* tilemap,
          const std::vector<CharacterCollisionBody>* npcBodies)
{
    // Fallback tile size when there is no tilemap; matches PlayerSystem::SetTilePosition.
    constexpr float TILE_SIZE = 16.0f;

    auto signWithDeadzone = [](float v, float dz = 0.2f) -> int
    {
        return (v > dz) ? 1 : (v < -dz) ? -1 : 0;
    };
    auto signStep = [](float v) -> int { return (v > 1e-4f) ? 1 : (v < -1e-4f) ? -1 : 0; };

    const bool hasInput = glm::length(direction) > 0.1f;
    glm::vec2 inputDir(0.0f);

    if (hasInput)
    {
        inputDir = glm::normalize(direction);
        int inDx = signWithDeadzone(direction.x);
        int inDy = signWithDeadzone(direction.y);
        if (inDx != 0)
        {
            movement.lastInputX = inDx;
        }
        if (inDy != 0)
        {
            movement.lastInputY = inDy;
        }
        UpdateFacing(facing, input, inDx, inDy, inputDir);
    }
    else
    {
        // Clear axis tracking so the next key-down is a rising edge.
        input.prevAxisXActive = false;
        input.prevAxisYActive = false;
    }

    // Target speed from the active mode (bicycle > running > walking).
    float targetSpeed = speed.value;
    if (modes.isBicycling)
    {
        targetSpeed *= CharacterConstants::BICYCLE_SPEED_MULTIPLIER;
    }
    else if (modes.isRunning)
    {
        targetSpeed *= CharacterConstants::RUN_SPEED_MULTIPLIER;
    }
    targetSpeed *= modes.speedMultiplier;

    const float tileSize = tilemap ? static_cast<float>(tilemap->GetTileWidth()) : TILE_SIZE;

    // Kinematics: the motor owns velocity, accel/decel, and the grid-resolved stop.
    glm::vec2 disp =
        MotionSystem::ComputeDisplacement(motor, xf.position, inputDir, targetSpeed, tileSize, dt);

    // Animation is driven by actual velocity, not raw input, so glide frames animate.
    const bool moving = MotionSystem::IsMoving(motor);
    const AnimationType targetAnim =
        (modes.isRunning || modes.isBicycling) ? AnimationType::RUN : AnimationType::WALK;
    if (moving)
    {
        if (!input.isMoving)
        {
            input.isMoving = true;
            anim.walkSequenceIndex = 0;
            anim.currentFrame = 1;
            anim.animationTime = 0.0f;
        }
        modes.animationType = targetAnim;
    }
    else if (input.isMoving)
    {
        input.isMoving = false;
        modes.animationType = AnimationType::IDLE;
        anim.currentFrame = 0;
        anim.walkSequenceIndex = 0;
        anim.animationTime = 0.0f;
    }

    // No collision world (no-clip / null tilemap): integrate raw displacement.
    if (!tilemap)
    {
        xf.position += disp;
        return;
    }

    const SupportState support = CharacterKinematics::GetSupport(elev);

    // Track the last safe position whenever the box is not embedded in a solid tile.
    if (!CollisionSystem::CollidesWithTilesStrict(
            hitbox, xf.position, tilemap, 0, 0, false, support))
    {
        movement.lastSafeTileCenter = CurrentTileCenter(xf.position, tileSize);
    }

    // Fully settled and idle: stuck-recovery only, then bail.
    if (glm::length(disp) < 1e-4f)
    {
        if (auto teleport =
                CollisionSystem::HandleStuckRecovery(hitbox,
                                                     xf.position,
                                                     movement,
                                                     support,
                                                     CurrentTileCenter(xf.position, tileSize),
                                                     tilemap,
                                                     npcBodies))
        {
            // Re-snap the feet to a tile's bottom-center and reset the motor, the way
            // PlayerSystem::SetTilePosition does. Use the resolved tileSize (not the 16px
            // fallback) so non-16px maps snap correctly.
            // TODO: teleport already sits at a tile's bottom edge, so the bare
            // TileMath::TileIndex row floors to the next row down and the snap lands one tile
            // south of the tile FindClosestSafeTileCenter validated. Use TileMath::AnchorTileRow
            // for the row, or assign *teleport directly.
            const int tileX = TileMath::TileIndex(teleport->x, tileSize);
            const int tileY = TileMath::TileIndex(teleport->y, tileSize);
            xf.position = TileMath::TileFeetCenter(tileX, tileY, tileSize);
            MotionSystem::Reset(motor);
        }
        return;
    }

    // Decay hysteresis timers while there is movement to resolve.
    if (movement.slideTimer > 0.0f)
    {
        movement.slideTimer -= dt;
    }
    if (movement.axisTimer > 0.0f)
    {
        movement.axisTimer -= dt;
    }

    glm::vec2 normalizedDir = (glm::length(disp) > 1e-5f) ? glm::normalize(disp) : glm::vec2(0.0f);

    // Reset slide hysteresis when the dominant axis flips.
    bool curHorizontal = std::abs(normalizedDir.x) > std::abs(normalizedDir.y);
    bool lastHorizontal =
        std::abs(input.lastMovementDirection.x) > std::abs(input.lastMovementDirection.y);
    if (curHorizontal != lastHorizontal && movement.slideTimer <= 0.0f)
    {
        movement.slideDir = glm::vec2(0.0f);
    }

    int moveDx = signStep(disp.x);
    int moveDy = signStep(disp.y);
    bool diagonalInput = (moveDx != 0 && moveDy != 0);

    glm::vec2 desiredMovement = disp;

    auto probeMovement = [&](glm::vec2 target, int dx, int dy, bool diagonal)
    {
        return CollisionSystem::ProbeMovement(
            hitbox, xf.position, target, support, tilemap, npcBodies, dx, dy, diagonal);
    };

    CollisionSystem::MovementProbeResult directProbe =
        probeMovement(xf.position + desiredMovement, moveDx, moveDy, diagonalInput);
    bool npcBlocked = directProbe.npcBlocked;
    bool tileBlocked = directProbe.tileBlocked || !directProbe.transition.connected;
    bool initiallyTileBlocked = tileBlocked;
    bool didCornerSlide = false;

    if (npcBlocked)
    {
        desiredMovement = glm::vec2(0.0f);
    }
    else if (tileBlocked)
    {
        glm::vec2 slideMovement = CollisionSystem::TrySlideMovement(hitbox,
                                                                    xf.position,
                                                                    movement,
                                                                    support,
                                                                    desiredMovement,
                                                                    normalizedDir,
                                                                    dt,
                                                                    targetSpeed,
                                                                    tilemap,
                                                                    npcBodies,
                                                                    moveDx,
                                                                    moveDy,
                                                                    diagonalInput);

        if (glm::length(slideMovement) > 0.001f)
        {
            desiredMovement = slideMovement;
            didCornerSlide = true;
            tileBlocked = false;
        }
        else if (diagonalInput)
        {
            glm::vec2 moveX(desiredMovement.x, 0.0f);
            glm::vec2 moveY(0.0f, desiredMovement.y);

            bool okX = !probeMovement(xf.position + moveX, moveDx, 0, false).IsBlocked();
            bool okY = !probeMovement(xf.position + moveY, 0, moveDy, false).IsBlocked();

            if (okX && !okY)
            {
                desiredMovement = moveX;
                moveDy = 0;
                diagonalInput = false;
                tileBlocked = false;
            }
            else if (okY && !okX)
            {
                desiredMovement = moveY;
                moveDx = 0;
                diagonalInput = false;
                tileBlocked = false;
            }
        }
    }

    // Lane snapping (gentle), only for non-diagonal motion that isn't blocked/sliding.
    int effDx = signStep(desiredMovement.x);
    int effDy = signStep(desiredMovement.y);
    bool effDiagonal = (effDx != 0 && effDy != 0);
    if (!effDiagonal && !didCornerSlide && !initiallyTileBlocked)
    {
        desiredMovement =
            CollisionSystem::ApplyLaneSnapping(hitbox,
                                               xf.position,
                                               CurrentTileCenter(xf.position, tileSize),
                                               support,
                                               desiredMovement,
                                               normalizedDir,
                                               dt,
                                               tilemap,
                                               npcBodies,
                                               effDx,
                                               effDy);
    }

    // Final collision check with axis-separated fallback. Keep the exact probe
    // that accepted the resolved movement: its direction flags describe player
    // intent, while a tiny lane-snap correction is not diagonal input.
    CollisionSystem::MovementProbeResult acceptedProbe =
        probeMovement(xf.position + desiredMovement, effDx, effDy, effDiagonal);
    if (acceptedProbe.IsBlocked())
    {
        glm::vec2 tryX = xf.position + glm::vec2(desiredMovement.x, 0.0f);
        glm::vec2 tryY = xf.position + glm::vec2(0.0f, desiredMovement.y);

        const CollisionSystem::MovementProbeResult xProbe = probeMovement(tryX, moveDx, 0, false);
        const CollisionSystem::MovementProbeResult yProbe = probeMovement(tryY, 0, moveDy, false);
        bool okX = !xProbe.IsBlocked();
        bool okY = !yProbe.IsBlocked();

        if (okX && okY)
        {
            bool preferX;
            if (movement.axisTimer > 0.0f && movement.axisPref != 0)
            {
                preferX = (movement.axisPref > 0);
            }
            else
            {
                float xMag = std::abs(normalizedDir.x);
                float yMag = std::abs(normalizedDir.y);
                float diff = xMag - yMag;
                if (std::abs(diff) > 0.15f)
                {
                    preferX = (diff > 0.0f);
                    movement.axisPref = preferX ? 1 : -1;
                    movement.axisTimer = 0.15f;
                }
                else
                {
                    preferX = (movement.axisPref > 0) || (movement.axisPref == 0 && xMag > yMag);
                }
            }

            if (preferX)
            {
                desiredMovement.y = 0.0f;
                acceptedProbe = xProbe;
            }
            else
            {
                desiredMovement.x = 0.0f;
                acceptedProbe = yProbe;
            }
        }
        else if (okX)
        {
            desiredMovement.y = 0.0f;
            acceptedProbe = xProbe;
        }
        else if (okY)
        {
            desiredMovement.x = 0.0f;
            acceptedProbe = yProbe;
        }
        else
        {
            desiredMovement = glm::vec2(0.0f);
        }
    }

    // Blocked-axis velocity kill: when an axis was wanted but produced almost no motion,
    // drop that velocity component so speed cannot build invisibly against a wall.
    //
    // Exception: a corner slide redirects the blocked axis into a productive perpendicular
    // move, so the forward momentum is not wasted against a wall - keep it. Zeroing it here
    // makes the next frame ramp the forward speed from zero, the player drifts slowly back
    // into the corner, slides again, and re-zeros: the climb stutters to ~1/3 speed ("jelly").
    // Preserving the velocity lets a held input round the corner at full speed, matching the
    // pre-momentum behavior where desiredMovement was always normalizedDir * speed * dt.
    if (!didCornerSlide)
    {
        if (std::abs(disp.x) > 1e-4f && std::abs(desiredMovement.x) < 1e-4f)
        {
            MotionSystem::ZeroAxisX(motor);
        }
        if (std::abs(disp.y) > 1e-4f && std::abs(desiredMovement.y) < 1e-4f)
        {
            MotionSystem::ZeroAxisY(motor);
        }
    }

    if (glm::length(desiredMovement) > 0.001f && !acceptedProbe.IsBlocked())
    {
        input.lastMovementDirection = glm::normalize(desiredMovement);
        xf.position += desiredMovement;
        CharacterKinematics::CommitSupport(elev, acceptedProbe.transition.support);
    }
}
}  // namespace PlayerMovementSystem
