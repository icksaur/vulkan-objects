# Spec: vkobjects RT acceleration structures — `Blas` / `Tlas` / `InstanceTable<T>`

**Type:** library addition to vkobjects (the shared Vulkan-wrapper dependency).
**Driver:** production DDGI in `hull`. The `spike-rt-blas` / `spike-ddgi` branches hand-rolled raw AS
code (`hull/src/rtspike.cpp`, `ddgispike.cpp`); this promotes the **invariant, dangerous-to-get-wrong**
parts into the library and leaves scene policy at the call site.
**Status:** ready for review.

## Goal
RAII the three implicit contracts every RT consumer currently re-implements by hand, matching the
`Buffer`/`Image`/`GpuTimer` style:

| # | Contract | Why it must be hidden |
|---|---|---|
| 1 | AS lifecycle: getBuildSizes → alloc backing+scratch → createAS → getASAddr → destroyAS | No RAII today; a manual `Blas` struct + destroy loop. |
| 2 | Scratch-offset alignment (`minAccelerationStructureScratchOffsetAlignment`) | Unmet ⇒ build **silently hangs the GPU; validation misses it** (rtspike.cpp). Highest-value to bury. |
| 3 | RT entrypoint loading (`vkGetAccelerationStructure*KHR`, not loader-exported) | Spike re-loads 5 PFNs (`RtApi`). Load once, centrally. |
| 4 | AS handle is **not memory** — needs fence-gated deferred destroy | A handle is a view onto a backing `Buffer`; freeing one in flight faults. Route through `DestroyGeneration`. |

**Litmus (the non-goal boundary):** abstract what is *identical for every RT caller and dangerous*;
leave *scene intent* legible at the call site. Geometry descriptors, instance records, build flags,
the cull/importance set, and **per-instance material data** encode scene policy — they stay out.

## What already exists on master (do NOT rebuild)
- `VulkanContextOptions().rayTracing()` — enables `acceleration_structure` + `ray_query` +
  `deferred_host_operations` + `bufferDeviceAddress` and the feature structs.
- `BufferBuilder::usageFlags(VkBufferUsageFlags)` — OR in AS-storage / build-input / device-address.
- `Buffer::deviceAddress()`; `VulkanContext::deviceHandle()/physicalDeviceHandle()`,
  `bindlessSetLayout()/Set()`.

## Design

### 0. Foundation (land first — call sites are ugly without it)

| Change | Detail |
|---|---|
| `Stage` enum | `+ AccelStructureBuild = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR` |
| `Access` enum | `+ AccelStructureRead / AccelStructureWrite` (`..._ACCELERATION_STRUCTURE_{READ,WRITE}_BIT_KHR`) |
| `DestroyGeneration` | `+ std::vector<VkAccelerationStructureKHR> accelStructures;` **and** `+ std::vector<uint32_t> tlasRIDs;` (mirrors `storageBufferRIDs`). **This is what makes caller-managed AS lifetime safe** — `~Blas`/`~Tlas` push the handle (and TLAS its bindless RID) here, reusing the frame-fence-gated machinery `Buffer` already trusts. |
| **Destroy ordering (MF1)** | `DestroyGeneration::destroy()` must destroy **AS handles before buffer allocations** (a handle is a view onto its backing buffer). `~Blas`/`~Tlas` likewise enqueue the AS handle before their member `Buffer`s drop. Under `enableImmediateDestroy`, destroy the AS immediately *before* its backing buffer in the same path. |
| `VulkanContext` | Load the 5 AS PFNs once in the ctor when `rayTracing()` is set; internal-only. The spike's free-floating `RtApi` disappears. Cache `minAccelerationStructureScratchOffsetAlignment` (used by **both** BLAS and TLAS scratch). |
| Barriers (MF3) | Two named helpers, not one (the access scopes differ): `Commands::blasToTlasBarrier(std::span<const VkBuffer> blasBackings)` (AS-build-write → AS-build-read, batched) and `Commands::tlasToShaderReadBarrier(VkBuffer tlasBacking)` (AS-build-write → shader-read). |

