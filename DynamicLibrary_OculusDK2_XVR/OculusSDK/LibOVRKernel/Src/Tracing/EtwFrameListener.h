
#ifndef OVR_EtwFrameListener_h
#define OVR_EtwFrameListener_h

__pragma(warning(push))
__pragma(warning(disable:4530 4265))
#include <array>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <map>
#include <condition_variable>
#include <string>
__pragma(warning(pop))

#include "EtwFrameEventData.h"

#include <tdh.h>    // Event Tracing

namespace OVR { namespace Etw {

using EtwProcessCallback = std::function<void(EtwEvent**, int)>;

enum EtwListenerErrorCode
{
    ovrError_EtwListenerInitialize  = -91001,
    ovrError_EtwListenerRuntime     = -91002,
    ovrError_EtwListenerLostEvents  = -91003
};

struct EtwListenerError
{
    // The following should be ovrSuccess.
    static const EtwListenerErrorCode ListenerNoError = (EtwListenerErrorCode)0;

    EtwListenerError() : code(ListenerNoError) {}
    EtwListenerError(EtwListenerErrorCode c, const std::string d) : code(c), desc(d) {}

    bool Succeeded() { return code == ListenerNoError; }

    EtwListenerErrorCode  code;
    std::string           desc;
};

class EtwFrameListener
{
public:

    /// Frame analyzer will generate a thread to monitor ETW events in realtime.
    ///
    /// \param etwSessionName   Session name to use for the Etw listener.
    ///                         Should be unique and not conflict with the
    ///                         rest of the system.
    /// \prama signalMessages   Number of new messages at which this class will
    ///                         signal that work is available. Signal is
    ///                         delivered to a single thread waiting on
    ///                         WaitUntilWorkAvailable.
    EtwFrameListener(const std::string& etwSessionName, int signalMessages);
    ~EtwFrameListener();

    /// Calls 'eventHandler' function if data is available. Does not block.
    ///
    /// \param eventHandler  Function to call if data is available.
    void HandleEvents(EtwProcessCallback eventHandler);

    /// Blocks until work is available for processing. Ensure your thread is
    /// not waiting on this condition when *this is destroyed. Undefined
    /// behavior results otherwise. Thread safe. Can be called from any thread.
    ///
    /// \param timeoutMs  Maximum time to wait for event buffer to fill. In milliseconds.
    ///                   -1 indicates that we should wait indefinitely.
    ///
    /// \return false if timeout was reached, true otherwise.
    bool WaitUntilWorkAvailable(int timeoutMs);

    /// Unblocks one thread waiting for work. Can be called from any thread.
    void SignalWork();

    /// Flushes the Etw session buffers. Can be used to collect data in realtime,
    /// but mostly used for testing.
    ///
    /// \return true if flush was successful.
    bool Flush();

    /// Returns whether errors have been encountered. Does not return the
    /// actual number of errors as that may change between when this function
    /// is called and GetAndClearErrors is called.
    bool HasErrors() const;

    /// Retrieves any errors that have occurred while initializing or running
    /// the ETW listener.
    std::vector<EtwListenerError>  GetAndClearErrors();

private:

    static const int32_t NumStoredEvents = 1900;  ///< Used to buffer events internally.
    static const int32_t NumStackEvents  = 192;

    /// \return 'StoredEventIndex' if no valid first event is found.
    size_t getIndexOfFirstEvent();

    /// Pushes a new listener error. Can be called from any thread.
    void pushError(const EtwListenerError& error);
    void pushError(EtwListenerErrorCode code, const std::string& desc);

    /// We could use ovrSysErrorCode for the final parameter, but including
    /// OVR_Error.h in this header file is too heavy weight (and OVR_Error.h
    /// exists in LibOVR).
    void pushError(EtwListenerErrorCode code, const std::string& prefix, uint32_t systemCode);

    static void traceThread(EtwFrameListener* This, TRACEHANDLE consumerHandle);

    /// Circular array containing stored events.
    std::array<EtwEvent,NumStoredEvents>      StoredEvents;

    /// Current index into 'StoredEvents' and indicates the index of the next
    /// event that should be placed in StoredEvents.
    int32_t     StoredEventIndex;

    /// Index of first unprocessed event.
    int32_t     UnprocessedEventsIndex;

    /// Number of events since we last emitted work signal.
    int32_t     EventsSinceLastSignal;

    /// Number of lost events since HandleEvents was last called.
    int32_t     NumLostEvents;

    /// Protects StoredEvents, StoredEventIndex, UnprocessedEventsIndex, 
    /// and EventsSinceLastSignal,
    std::mutex  StoredEventsMutex;

    /// Protects event overwrite when issuing a callback to a user-provided 
    /// EtwProcessCallback. This mutex is used so we can unlock
    /// 'StoredEventsMutex' to let events continue to be collected while we call
    /// a possibly expensive user function. This mutex protects pointer data
    /// from being overwritten inside of ProcessEvent and is only ever rarely
    /// locked from ProcessEvent. Only when we detect we are about to overwrite
    /// unprocessed events and subsequently lose event data is it locked.
    /// It does not keep us from overwriting unprocessed data, just from
    /// overwriting data that is currently being processed.
    std::mutex  ProcessEventsMutex;

    std::atomic<bool>       WorkPending;
    std::condition_variable WorkCondition;
    std::mutex              WorkMutex;

    /// Number of events that have occured before we last signaled that work
    /// is available.
    int                     NumEventsOnSignal;

    mutable std::mutex      ErrorMutex;  ///< Used to access 'Errors'.
    std::vector<EtwListenerError> Errors;

    //----------------------
    // Realtime ETW Tracing
    //----------------------

    EtwEventDataConv        EtwDataConv;
    PEVENT_TRACE_PROPERTIES ControllerProperties;
    TRACEHANDLE             ControllerHandle;
    std::thread             ConsumerTracingThread;
    std::string             EtwSessionName;

    std::atomic<bool>       ProcessThreadTerminated;  ///< Set to true when processing thread terminates.

    static VOID WINAPI ProcessEvent(PEVENT_RECORD p);

    bool startRealtimeTracing();
    bool stopRealtimeTracing();
};

} // namespace Etw
} // namespace OVR

#endif // OVR_EtwFrameListener_h
