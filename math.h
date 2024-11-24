#pragma once
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>

template <typename T>
class Vec3 {
public:
    T x, y, z;
    Vec3() :x(0), y(0), z(0) {}
    Vec3(T x, T y, T z) :x(x), y(y), z(z) {}
    Vec3(T x, T y) :x(x), y(y), z(0) {}
    Vec3(const Vec3 & v) :x(v.x), y(v.y), z(v.z) {}
    Vec3 & operator=(const Vec3 & r) {
        if (&r != this) {
            x = r.x;
            y = r.y;
            z = r.z;
        }
        return *this;
    }
    T magnitude() const {
        return sqrt(mag2());
    }
    T mag2() const {
        return x*x + y*y + z*z;
    };
    void normalize() {
        T mag = magnitude();
        x /= mag;
        y /= mag;
        z /= mag;
    }
    Vec3 normalized() const{
        Vec3 n(*this);
        T mag = magnitude();
        n.x /= mag;
        n.y /= mag;
        n.z /= mag;
        return n;
    }
    Vec3 cross(Vec3 v) const {
        return Vec3(
            y*v.z - z*v.y,
            z*v.x - x*v.z,
            x*v.y - y*v.x);
    };
    Vec3 operator* (T s) const {
        return Vec3(x*s, y*s, z*s);
    };
    void operator-= (const Vec3 & v) {
        x -= v.x;
        y -= v.y;
        z -= v.z;
    }
    void operator+= (const Vec3 & v) {
        x += v.x;
        y += v.y;
        z += v.z;
    }
    void operator*= (T s) {
        x *= s;
        y *= s;
        z *= s;
    }
    void operator/= (T s) {
        x /= s;
        y /= s;
        z /= s;
    }
    T dot(const Vec3 & v) const {
        return x*v.x + y*v.y + z*v.z;
    };
    Vec3 operator-() const {
        return Vec3(-x, -y, -z);
    }
    Vec3 operator-(const Vec3<T> & r) const {
        return Vec3(x - r.x, y - r.y, z - r.z);
    }
    Vec3 operator+(const Vec3 & r) const {
        return Vec3(x + r.x, y + r.y, z + r.z);
    }
    Vec3 lerp(const Vec3 & from, const Vec3 & to, T amount) const {
        return from + (to - from) * amount;
    }
};

template <typename T>
Vec3<T> operator*(float f, Vec3<T> v) {
    return Vec3(v.x*f, v.y*f, v.z*f);
}

template <typename T>
Vec3<T> lerp(const Vec3<T> & from, const Vec3<T> & to, T amount) {
    return from + (to - from) * amount;
}

////////////////// MATRIX