### 1. `Blas` / `BlasBuilder` (bottom-level, RAII, mirrors `Buffer`/`BufferBuilder`)

```cpp
// One triangle geometry: addresses into caller-owned Buffers + counts. Addresses are STABLE;
// their contents change each frame (re-skinned). Stored at construction, re-read by every build.
struct BlasTriangles {
    VkDeviceAddress vertices;  uint32_t vertexCount;  uint32_t vertexStride = 12;
    VkFormat        vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    VkDeviceAddress indices = 0;   // 0 => non-indexed triangle list
    uint32_t        triangleCount; VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    bool            opaque = true;
};

struct BlasBuilder {
    std::vector<BlasTriangles> geoms;
    bool allowUpdate = false, fastTrace = true;
    BlasBuilder& triangles(Buffer& vtx, uint32_t vtxCount, Buffer& idx, uint32_t triCount); // common
    BlasBuilder& triangles(const BlasTriangles&);   // explicit (multi-geometry / custom stride)
    BlasBuilder& refittable();                       // ALLOW_UPDATE (must match every refit, VUID-03759)
    BlasBuilder& fastBuild();                         // PREFER_FAST_BUILD (default PREFER_FAST_TRACE)
};

class Blas {                                  // move-only; operator VkAccelerationStructureKHR()
    std::unique_ptr<Buffer> backing_, scratch_, updateScratch_;
    std::vector<BlasTriangles> geometry_;     // stored at ctor; ONLY raw device addresses + counts
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkDeviceAddress address_ = 0, scratchAddress_ = 0;
    bool updatable_ = false, built_ = false;  // built_ tracks whether a MODE_BUILD has run (MF8)
public:
    Blas(BlasBuilder&);   // getBuildSizes → alloc backing+scratch → create EMPTY handle. NO BVH yet.
    Blas(Blas&&) noexcept; Blas& operator=(Blas&&) noexcept;  // MF7: transfer handle+buffers+addrs,
    ~Blas();              // null moved-from handle so it is not double-deferred. ~ enqueues handle.
    VkDeviceAddress address() const;          // for TLAS instance references
    Buffer& backing();                        // for the post-build barrier
    bool updatable() const;
};
```

**Move safety (MF7):** `geometry_` stores **only raw `VkDeviceAddress` + counts** (never pointers into
the moved-from object), so a move can't dangle cached geometry. The move ctor/assign transfers
`handle_`/buffers/`address_`/`built_` and **nulls the moved-from `handle_`** (and, for `Tlas`, its
bindless RID) so the moved-from destructor enqueues nothing.

### 2. `Tlas` / `TlasInstances` (top-level)

```cpp
struct TlasInstances {                        // populate per frame, hand to Tlas::build
    std::vector<VkAccelerationStructureInstanceKHR> raw;
    TlasInstances& add(const Blas&, const float (&xform3x4)[12], uint32_t customIndex, uint8_t mask=0xFF);
    void clear();
};

class Tlas {                                  // move-only
    std::unique_ptr<Buffer> backing_, scratch_, instanceBuf_;  // instanceBuf_ HOST-VISIBLE AS-input
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    uint32_t rid_ = kNullRid;
public:
    explicit Tlas(uint32_t maxInstances);     // TLAS sizing = max instance (primitive) count
    Tlas(Tlas&&) noexcept; Tlas& operator=(Tlas&&) noexcept; ~Tlas();   // MF7 (nulls handle_ + rid_)
    VkAccelerationStructureKHR handle() const; Buffer& backing();
    uint32_t rid() const;                     // bindless AS slot — see §5
};
```

**MF4 — instance upload.** `instanceBuf_` is **host-visible AS-build-input** (not device-local), so
`buildTlas` writes instances via the existing `Buffer::upload` mapping with no staging copy/transfer
barrier. (If a device-local instance buffer is ever wanted for bandwidth, `buildTlas` must then do an
internal staging upload + `transfer-write → AS-build-read` barrier — explicitly out of scope here.)

