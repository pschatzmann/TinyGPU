#pragma once
#include <math.h>
#include <stdint.h>

#include "TinyGPU/Emulation.h"
#include "TinyGPU/Input/TouchDriver.h"

namespace tinygpu {

/// Lifecycle phase of a continuous gesture (drag/pan/scroll/pinch/rotate).
/// Discrete gestures (tap/double_tap/long_press/swipe_*) always report
/// kEnded, since they are recognized once, at the moment they complete.
enum class GesturePhase : uint8_t { kBegan, kChanged, kEnded };

enum class GestureType : uint8_t {
  kNone,
  kTap,
  kDoubleTap,
  kLongPress,  ///< aka "press"
  kSwipeLeft,
  kSwipeRight,
  kSwipeUp,
  kSwipeDown,
  kDrag,    ///< continuous move that started over a draggable target
  kPan,     ///< continuous move over the background, not vertical-dominant
  kScroll,  ///< continuous move over the background, vertical-dominant
  kPinchIn,   ///< two fingers moving together (zoom out)
  kPinchOut,  ///< two fingers moving apart (zoom in)
  kRotate,    ///< two-finger rotation
};

/// Human-readable name for a GestureType, useful for logging.
inline const char* toString(GestureType type) {
  switch (type) {
    case GestureType::kTap:
      return "tap";
    case GestureType::kDoubleTap:
      return "double_tap";
    case GestureType::kLongPress:
      return "long_press";
    case GestureType::kSwipeLeft:
      return "swipe_left";
    case GestureType::kSwipeRight:
      return "swipe_right";
    case GestureType::kSwipeUp:
      return "swipe_up";
    case GestureType::kSwipeDown:
      return "swipe_down";
    case GestureType::kDrag:
      return "drag";
    case GestureType::kPan:
      return "pan";
    case GestureType::kScroll:
      return "scroll";
    case GestureType::kPinchIn:
      return "pinch_in";
    case GestureType::kPinchOut:
      return "pinch_out";
    case GestureType::kRotate:
      return "rotate";
    default:
      return "none";
  }
}

struct GestureEvent {
  GestureType type = GestureType::kNone;
  GesturePhase phase = GesturePhase::kEnded;
  Point point;       ///< current touch position
  Point startPoint;  ///< where the touch began
  int16_t deltaX = 0;      ///< cumulative movement since the touch began
  int16_t deltaY = 0;
  int16_t stepDeltaX = 0;  ///< movement since the previous kChanged event
  int16_t stepDeltaY = 0;
  float scale = 1.0f;            ///< pinch: current / initial two-finger distance
  float rotationDegrees = 0.0f;  ///< rotate: cumulative angle change
  uint32_t durationMs = 0;       ///< how long the touch has been held
};

/**
 * @brief Recognizes touch gestures from a stream of single-point touch
 * samples, and reports them through a single callback.
 *
 * This does not replace TouchDriver - feed it the same driver you'd
 * otherwise poll directly, once per loop():
 *
 *   GestureDetector gestures;
 *   gestures.onGesture = handleGesture;
 *   gestures.isDraggable = [](int16_t x, int16_t y) { ... };  // optional
 *   void loop() { gestures.update(touchDriver); }
 *
 * isDraggable, if set, is checked against the position where a touch
 * started; if it returns true the resulting continuous-move gesture is
 * reported as kDrag, otherwise as kPan/kScroll. Without it, continuous
 * moves are always reported as kPan/kScroll (never kDrag).
 *
 * pinch/rotate require TouchDriver::getSecondPoint() to return a genuine
 * second simultaneous touch; none of the drivers built into this library
 * can supply one (see TouchDriver::getSecondPoint), so those two gesture
 * types will not fire against them.
 */
class GestureDetector {
 public:
  void (*onGesture)(GestureEvent&) = nullptr;
  bool (*isDraggable)(int16_t x, int16_t y) = nullptr;

  // Tuning knobs, in pixels / milliseconds.
  uint16_t tapMaxDurationMs = 300;
  uint16_t tapMaxMovePx = 12;
  uint16_t doubleTapMaxGapMs = 350;
  uint16_t longPressMinDurationMs = 600;
  uint16_t swipeMinDistancePx = 40;
  uint16_t swipeMaxDurationMs = 500;
  uint16_t dragStartThresholdPx = 8;