// assume column major vectors
// elements 0, 1, 2, 3 represent first column
// to transform a vertex:
// vertex` = M * vertex
// to append transformations, left-multiply:
// projection * view * model * vertex
template <typename T>
class Mat16 {
    typedef Vec3<T> VectorType;
    static inline void element(int i, int j, const T * left, const T * right, T * out) {
        out[i * 4 + j] =
            left[j] * right[i * 4] +
            left[j + 4] * right[i * 4 + 1] +
            left[j + 8] * right[i * 4 + 2] +
            left[j + 12] * right[i * 4 + 3];
    }
public:
    T c[16];
    void identity() {
        memset(c, 0, sizeof(T)* 16);
        c[0] = 1;
        c[5] = 1;
        c[10] = 1;
        c[15] = 1;
    }
    Mat16(T m11, T m12, T m13,
        T m21, T m22, T m23,
        T m31, T m32, T m33) {
        memset(c, 0, sizeof(T)* 16);
        c[0] = m11; c[4] = m12; c[8] = m13;
        c[1] = m21; c[5] = m22; c[9] = m23;
        c[2] = m31; c[6] = m32; c[10] = m33;
        c[15] = 1.0f;
    };
    Mat16() {
        identity();
    }
    // a "lookAt" matrix given right, up, and forward vectors
    Mat16(VectorType x, VectorType y, VectorType z) {
        identity();
        c[0] = x.x; c[4] = x.y; c[8] = x.z;
        c[1] = y.x; c[5] = y.y; c[9] = y.z;
        c[2] = z.x; c[6] = z.y; c[10] = z.z;
    }
    T & operator()(int i, int j) {
        return c[i * 4 + j];
    }
    T operator()(int i, int j) const {
        return c[i * 4 + j];
    };
    void rightMultiply(const Mat16 & m) {
        T final[16];
        for (size_t i = 0; i<4; i++) {
            for (size_t j = 0; j<4; j++) {
                element(i, j, c, m.c, final);
            }
        }
        std::memcpy(c, final, sizeof(T)* 16);
    }
    void leftMultiply(const Mat16 & m) {
        T final[16];
        for (int i = 0; i<4; i++) {
            for (int j = 0; j<4; j++) {
                element(i, j, m.c, c, final);
            }
        }
        std::memcpy(c, final, sizeof(T)* 16);
    }
    void translate(const VectorType & v) {
        Mat16 m;
        m.c[12] = v.x;
        m.c[13] = v.y;
        m.c[14] = v.z;
        leftMultiply(m);
    }
    void scale(const VectorType & v) {
        Mat16 m;
        m(0, 0) = v.x;
        m(1, 1) = v.y;
        m(2, 2) = v.z;
        leftMultiply(m);
    }
    void scale(T s) {
        scale(Vec3<T>(s, s, s));
    }
    void orient(const VectorType & direction) {
        VectorType yaxis(0, 1, 0);
        VectorType zaxis(direction);
        zaxis.normalize();
        VectorType out = zaxis.cross(yaxis);
        VectorType xaxis = zaxis.cross(out);
        Mat16 m(xaxis, zaxis, out);
        leftMultiply(m);
    }
    void orient(const VectorType & yaxis, const VectorType & zaxis) {
        VectorType forwardN(zaxis);
        forwardN.normalize();
        VectorType upN(yaxis);
        upN.normalize();
        VectorType xaxis = upN.cross(forwardN);
        Mat16 m(xaxis, upN, forwardN);
        leftMultiply(m);
    }
    // rotate around an arbitrary axis.  theta is in radians, so for a 90 degree rotation, pass 0.5 * M_PI
    // rotation direction is right-hand rule when thumb is given axis
    void rotate(T x, T y, T z, T radians) {
        T d = std::sqrt(x*x + y*y + z*z);
        T cost1 = 1 - std::cos(radians);
        T sint = sin(radians);
        x /= d;
        y /= d;
        z /= d;
        Mat16 m;
        m.c[0] = 1 + cost1*(x*x - 1); m.c[4] = cost1*x*y - z*sint; m.c[8] = cost1*x*z + y*sint;
        m.c[1] = cost1*x*y + z*sint; m.c[5] = 1 + cost1*(y*y - 1); m.c[9] = cost1*y*z - x*sint;
        m.c[2] = cost1*x*z - y*sint; m.c[6] = cost1*y*z + x*sint; m.c[10] = 1 + cost1*(z*z - 1);
        leftMultiply(m);
    }
    // right multiply column vector: M * V
    void transform(VectorType & v) const {
        VectorType out(
            v.x*c[0] + v.y*c[4] + v.z*c[8] + c[12],
            v.x*c[1] + v.y*c[5] + v.z*c[9] + c[13],
            v.x*c[2] + v.y*c[6] + v.z*c[10] + c[14]);
        // w divide is required for perspective transforms such as inverse perspective picking
        T w = v.x*c[3] + v.y*c[7] + v.z*c[11] + c[15];
        v = out * (1.0f/w);
    }
    void transpose() {
        Mat16 transposed;
        Mat16 & m = *this;
        for (size_t i = 0; i<4; i++) {
            for (size_t j = 0; j<4; j++) {
                transposed(j, i) = m(i, j);
            }
        }
        std::memcpy(m.c, transposed.c, sizeof(T)* 16);
    }
    VectorType angles() const {
        static const T radians = 57.2957795f;
        T x = atan2(c[6], c[10]);
        T y = -asin(c[2]);
        T z = atan2(c[1], c[0]);
        return VectorType(x*radians, y*radians, z*radians);
    }
    void rotationOnlyMatrix(Mat16 & m) {
        std::memcpy(m.c, this->c, sizeof(T)* 16);
        m.c[3] = 0.0f;
        m.c[7] = 0.0f;
        m.c[11] = 0.0f;
        m.c[12] = 0.0f;
        m.c[13] = 0.0f;
        m.c[14] = 0.0f;
        m.c[15] = 1.0f;
    }
    void getTranslation(VectorType & v) const {
        v.x = c[12];
        v.y = c[13];
        v.z = c[14];
    }
    Mat16 inverted() const {
        Mat16 m;
        T det;

        m.c[0] = c[5] * c[10] * c[15] -
            c[5] * c[11] * c[14] -
            c[9] * c[6] * c[15] +
            c[9] * c[7] * c[14] +
            c[13] * c[6] * c[11] -
            c[13] * c[7] * c[10];

        m.c[4] = -c[4] * c[10] * c[15] +
            c[4] * c[11] * c[14] +
            c[8] * c[6] * c[15] -
            c[8] * c[7] * c[14] -
            c[12] * c[6] * c[11] +
            c[12] * c[7] * c[10];

        m.c[8] = c[4] * c[9] * c[15] -
            c[4] * c[11] * c[13] -
            c[8] * c[5] * c[15] +
            c[8] * c[7] * c[13] +
            c[12] * c[5] * c[11] -
            c[12] * c[7] * c[9];

        m.c[12] = -c[4] * c[9] * c[14] +
            c[4] * c[10] * c[13] +
            c[8] * c[5] * c[14] -
            c[8] * c[6] * c[13] -
            c[12] * c[5] * c[10] +
            c[12] * c[6] * c[9];

        m.c[1] = -c[1] * c[10] * c[15] +
            c[1] * c[11] * c[14] +
            c[9] * c[2] * c[15] -
            c[9] * c[3] * c[14] -
            c[13] * c[2] * c[11] +
            c[13] * c[3] * c[10];

        m.c[5] = c[0] * c[10] * c[15] -
            c[0] * c[11] * c[14] -
            c[8] * c[2] * c[15] +
            c[8] * c[3] * c[14] +
            c[12] * c[2] * c[11] -
            c[12] * c[3] * c[10];

        m.c[9] = -c[0] * c[9] * c[15] +
            c[0] * c[11] * c[13] +
            c[8] * c[1] * c[15] -
            c[8] * c[3] * c[13] -
            c[12] * c[1] * c[11] +
            c[12] * c[3] * c[9];

        m.c[13] = c[0] * c[9] * c[14] -
            c[0] * c[10] * c[13] -
            c[8] * c[1] * c[14] +
            c[8] * c[2] * c[13] +
            c[12] * c[1] * c[10] -
            c[12] * c[2] * c[9];

        m.c[2] = c[1] * c[6] * c[15] -
            c[1] * c[7] * c[14] -
            c[5] * c[2] * c[15] +
            c[5] * c[3] * c[14] +
            c[13] * c[2] * c[7] -
            c[13] * c[3] * c[6];

        m.c[6] = -c[0] * c[6] * c[15] +
            c[0] * c[7] * c[14] +
            c[4] * c[2] * c[15] -
            c[4] * c[3] * c[14] -
            c[12] * c[2] * c[7] +
            c[12] * c[3] * c[6];

        m.c[10] = c[0] * c[5] * c[15] -
            c[0] * c[7] * c[13] -
            c[4] * c[1] * c[15] +
            c[4] * c[3] * c[13] +
            c[12] * c[1] * c[7] -
            c[12] * c[3] * c[5];

        m.c[14] = -c[0] * c[5] * c[14] +
            c[0] * c[6] * c[13] +
            c[4] * c[1] * c[14] -
            c[4] * c[2] * c[13] -
            c[12] * c[1] * c[6] +
            c[12] * c[2] * c[5];

        m.c[3] = -c[1] * c[6] * c[11] +
            c[1] * c[7] * c[10] +
            c[5] * c[2] * c[11] -
            c[5] * c[3] * c[10] -
            c[9] * c[2] * c[7] +
            c[9] * c[3] * c[6];

        m.c[7] = c[0] * c[6] * c[11] -
            c[0] * c[7] * c[10] -
            c[4] * c[2] * c[11] +
            c[4] * c[3] * c[10] +
            c[8] * c[2] * c[7] -
            c[8] * c[3] * c[6];

        m.c[11] = -c[0] * c[5] * c[11] +
            c[0] * c[7] * c[9] +
            c[4] * c[1] * c[11] -
            c[4] * c[3] * c[9] -
            c[8] * c[1] * c[7] +
            c[8] * c[3] * c[5];

        m.c[15] = c[0] * c[5] * c[10] -
            c[0] * c[6] * c[9] -
            c[4] * c[1] * c[10] +
            c[4] * c[2] * c[9] +
            c[8] * c[1] * c[6] -
            c[8] * c[2] * c[5];

        det = c[0] * m.c[0] + c[1] * m.c[4] + c[2] * m.c[8] + c[3] * m.c[12];

        if (det == 0) throw std::runtime_error("cannot invert matrix; determinant is 0.");

        det = 1.0f / det;

        for (int i = 0; i < 16; i++) {
            m.c[i] = m.c[i] * det;
        }

        return m;
    }
    Mat16 operator*(const Mat16 & r) const {
        Mat16 final(*this);
        final.rightMultiply(r);
        return final;
    }
    VectorType operator*(const VectorType & v) const {
        VectorType r(v);
        this->transform(r);
        return r;
    }
    operator T*() {
        return c;
    }
};

