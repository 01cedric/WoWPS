// cpu_budget.hpp - what the console's measured CPU headroom buys, as arithmetic.
//
// Split out of cpu_memory.hpp, which cannot be included anywhere but the
// console: it opens with <orbis/libkernel.h>. The numbers below are the whole
// of the policy and none of the measurement, so they compile and can be tested
// on the host, which is the only place this project can execute anything.
//
// B39. The console died with std::bad_alloc after minutes of running at
// flexibleFreeMiB=12, and the shape of that session is why these functions
// exist rather than a wider cap:
//
//   * queryAvailableCpuMemory counts flexible pages the process has *not yet
//     mapped*. musl's arena is never handed back, so once the heap has grown
//     towards the 448 MiB title budget this number decays to a small residue
//     and stays there however much malloc can still recycle internally. The
//     game ran for minutes at 12 MiB, which is only possible if far more than
//     12 MiB was actually obtainable - so the reading is a measure of how
//     little room a *new mapping* has, not of how little the game has left.
//
//   * Every consumer treated that residue as a boolean: below 32 MiB is
//     "pressure", and pressure turned the file cache off outright. The valve
//     therefore latched on and never released, and turning the cache off does
//     not return arena pages - it only multiplies MPQ reads and their
//     decompression workspace, each of which is a fresh large allocation. The
//     shipped log reads "3912 hits, 34722 misses (10% hit rate), 0 MB cached".
//
// So the decisions here are graded rather than binary, they never reach zero
// while a measurement exists, and an unavailable measurement is conservative
// instead of maximal.
#pragma once

#include <cstddef>

