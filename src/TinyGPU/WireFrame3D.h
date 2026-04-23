#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "ISurface.h"
#include "TinyGPUConfig.h"
#include "TinyGPU/Vector.h"

#if TINYGPU_ENABLE_ESP32S3_OPTIMIZATIONS
#include "esp_dsp.h"
#endif

namespace tinygpu {

/**
 * @brief Lightweight 3D wireframe renderer for TinyGPU drawing targets.
 *
 * WireFrame3D implements a compact CPU-side 3D pipeline intended for embedded
 * and Arduino-style environments where a full 3D engine would be excessive.
 * It stores no scene graph of its own; instead, it provides the math and
 * rendering steps needed to transform user-supplied mesh data into 2D line
 * output on any ISurface-compatible surface.
 *
 * The class covers the main stages of a simple wireframe renderer:
 * - 3D vector and 4x4 matrix operations
 * - model transforms such as translation, scaling, and rotation
 * - camera setup via a look-at style view transform
 * - perspective or orthographic projection into screen space
 * - minimal per-pixel depth handling for overlapping wireframe edges
 *
 * Meshes are represented as vertices plus edge lists, which keeps memory usage
 * predictable and makes the class suitable for simple geometry such as cubes,
 * spheres, axes, grids, and custom debug or UI visualizations.
 */
template <typename RGB_T = RGB565>
class WireFrame3D {
 public:
  /// Represents a 3D point or vector.
  struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3 operator+(const Vec3& other) const {
      return {x + other.x, y + other.y, z + other.z};
    }

    Vec3 operator-(const Vec3& other) const {
      return {x - other.x, y - other.y, z - other.z};
    }

    Vec3 operator*(float scalar) const {
      return {x * scalar, y * scalar, z * scalar};
    }

    Vec3 operator/(float scalar) const {
      return {x / scalar, y / scalar, z / scalar};
    }
  };

  /// Represents a homogeneous 4D vector.
  struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
  };

  /// Represents a 4x4 transformation matrix.
  struct Mat4 {
    float m[4][4] = {};

    /// Returns an identity matrix.
    static Mat4 identity() {
      Mat4 result;
      result.m[0][0] = 1.0f;
      result.m[1][1] = 1.0f;
      result.m[2][2] = 1.0f;
      result.m[3][3] = 1.0f;
      return result;
    }

    /// Multiplies two matrices.
    Mat4 operator*(const Mat4& other) const {
#if TINYGPU_ENABLE_ESP32S3_OPTIMIZATIONS
      return multiplyEspDsp(*this, other);
#else
      Mat4 result;
      for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
          for (size_t index = 0; index < 4; ++index) {
            result.m[row][column] += m[row][index] * other.m[index][column];
          }
        }
      }
      return result;
#endif
    }

    /// Transforms a homogeneous vector.
    Vec4 operator*(const Vec4& vector) const {
#if TINYGPU_ENABLE_ESP32S3_OPTIMIZATIONS
      return transformEspDsp(*this, vector);
#else
      return {(m[0][0] * vector.x) + (m[0][1] * vector.y) +
                  (m[0][2] * vector.z) + (m[0][3] * vector.w),
              (m[1][0] * vector.x) + (m[1][1] * vector.y) +
                  (m[1][2] * vector.z) + (m[1][3] * vector.w),
              (m[2][0] * vector.x) + (m[2][1] * vector.y) +
                  (m[2][2] * vector.z) + (m[2][3] * vector.w),
              (m[3][0] * vector.x) + (m[3][1] * vector.y) +
                  (m[3][2] * vector.z) + (m[3][3] * vector.w)};
#endif
    }

   private:
#if TINYGPU_ENABLE_ESP32S3_OPTIMIZATIONS
    static Mat4 multiplyEspDsp(const Mat4& left, const Mat4& right) {
      Mat4 result;
      const esp_err_t status =
          dspm_mult_f32(reinterpret_cast<const float*>(left.m),
                        reinterpret_cast<const float*>(right.m),
                        reinterpret_cast<float*>(result.m), 4, 4, 4);
      if (status == ESP_OK) {
        return result;
      }
      return multiplyGeneric(left, right);
    }

    static Vec4 transformEspDsp(const Mat4& matrix, const Vec4& vector) {
      const float input[4] = {vector.x, vector.y, vector.z, vector.w};
      float output[4] = {};

      const esp_err_t status = dspm_mult_f32(
          reinterpret_cast<const float*>(matrix.m), input, output, 4, 4, 1);
      if (status == ESP_OK) {
        return {output[0], output[1], output[2], output[3]};
      }
      return transformGeneric(matrix, vector);
    }
