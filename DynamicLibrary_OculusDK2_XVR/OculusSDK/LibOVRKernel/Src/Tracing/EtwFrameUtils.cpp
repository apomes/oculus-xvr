
#include <chrono>
#include "EtwFrameUtils.h"

namespace OVR {
namespace Etw {


static void handleVSyncEvents(
    int32_t& numFrames, int32_t& numValidFrames, uint32_t& lastFrameIndex,
    double vsyncToVSync, double /*v0*/, double v1, double v2, double /*v3*/,
    uint32_t ft, uint32_t /*fn*/, EtwEvent** /*bufferedEventSet*/, int /*numEventsTotal*/
    )
{
    if ((v2 - v1) > vsyncToVSync * 1.5 || ft != lastFrameIndex + 1)
    {
        // We lost a frame, reset valid frame count.
        numValidFrames = 0;
    }
    lastFrameIndex = ft;
    ++numValidFrames;
    ++numFrames;
}

bool etwWaitForFrameStabilization(int targetSequentialFrames, double vsyncToVSync, int64_t timeout,
                                  const std::string& sessionName)
{
    using namespace std::placeholders;

    const int etwEventBatchSize = 50;

    EtwFrameListener      listener(sessionName , etwEventBatchSize);

    int32_t  numFrames      = 0;  // Number of frames total.
    int32_t  numValidFrames = 0;  // Number of non-lost compositor frames.
    uint32_t lastFrameIndex = 0;  // Last frame index

    // Create function closure so numFrame and numValidFrames are appropriately
    // updated when we receive callbacks from listener.
    VSyncProcessCallback handleVSyncEventsCb =
      std::bind(handleVSyncEvents, std::ref(numFrames), std::ref(numValidFrames),
                std::ref(lastFrameIndex), vsyncToVSync, _1, _2, _3, _4, _5, _6, _7, _8);

    // Function to handle events from the listener. Pipes events into
    // VSyncEventPartitioner and issues a call to a handleVSyncEvents closure.
    VSyncEventPartitioner vsyncPartitioner;
    EtwProcessCallback handleEvents = [&vsyncPartitioner, &handleVSyncEventsCb, etwEventBatchSize](EtwEvent** events, int numEvents) {
        vsyncPartitioner.HandleEvents(events, numEvents, handleVSyncEventsCb);
    };

    // Timer to ensure we do not surpass our timeout.
    std::chrono::time_point<std::chrono::system_clock> start, now;
    start = std::chrono::system_clock::now();

    bool stable = false;
    while (!stable)
    {
        // Wait for a maximum of 6 seconds for events to become available.
        if (!listener.WaitUntilWorkAvailable((int)timeout))
        {
            printf("[FrameStabilization] No ETW events received.");
            return false;
        }

        listener.HandleEvents(handleEvents);

        if (numValidFrames >= targetSequentialFrames)
        {
            stable = true;
            break;
        }

        now = std::chrono::system_clock::now();
        int64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(now-start).count();
        if (diff > timeout)
        {
            break;
        }

    }

    return stable;
}

// Finds the event that occured at 'time'.
const EtwEvent* const* etwFindExactEvent(
    double time, const EtwEvent* const* events, int numEvents)
{
    if (numEvents <= 0) {return nullptr;}
    if (numEvents == 1) {return (events[0]->EventTime == time) ? &events[0] : nullptr;}

    int index = numEvents / 2;
    if (events[index]->EventTime == time)
    {
        return &events[index];
    }
    else if (time < events[index]->EventTime)
    {
        return etwFindExactEvent(time, events, index);
    }
    else
    {
        return etwFindExactEvent(time, &events[index + 1], (numEvents - index));
    }
}

// Finds the event that occured directly before or equal to 'time' (errs on the
// left hand side of interval).
const EtwEvent* const* etwFindLeftEvent(
    double time, const EtwEvent* const* events, int numEvents)
{
    if (numEvents <= 0) {return nullptr;}
    if (numEvents == 1) {return &events[0];}

    // Must include since we include last iteration's index every iteration.
    // We should end up with an interval for last iteration if we don't
    // match equality.
    if (numEvents == 2)
    {
        if (events[1]->EventTime <= time) {return &events[1];}
        else                              {return &events[0];}
    }

    int index = numEvents / 2;
    if (events[index]->EventTime == time)
    {
        return &events[index];
    }
    else if (time < events[index]->EventTime)
    {
        return etwFindLeftEvent(time, events, index + 1);
    }
    else
    {
        return etwFindLeftEvent(time, &events[index], (numEvents - index) + 1);
    }
}

// Same as above except that this funciton errs on the right hand side of the interval.
const EtwEvent* const* etwFindRightEvent(
    double time, const EtwEvent* const* events, int numEvents)
{
    if (numEvents <= 0) {return nullptr;}
    if (numEvents == 1) {return &events[0];}

    // Must include since we include last iteration's index every iteration.
    // We should end up with an interval for last iteration if we don't
    // match equality.
    if (numEvents == 2)
    {
        if (events[1]->EventTime >= time) {return &events[1];}
        else                              {return &events[0];}
    }

    int index = numEvents / 2;
    if (events[index]->EventTime == time)
    {
        return &events[index];
    }
    else if (time < events[index]->EventTime)
    {
        return etwFindRightEvent(time, events, index + 1);
    }
    else
    {
        return etwFindRightEvent(time, &events[index], (numEvents - index) + 1);
    }
}








VSyncEventPartitioner::VSyncEventPartitioner()
    : StoredEventIndex(0)
    , StoredVSyncPairIndex(0)
    , LastAnalyzedFrameIndex(-1)
    , LastVSyncTime(0.0)
{
}

void VSyncEventPartitioner::HandleEvents(EtwEvent** events, int numEvents,
                                         VSyncProcessCallback eventHandler)
{
    for (int i = 0; i < numEvents; ++i)
    {
        EtwEvent* event = events[i];
        
        StoredEvents[StoredEventIndex] = *event;
        StoredEventIndex = (StoredEventIndex + 1) % NumStoredEvents;

        if (   event->ProviderId == EtwData_VSync::Provider
            && event->EventId    == EtwData_VSync::EventId)
        {
            OVR_ASSERT(EtwData_VSync::Provider  == event->ProviderId);
            OVR_ASSERT(EtwData_VSync::EventId   == event->EventId);
            OVR_ASSERT(event->UserDataSize == sizeof(EtwData_VSync));

            const EtwData_VSync* eventData = reinterpret_cast<const EtwData_VSync*>(event->UserData);

            double  vsyncTime  = eventData->VSyncTime;
            int32_t frameIndex = eventData->FrameIndex;

            handleVSync(vsyncTime, frameIndex, eventHandler);
        }

    }
}

void VSyncEventPartitioner::handleVSync(double vsyncTime, int frameIndex,
                                        VSyncProcessCallback callback)
{
    // LastVSyncTime is set in the constructor to 0.0. We are using that initial value
    // as a boolean value indicating when handleVSync is called for the first time.
    if (LastVSyncTime == 0.0)
    {
        // Do not try and calculate vsync timing from 0.0 to current time.
        LastVSyncTime = vsyncTime;
        return;
    }

    // Push this new vsync pair to the beginning of StoredVSyncPairs.
    // If we have VSyncPairsToAnalyze worth of data, issue a processing callback.
    pushVSyncPair(VSyncPair(LastVSyncTime, vsyncTime, frameIndex));

    const int MaxEventsSize = 2048;
    std::array<EtwEvent*, MaxEventsSize> bufferedEventSet;

    if (numValidPairs() == VSyncPairsToAnalyze)
    {
        VSyncPair& vnext   = StoredVSyncPairs[0];
        VSyncPair& vtarget = StoredVSyncPairs[1];
        VSyncPair& vprev   = StoredVSyncPairs[2];

        if (LastAnalyzedFrameIndex != -1)
        {
            if (vtarget.frameIndex - LastAnalyzedFrameIndex > 1)
            {
                printf("[VSyncEventPartitioner] Skipped frame frame analysis for %d frames\n",
                    vtarget.frameIndex - LastAnalyzedFrameIndex);
            }
        }
        LastAnalyzedFrameIndex = (int32_t)vtarget.frameIndex;

        // There is room for optimization here. We could easily store
        // the starting event for the vsync in our VSyncPair. No floating
        // point comparison and a linear run through the list. I'm leaving
        // this alone since it is only used in tests for now.
        int numBufferedEvents = buildEventList(
                &bufferedEventSet[0], MaxEventsSize,
                vprev.vsyncBegin, vnext.vsyncEnd);

        double v0 = vprev.vsyncBegin;
        double v1 = vtarget.vsyncBegin;
        double v2 = vnext.vsyncBegin;
        double v3 = vnext.vsyncEnd;

        uint32_t ft = static_cast<uint32_t>(vtarget.frameIndex);
        uint32_t fn = static_cast<uint32_t>(vnext.frameIndex);

        callback(v0, v1, v2, v3, ft, fn, &bufferedEventSet[0], numBufferedEvents);
    }

    LastVSyncTime = vsyncTime;
}

/// Push a new vsync pair onto the pair list.
void VSyncEventPartitioner::pushVSyncPair(const VSyncPair& pair)
{
    VSyncPair toStore = pair;
    for (int i = 0; i < VSyncPairsToAnalyze; ++i)
    {
        VSyncPair save = StoredVSyncPairs[i]; // Ensure we copy.

        StoredVSyncPairs[i] = toStore;
        toStore             = save;
    }
}

/// Returns the number of valid pairs.
int VSyncEventPartitioner::numValidPairs()
{
    int validCount = 0;
    for (int i = 0; i < VSyncPairsToAnalyze; ++i)
    {
        if (!StoredVSyncPairs[i].isValid())
        {
            break;
        }
        ++validCount;
    }

    return validCount;
}

size_t VSyncEventPartitioner::getIndexOfFirstEvent()
{
    // Start from the last possible event, plus one (StoredEventIndex + 1).
    int startingIndex = (StoredEventIndex + 1) % NumStoredEvents;

    for (int i = startingIndex; i != StoredEventIndex;
          i = (i + 1) % NumStoredEvents)
    {
        EtwEvent& event = StoredEvents[i];
        if (event.IsValid())
        {
            return (size_t)i;
        }
    }

    return (size_t)StoredEventIndex;
}

int VSyncEventPartitioner::buildEventList(EtwEvent** out, int outSize, double begin, double end)
{
    size_t firstEventIndex = getIndexOfFirstEvent();

    if (firstEventIndex == (size_t)StoredEventIndex)
    {
        return 0;
    }

    int numEvents = 0;
    for (int i = (int)firstEventIndex; i != StoredEventIndex; i = (i + 1) % NumStoredEvents)
    {
        EtwEvent* event = &StoredEvents[i];
        if (event->EventTime > begin && event->EventTime <= end)
        {
            if (numEvents == outSize)
            {
                printf("[VSyncEventPartitioner] Increase intermediate event list size.");
                break;
            }
            out[numEvents] = event;
            ++numEvents;
        }
        else if (event->EventTime > end)
        {
            break;
        }
    }

    return numEvents;
}



} // namespace Etw
} // namespace OVR