template <typename T>
Vec3<T> operator*(const Vec3<T> & l, const Mat16<T> & r) {
    Vec3<T> out(
        l.x*r.c[0] + l.y*r.c[1] + l.z*r.c[2] + r.c[3],
        l.x*r.c[4] + l.y*r.c[5] + l.z*r.c[6] + r.c[7],
        l.x*r.c[8] + l.y*r.c[9] + l.z*r.c[10] + r.c[11]);
    l = out;
}

template <typename T>
class Quaternion {
public:
    T w, x, y, z;
    Quaternion() :x(0), y(0), z(0), w(1.0f) {}
    Quaternion(T w, T x, T y, T z) :x(x), y(y), z(z), w(w) {};
    Quaternion(Vec3<T> axis, T radians) {
        T sinHalfRadians = sin(radians / 2);
        this->x = x * sinHalfRadians;
        this->y = y * sinHalfRadians;
        this->z = z * sinHalfRadians;
        this->w = cos(radians / 2);
    }
    Quaternion(const Mat16<T> & m) { 
        T t = 1.0f + m.c[0] + m.c[5] + m.c[10];
        T s = 0.5f / sqrt(t);
        w = 0.25f / s;
        x = (m.c[9] - m.c[6])*s;
        y = (m.c[2] - m.c[8])*s;
        z = (m.c[4] - m.c[1])*s;
    }
    Quaternion(const Quaternion & rhs) :x(rhs.x), y(rhs.y), z(rhs.z), w(rhs.w) {};
    Quaternion & operator=(const Quaternion & rhs) {
        if (this != &rhs) {
            x = rhs.x;
            y = rhs.y;
            z = rhs.z;
            w = rhs.w;
        }
        return *this;
    }
    Mat16<T> matrix() const {
        return Mat16<T>(
            1.0f - 2 * (y*y + z*z), 2 * (x*y - w*z), 2 * (x*z + w*y),
            2 * (x*y + w*z), 1.0f - 2 * (x*x + z*z), 2 * (y*z - w*x),
            2 * (x*z - w*y), 2 * (y*z + w*x), 1.0f - 2 * (x*x + y*y));
    }
    Mat16<T> matrixAbout(const Vec3<T> & p) const {
        Mat16<T> r = matrix();
        r.c[3] = p.x - p.x * r(0, 0) - p.y * r(0, 1) - p.z * r(0, 2);
        r.c[7] = p.y - p.x * r(1, 0) - p.y * r(1, 1) - p.z * r(1, 2);
        r.c[11] = p.z - p.x * r(2, 0) - p.y * r(2, 1) - p.z * r(2, 2);
        return r;
    }
    Quaternion operator*(const Quaternion & q) const {
        return Quaternion(
            w*q.w - x*q.x - y*q.y - z*q.z,
            w*q.x + x*q.w + z*q.y - y*q.z,
            w*q.y + y*q.w + x*q.z - z*q.x,
            w*q.z + z*q.w + y*q.x - x*q.y);
    }
    Quaternion operator*=(const Quaternion & q) {
        *this = *this * q;
        return *this;
    }
    void normalize() {
        T n = sqrt(x*x + y*y + z*z + w*w);
        x /= n;
        y /= n;
        z /= n;
        w /= n;
    }
    Quaternion slerp(const Quaternion & q1, T t) const {
        if (t <= 0.0) { return *this; }
        else if (t >= 1.0) { return q1; }

        T cosOmega = w*q1.w + x*q1.x + y*q1.y + z*q1.z;
        Quaternion q1h = q1;
        if (cosOmega < 0.0f) {
            q1h.w = -q1h.w;
            q1h.x = -q1h.x;
            q1h.y = -q1h.y;
            q1h.z = -q1h.z;
            cosOmega = -cosOmega;
        }
        T k0, k1;
        if (cosOmega > 0.9999f) {
            k0 = 1.0f - 1;
            k1 = t;
        }
        else {
            T sinOmega = sqrt(1.0f - cosOmega*cosOmega);
            T omega = atan2(sinOmega, cosOmega);
            T invSinOmega = 1.0f / sinOmega;

            k0 = sin((1.0f - t) * omega) * invSinOmega;
            k1 = sin(t * omega) * invSinOmega;
        }

        return Quaternion(
            k0*w + k1*q1h.w,
            k0*x + k1*q1h.x,
            k0*y + k1*q1h.y,
            k0*z + k1*q1h.z);           
    }
};