**MF5 — TLAS scratch** overallocates + `alignUp`s to `minAccelerationStructureScratchOffsetAlignment`
exactly like BLAS scratch. Same silent-hang class; same mitigation.

`Blas`/`Tlas` are **split, not a unified `AccelStructure`**: they take different build inputs
(triangles vs an instance buffer), TLAS needs max-instance sizing + an owned instance buffer, and only
TLAS is shader-bound. A shared base buries more than it saves.

### 3. Recording builds — `Commands` methods

```cpp
void Commands::buildBlas(Blas&, bool refit);   // refit asserts updatable_ && built_; build sets built_
void Commands::buildTlas(Tlas&, const TlasInstances&);    // uploads instances → MODE_BUILD (rebuild)
```

**MF8 — refit guard.** `buildBlas(refit=true)` asserts `updatable_ && built_`; the first
`buildBlas(refit=false)` sets `built_`. This makes the invalid `MODE_UPDATE`-before-`MODE_BUILD`
sequence (easy to hit under construct-once/build-every-frame) a hard assert, not a driver fault.

**TLAS is rebuild-only by decision (SC3):** instance count and the cull/importance set change often and
TLAS rebuild is ~0.1 ms (measured), so `buildTlas` always does `MODE_BUILD`. No `refit` parameter —
revisit only if profiling shows TLAS rebuild matters.

`buildBlas` takes **no geometry argument** — the AS stored it at construction (stable addresses, only
contents rotate). Re-passing would let build-geometry diverge from sized-geometry (a Vulkan error
class); storing once removes the footgun.

**Lifecycle — construct once, build every frame.** `getBuildSizes` uses only counts/format (reads no
vertex data), so construction binds no content. Per frame for a dynamic hull: skin → overwrite the
posed buffer → `buildBlas(refit)` re-reads the same address and rewrites `backing`. The object is
never re-created. Static (skeleton-less) hull: `buildBlas(refit=false)` exactly once.

### 4. `InstanceTable<T>` — the per-instance side table (sibling, **not** owned by `Tlas`)

You almost always need a `customIndex → {RIDs, albedo, ...}` map, but it must **not** live inside
`Tlas`. Four reasons:

| Reason | Consequence if nested in `Tlas` |
|---|---|
| **Lifetime mismatch** — TLAS rebuilds every frame; payload changes only on spawn/despawn | Re-uploads static data every frame, or bolts dirty-tracking onto TLAS |
| **Shared across TLASes** — one camera TLAS + one probe TLAS, same instances | Ambiguous ownership / duplication |
| **`customIndex` is an app namespace**, not a dense instance index | `Tlas` can't size the table (knows instance count, not index space) |
| **Payload type is app-defined** (`T`) | Either hard-codes hull's schema or degrades to `void*` |

```cpp
template<class Payload>                        // Payload = hull's POD: { Rid vtx, idx; vec4 albedo; ... }
class InstanceTable {
    static_assert(std::is_trivially_copyable_v<Payload>);   // SC2: it is byte-copied to the GPU
    std::unique_ptr<Buffer> buf_;              // bindless storage, indexed by customIndex
public:
    explicit InstanceTable(uint32_t maxEntries);   // SC2: assert maxEntries <= (1u<<24) — customIndex is 24-bit
    uint32_t add(const Payload&);              // returns the customIndex to give TlasInstances::add
    void set(uint32_t customIndex, const Payload&);
    void upload(Commands&);                    // app controls cadence (NOT necessarily per frame)
    uint32_t rid() const;                      // shaders read by customIndex
};
```

The `customIndex` is the seam — the same value the hardware returns at a hit. `Tlas` owns the
RT-internal `VkAccelerationStructureInstanceKHR` array; `InstanceTable<T>` owns the app payload; they
share nothing but the index.