  /// Call once per loop() with the same driver you'd otherwise poll
  /// directly via isTouched()/getPoint().
  void update(TouchDriver& driver) {
    Point point;
    const bool touched = driver.isTouched() && driver.getPoint(point);
    const uint32_t now = millis();

    if (touched) {
      handleTouched(point, now);
      Point second;
      updatePinchRotate(driver.getSecondPoint(second), point, second);
    } else if (isDown_) {
      handleReleased(now);
    }
  }

 private:
  bool isDown_ = false;
  bool dragActive_ = false;
  bool longPressFired_ = false;
  bool lastTapValid_ = false;
  GestureType dragType_ = GestureType::kPan;
  uint32_t downTime_ = 0;
  uint32_t lastTapTime_ = 0;
  Point downPoint_;
  Point lastPoint_;

  bool secondActive_ = false;
  float initialPinchDistance_ = 1.0f;
  float lastPinchDistance_ = 0.0f;
  float initialPinchAngle_ = 0.0f;
  float lastPinchAngle_ = 0.0f;

  static int16_t iabs(int16_t v) { return v < 0 ? static_cast<int16_t>(-v) : v; }

  void handleTouched(Point point, uint32_t now) {
    if (!isDown_) {
      isDown_ = true;
      downTime_ = now;
      downPoint_ = point;
      lastPoint_ = point;
      dragActive_ = false;
      longPressFired_ = false;
      return;
    }

    const int16_t dx = static_cast<int16_t>(point.x - downPoint_.x);
    const int16_t dy = static_cast<int16_t>(point.y - downPoint_.y);
    const int16_t adx = iabs(dx);
    const int16_t ady = iabs(dy);

    if (!dragActive_ && (adx > dragStartThresholdPx || ady > dragStartThresholdPx)) {
      dragActive_ = true;
      dragType_ = classifyDragType(downPoint_, dx, dy);
      emitContinuous(dragType_, GesturePhase::kBegan, point, dx, dy, 0, 0, now);
    } else if (dragActive_) {
      const int16_t stepDx = static_cast<int16_t>(point.x - lastPoint_.x);
      const int16_t stepDy = static_cast<int16_t>(point.y - lastPoint_.y);
      if (stepDx != 0 || stepDy != 0) {
        emitContinuous(dragType_, GesturePhase::kChanged, point, dx, dy, stepDx, stepDy, now);
      }
    }

    if (!dragActive_ && !longPressFired_ && (now - downTime_) >= longPressMinDurationMs) {
      longPressFired_ = true;
      emitDiscrete(GestureType::kLongPress, point, dx, dy, now - downTime_);
    }

    lastPoint_ = point;
  }

  void handleReleased(uint32_t now) {
    isDown_ = false;
    const uint32_t duration = now - downTime_;
    const int16_t dx = static_cast<int16_t>(lastPoint_.x - downPoint_.x);
    const int16_t dy = static_cast<int16_t>(lastPoint_.y - downPoint_.y);
    const int16_t adx = iabs(dx);
    const int16_t ady = iabs(dy);

    if (dragActive_) {
      emitContinuous(dragType_, GesturePhase::kEnded, lastPoint_, dx, dy, 0, 0, now);
      // A drag/pan/scroll that was also fast and far enough is additionally
      // reported as a swipe, so callers that only care about swipes don't
      // need to track distance/duration themselves.
      if (duration <= swipeMaxDurationMs &&
          (adx >= swipeMinDistancePx || ady >= swipeMinDistancePx)) {
        emitDiscrete(swipeTypeFor(dx, dy, adx, ady), lastPoint_, dx, dy, duration);
      }
      dragActive_ = false;
    } else if (!longPressFired_ && adx <= tapMaxMovePx && ady <= tapMaxMovePx &&
               duration <= tapMaxDurationMs) {
      if (lastTapValid_ && (downTime_ - lastTapTime_) <= doubleTapMaxGapMs) {
        emitDiscrete(GestureType::kDoubleTap, lastPoint_, dx, dy, duration);
        lastTapValid_ = false;
      } else {
        emitDiscrete(GestureType::kTap, lastPoint_, dx, dy, duration);
        lastTapValid_ = true;
        lastTapTime_ = downTime_;
      }
    }

    secondActive_ = false;
  }

