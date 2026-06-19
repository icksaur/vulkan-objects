#pragma once

// Bindless resource IDs (spec: hull/doc/spec-bindless-rid.md, Layers 1+3).
//
// Deliberately dependency-light: only <cstdint>/<type_traits>, no Vulkan/SDL.
// This lets POD layout-mirror structs (e.g. a push-constant view) and pure-CPU
// unit-test code reference RID / Rid<Tag> without pulling in the full vkobjects
// header. vkobjects.h includes this; downstream code may include it directly.

#include <cstdint>
#include <type_traits>

// A bindless descriptor-array slot index. kNullRid is the shared "absent"
// sentinel; pipelines that overload the bit pattern for a non-absent meaning
// (e.g. "use the natural workgroup id") must name that meaning separately
// rather than reuse kNullRid, so the two cases stay distinguishable.
using RID = uint32_t;
constexpr RID kNullRid = 0xFFFFFFFFu;

// Tag-typed wrapper over a RID. Two Rid<Tag> with different tags do not convert
// to each other, so passing a bone-matrix slot where a surfel slot is expected
// is a compile error. The single-uint32_t layout is standard-layout AND
// trivially-copyable so a push-constant struct holding Rid<Tag> fields is
// byte-identical to one holding raw uints; pushConstants<T>() byte-copies it
// unchanged and the shader sees a plain uint. raw() is the ONLY sanctioned
// crossing of the C++/GLSL boundary; there is deliberately no implicit
// conversion to RID, so the wrong-slot check cannot be silently defeated.
template<typename Tag>
struct Rid {
    RID value = kNullRid;
    Rid() = default;
    explicit Rid(RID v) : value(v) {}
    bool valid() const { return value != kNullRid; }
    RID raw() const { return value; }
    static Rid null() { return {}; }
};
static_assert(sizeof(Rid<struct AnyRidTag>) == 4 &&
              std::is_standard_layout_v<Rid<struct AnyRidTag>> &&
              std::is_trivially_copyable_v<Rid<struct AnyRidTag>>,
              "Rid<Tag> must be a 4-byte standard-layout, trivially-copyable slot index");
