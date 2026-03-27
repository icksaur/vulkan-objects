# Code Review -- vkobjects

Reviewed: 2026-03-26. Scope: buffer lifecycle and safety patterns.

Context: the `mesh` library that depends on vkobjects has chronic GPU buffer
bugs -- buffers not initialized, not cleared, not re-initialized after
reallocation, stale data persisting across frames.  vkobjects provides solid
low-level Vulkan wrapping but could do more to prevent these bugs at the API
level.

---

## What vkobjects gets right

**Deferred destruction** via `DestroyGeneration` per swapchain frame prevents
use-after-free from GPU pipeline lag.  This is well-implemented and correctly
frame-indexed.

**BufferBuilder** fluent API is clean and hard to misuse for *creation*.
The flag combinations (storage, hostVisible, transferDestination, etc.) are
correct and the VMA integration handles memory type selection automatically.

**Bindless descriptor registration** is automatic on buffer creation.  The
`rid()` accessor is simple and correct.

**Move semantics** are enforced (no copy constructor), preventing accidental
double-destroy.

**Size validation on upload/download** catches size > buffer capacity.

These are all good.  The problems are in what happens *after* creation.

---

## Suggestion 1: Initialization guard (debug mode)

The single most common bug in the mesh codebase is using a buffer that was
allocated but never initialized.  vkobjects could catch this:

```cpp
class Buffer {
#ifndef NDEBUG
    enum class InitState { Uninitialized, Cleared, Uploaded };
    mutable InitState initState_ = InitState::Uninitialized;
#endif
public:
    void upload(const void* data, size_t size, size_t offset = 0) {
        // ... existing upload ...
#ifndef NDEBUG
        initState_ = InitState::Uploaded;
#endif
    }

    // Called after Commands::fillBuffer targeting this buffer
    void markCleared() {
#ifndef NDEBUG
        initState_ = InitState::Cleared;
#endif
    }

    uint32_t rid() const {
#ifndef NDEBUG
        if (initState_ == InitState::Uninitialized) {
            fprintf(stderr, "WARNING: buffer RID %u accessed before initialization\n", rid_);
        }
#endif
        return rid_;
    }
};
```

This would have caught 4 of the critical bugs in the mesh codebase on
first run.

The `markCleared()` call is awkward because `fillBuffer` is on Commands,
not on Buffer.  One option is to have `Commands::fillBuffer(Buffer&, ...)`
call `buf.markCleared()` internally.  This couples the two classes, but
since Commands already takes Buffer references, the coupling already exists.

---

## Suggestion 2: clearBuffer convenience method

The mesh code has many instances of:
```cpp
cmd.fillBuffer(*c.brickHashTableBuffer, 0xFFFFFFFF);
Barrier(cmd).buffer(*c.brickHashTableBuffer)
    .from(Stage::Transfer, Access::TransferWrite)
    .to(Stage::Compute, Access::ShaderRead | Access::ShaderWrite)
    .record();
```

This is 4 lines for a conceptually simple operation: "clear this buffer and
make it ready for compute."  A convenience method would reduce boilerplate
and prevent forgetting the barrier:

```cpp
// On Commands:
void clearForCompute(Buffer& buf, uint32_t value = 0) {
    fillBuffer(buf, value);
    Barrier(*this).buffer(buf)
        .from(Stage::Transfer, Access::TransferWrite)
        .to(Stage::Compute, Access::ShaderRead | Access::ShaderWrite)
        .record();
}
```

The mesh codebase has at least 8 fill+barrier pairs that could use this.
More importantly, it eliminates the class of bug where the fill is present
but the barrier is forgotten.

---

## Suggestion 3: BufferBuilder with initial clear

Allow the builder to specify initial contents:

```cpp
Buffer buf(BufferBuilder(size)
    .storage()
    .transferDestination()   // needed for fillBuffer
    .clearedTo(0));          // fill with 0 after creation
```

This would internally record the fill value and apply it in the Buffer
constructor (via a one-shot command).  The `transferDestination()` flag
would be added automatically if `clearedTo()` is used.

This catches the "allocated but never initialized" bug at the point of
construction rather than at the point of first use.

Downside: implicit GPU work in a constructor is surprising.  An alternative
is to return a `PendingBuffer` that must be submitted before use:

```cpp
auto [buf, initCmd] = BufferBuilder(size).storage().cleared(0).buildDeferred();
// initCmd must be submitted before buf is used
```