### 5. Binding the TLAS into shaders — **DECIDED: option A (bindless TLAS slot)**

A ray query needs the TLAS as a descriptor (`VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`), but the
binding model is bindless RIDs in set 0. **Chosen: (A)** — keep the "everything is an RID" model so a
TLAS binds exactly like a buffer; no special-case descriptor set in RT passes.

- **(A — CHOSEN) Bindless TLAS slot.** Add a small `MAX_TLAS` (e.g. 4) array binding to the set-0
  layout + `BindlessTable::registerTlas()` → RID and `releaseTlas()`. `tlas.rid()` then works like
  `buffer.rid()`; shaders declare `layout(set=0, binding=N) uniform accelerationStructureEXT
  tlasTable[];` indexed by a pushed RID. Cost (accepted): every RT-capable context's set-0 layout
  declares the AS binding — gate it on `rayTracing()` so non-RT contexts don't.
  - **MF2 — RID lifetime:** the TLAS bindless slot is released via `DestroyGeneration::tlasRIDs`
    (fence-gated), never eagerly in `~Tlas`, or a slot can be reused while an in-flight shader still
    indexes it.
  - **SC1 — validate at init:** check the acceleration-structure descriptor limit/feature and size
    `MAX_TLAS` from a conservative validated cap.
- **(B — rejected) Dedicated descriptor set** bound at a fixed set index for RT passes only. More
  explicit and avoids touching the global layout, but breaks the single-bindless-set model and makes
  every RT pass special-case its descriptor binding. Rejected for ergonomics.

### 6. N-buffering dynamic AS — `AccelStructureRing` (optional sibling)

A dynamic BLAS/TLAS is GPU-rebuilt each frame while a prior in-flight frame may still read it ⇒ needs
**N = swapchainImageCount** copies, indexed by `Frame::inFlight()`. Hull's `FrameRing<T>` is
host-written POD only (`static_assert(trivially_copyable<T>)`), so it **cannot** hold a GPU-owned RAII
`Blas`. Provide a sibling:

```cpp
template<class T>                              // T = Blas or Tlas
class AccelStructureRing {
public:
    void init(uint32_t n, const std::function<T(uint32_t slot)>& makeSlot);  // MF6: factory gets slot
    T& current();                              // by Frame::inFlight(); slot 0 when no Frame is live
    uint32_t size() const;
};
```

**MF6 — the factory takes the slot index.** Each dynamic ring slot's AS must read *that slot's* posed
(or instance) buffer; a zero-arg factory makes it trivially easy to bind every slot to the same
address (a frame-in-flight write-after-read hazard). `makeSlot(slot)` forces the call site to pass
`posedRing[slot]`'s address into the `BlasBuilder`.

Static BLAS = a bare `Blas` (1 copy); dynamic = `AccelStructureRing<Blas>` (N copies) — the
static/dynamic split becomes **visible in the type**. The posed-vertex and instance *buffers* feeding
a dynamic AS still ring via the existing `FrameRing` (host/GPU-written buffers, not AS objects).

### Per-frame call site (target ergonomics)

```cpp
size_t s = Frame::current()->inFlight();
skinPass.run(cmd, s);
cmd.bufferBarrier(posed[s], Stage::Compute, Access::ShaderWrite,
                  Stage::AccelStructureBuild, Access::AccelStructureRead);
cmd.buildBlas(blas.current(), /*refit=*/true);
cmd.blasToTlasBarrier({ &blas.current().backing(), 1 });   // batched if many BLAS
inst.clear();
for (auto& h : hulls) inst.add(blas.current(), h.xform, h.customIndex);
cmd.buildTlas(tlas.current(), inst);
cmd.tlasToShaderReadBarrier(tlas.current().backing());
ddgiUpdate.run(cmd, tlas.current().rid());
```