  GestureType classifyDragType(Point downPoint, int16_t dx, int16_t dy) const {
    if (isDraggable != nullptr && isDraggable(downPoint.x, downPoint.y)) {
      return GestureType::kDrag;
    }
    return (iabs(dy) >= iabs(dx)) ? GestureType::kScroll : GestureType::kPan;
  }

  static GestureType swipeTypeFor(int16_t dx, int16_t dy, int16_t adx, int16_t ady) {
    if (adx >= ady) {
      return dx > 0 ? GestureType::kSwipeRight : GestureType::kSwipeLeft;
    }
    return dy > 0 ? GestureType::kSwipeDown : GestureType::kSwipeUp;
  }

  void emitContinuous(GestureType type, GesturePhase phase, Point point, int16_t dx,
                      int16_t dy, int16_t stepDx, int16_t stepDy, uint32_t now) {
    if (!onGesture) return;
    GestureEvent ev;
    ev.type = type;
    ev.phase = phase;
    ev.point = point;
    ev.startPoint = downPoint_;
    ev.deltaX = dx;
    ev.deltaY = dy;
    ev.stepDeltaX = stepDx;
    ev.stepDeltaY = stepDy;
    ev.durationMs = now - downTime_;
    onGesture(ev);
  }

  void emitDiscrete(GestureType type, Point point, int16_t dx, int16_t dy,
                    uint32_t duration) {
    if (!onGesture) return;
    GestureEvent ev;
    ev.type = type;
    ev.phase = GesturePhase::kEnded;
    ev.point = point;
    ev.startPoint = downPoint_;
    ev.deltaX = dx;
    ev.deltaY = dy;
    ev.durationMs = duration;
    onGesture(ev);
  }

  /// Tracks a second touch point (if the driver ever supplies one) to
  /// recognize pinch/rotate. See TouchDriver::getSecondPoint.
  void updatePinchRotate(bool hasSecond, Point first, Point second) {
    if (!hasSecond) {
      secondActive_ = false;
      return;
    }

    const float dxp = static_cast<float>(second.x - first.x);
    const float dyp = static_cast<float>(second.y - first.y);
    const float distance = sqrtf(dxp * dxp + dyp * dyp);
    const float angle = atan2f(dyp, dxp) * 180.0f / 3.14159265f;

    if (!secondActive_) {
      secondActive_ = true;
      initialPinchDistance_ = distance > 0.0f ? distance : 1.0f;
      lastPinchDistance_ = distance;
      initialPinchAngle_ = angle;
      lastPinchAngle_ = angle;
      return;
    }

    if (!onGesture) {
      lastPinchDistance_ = distance;
      lastPinchAngle_ = angle;
      return;
    }

    const float distDelta = distance - lastPinchDistance_;
    if (distDelta > 0.5f || distDelta < -0.5f) {
      GestureEvent ev;
      ev.type = (distDelta > 0.0f) ? GestureType::kPinchOut : GestureType::kPinchIn;
      ev.phase = GesturePhase::kChanged;
      ev.point = first;
      ev.startPoint = downPoint_;
      ev.scale = distance / initialPinchDistance_;
      ev.durationMs = millis() - downTime_;
      onGesture(ev);
    }

    float angleDelta = angle - lastPinchAngle_;
    if (angleDelta > 180.0f) angleDelta -= 360.0f;
    if (angleDelta < -180.0f) angleDelta += 360.0f;
    if (angleDelta > 1.0f || angleDelta < -1.0f) {
      GestureEvent ev;
      ev.type = GestureType::kRotate;
      ev.phase = GesturePhase::kChanged;
      ev.point = first;
      ev.startPoint = downPoint_;
      ev.rotationDegrees = angle - initialPinchAngle_;
      ev.durationMs = millis() - downTime_;
      onGesture(ev);
    }

    lastPinchDistance_ = distance;
    lastPinchAngle_ = angle;
  }
};

}  // namespace tinygpu