#endif

    static Mat4 multiplyGeneric(const Mat4& left, const Mat4& right) {
      Mat4 result;
      for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
          for (size_t index = 0; index < 4; ++index) {
            result.m[row][column] +=
                left.m[row][index] * right.m[index][column];
          }
        }
      }
      return result;
    }

    static Vec4 transformGeneric(const Mat4& matrix, const Vec4& vector) {
      return {(matrix.m[0][0] * vector.x) + (matrix.m[0][1] * vector.y) +
                  (matrix.m[0][2] * vector.z) + (matrix.m[0][3] * vector.w),
              (matrix.m[1][0] * vector.x) + (matrix.m[1][1] * vector.y) +
                  (matrix.m[1][2] * vector.z) + (matrix.m[1][3] * vector.w),
              (matrix.m[2][0] * vector.x) + (matrix.m[2][1] * vector.y) +
                  (matrix.m[2][2] * vector.z) + (matrix.m[2][3] * vector.w),
              (matrix.m[3][0] * vector.x) + (matrix.m[3][1] * vector.y) +
                  (matrix.m[3][2] * vector.z) + (matrix.m[3][3] * vector.w)};
    }
  };

  /// Represents an edge between two mesh vertices.
  struct Edge {
    size_t start = 0;
    size_t end = 0;
  };

  /// Represents a wireframe mesh.
  struct Mesh {
    Vector<Vec3> vertices;
    Vector<Edge> edges;
  };

  /// Stores camera parameters for view transformation.
  struct Camera {
    Vec3 position{0.0f, 0.0f, 5.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
  };

  /// Creates an empty wireframe renderer.
  WireFrame3D() {
    updateViewMatrix();
    updateProjectionMatrix();
  }

  /// Creates a wireframe renderer with the specified viewport size.
  WireFrame3D(size_t viewportWidth, size_t viewportHeight)
      : viewportWidth_(viewportWidth), viewportHeight_(viewportHeight) {
    updateViewMatrix();
    updateProjectionMatrix();
  }

  /// Creates a wireframe renderer with the dimensions of the given surface.
  WireFrame3D(ISurface<RGB_T>& surface)
      : viewportWidth_(surface.width()), viewportHeight_(surface.height()) {
    updateViewMatrix();
    updateProjectionMatrix();
  }

  /// Sets the viewport size used for projection and depth buffering.
  void setViewport(size_t viewportWidth, size_t viewportHeight) {
    viewportWidth_ = viewportWidth;
    viewportHeight_ = viewportHeight;
    updateProjectionMatrix();
  }

  bool begin() {
    resizeDepthBuffer();
    return true;
  }

  /// Returns the current viewport width.
  size_t viewportWidth() const { return viewportWidth_; }

  /// Returns the current viewport height.
  size_t viewportHeight() const { return viewportHeight_; }

  /// Sets the active camera.
  void setCamera(const Camera& camera) {
    camera_ = camera;
    updateViewMatrix();
    updateProjectionMatrix();
  }

  /// Returns the active camera.
  const Camera& camera() const { return camera_; }

  /// Sets a perspective projection using the current viewport aspect ratio.
  void setPerspective(float fovYDegrees, float nearPlane = 0.1f,
                      float farPlane = 100.0f) {
    projectionMode_ = ProjectionMode::Perspective;
    perspectiveFovYDegrees_ = fovYDegrees;
    camera_.nearPlane = nearPlane;
    camera_.farPlane = farPlane;
    updateProjectionMatrix();
  }

  /// Sets an orthographic projection volume.
  void setOrthographic(float left, float right, float bottom, float top,
                       float nearPlane = 0.1f, float farPlane = 100.0f) {
    projectionMode_ = ProjectionMode::Orthographic;
    orthographicLeft_ = left;
    orthographicRight_ = right;
    orthographicBottom_ = bottom;
    orthographicTop_ = top;
    camera_.nearPlane = nearPlane;
    camera_.farPlane = farPlane;
    updateProjectionMatrix();
  }

  /// Returns the current view matrix.
  const Mat4& viewMatrix() const { return viewMatrix_; }

  /// Returns the current projection matrix.
  const Mat4& projectionMatrix() const { return projectionMatrix_; }

  /// Clears the internal depth buffer.
  void clearDepthBuffer() {
    std::fill(depthBuffer_.begin(), depthBuffer_.end(),
              std::numeric_limits<float>::infinity());
  }

  /// Transforms a point by a matrix and divides by w when possible.
  static Vec3 transformPoint(const Mat4& matrix, const Vec3& point) {
    const Vec4 transformed = matrix * Vec4{point.x, point.y, point.z, 1.0f};
    if (transformed.w != 0.0f) {
      return {transformed.x / transformed.w, transformed.y / transformed.w,
              transformed.z / transformed.w};
    }
    return {transformed.x, transformed.y, transformed.z};
  }

  /// Creates a translation matrix.
  static Mat4 translation(float x, float y, float z) {
    Mat4 result = Mat4::identity();
    result.m[0][3] = x;
    result.m[1][3] = y;
    result.m[2][3] = z;
    return result;
  }

  /// Creates a scale matrix.
  static Mat4 scaling(float x, float y, float z) {
    Mat4 result = Mat4::identity();
    result.m[0][0] = x;
    result.m[1][1] = y;
    result.m[2][2] = z;
    return result;
  }

  /// Creates a rotation matrix around the X axis.
  static Mat4 rotationX(float radians) {
    Mat4 result = Mat4::identity();
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result.m[1][1] = cosine;
    result.m[1][2] = -sine;
    result.m[2][1] = sine;
    result.m[2][2] = cosine;
    return result;
  }

  /// Creates a rotation matrix around the Y axis.
  static Mat4 rotationY(float radians) {
    Mat4 result = Mat4::identity();
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result.m[0][0] = cosine;
    result.m[0][2] = sine;
    result.m[2][0] = -sine;
    result.m[2][2] = cosine;
    return result;
  }

  /// Creates a rotation matrix around the Z axis.
  static Mat4 rotationZ(float radians) {
    Mat4 result = Mat4::identity();
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result.m[0][0] = cosine;
    result.m[0][1] = -sine;
    result.m[1][0] = sine;
    result.m[1][1] = cosine;
    return result;
  }

  /// Creates a wireframe cube centered at the origin.
  static Mesh cube(float size = 1.0f) {
    const float halfSize = size * 0.5f;
    return {{{-halfSize, -halfSize, -halfSize},
             {halfSize, -halfSize, -halfSize},
             {halfSize, halfSize, -halfSize},
             {-halfSize, halfSize, -halfSize},
             {-halfSize, -halfSize, halfSize},
             {halfSize, -halfSize, halfSize},
             {halfSize, halfSize, halfSize},
             {-halfSize, halfSize, halfSize}},
            {{0, 1},
             {1, 2},
             {2, 3},
             {3, 0},
             {4, 5},
             {5, 6},
             {6, 7},
             {7, 4},
             {0, 4},
             {1, 5},
             {2, 6},
             {3, 7}}};
  }

  /// Creates RGB-style axis lines centered at the origin.
  static Mesh axis(float length = 1.0f) {
    return {{{0.0f, 0.0f, 0.0f},
             {length, 0.0f, 0.0f},
             {0.0f, length, 0.0f},
             {0.0f, 0.0f, length}},
            {{0, 1}, {0, 2}, {0, 3}}};
  }

  /// Creates a wireframe grid on the XZ plane.
  static Mesh grid(size_t subdivisions = 10, float spacing = 1.0f) {
    Mesh mesh;
    const float halfExtent =
        (static_cast<float>(subdivisions) * spacing) * 0.5f;

    for (size_t index = 0; index <= subdivisions; ++index) {
      const float offset = (static_cast<float>(index) * spacing) - halfExtent;

      const size_t lineStart = mesh.vertices.size();
      mesh.vertices.push_back({-halfExtent, 0.0f, offset});
      mesh.vertices.push_back({halfExtent, 0.0f, offset});
      mesh.edges.push_back({lineStart, lineStart + 1});

      const size_t columnStart = mesh.vertices.size();
      mesh.vertices.push_back({offset, 0.0f, -halfExtent});
      mesh.vertices.push_back({offset, 0.0f, halfExtent});
      mesh.edges.push_back({columnStart, columnStart + 1});
    }

    return mesh;
  }

  /// Creates a wireframe sphere centered at the origin.
  static Mesh sphere(float radius = 1.0f, size_t latitudeSteps = 8,
                     size_t longitudeSteps = 12) {
    Mesh mesh;
    const size_t latCount = std::max<size_t>(2, latitudeSteps);
    const size_t lonCount = std::max<size_t>(3, longitudeSteps);
    const float pi = 3.14159265358979323846f;

    mesh.vertices.reserve((latCount + 1) * (lonCount + 1));
    mesh.edges.reserve((latCount * lonCount) + ((latCount + 1) * lonCount));

    for (size_t latitude = 0; latitude <= latCount; ++latitude) {
      const float v =
          static_cast<float>(latitude) / static_cast<float>(latCount);
      const float phi = v * pi;
      const float ringY = radius * std::cos(phi);
      const float ringRadius = radius * std::sin(phi);

      for (size_t longitude = 0; longitude <= lonCount; ++longitude) {
        const float u =
            static_cast<float>(longitude) / static_cast<float>(lonCount);
        const float theta = u * (2.0f * pi);
        mesh.vertices.push_back({ringRadius * std::cos(theta), ringY,
                                 ringRadius * std::sin(theta)});
      }
    }

    const size_t rowSize = lonCount + 1;
    for (size_t latitude = 0; latitude <= latCount; ++latitude) {
      for (size_t longitude = 0; longitude < lonCount; ++longitude) {
        const size_t current = (latitude * rowSize) + longitude;
        mesh.edges.push_back({current, current + 1});
      }
    }

    for (size_t latitude = 0; latitude < latCount; ++latitude) {
      for (size_t longitude = 0; longitude <= lonCount; ++longitude) {
        const size_t current = (latitude * rowSize) + longitude;
        mesh.edges.push_back({current, current + rowSize});
      }
    }

    return mesh;
  }

  /// Creates a view matrix from camera parameters.
  static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 forward = normalize(target - eye);
    const Vec3 right = normalize(cross(forward, up));
    const Vec3 actualUp = cross(right, forward);

    Mat4 result = Mat4::identity();
    result.m[0][0] = right.x;
    result.m[0][1] = right.y;
    result.m[0][2] = right.z;
    result.m[0][3] = -dot(right, eye);

    result.m[1][0] = actualUp.x;
    result.m[1][1] = actualUp.y;
    result.m[1][2] = actualUp.z;
    result.m[1][3] = -dot(actualUp, eye);

    result.m[2][0] = -forward.x;
    result.m[2][1] = -forward.y;
    result.m[2][2] = -forward.z;
    result.m[2][3] = dot(forward, eye);
    return result;
  }

  /// Projects a world-space point into screen space.
  bool projectPoint(const Vec3& point, int16_t& screenX, int16_t& screenY,
                    float& depth, const Mat4& model = Mat4::identity()) const {
    ProjectedPoint projected;
    Vec3 viewPoint = transformAffine(viewMatrix_ * model, point);
    if (!clipPointToCameraRange(viewPoint)) {
      return false;
    }
    if (!projectViewPoint(viewPoint, projected)) {
      return false;
    }

    screenX = projected.x;
    screenY = projected.y;
    depth = projected.depth;
    return true;
  }

  /// Renders a wireframe mesh with minimal depth buffering.
  void renderWireframe(ISurface<RGB_T>& target, const Mesh& mesh,
                       const Mat4& model = Mat4::identity(),
                       RGB_T color = RGB_T(255, 255, 255),
                       bool clearDepth = true) {
    if (viewportWidth_ != target.width() ||
        viewportHeight_ != target.height()) {
      setViewport(target.width(), target.height());
    }
    if (clearDepth) {
      clearDepthBuffer();
    }

    const Mat4 modelView = viewMatrix_ * model;
    for (const Edge& edge : mesh.edges) {
      if (edge.start >= mesh.vertices.size() ||
          edge.end >= mesh.vertices.size()) {
        continue;
      }

      Vec3 start = transformAffine(modelView, mesh.vertices[edge.start]);
      Vec3 end = transformAffine(modelView, mesh.vertices[edge.end]);
      if (!clipLineToCameraRange(start, end)) {
        continue;
      }

      ProjectedPoint projectedStart;
      ProjectedPoint projectedEnd;
      if (!projectViewPoint(start, projectedStart) ||
          !projectViewPoint(end, projectedEnd)) {
        continue;
      }

      drawDepthLine(target, projectedStart, projectedEnd, color);
    }
  }

 protected:
  /// Represents a projected vertex on screen.
  struct ProjectedPoint {
    int16_t x = 0;
    int16_t y = 0;
    float depth = 0.0f;
  };

  enum class ProjectionMode { Perspective, Orthographic };

  size_t viewportWidth_ = 0;
  size_t viewportHeight_ = 0;
  Camera camera_;
  Mat4 viewMatrix_ = Mat4::identity();
  Mat4 projectionMatrix_ = Mat4::identity();
  ProjectionMode projectionMode_ = ProjectionMode::Perspective;
  float perspectiveFovYDegrees_ = 60.0f;
  float orthographicLeft_ = -1.0f;
  float orthographicRight_ = 1.0f;
  float orthographicBottom_ = -1.0f;
  float orthographicTop_ = 1.0f;
  Vector<float> depthBuffer_;

  static float dot(const Vec3& first, const Vec3& second) {
#if TINYGPU_ENABLE_ESP32S3_OPTIMIZATIONS
    return dotEspDsp(first, second);
#else
    return (first.x * second.x) + (first.y * second.y) + (first.z * second.z);
#endif
  }

  static Vec3 cross(const Vec3& first, const Vec3& second) {
    return {(first.y * second.z) - (first.z * second.y),
            (first.z * second.x) - (first.x * second.z),
            (first.x * second.y) - (first.y * second.x)};
  }

