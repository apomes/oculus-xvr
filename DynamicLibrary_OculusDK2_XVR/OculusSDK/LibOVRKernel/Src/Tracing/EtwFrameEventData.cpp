
#include "EtwFrameEventData.h"
#include "Kernel/OVR_Types.h"
#include "Kernel/OVR_Log.h"

namespace OVR { namespace Etw {

//------------------------------------------------------------------------------
// Deserialization without manifest
//------------------------------------------------------------------------------

EtwProviders etwLookupProviderId(GUID provider)
{
    if (provider == LibOVRProvider)
    {
        return EtwProviders::Sdk;
    }

    return EtwProviders::Unknown;
}

// Simple helper class for ETW event registration.
template <class T>
class RegisterStruct
{
public:
    RegisterStruct(EtwEventDataConv* conv)
    {
        conv->registerStruct(T::Provider, T::EventId, sizeof(T), T::Version);
    }
};

EtwEventDataConv::EtwEventDataConv()
{
    // There's definitely a better way to do this.
    RegisterStruct<EtwData_DistortionBegin>(this);
    RegisterStruct<EtwData_DistortionEnd>(this);
    RegisterStruct<EtwData_PoseLatchCpuWrite>(this);
    RegisterStruct<EtwData_PoseLatchGpuReadback>(this);
    RegisterStruct<EtwData_VSync>(this);
    RegisterStruct<EtwData_AppCompositorFocus>(this);
    RegisterStruct<EtwData_AppConnect>(this);
    RegisterStruct<EtwData_AppDisconnect>(this);
    RegisterStruct<EtwData_AppNoOp>(this);
    RegisterStruct<EtwData_LatencyTiming>(this);
    RegisterStruct<EtwData_EndFrameAppTiming>(this);

    LARGE_INTEGER freq;
    ::QueryPerformanceFrequency(&freq);
    PerfFrequency        = freq.QuadPart;
    PerfFrequencyInverse = 1.0 / (double)PerfFrequency;
}

void EtwEventDataConv::registerStruct(EtwProviders provider, UINT eventId, size_t structSize, int version)
{
    EventToDeserializeMap.insert(std::make_pair(
        EtwEventKey(provider, eventId),
        EtwStructInfo(structSize, version)));
}

bool EtwEventDataConv::Deserialize(PEVENT_RECORD event, EtwEvent& buffer)
{
    EtwProviders provider = etwLookupProviderId(event->EventHeader.ProviderId);
    UINT         eventId  = event->EventHeader.EventDescriptor.Id;

    // Find appropriate deserialization function for event.
    auto deserializeDataIt = EventToDeserializeMap.find(EtwEventKey(provider, eventId));
    if (deserializeDataIt == EventToDeserializeMap.end())
    {
        // Ignore the event, we have no way of deserializing it.
        return false;
    }

    buffer.ProviderId = provider;
    buffer.EventId    = eventId;

    EtwStructInfo deserializeInfo = deserializeDataIt->second;

    if (deserializeInfo.Version != event->EventHeader.EventDescriptor.Version)
    {
        LogError("[EtwEventDataConv] Mismatched deserialization structure version.");
        return false;
    }

    if (deserializeInfo.StructSize != event->UserDataLength)
    {
        LogError("[EtwEventDataConv] Unable to deserialize event. Sizes don't match.");
        return false;
    }

    if (deserializeInfo.StructSize > EtwEvent::MaxUserDataSize)
    {
        LogError("[EtwEventDataConv] Increase EtwEvent::MaxUserDataSize");
        return false;
    }

    static const LONGLONG WIN_TIMESTAMP_SEC_SCALE = 10000000LL; // 100ns resolution -> seconds
    static const LONGLONG WIN_TIMESTAMP_NS_SCALE = 100LL;       // windows has 100ns resolution
    const LONGLONG timestamp = event->EventHeader.TimeStamp.QuadPart;
    LONGLONG s = timestamp / WIN_TIMESTAMP_SEC_SCALE;
    LONGLONG ns = WIN_TIMESTAMP_NS_SCALE * (timestamp - (s * WIN_TIMESTAMP_SEC_SCALE));

    double seconds = static_cast<double>(s) + (static_cast<double>(ns) * 1.0e-9);
    seconds = static_cast<double>(event->EventHeader.TimeStamp.QuadPart) * PerfFrequencyInverse;
    buffer.EventTime = seconds;

    SYSTEMTIME  st;
    SYSTEMTIME  stLocal;
    FILETIME    ft;
    ULONGLONG   ts;

    ft.dwHighDateTime = event->EventHeader.TimeStamp.HighPart;
    ft.dwLowDateTime  = event->EventHeader.TimeStamp.LowPart;

    FileTimeToSystemTime(&ft, &st);
    SystemTimeToTzSpecificLocalTime(NULL, &st, &stLocal);

    ts = event->EventHeader.TimeStamp.QuadPart;

    buffer.UserDataSize = event->UserDataLength;
    memcpy(buffer.UserData, event->UserData, buffer.UserDataSize);

    return true;
}

//------------------------------------------------------------------------------
// EtwEvent Implementation
//------------------------------------------------------------------------------

EtwEvent::EtwEvent()
      : UserDataSize(0)
      , ProviderId(EtwProviders::Unknown)
      , EventId(0)
      , EventTime(0.0)
{ }

bool EtwEvent::IsValid()
{
    return ProviderId != EtwProviders::Unknown;
}

} // namespace Etw
} // namespace OVR
