#pragma once

#include "math.h"

class Camera {
    Vec3<float> position;
    Vec3<float> zaxis;
    Vec3<float> xaxis;
    Vec3<float> yaxis;
    float distance; // how far panned back camera is from position; useful for orbiting a position

    Mat16<float> view;
    Mat16<float> projection;

    bool dirty; // whether viewProjection needs recalculating
    Mat16<float> viewProjection;

    void rebuildViewVectors();
    void makeViewProjection();

    enum ProjectionType { persp, ortho } projectionType;

public:
    Camera();
    Camera& perspective(float angleViewPiRadians, int xrez, int yrez, float zNear, float zFar);\
    Camera& orthographic(float halfWidth, float halfHeight, float zNear, float zFar);

    Camera& rotate(float x, float y, float z, float angleRadians);
    Camera& rotate(float yawRadians, float pitchRadians);

    // move the camera in world space (screen space -1,1 in ortho)
    Camera& moveTo(float x, float y, float z);

    // point the camera at the given world space
    Camera& lookAt(float x, float y, float z);

    // point the camera at the given world space point, with the specified up vector
    Camera& lookAt(vec3f to, vec3f up);

    // point the camera in a given direction
    Camera& look(vec3f dir, vec3f updir);

    // view everything some set distance further away from camera location
    // this only removes from translation component Z when getting a viewMatrix
    // it doesn't move the camera
    Camera& setDistance(float distance);

    // Get a viewProjection matrix useful for: screenSpace = viewProjection * vertex
    Mat16<float> getViewProjection();

    // Get a view matrix and projection matrix which are not appended
    void getViewProjection(Mat16<float>& view, Mat16<float>& projection);

    // Get the camera location, taking account for distance
    Vec3<float> getLocation() const;

    // Get a direction vector for the camera (not orientation, no up)
    Vec3<float> getDirection() const;
};