template <typename T>
void makePerspectiveProjectionMatrix(Mat16<T> & mat, T yFieldOfViewRadians, float aspectWidth, float aspectHeight, T zNear, T zFar) {
    float screenDistance = 1.0f / tan(yFieldOfViewRadians * 0.5f);
    float aspectRatio = aspectWidth / aspectHeight;
    mat.identity();
    mat.c[0] = screenDistance / aspectRatio; // scale X to matcha Y FOV
    mat.c[5] = screenDistance; // usually 1.0 for pi/2 (90 degree) Y FOV
    mat.c[10] = zFar / (zNear - zFar); // a negative value a bit larger than 1, mapping our -Z view space visible vertices to 0,1 range
    mat.c[11] = -1.0f; // perspective divide
    mat.c[14] = (zFar * zNear) / (zNear - zFar); // depth mapping
    mat.c[15] = 0.0f;
}

typedef Vec3<float> vec3f;
typedef Mat16<float> mat16f;

static float _lerp(float from, float to, float amount) {
    return from * (1 - amount) + to * amount;
}

// Rotor which stores a rotation.
// We keep xyz components before scalar so that we can easily pack into a GPU vec4 where w is the scalar.
// we store b^a because a rotation is done by the geometric product bavab (aka RvR~)
struct Rotor {
    vec3f bivector; // bivector sin(theta/2)*b^a
    float scalar; // scalar cos(theta/2)
    Rotor() : bivector(), scalar(1) {}
    Rotor(vec3f bivector, float scalar):bivector(bivector),scalar(scalar) {}
    Rotor(vec3f a, vec3f b) {
        float dot = a.dot(b);
        if (dot < -0.999999) { // close to a flip, choose a plane ourselves
            if (fabs(a.x) > fabs(a.z)) {
                bivector = vec3f(a.x, a.y, 0.0f);
            } else {
                bivector = vec3f(0.0f, a.y, a.z);
            }
            bivector.normalize();
            scalar = 0;
            return;
        }
        scalar = sqrt((1+dot)/2.0f); // cos(theta/2)
        float sinTheta2 = sqrt((1-dot)/2.0f); // sin(theta/2)
        
        bivector = vec3f(b.x*a.y - b.y*a.x, b.y*a.z - b.z*a.y, b.z*a.x - b.x*a.z); // b^a
        bivector = bivector.normalized() * sinTheta2; // B = sin(theta/2) * b^a / ||b^a||
    }
    Rotor operator*(const Rotor& other) const {
        float new_scalar = scalar * other.scalar - bivector.dot(other.bivector);
        vec3f new_bivector(
            scalar*other.bivector.x + bivector.x*other.scalar - bivector.y*other.bivector.z + bivector.z*other.bivector.y,
            scalar*other.bivector.y + bivector.x*other.bivector.z + bivector.y*other.scalar - bivector.z*other.bivector.x,
            scalar*other.bivector.z - bivector.x*other.bivector.y + bivector.y*other.bivector.x + bivector.z*other.scalar);
        return { new_bivector, new_scalar };
    }
    Rotor reversed() const {
        return Rotor(-bivector, scalar);
    }
    Rotor operator-() const {
        return reversed();
    }
    vec3f rotate(vec3f v) const {
        const float S_x = scalar*v.x + bivector.x*v.y - bivector.z*v.z;
        const float S_y = scalar*v.y - bivector.x*v.x + bivector.y*v.z;
        const float S_z = scalar*v.z - bivector.y*v.y + bivector.z*v.x;
        const float S_xyz = bivector.x*v.z + bivector.y*v.x + bivector.z*v.y;

        vec3f result(
            S_x*scalar +   S_y*bivector.x + S_xyz*bivector.y -   S_z*bivector.z,
            S_y*scalar -   S_x*bivector.x +   S_z*bivector.y + S_xyz*bivector.z,
            S_z*scalar + S_xyz*bivector.x -   S_y*bivector.y +   S_x*bivector.z);
        return result;
    }
    Rotor nlerp(Rotor to, float amount) const {
        const float dot = scalar*to.scalar + bivector.x*to.bivector.x + bivector.y*to.bivector.y + bivector.z*to.bivector.z;
        if(dot < 0.0f) {
            to.scalar = -to.scalar;
            to.bivector = -to.bivector;
        }
        Rotor r(
            vec3f(
                _lerp(bivector.x, to.bivector.x, amount),
                _lerp(bivector.y, to.bivector.y, amount),
                _lerp(bivector.z, to.bivector.z, amount)),
            _lerp(scalar, to.scalar, amount));

        const float magnitude = sqrtf(r.scalar*r.scalar + r.bivector.dot(r.bivector));
        r.scalar /= magnitude;
        r.bivector /= magnitude;
        return r;
    }