namespace wowee::platform::ps4 {

// CPU malloc uses flexible memory, whereas Vulkan's direct-memory allocations
// have their own much larger pool. This is a CPU budgeting ceiling for the
// supplied title configuration, not a measurement of installed system RAM.
inline constexpr std::size_t kCpuMemoryBudgetLimit = 448ull * 1024 * 1024;
inline constexpr std::size_t kCpuCacheBudgetLimit = 32ull * 1024 * 1024;
inline constexpr std::size_t kCpuPressureHeadroom = 32ull * 1024 * 1024;
inline constexpr std::size_t kCpuSeverePressureHeadroom = 16ull * 1024 * 1024;

// The smallest file cache worth keeping, and the one the console falls to
// rather than switching off.
//
// Four MiB of a 448 MiB budget is under one percent, and it is bounded and
// resident: the alternative is not "no memory used", it is the same bytes
// allocated, decompressed and freed again on every one of tens of thousands of
// repeat reads, which is strictly more transient pressure than holding them.
inline constexpr std::size_t kCpuCacheBudgetFloor = 4ull * 1024 * 1024;

// Where the world loader stops asking for more.
//
// Below the stop line an unmapped-page residue this small means the next
// mapping the allocator needs is likely to be the one that fails, so optional
// work is declined outright.
//
// The loader starts holding detail at kCpuPressureHeadroom, which is the line
// the rest of the client already treats as pressure, and it does not stop
// holding until it is back above the resume line - which is therefore strictly
// *above* the hold line, not below it. A resume line under the hold line is not
// weak hysteresis, it is none: every reading below the hold line answers hold
// before the resume test is ever reached, and the clause is dead. That is what
// the first draft of this header had, and what test_ps4_cpu_budget caught.
inline constexpr std::size_t kCpuStreamStopHeadroom = 8ull * 1024 * 1024;
inline constexpr std::size_t kCpuStreamResumeHeadroom = 48ull * 1024 * 1024;
static_assert(kCpuStreamStopHeadroom < kCpuPressureHeadroom &&
                  kCpuPressureHeadroom < kCpuStreamResumeHeadroom,
              "stop < hold < resume, or the band between them means nothing");

/// How much more world the console will accept right now.
enum class WorldStreamAdmission {
    Admit,       ///< Normal streaming.
    HoldDetail,  ///< Required geometry only: no hysteresis ring, no new detail.
    Stop,        ///< Decline optional assets entirely; keep what is resident.
};

/// The file cache size the measured headroom pays for.
///
/// A quarter of what is free, which is the proportion getRecommendedCacheBudget
/// already used, clamped into [floor, limit]. The clamp is the fix: the console
/// path clamped *up* to a 32 MiB minimum, so at 12 MiB free the budget was
/// still 32 MiB - the measurement could not shrink anything, and the only thing
/// that reacted to pressure was a separate switch that disabled the cache
/// completely. Here the measurement is the whole of the answer and the answer
/// is never zero.
///
/// An unavailable measurement returns the floor rather than the limit. This
/// used to invert: getAvailableRAM answers 0 when the query fails, 0 clamped up
/// against the old 32 MiB minimum handed the *largest* cache to the firmware
/// that could not say whether there was room for it.
/// Quantised to the floor's granularity, which is what makes it usable as a
/// live setting rather than only as a number: a quarter of a free-page count
/// that moves every time anything allocates would give a different answer on
/// every sample, and the cache would spend the session being resized and
/// re-trimmed - and logging that it had been. In steps it only moves when the
/// console has genuinely crossed into a different amount of room.
constexpr std::size_t cpuFileCacheBudgetFor(std::size_t freeBytes, bool measured) noexcept {
    if (!measured) return kCpuCacheBudgetFloor;
    const std::size_t quarter = (freeBytes / 4 / kCpuCacheBudgetFloor) * kCpuCacheBudgetFloor;
    if (quarter < kCpuCacheBudgetFloor) return kCpuCacheBudgetFloor;
    if (quarter > kCpuCacheBudgetLimit) return kCpuCacheBudgetLimit;
    return quarter;
}

/// Whether the world loader should keep admitting work.
///
/// `holding` is the caller's previous answer, and only widens the band it takes
/// to get back to Admit; it never makes the decision stricter.
///
/// An unavailable measurement admits. A firmware that cannot report free pages
/// is not a firmware that is out of them, and stalling the terrain workers
/// forever awaiting a number they will never get is the failure mode
/// isMemoryPressure already documents avoiding.
constexpr WorldStreamAdmission worldStreamAdmissionFor(std::size_t freeBytes, bool measured,
                                                       bool holding) noexcept {
    if (!measured) return WorldStreamAdmission::Admit;
    if (freeBytes < kCpuStreamStopHeadroom) return WorldStreamAdmission::Stop;
    if (freeBytes < kCpuPressureHeadroom) return WorldStreamAdmission::HoldDetail;
    if (holding && freeBytes < kCpuStreamResumeHeadroom) return WorldStreamAdmission::HoldDetail;
    return WorldStreamAdmission::Admit;
}

/// True while optional assets - a doodad set, an .anim variant, a sidecar -
/// are still worth reading. False is a picture with less in it, which is the
/// point: the console is to lose detail rather than lose the session.
constexpr bool admitsOptionalDetail(WorldStreamAdmission admission) noexcept {
    return admission != WorldStreamAdmission::Stop;
}

/// The resident tile cap to run at, given the full cap and the load radius.
///
/// Never below the required square around the player. A cap under that ring is
/// not a smaller picture, it is terrain and collision missing from under the
/// character's feet - the one degradation that is worse than the crash.
constexpr unsigned residentTileCapFor(WorldStreamAdmission admission, unsigned fullCap,
                                      int loadRadius) noexcept {
    if (admission == WorldStreamAdmission::Admit) return fullCap;
    const int span = 2 * (loadRadius < 0 ? 0 : loadRadius) + 1;
    const unsigned required = static_cast<unsigned>(span * span);
    return fullCap < required ? fullCap : required;
}

// --- Character skin composites --------------------------------------------
//
// B40. The console died with std::bad_alloc in the entity spawner while every
// [WORLD_PERF] sample of the session read 43-52 MiB free, and the asset log
// beside it reads "24 read(s) lost to allocation failure, 0 optional read(s)
// declined for headroom" - twenty-four 1-to-4 MiB BLP decodes that could not be
// served, and not one decline, because at 44 MiB free worldStreamAdmissionFor
// answers Admit and admitsOptionalDetail is true for everything short of Stop.
//
// The lesson of the last round was that the free-page residue is not the heap.
// The lesson of this one is narrower and sharper: the ladder above has no
// question a 4 MiB block could answer. It grades how much *world* to stream,
// on a number sampled once per timing window, and a composite's peak is spent
// and released between two of those samples. So 44 MiB is a true reading of a
// pool that had nothing in it at the instant the composite asked.
//
// What one composite actually holds live, for an atlas of edge E and
// B = E*E*4 bytes (B is 4 MiB at 1024, 1 MiB at 512):
//
//   composite working buffer                                B
//   bleedAndStripMagentaKey's source copy + its mask        B + B/4
//   the CPU mip chain VkTexture::upload builds, levels 1..n B/3
//   the staging buffer for that chain, held to batch end    4B/3
//                                                          ------
//                                                        ~3.9 B
//
// plus the decoded layers the composite is made of, which are not a fixed
// multiple of B. At 1024 that is around sixteen megabytes for one character,
// and a city queues them back to back.
//
// B41. The mip line used to read 4B/3, because the chain opened by copying the
// caller's whole level 0 into a buffer of its own - a second full atlas, and
// the largest single request on the path, so the first to be refused by a
// fragmented arena. It is gone: uploadMips reads level 0 through the caller's
// pointer, and only levels one and below are built, which is a third of B
// between them.
//
// The multiple below stays at five, and that is deliberate rather than stale.
// The ledger it is drawn from covers only the buffers that are a fixed multiple
// of the finished image; the decoded layers are not, and until now the multiple
// had no room in it for them at all. The saved megabyte is spent on being right
// about the term that was missing rather than on admitting a larger atlas at a
// reading that has already crashed this client three times.
inline constexpr int kCompositeEdgeFull = 1024;
inline constexpr int kCompositeEdgeHalf = 512;
inline constexpr int kCompositeEdgeMin = 256;
inline constexpr std::size_t kCompositePeakMultiple = 5;

/// Bytes a finished composite of this edge occupies as RGBA8 - and the size of
/// the single largest contiguous block the path asks the allocator for.
constexpr std::size_t compositeBytesFor(int edge) noexcept {
    if (edge <= 0) return 0;
    return static_cast<std::size_t>(edge) * static_cast<std::size_t>(edge) * 4u;
}

/// Bytes one composite of this edge holds live at its peak, per the ledger above.
constexpr std::size_t compositePeakBytesFor(int edge) noexcept {
    return compositeBytesFor(edge) * kCompositePeakMultiple;
}

/// How much of a character's skin the console will build right now.
enum class CompositeAdmission {
    Full,   ///< Composite at the art's own size, up to kCompositeEdgeFull.
    Half,   ///< Composite, but never grow the atlas past kCompositeEdgeHalf.
    Plain,  ///< No composite: bind the base skin alone. Loses the equipment and
            ///< face overlays; costs one already-cached texture and no atlas.
    Defer,  ///< Build nothing this frame. The character keeps its default skin
            ///< and stays queued, so the crowd fills in rather than dropping.
};

/// The composite this console can pay for, from the two things worth asking.
///
/// Two numbers, because neither answers alone - which is the whole of the fault
/// this replaces. `largestBlockBytes` is what a probe allocation actually got
/// back, and it answers the question that throws: a composite is one contiguous
/// RGBA buffer, and musl serves anything over its mmap threshold by mapping it
/// whole, so a request either finds that much in one piece or returns null.
/// `freeBytes` is sceKernelAvailableFlexibleMemorySize, and it answers what the
/// probe cannot - whether the *whole* ledger fits, not only its largest line.
///
/// The free count has to cover the peak and still leave kCpuPressureHeadroom
/// behind it. Not a margin invented for this decision - it is the line the rest
/// of this client already calls pressure, and a character's skin is optional
/// detail. Taking the console into the state its own loader treats as trouble,
/// in order to draw armour on one NPC, is the trade this file exists to refuse.
/// It is also what makes the shipped reading come out right: at 44 MiB free a
/// 20 MiB peak leaves 24, which is under that line, and the session that read
/// 43-52 MiB the whole way should have been building half-size atlases rather
/// than full ones.
///
/// The two axes end up dividing the work between them, which is worth naming so
/// a later reader does not think a rung is dead. On the free-count axis the
/// meaningful steps are Full, Half and not-now: the reserve dominates the peak
/// once the edge is 512 or less, so Plain and Defer sit within a few MiB of
/// each other there. They separate on the probe axis instead, which is the
/// fragmented case - a heap that hands back 256 KiB but not 1 MiB is exactly a
/// Plain, and one that hands back nothing is exactly a Defer.
///
/// An unmeasurable free count still admits on the strength of the probe. A
/// firmware that will not report free pages has not run out of them, and the
/// probe is the direct evidence here; refusing on the indirect one would be
/// reading the wrong number again in the other direction.
constexpr CompositeAdmission compositeAdmissionFor(std::size_t largestBlockBytes,
                                                   std::size_t freeBytes,
                                                   bool freeMeasured) noexcept {
    const auto affords = [&](int edge) noexcept {
        return largestBlockBytes >= compositeBytesFor(edge) &&
               (!freeMeasured ||
                freeBytes >= compositePeakBytesFor(edge) + kCpuPressureHeadroom);
    };
    if (affords(kCompositeEdgeFull)) return CompositeAdmission::Full;
    if (affords(kCompositeEdgeHalf)) return CompositeAdmission::Half;
    if (affords(kCompositeEdgeMin)) return CompositeAdmission::Plain;
    return CompositeAdmission::Defer;
}

/// The widest atlas this admission allows, or 0 when it allows none.
///
/// Plain returns kCompositeEdgeMin rather than 0: a base skin is still a
/// texture and still has to be uploaded, and this is the size that bounds it.
constexpr int compositeEdgeCapFor(CompositeAdmission admission) noexcept {
    switch (admission) {
        case CompositeAdmission::Full: return kCompositeEdgeFull;
        case CompositeAdmission::Half: return kCompositeEdgeHalf;
        case CompositeAdmission::Plain: return kCompositeEdgeMin;
        case CompositeAdmission::Defer: break;
    }
    return 0;
}

/// True while the console will still blend layers rather than bind one texture.
constexpr bool admitsCompositing(CompositeAdmission admission) noexcept {
    return admission == CompositeAdmission::Full || admission == CompositeAdmission::Half;
}

/// The atlas cap to hand a renderer that also builds composites this rung has
/// no say over - the local player's skin, the character preview's.
///
/// Never below kCompositeEdgeHalf. The rungs above ration a crowd of several
/// hundred; those two are one character each and one of them is the face the
/// camera is pointed at. Shrinking it to a quarter of its pixels to make room
/// for the crowd gets the trade backwards, and 512 is what the stock art ships
/// at anyway - the cap only ever declines to grow past it.
constexpr int rendererCompositeEdgeCapFor(CompositeAdmission admission) noexcept {
    const int cap = compositeEdgeCapFor(admission);
    return cap < kCompositeEdgeHalf ? kCompositeEdgeHalf : cap;
}

/// The atlas edge to build at, given what the art asks for and what is affordable.
///
/// Never grows what the art did not ask for - this only ever reduces. An art set
/// that wants 512 gets 512 under a Full admission, not 1024.
constexpr int compositeEdgeFor(CompositeAdmission admission, int wantedEdge) noexcept {
    const int cap = compositeEdgeCapFor(admission);
    if (cap <= 0) return 0;
    if (wantedEdge <= 0) return cap;
    return wantedEdge < cap ? wantedEdge : cap;
}

// --- The decoded-skin reserve ----------------------------------------------
//
// B41. Between an async creature load finishing and the spawn that consumes it,
// the spawner holds that display's decoded BLPs so the main thread does not
// have to decode them mid-frame. They are handed over by pointer and released
// by the one function that reads them, and that function runs at most once per
// displayId - so an entry whose spawn never arrives is never released at all.
// A creature that despawns before its queued spawn is reached, a display whose
// DBC row is missing, and every entry still resident at a zone change all leave
// one behind. None of them is a small allocation: an HD creature skin is a
// megabyte decoded and a display can carry three.
//
// So the reserve is given a size, and the size is taken from the rung the
// console is already on. It asks the *free page count* rather than the probe,
// and that is the point of it sitting beside the composite decision rather than
// inside it: a composite fails on one contiguous block, a reserve of two dozen
// separately-decoded images fails on the total. Different shapes, different
// questions - which is the distinction the fault this file documents got wrong
// in the other direction.
//
// Dropping an entry costs nothing but a decode: the consumer falls back to
// asking the asset manager for the same texture, which is what every display
// without a pre-decoded entry already does.
inline constexpr std::size_t kPredecodedSkinReserveLimit = 16ull * 1024 * 1024;
inline constexpr std::size_t kPredecodedSkinReserveFloor = 2ull * 1024 * 1024;

/// Bytes of decoded creature skin worth holding ahead of the spawn that uses them.
///
/// Zero at Defer, and deliberately: nothing is being spawned at that rung, so a
/// reserve held for spawns that are not happening is the purest form of the
/// memory this whole ladder exists to give back. The work is not lost, only the
/// decode - the files are still in the asset cache, and the spawn that resumes
/// re-reads them.
constexpr std::size_t predecodedSkinReserveFor(CompositeAdmission admission) noexcept {
    switch (admission) {
        case CompositeAdmission::Full: return kPredecodedSkinReserveLimit;
        case CompositeAdmission::Half: return kPredecodedSkinReserveLimit / 2;
        case CompositeAdmission::Plain: return kPredecodedSkinReserveFloor;
        case CompositeAdmission::Defer: break;
    }
    return 0;
}

static_assert(kPredecodedSkinReserveFloor < kPredecodedSkinReserveLimit / 2,
              "the rungs have to separate, or the reserve does not react at all");
static_assert(kPredecodedSkinReserveLimit < kCpuMemoryBudgetLimit / 8,
              "a decode cache is an optimisation and may not be a fraction of the "
              "title budget worth naming");

static_assert(compositePeakBytesFor(kCompositeEdgeFull) < kCpuMemoryBudgetLimit,
              "one character may not cost the whole title budget");
static_assert(compositeBytesFor(kCompositeEdgeMin) < kCpuStreamStopHeadroom,
              "the smallest skin has to fit inside the headroom the stop line keeps back, "
              "or Plain is a promise the console cannot keep");

} // namespace wowee::platform::ps4