This is more explicit but also more cumbersome.  The simpler `clearedTo()`
approach is probably better for this codebase.

---

## Suggestion 4: Buffer size query by element count

The mesh codebase has repeated patterns like:
```cpp
Buffer buf(BufferBuilder(count * sizeof(uint32_t)).storage());
```

And then later uses `count` to bounds-check shader dispatches.  But `count`
is a local variable that goes out of scope, and the buffer only knows its
byte size.  Adding element-count awareness:

```cpp
template<typename T>
class TypedBufferView {
    Buffer& buf_;
    size_t count_;
public:
    TypedBufferView(Buffer& b) : buf_(b), count_(b.byteSize() / sizeof(T)) {}
    size_t count() const { return count_; }
    void upload(const T* data, size_t n) {
        assert(n <= count_);
        buf_.upload(data, n * sizeof(T));
    }
    void download(T* data, size_t n) {
        assert(n <= count_);
        buf_.download(data, n * sizeof(T));
    }
};
```

This is lightweight (no ownership, just a view) and prevents the common
"upload N*sizeof(T) bytes to a buffer sized for M*sizeof(T)" mismatch.

This does NOT need to replace Buffer -- it can be a non-owning wrapper
used at call sites that want type safety.

---

## Suggestion 5: Barrier validation (debug mode)

The mesh codebase has multiple instances of missing barriers between compute
passes.  vkobjects could track buffer state in debug mode:

```cpp
class Buffer {
#ifndef NDEBUG
    mutable Stage lastWriteStage_ = Stage::None;
    mutable bool needsBarrier_ = false;
#endif
public:
    void recordWrite(Stage stage) {
#ifndef NDEBUG
        lastWriteStage_ = stage;
        needsBarrier_ = true;
#endif
    }

    void recordBarrier() {
#ifndef NDEBUG
        needsBarrier_ = false;
#endif
    }

    void validateRead(Stage stage) const {
#ifndef NDEBUG
        if (needsBarrier_) {
            fprintf(stderr, "WARNING: buffer RID %u read at stage %d "
                    "without barrier after write at stage %d\n",
                    rid_, (int)stage, (int)lastWriteStage_);
        }
#endif
    }
};
```

The `recordWrite`/`recordBarrier`/`validateRead` calls would be inserted
by Commands internally when dispatching, recording barriers, and binding
buffers.  This requires Commands to know which buffers are being read/written
by each dispatch, which it currently does not -- but the push constants
contain all the RIDs, so a debug-mode scan of push constant data could
identify them.

This is a larger change and may not be worth the complexity.  But even a
manual `Buffer::assertBarriered()` that users call in debug builds would
catch bugs.

---

## Suggestion 6: copyBuffer bounds validation

**Commands::copyBuffer()** currently does not validate that `srcOffset + size
<= src.byteSize()` or `dstOffset + size <= dst.byteSize()`.  Adding these
assertions would catch buffer overrun bugs at the API level.

Similarly, **Barrier::buffer()** uses `VK_WHOLE_SIZE` unconditionally.
Supporting offset+size ranges would allow more precise barriers and could
catch cases where a barrier is applied to the wrong buffer region.

---

## What NOT to change

The core `Buffer` class should remain simple and non-owning of GPU state
beyond the allocation.  The suggestions above are all either debug-mode
checks or optional convenience methods.  The mesh codebase's bugs are not
caused by vkobjects being wrong -- they are caused by the *user* of vkobjects
forgetting to initialize, clear, or barrier.  The goal is to make forgetting
harder, not to make the API heavier.

The bindless descriptor approach (every buffer gets a RID, shaders index by
RID) is correct and well-implemented.  Do not add per-draw-call descriptor
binding -- the current approach is both simpler and faster.

---

## Summary

| Suggestion | Effort | Impact | Catches |
|-----------|--------|--------|---------|
| Init guard (debug) | Small | High | Uninitialized buffer use |
| clearForCompute() | Small | Medium | Missing post-fill barriers |
| BufferBuilder::clearedTo() | Medium | High | Uninitialized buffers at creation |
| TypedBufferView | Small | Medium | Size/type mismatches on upload |
| Barrier validation (debug) | Large | High | Missing barriers between passes |
| copyBuffer bounds check | Small | Low | Buffer overruns in copies |