    Rotor slerp(Rotor to, float amount) {
        float dot = scalar*to.scalar + bivector.x*to.bivector.x + bivector.y*to.bivector.y + bivector.z*to.bivector.z;
        if(dot < 0.0f) {
            to.scalar = -to.scalar;
            to.bivector = -to.bivector;
            dot = -dot;
        } else if(dot > 0.99995f) {
            return nlerp(to, amount);
        }

        const float cos_theta = dot;
        const float theta = acosf(cos_theta);
        const float from_factor = sinf((1.0f - amount)*theta)/sinf(theta);
        const float to_factor = sinf(amount*theta)/sinf(theta);

        return Rotor(
            bivector * from_factor + to.bivector * to_factor,
            from_factor*scalar + to_factor*to.scalar);
    }
    mat16f toMatrix() {
        return mat16f(
            rotate(vec3f(1,0,0)),
            rotate(vec3f(0,1,0)),
            rotate(vec3f(0,0,1)));
    }
    mat16f matrix() const {
        float x = -bivector.x;
        float y = -bivector.y;
        float z = -bivector.z;
        float w = scalar;
        return mat16f(
            1.0f - 2 * (y*y + z*z), 2 * (x*y - w*z), 2 * (x*z + w*y),
            2 * (x*y + w*z), 1.0f - 2 * (x*x + z*z), 2 * (y*z - w*x),
            2 * (x*z - w*y), 2 * (y*z + w*x), 1.0f - 2 * (x*x + y*y));
    }
};