

#include "EtwFrameListener.h"

#include "Kernel/OVR_Types.h"
#include "Kernel/OVR_String.h"

#include <vector>

#include <locale>
#include <codecvt>

namespace OVR {
namespace Etw {

EtwFrameListener::EtwFrameListener(const std::string& etwSessionName, int signalMessages)
    : StoredEventIndex(0)
    , UnprocessedEventsIndex(0)
    , EventsSinceLastSignal(0)
    , NumLostEvents(0)
    , ControllerProperties(nullptr)
    , ControllerHandle(0)
    , NumEventsOnSignal(signalMessages)
    , EtwSessionName(etwSessionName)
{
    startRealtimeTracing();
}

EtwFrameListener::~EtwFrameListener()
{
    stopRealtimeTracing();
    // No events should be waiting on our condition variable at this point.

    if (ControllerProperties)
    {
        delete[] ControllerProperties;
        ControllerProperties = nullptr;
    }
}

void EtwFrameListener::HandleEvents(EtwProcessCallback eventHandler)
{
    std::array<EtwEvent*, NumStackEvents> bufferedEventSet;

    // Begin at UnprocessedEventsIndex and progress forward until we encounter
    // StoredEventIndex.
    int i = UnprocessedEventsIndex;

    // Ensure we consume all events before proceeding forward,
    // regardless of NumStackEvents.
    while (i != StoredEventIndex)
    {
        int numEvents = 0;

        std::unique_lock<std::mutex> storedEventLock(StoredEventsMutex);

        for (i = UnprocessedEventsIndex; i != StoredEventIndex;
            i = (i + 1) % NumStoredEvents)
        {
            if (numEvents >= NumStackEvents)
            {
                break;
            }
            bufferedEventSet[numEvents] = &StoredEvents[i];
            ++numEvents;
        }
        UnprocessedEventsIndex = i;

        // We have processed all relevant events. Acquire processEventsMutex before
        // unlocking StoredEventsMutex so we can ensure that no pointer data will
        // be changed underneath us when issuing the user callback.
        std::unique_lock<std::mutex> processLock(ProcessEventsMutex);

        if (NumLostEvents > 0)
        {
            std::string lostEventsDesc = "Lost " + std::to_string(NumLostEvents) + " Events";
            pushError(ovrError_EtwListenerLostEvents, lostEventsDesc);
            NumLostEvents = 0;
        }

        storedEventLock.unlock();

        eventHandler(&bufferedEventSet[0], numEvents);

        processLock.unlock();
    }
}

bool EtwFrameListener::WaitUntilWorkAvailable(int timeout)
{
    bool success = true;  // Be positive!

    std::unique_lock<std::mutex> lock(WorkMutex);

    if (timeout > -1)
    {
        auto timeoutMs = std::chrono::milliseconds(timeout);
        success = WorkCondition.wait_for(lock, timeoutMs, [this]{return WorkPending.load();});
    }
    else
    {
        WorkCondition.wait(lock, [this]{return WorkPending.load();});
    }

    WorkPending.store(false);
    lock.unlock();

    return success;
}

void EtwFrameListener::SignalWork()
{
    WorkPending.store(true);
    WorkCondition.notify_one();
}

bool EtwFrameListener::Flush()
{
    if (ControllerHandle != 0)
    {
        ULONG status = ::ControlTraceW(ControllerHandle, NULL, ControllerProperties,
                                       EVENT_TRACE_CONTROL_FLUSH);

        return (status == ERROR_SUCCESS);
    }
    else
    {
        return false;
    }
}

bool EtwFrameListener::HasErrors() const
{
    std::lock_guard<std::mutex> lock(ErrorMutex);
    return (Errors.size() > 0);
}

std::vector<EtwListenerError> EtwFrameListener::GetAndClearErrors()
{
    std::lock_guard<std::mutex> lock(ErrorMutex);
    std::vector<EtwListenerError> result = Errors;
    Errors.clear(); 
    return result;
}

VOID WINAPI EtwFrameListener::ProcessEvent(_In_ PEVENT_RECORD p)
{
    if (p->EventHeader.Flags & EVENT_HEADER_FLAG_CLASSIC_HEADER)
    {
        return;
    }

    // According to the documentation for ProcessTrace, it sorts all incoming
    // events chronologically. Events can occur out-of-order if the session
    // specifies system time as the clock (low resolution). This manifests
    // as multiple events having the same timestamp. Order cannot be guarenteed
    // in this case, but we are guaranteed a sorted set of records. That being
    // said, we always use high performance QPC.

    EtwFrameListener* ctx = reinterpret_cast<EtwFrameListener*>(p->UserContext);

    EtwEvent event;
    if (ctx->EtwDataConv.Deserialize(p, event))
    {
        std::unique_lock<std::mutex> lock(ctx->StoredEventsMutex);
        int index = ctx->StoredEventIndex;
        ctx->StoredEvents[index] = event;

        int nextIndex = (ctx->StoredEventIndex + 1) % ctx->NumStoredEvents;

        // Ensure we don't run over the end of our unprocessed events.
        if (nextIndex == ctx->UnprocessedEventsIndex)
        {
            // Ensure we don't overwrite data that is being processed before we
            // manually force an increment of UnprocessedEventsIndex and lose
            // data.
            std::unique_lock<std::mutex> processLock(ctx->ProcessEventsMutex);
            ++ctx->NumLostEvents;
            ctx->UnprocessedEventsIndex = (ctx->UnprocessedEventsIndex + 1) % ctx->NumStoredEvents;
        }

        ctx->StoredEventIndex = nextIndex;
        ++ctx->EventsSinceLastSignal;
        if (ctx->EventsSinceLastSignal > ctx->NumEventsOnSignal)
        {
            ctx->SignalWork();
        }
    }
}


void EtwFrameListener::pushError(const EtwListenerError& error)
{
    std::lock_guard<std::mutex> lock(ErrorMutex);
    Errors.push_back(error);
}

void EtwFrameListener::pushError(EtwListenerErrorCode code, const std::string& desc)
{
    std::lock_guard<std::mutex> lock(ErrorMutex);
    Errors.emplace_back(code, desc);
}

void EtwFrameListener::pushError(EtwListenerErrorCode code, const std::string& prefix, uint32_t systemCode)
{
    // This code does not work because EtwFrameListener is built inside of the
    // kernel and does not have access to OVR_Error.h/cpp.
    // std::string errorDesc = prefix;
    // OVR::String systemError;
    // GetSysErrorCodeString(systemCode, false, systemError);
    // errorDesc += systemError.ToCStr();

    std::string errorDesc = prefix + std::to_string(systemCode);

    pushError(code, errorDesc);
}

//-----------------------------------------------------------------------------
// Realtime ETW Tracing
//-----------------------------------------------------------------------------

#pragma comment(lib, "tdh.lib")

static const size_t   LoggerNameSize = 1024;
static const int32_t  MAX_PROCESS_TRACE_HANDLES = 64;

struct ProviderDesc
{
    GUID        guid;
    std::string name;
};

// TODO Only retrieve the OVR SDK provider.
static EtwListenerError findOVRProviders(std::vector<ProviderDesc>& providers)
{
    PROVIDER_ENUMERATION_INFO* penum = nullptr;
    DWORD penumSize = 0;

    DWORD status = TdhEnumerateProviders(penum, &penumSize);

    auto cleanup = [&penum]() {
        if (penum)
        {
            free(penum);
            penum = nullptr;
        }
    };

    // Allocate the required buffer and call TdhEnumerateProviders. The list of
    // providers can change between the time you retrieved the required buffer
    // size and the time you enumerated the providers, so call TdhEnumerateProviders
    // in a loop until the function does not return ERROR_INSUFFICIENT_BUFFER.
    while (status == ERROR_INSUFFICIENT_BUFFER)
    {
        PROVIDER_ENUMERATION_INFO *ptemp = nullptr;
        ptemp = (PROVIDER_ENUMERATION_INFO *)realloc(penum, penumSize);
        if (ptemp == nullptr)
        {
            std::string desc = "Allocation failed for " + std::to_string(penumSize) + "bytes";
            cleanup();
            return EtwListenerError(ovrError_EtwListenerRuntime, desc);
        }

        penum = ptemp;
        ptemp = nullptr;

        status = TdhEnumerateProviders(penum, &penumSize);
    }

    if (status != ERROR_SUCCESS)
    {
        // Want 'GetSysErrorCodeString'
        std::string desc = "Failed to enumerate providers: " + std::to_string(status);
        cleanup();
        return EtwListenerError(ovrError_EtwListenerRuntime, desc);
    }

    for (DWORD i = 0; i < penum->NumberOfProviders; ++i)
    {
        // Per the documentation, ProviderNameOffset is an 'Offset to a
        // null-terminated Unicode string'.
        ProviderDesc desc;
        desc.guid = penum->TraceProviderInfoArray[i].ProviderGuid;

        const wchar_t* providerName = 
          (const wchar_t*)((PBYTE)(penum)+penum->TraceProviderInfoArray[i].ProviderNameOffset);
        const int bufferSize = 1024;
        char multibyteBuffer[bufferSize];
        int count = WideCharToMultiByte(CP_UTF8, 0, providerName, -1,
            multibyteBuffer, bufferSize, nullptr, nullptr);
        if (count > 0)
        {
            desc.name = multibyteBuffer;

            const std::string ovrPrefix = "OVR-";
            // Test whether this is an oculus provider.
            if (desc.name.length() > ovrPrefix.length())
            {
                if (std::equal(ovrPrefix.begin(), ovrPrefix.end(), desc.name.begin()))
                {
                    providers.push_back(desc);
                }
            }
        }
    }

    cleanup();
    return EtwListenerError();
}

static EtwListenerError enableProvider(const ProviderDesc& desc, TRACEHANDLE controllerHandle)
{
    // CONSIDER: Filter by specific provider. We only care about SDK events currently (jhughes).
    // CONSIDER: Add Id filters (this may obviate filtering by provider) (jhughes).

    // EVENT_CONTROL_CODE_DISABLE_PROVIDER -- Don't forsee using this.
    ULONG     ControlCode     = EVENT_CONTROL_CODE_ENABLE_PROVIDER;
    UCHAR     Level           = TRACE_LEVEL_INFORMATION;
    ULONGLONG MatchAnyKeyword = ULONGLONG(-1);
    ULONGLONG MatchAllKeyword = 0;
    ULONG     Timeout         = 0;
    ENABLE_TRACE_PARAMETERS EnableParameters;
    ZeroMemory(&EnableParameters, sizeof(EnableParameters));
    EnableParameters.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;

    typedef struct {
        EVENT_FILTER_EVENT_ID fid;
        USHORT ids[MAX_EVENT_FILTER_EVENT_ID_COUNT];
    } FilterEventIds;

    const int maxFilterDescs = 3;
    int       numFilterDescs = 0;
    EVENT_FILTER_DESCRIPTOR EnableFilterDesc[maxFilterDescs];
    FilterEventIds EventFilterById;
    FilterEventIds EventStackwalkById;
    DWORD FilterByPID[MAX_EVENT_FILTER_PID_COUNT];

    ZeroMemory(&EnableFilterDesc,   sizeof(EnableFilterDesc));
    ZeroMemory(&EventFilterById,    sizeof(EventFilterById));
    ZeroMemory(&EventStackwalkById, sizeof(EventStackwalkById));
    ZeroMemory(&FilterByPID,        sizeof(FilterByPID));

    if (numFilterDescs > 0)
    {
        EnableParameters.FilterDescCount = numFilterDescs;
        EnableParameters.EnableFilterDesc = &EnableFilterDesc[0];
    }

    ULONG status = ::EnableTraceEx2(
        controllerHandle,
        &desc.guid,
        ControlCode,
        Level,
        MatchAnyKeyword,
        MatchAllKeyword,
        Timeout,
        &EnableParameters
    );

    if (status != ERROR_SUCCESS)
    {
        // Want 'GetSysErrorCodeString'
        std::string desc = "EnableTrace failed: " + std::to_string(status);
        return EtwListenerError(ovrError_EtwListenerRuntime, desc);
    }

    return EtwListenerError();
}

// Called when a buffer of events were delivered by ProcessTrace
static ULONG WINAPI BufferCallback(_In_ PEVENT_TRACE_LOGFILEW pLog)
{
    EtwFrameListener* ctx = reinterpret_cast<EtwFrameListener*>(pLog->Context);
    OVR_UNUSED(ctx);

    return 1; // Return 0 if we should stop processing.
}

void EtwFrameListener::traceThread(EtwFrameListener* This, TRACEHANDLE consumerHandle)
{
    FILETIME startTime, endTime;
    startTime.dwLowDateTime  = 0;
    startTime.dwHighDateTime = 0;
    endTime.dwLowDateTime  = 0xffffffff;
    endTime.dwHighDateTime = 0x7fffffff;

    ULONG ret = ::ProcessTrace(&consumerHandle, 1, &startTime, &endTime);
    if (ret != ERROR_SUCCESS)
    {
        This->pushError(ovrError_EtwListenerRuntime, "ProcessTrace failed: ", ret);
    }

    This->ProcessThreadTerminated.store(true);
    ::CloseTrace(consumerHandle);
}

bool EtwFrameListener::startRealtimeTracing()
{
    // CONSIDER: Maching keywords (MatchAnyKeyword, MatchAllKeyword) (jhughes).

    OVR_ASSERT(ControllerHandle == 0);

    std::wstring wsessionName = OVR::UTF8StringToUCSString(EtwSessionName.c_str());

    //-----------------------
    // INITIALIZE CONTROLLER
    //-----------------------

    // Allocate EVENT_TRACE_PROPERTIES struct for ETW tracing.
    size_t size = sizeof(EVENT_TRACE_PROPERTIES) + LoggerNameSize;
    ControllerProperties = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(new char[size]);

    ZeroMemory(ControllerProperties, size);
    ControllerProperties->BufferSize = 16;                     // default to 16k buffers
    ControllerProperties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    ControllerProperties->Wnode.BufferSize = ULONG(size);
    ControllerProperties->Wnode.ClientContext = 1;              // Use QueryPerformanceCounter() for everything
    ControllerProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    ControllerProperties->LogFileNameOffset = 0;
    
    ControllerProperties->LogFileMode |= EVENT_TRACE_REAL_TIME_MODE;

    ULONG status = ::StartTraceW(&ControllerHandle, wsessionName.c_str(), ControllerProperties);
    if (status == ERROR_ALREADY_EXISTS)
    {
        // Stop pre-existing trace and try once more.
        ::ControlTraceW(NULL, wsessionName.c_str(), ControllerProperties, EVENT_TRACE_CONTROL_STOP);
        status = ::StartTraceW(&ControllerHandle, wsessionName.c_str(), ControllerProperties);
    }
    if (status != ERROR_SUCCESS)
    {
        pushError(ovrError_EtwListenerInitialize, "Failed to start tracing: ", status);
        return false;
    }

    // Add all LibOVR providers.
    std::vector<ProviderDesc> ovrProviders;
    EtwListenerError err = findOVRProviders(ovrProviders);
    if (!err.Succeeded())
    {
        pushError(err);
        return false;
    }

    for (ProviderDesc& provider : ovrProviders)
    {
        EtwListenerError err = enableProvider(provider, ControllerHandle);
        if (!err.Succeeded())
        {
            pushError(err);
        }
    }

    //---------------------
    // INITIALIZE CONSUMER
    //---------------------

    EVENT_TRACE_LOGFILEW trace;
    ZeroMemory(&trace, sizeof(EVENT_TRACE_LOGFILEW));

    trace.LoggerName          = LPWSTR(wsessionName.c_str());
    trace.ProcessTraceMode    = PROCESS_TRACE_MODE_EVENT_RECORD | EVENT_TRACE_REAL_TIME_MODE | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    trace.BufferCallback      = BufferCallback;
    trace.EventRecordCallback = ProcessEvent;
    trace.Context             = this;

    TRACEHANDLE consumerHandle = ::OpenTraceW(&trace);
    if (INVALID_PROCESSTRACE_HANDLE == consumerHandle)
    {
        pushError(ovrError_EtwListenerInitialize, "Consumer failed to OpenTrace");
        return false;
    }

    ProcessThreadTerminated.store(false);
    ConsumerTracingThread = std::thread(traceThread, this, consumerHandle);

    return true;
}

bool EtwFrameListener::stopRealtimeTracing()
{
    OVR_ASSERT(ControllerHandle != 0);

    ULONG status = ::ControlTraceW(ControllerHandle, NULL, ControllerProperties,
                                  EVENT_TRACE_CONTROL_STOP);
    if (status == ERROR_SUCCESS)
    {
        if (ConsumerTracingThread.joinable())
        {
            ConsumerTracingThread.join();
        }
        ControllerHandle = 0;
        delete[] ControllerProperties;
        ControllerProperties = nullptr;
        return true;
    }
    else
    {
        return false;
    }
}


} // namespace Etw
} // namespace OVR
