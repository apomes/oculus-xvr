#ifndef OVR_EtwFrameUtils_h
#define OVR_EtwFrameUtils_h

#include "EtwFrameListener.h"
#include "EtwFrameEventData.h"

namespace OVR {
namespace Etw {

/// Waits for compositor frames to stabilize. Collects statistics for
/// 'sequentialFrames' number of frames.
///
/// \param sequentialFrames Number of valid frames in a row required for stabilization.
/// \param vsyncToVsync     Period between vsync events. Corresponds to HMD refresh rate.
/// \param timeout          Timeout, in milliseconds, until stabilization is aborted.
/// \param sessionName      Session name for EtwFrameListener.
///
/// \return True if frame stabilized, false otherwise.
bool etwWaitForFrameStabilization(int sequentialFrames, double vsyncToVSync, int64_t timeout,
                                  const std::string& sessionName = "OVRFrameStabilization");

/// Finds an event that *exactly* matches the input time.
const EtwEvent* const* etwFindExactEvent(double time, const EtwEvent* const* events, int numEvents);

/// Converts typeless userdata buffer into type 'T'. Extra runtime checks are
/// performed to ensure a valid type conversion.
///
/// \param[in] event EtwEvent that was deserialized using 'Deserialize'.
template<class T>
const T* etwGetUserData(const EtwEvent* event)
{
    OVR_ASSERT(T::Provider         == event->ProviderId);
    OVR_ASSERT(T::EventId          == event->EventId);
    OVR_ASSERT(event->UserDataSize == sizeof(T));

    // We should be fine with this cast as UserData is located at the
    // beginning of the event data structure.
    return reinterpret_cast<const T*>(event->UserData);
}

template <class T>
bool etwIsEventType(const EtwEvent* event)
{
    if (   (event->ProviderId == T::Provider)
        && (event->EventId == T::EventId))
    {
        return true;
    }
    else
    {
        return false;
    }
}

/// Finds all events between two time points, exact interval.
template <class T>
std::vector<const EtwEvent*> etwGetEvents(const EtwEvent* const* begin,
                                          const EtwEvent* const* end)
{
    if (!begin || !end) {return std::vector<const EtwEvent*>();}

    std::vector<const EtwEvent*> result;
    int numEvents = static_cast<int>(end - begin);
    for (int i = 0; i < numEvents; ++i)
    {
        const EtwEvent* event = begin[i];
        if (event->ProviderId == T::Provider && event->EventId == T::EventId)
        {
            result.push_back(event);
        }
    }
    return result;
}

template <class T>
const EtwEvent* const* etwFindEvent(const EtwEvent* const* events, size_t numEvents,
                                    std::function<bool(const T&)> predicate)
{
    if (events == nullptr) { return nullptr; }

    for (int i = 0; i < (int)numEvents; ++i)
    {
        const EtwEvent* event = events[i];
        if (   event->ProviderId == T::Provider && event->EventId == T::EventId
            && predicate(*etwGetUserData<T>(event)))
        {
            return &events[i];
        }
    }

    return nullptr;
}



using VSyncProcessCallback = std::function<void(
    double, double, double, double, // v0, v1, v2, v3 (see description above)
    uint32_t, uint32_t,             // ft, fn (see description above)
    EtwEvent**, int                 // bufferedEventSet, numEventsTotal
    )>;

// This class consumes EtwEvents from EtwFrameListener and partitions events
// onto vsync boundaries. Used primarily for testing purposes.
class VSyncEventPartitioner
{
public:

    VSyncEventPartitioner();

    // Expects an event buffer containing new events in chronological order.
    // Issues a call back to 'eventHandler' when sufficient data is available
    // and has been partitioned on vsync boundaries.
    void HandleEvents(EtwEvent** events, int numEvents,
                      VSyncProcessCallback eventHandler);

private:

    /// Used to buffer events internally. Should be large enough to contain
    /// 3 vsyncs worth of data.
    static const int32_t NumStoredEvents = 1024 * 2;

    /// Corresponds to frame ring-buffer size. VSyncPair is lightweight and does
    /// not contain event information. This is only used to delimit frame
    /// boundaries in which to analyze data.
    static const int32_t VSyncPairsToAnalyze = 3;

    /// Handles vsync. Requires HMDInfo (pulled from CAPI if using tests).
    void handleVSync(double vsyncTime, int frameIndex,
                     VSyncProcessCallback callback);

    size_t VSyncEventPartitioner::getIndexOfFirstEvent();
    int buildEventList(EtwEvent** out, int outSize, double begin, double end);

    struct VSyncPair
    {
        VSyncPair()
            : vsyncBegin(0.0)
            , vsyncEnd(0.0)
            , frameIndex(-1)
        {}

        VSyncPair(double begin, double end, int64_t index)
            : vsyncBegin(begin)
            , vsyncEnd(end)
            , frameIndex(index)
        {}

        bool isValid()    {return ( frameIndex != -1 );}

        double  vsyncBegin;
        double  vsyncEnd;
        int64_t frameIndex;
    };

    /// Push a new vsync pair onto the pair list.
    void pushVSyncPair(const VSyncPair& pair);

    /// Returns the number of valid pairs.
    int numValidPairs();

    /// Retrieves next vsync ETW event. Want std::optional.
    EtwEvent* getAndClearNextVSyncEvent();

    /// Adds a vsync event given an index into StoredEvents.
    void addVSyncEvent(EtwEvent* event);

    /// Circular queue containing stored events.
    std::array<EtwEvent,NumStoredEvents>      StoredEvents;

    /// Current index into 'StoredEvents' and indicates the index of the next
    /// event that should be placed in StoredEvents.
    int32_t     StoredEventIndex;

    /// Circular queue storing VSync boundaries.
    std::array<VSyncPair,VSyncPairsToAnalyze> StoredVSyncPairs;

    /// Current index into 'StoreVSyncPairs' and indicates the index of the next
    /// pair that should be placed in StoredVSyncPairs.
    int32_t     StoredVSyncPairIndex;

    /// Last frameIndex that was successfully analyzed. Used to determine if
    /// we have been skipping analysis frames.
    int32_t     LastAnalyzedFrameIndex;

    double      LastVSyncTime;        ///< Time at which last VSync occurred.
};

} // namespace Etw
} // namespace OVR

#endif // OVR_EtwFrameUtils_h
