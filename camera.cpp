#include "camera.h"

Camera::Camera() : zaxis(0,0,1), xaxis(1,0,0), yaxis(0,1,0), distance(0), dirty(false), projectionType(ortho) { }

Camera& Camera::perspective(float yFieldOfViewRadians, int xrez, int yrez, float zNear, float zFar) {
    projectionType = ProjectionType::persp;
    makePerspectiveProjectionMatrix(projection, yFieldOfViewRadians, (float)xrez, (float)yrez, zNear, zFar);
    projection[5] *= -1.0f; // flip Y for Vulkan
    dirty = true;
    return *this;
}
Camera& Camera::orthographic(float halfWidth, float halfHeight, float zNear, float zFar) {
    projectionType = ProjectionType::ortho;
    memset(projection.c, 0, sizeof(projection.c));
    projection.c[0] = 1.0f / halfWidth;
    projection.c[5] = 1.0f / halfHeight;
    projection.c[10] = -2.0f / (zFar - zNear);
    projection.c[14] = -((zFar + zNear) / (zFar - zNear));
    projection.c[15] = 1.0f;
    dirty = true;
    return *this;
}

Camera& Camera::rotate(float x, float y, float z, float angleRadians) {
    Mat16<float> rotation;
    rotation.rotate(x, y, z, angleRadians);
    rotation.transform(xaxis);
    rotation.transform(yaxis);
    rotation.transform(zaxis);
    xaxis.normalize();
    yaxis.normalize();
    zaxis.normalize();
    dirty = true;
    return *this;
}

// choose what direction is 'up' depending on your own conventions
const vec3f zenith(0, 1, 0);

Camera& Camera::rotate(float yawRadians, float pitchRadians) {
    Mat16<float> t;
    t.rotate(0, 0, 1, yawRadians); // spin
    t.transform(xaxis);
    t.rotate(xaxis.x, xaxis.y, xaxis.z, pitchRadians);
    t.transform(zaxis);
    rebuildViewVectors();
    dirty = true;
    return *this;
}

Camera& Camera::moveTo(float x, float y, float z) {
    position = vec3f(x, y, z);
    dirty = true;
    return *this;
}

Camera& Camera::look(vec3f dir, vec3f up) {
    if (dir.mag2() > 0.000001f && up.mag2() > 0.000001f) { // valid vectors
        up.normalize();
        zaxis = (-dir).normalized();
        xaxis = up.cross(zaxis);
        yaxis = zaxis.cross(xaxis);

        dirty = true;
    }
    return *this;
}

Camera& Camera::lookAt(float x, float y, float z) {
    lookAt(vec3f(x, y, z), zenith);
    return *this;
}

Camera& Camera::lookAt(vec3f to, vec3f up) {
    vec3f newZaxis = position - to;
    if (newZaxis.mag2() > 0.000001f && up.mag2() > 0.000001f) { // valid vectors
        up.normalize();
        zaxis = newZaxis.normalized();
        xaxis = up.cross(zaxis).normalized();
        yaxis = zaxis.cross(xaxis).normalized();

        dirty = true;
    }
    return *this;
}

void Camera::rebuildViewVectors() {
    zaxis.normalize();
    if (abs(zaxis.z) < 0.999999f) { // far enough from zenith to use
        xaxis = zenith.cross(zaxis);
        yaxis = zaxis.cross(xaxis);
    } else { // denegerate forward to zenith, use right
        yaxis = zaxis.cross(xaxis);
        xaxis = yaxis.cross(zaxis);
    }
    yaxis.normalize();
    xaxis.normalize();
}

void Camera::makeViewProjection() {
    view = Mat16<float>();
    view.translate(-position);

    Mat16<float> rotate(xaxis, yaxis, zaxis);
    view.leftMultiply(rotate);

    // back off by translating in z distance
    // not sure if this is affine (thus invertible) :(
    view.c[14] -= distance;

    // left-multiply successive transformations
    viewProjection = projection * view;
}

Camera& Camera::setDistance(float distance) {
    this->distance = distance;
    dirty = true;
    return *this;
}

Mat16<float> Camera::getViewProjection() {
    if (dirty) {
        makeViewProjection();
        dirty = false;
    }
    return viewProjection;
}

void Camera::getViewProjection(Mat16<float> & view, Mat16<float> & projection) {
    if (dirty) {
        makeViewProjection();
        dirty = false;
    }
    view = this->view;
    projection = this->projection;
}

vec3f Camera::getLocation() const {
    return position + zaxis.normalized() * distance;
}

Vec3<float> Camera::getDirection() const {
    mat16f inverse = view.inverted();
    return inverse * vec3f(0,0,-1); // This is not the current z-axis, but the opengl convention axis.
}