#if TINYGPU_ENABLE_ESP32S3_OPTIMIZATIONS
  static float dotEspDsp(const Vec3& first, const Vec3& second) {
    float result = 0.0f;
    const esp_err_t status = dsps_dotprod_f32(&first.x, &second.x, &result, 3);
    if (status == ESP_OK) {
      return result;
    }
    return (first.x * second.x) + (first.y * second.y) + (first.z * second.z);
  }
#endif

  static float length(const Vec3& vector) {
    return std::sqrt(dot(vector, vector));
  }

  static Vec3 normalize(const Vec3& vector) {
    const float vectorLength = length(vector);
    if (vectorLength == 0.0f) {
      return {0.0f, 0.0f, 0.0f};
    }
    return vector / vectorLength;
  }

  static Vec3 transformAffine(const Mat4& matrix, const Vec3& point) {
    const Vec4 transformed = matrix * Vec4{point.x, point.y, point.z, 1.0f};
    return {transformed.x, transformed.y, transformed.z};
  }

  void updateViewMatrix() {
    viewMatrix_ = lookAt(camera_.position, camera_.target, camera_.up);
  }

  void updateProjectionMatrix() {
    if (projectionMode_ == ProjectionMode::Perspective) {
      projectionMatrix_ =
          createPerspectiveMatrix(perspectiveFovYDegrees_, currentAspectRatio(),
                                  camera_.nearPlane, camera_.farPlane);
      return;
    }

    projectionMatrix_ = createOrthographicMatrix(
        orthographicLeft_, orthographicRight_, orthographicBottom_,
        orthographicTop_, camera_.nearPlane, camera_.farPlane);
  }

  float currentAspectRatio() const {
    if (viewportWidth_ == 0 || viewportHeight_ == 0) {
      return 1.0f;
    }
    return static_cast<float>(viewportWidth_) /
           static_cast<float>(viewportHeight_);
  }

  bool resizeDepthBuffer() {
    size_t bytes = viewportWidth_ * viewportHeight_;
    depthBuffer_.resize(bytes);
    clearDepthBuffer();
    return depthBuffer_.size() == bytes;
  }

  static Mat4 createPerspectiveMatrix(float fovYDegrees, float aspect,
                                      float nearPlane, float farPlane) {
    Mat4 result;
    const float halfFovRadians =
        (fovYDegrees * 3.14159265358979323846f / 180.0f) * 0.5f;
    const float focalLength = 1.0f / std::tan(halfFovRadians);

    result.m[0][0] = focalLength / aspect;
    result.m[1][1] = focalLength;
    result.m[2][2] = (farPlane + nearPlane) / (nearPlane - farPlane);
    result.m[2][3] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    result.m[3][2] = -1.0f;
    return result;
  }

  static Mat4 createOrthographicMatrix(float left, float right, float bottom,
                                       float top, float nearPlane,
                                       float farPlane) {
    Mat4 result = Mat4::identity();
    result.m[0][0] = 2.0f / (right - left);
    result.m[1][1] = 2.0f / (top - bottom);
    result.m[2][2] = -2.0f / (farPlane - nearPlane);
    result.m[0][3] = -(right + left) / (right - left);
    result.m[1][3] = -(top + bottom) / (top - bottom);
    result.m[2][3] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    return result;
  }

  bool clipPointToCameraRange(const Vec3& point) const {
    return point.z <= -camera_.nearPlane && point.z >= -camera_.farPlane;
  }

  bool clipLineToCameraRange(Vec3& start, Vec3& end) const {
    if (!clipLineToPlane(start, end, -camera_.nearPlane, true)) {
      return false;
    }
    return clipLineToPlane(start, end, -camera_.farPlane, false);
  }

  static bool clipLineToPlane(Vec3& start, Vec3& end, float planeZ,
                              bool keepLessEqual) {
    const bool startInside =
        keepLessEqual ? start.z <= planeZ : start.z >= planeZ;
    const bool endInside = keepLessEqual ? end.z <= planeZ : end.z >= planeZ;

    if (!startInside && !endInside) {
      return false;
    }
    if (startInside && endInside) {
      return true;
    }

    const float deltaZ = end.z - start.z;
    if (deltaZ == 0.0f) {
      return false;
    }

    const float t = (planeZ - start.z) / deltaZ;
    const Vec3 intersection = start + ((end - start) * t);
    if (!startInside) {
      start = intersection;
    } else {
      end = intersection;
    }
    return true;
  }

  bool projectViewPoint(const Vec3& viewPoint,
                        ProjectedPoint& projected) const {
    const Vec4 clip =
        projectionMatrix_ * Vec4{viewPoint.x, viewPoint.y, viewPoint.z, 1.0f};
    if (clip.w == 0.0f) {
      return false;
    }

    const float inverseW = 1.0f / clip.w;
    const float ndcX = clip.x * inverseW;
    const float ndcY = clip.y * inverseW;
    const float ndcZ = clip.z * inverseW;
    const float screenX =
        ((ndcX * 0.5f) + 0.5f) *
        static_cast<float>(viewportWidth_ > 0 ? viewportWidth_ - 1 : 0);
    const float screenY =
        (1.0f - ((ndcY * 0.5f) + 0.5f)) *
        static_cast<float>(viewportHeight_ > 0 ? viewportHeight_ - 1 : 0);

    projected.x = static_cast<int16_t>(std::lround(screenX));
    projected.y = static_cast<int16_t>(std::lround(screenY));
    projected.depth = (ndcZ + 1.0f) * 0.5f;
    return true;
  }

  void drawDepthLine(ISurface<RGB_T>& target, const ProjectedPoint& start,
                     const ProjectedPoint& end, RGB_T color) {
    const int deltaX = static_cast<int>(end.x) - static_cast<int>(start.x);
    const int deltaY = static_cast<int>(end.y) - static_cast<int>(start.y);
    const int steps = std::max(std::abs(deltaX), std::abs(deltaY));

    if (steps == 0) {
      plotDepthPixel(target, start.x, start.y, start.depth, color);
      return;
    }

    for (int step = 0; step <= steps; ++step) {
      const float factor = static_cast<float>(step) / static_cast<float>(steps);
      const int x = static_cast<int>(std::lround(
          static_cast<float>(start.x) + (static_cast<float>(deltaX) * factor)));
      const int y = static_cast<int>(std::lround(
          static_cast<float>(start.y) + (static_cast<float>(deltaY) * factor)));
      const float depth = start.depth + ((end.depth - start.depth) * factor);
      plotDepthPixel(target, x, y, depth, color);
    }
  }

  void plotDepthPixel(ISurface<RGB_T>& target, int x, int y, float depth,
                      RGB_T color) {
    if (x < 0 || y < 0 || static_cast<size_t>(x) >= viewportWidth_ ||
        static_cast<size_t>(y) >= viewportHeight_) {
      return;
    }

    const size_t bufferIndex =
        (static_cast<size_t>(y) * viewportWidth_) + static_cast<size_t>(x);
    if (bufferIndex >= depthBuffer_.size()) {
      return;
    }
    if (depth >= depthBuffer_[bufferIndex]) {
      return;
    }

    depthBuffer_[bufferIndex] = depth;
    target.setPixel(static_cast<size_t>(x), static_cast<size_t>(y), color);
  }
};

}  // namespace tinygpu