## What stays at the call site (application policy)
- `BlasTriangles` / instance-record population — scene geometry + per-frame cull/importance set.
- Build-vs-refit policy per hull (static → build once; skeletal → ring + refit).
- **BLAS *sets* with independent lifetime** (static / dynamic / evictable-GI-coverage pools) — app-owned
  collections, allocated/evicted independently, referenced together from one TLAS instance list.
  vkobjects supplies the unit (`Blas`) + the ring, not pools/eviction. Two distinct groupings, never
  conflated: geometries *within* one BLAS (the `BlasTriangles` span) vs BLAS *sets* across pools (the
  TLAS instance list).
- `InstanceTable<T>` payload schema + upload cadence; the skinning pass (`skin_to_buffer.comp`).

## Acceptance / oracle
- **Oracle (hand case):** a unit-axis single-triangle `Blas` + 1-instance `Tlas`; ray-query a ray with
  a known analytic hit; assert `instanceCustomIndex`, `primitiveIndex`, and `t` match the
  hand-computed values. Independent of any rendering path → a true oracle, not a weak invariant.
- Move/RAII leak check via VMA budget (as `Buffer` is tested): construct+destroy a `Blas`/`Tlas` in a
  loop, assert no growth after `flushDestroys`.
- `AccelStructureRing` init + `current()`-rotation-by-inFlight check.
- A refit path: build a `Blas` `refittable()`, `buildBlas(refit=false)` then `buildBlas(refit=true)`
  after moving the vertex contents; assert the re-queried hit `t` tracks the moved geometry.
- **Lifetime/sync (SC4, the failures the hit-oracle alone can't catch):** (a) deferred AS-before-backing
  destroy order under both normal and `immediateDestroy` modes (VMA-budget + validation clean); (b) a
  TLAS bindless RID is reused only after the gating fence; (c) two ring slots bind **distinct** posed
  addresses (assert the `BlasTriangles.vertices` differ per slot); (d) `buildBlas(refit=true)` before
  any build trips the `built_` assert.
- No hull dependency in any test.

## Plan
1. **Foundation (§0):** `Stage`/`Access` enum entries; `DestroyGeneration` AS list + `tlasRIDs` +
   `destroy()` ordering (AS before buffers); context PFN load behind `rayTracing()` + cache scratch
   alignment; `blasToTlasBarrier` / `tlasToShaderReadBarrier`.
2. **`Blas`/`BlasBuilder` (§1)** + `Commands::buildBlas`. Internal scratch with `alignUp` (bury the
   silent-hang). Unit: single-triangle oracle (write the oracle test first).
3. **`Tlas`/`TlasInstances` (§2,§3)** + `Commands::buildTlas`. Unit: 1-instance ray-query hit.
4. **Bindless-TLAS slot (§5, decided: A)** → `BindlessTable::registerTlas/releaseTlas`, `MAX_TLAS`
   binding on the `rayTracing()` set-0 layout, `Tlas::rid()`; ray-query oracle reads via RID.
5. **`InstanceTable<T>` (§4).** Unit: add/set/upload, shader reads payload by `customIndex`.
6. **`AccelStructureRing` (§6).** Unit: init/rotation.
7. **Migrate** `rtspike.cpp` + the DDGI production path off `RtApi`/`Blas`/`makeScratch`/manual destroy.
   Drive the API through the real DDGI integration (the second caller that validates the shape).

## Risks / notes
- vkobjects is the shared dependency → review against its `code-quality.md`; keep the surface minimal
  (resist geometry/flag wrappers "for completeness").
- Scratch is owned persistently per AS (build + a separate refit scratch if `updatable`). Acceptable
  for per-instance dynamic AS; a **shared `ScratchPool` is a later optimization**, not in this addition.
- Driver quirk (spike): a timestamp pool reused across submits read zero on this driver — orthogonal,
  but prefer the existing `GpuTimer` if AS timing lands nearby.
- **Compaction (SC5)** omitted for now (acceptable), but keep the API able to swap in a compacted
  `backing_` later without changing call sites — i.e. nothing outside `Blas` caches its backing
  buffer/handle; consumers reach the AS only via `address()` / `rid()`.
