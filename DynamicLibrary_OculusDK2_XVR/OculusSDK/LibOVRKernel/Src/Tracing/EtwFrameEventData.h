///
/// \brief Contains structures analagous to Etw Events. Also contains structures
///        representing the user-data in these events.
///

#ifndef OVR_EtwFrameEventData_h
#define OVR_EtwFrameEventData_h

__pragma(warning(push))
__pragma(warning(disable:4530))
#include <map>
#include <functional>
#include <vector>
#include <stdint.h>
__pragma(warning(pop))

#include <Windows.h>
#include <tdh.h>

#include "Kernel/OVR_Types.h" // Needs to be included before Tracing.h for OVR_OS_WIN32
#include "Tracing/Tracing.h"

namespace OVR { namespace Etw {

//------------------------------------------------------------------------------
// Common Structures and Enumerations
//------------------------------------------------------------------------------

/// All known EtwProviders. All other provider events are ignored.
enum class EtwProviders
{
    Sdk,          ///< Corresponds to OVR-SDK-LibOVR (LibOVREvents.man)
    Unknown,      ///< Unknown provider.
};

/// Event structure deserialized from ETW events. Use in conjunction with
/// EtwEventDataConv's Deserialize function to retrieve event user data in
/// a type safe manner (the 'UserData' member variable of this structure).
struct EtwEvent
{
    EtwEvent();
    bool IsValid();

    static const int MaxUserDataSize = 96;

    uint8_t      UserData[MaxUserDataSize]; ///< Generic user data from the event.
                                            ///< Should be first member of structure.
    int32_t      UserDataSize;              ///< Size of 'userData' in bytes.
    EtwProviders ProviderId;                ///< Internal provider ID. Set based on GUID.
    int32_t      EventId;                   ///< Event ID bound with provider.
    double       EventTime;                 ///< Time when event occurred.
};


//------------------------------------------------------------------------------
// ETW user data structures for easy access
//------------------------------------------------------------------------------

// NOTE: The values for 'EventId' originate from LibOVREvents.h.
//       E.G. DistortionBegin_value is defined in the auto-generated
//       LibOVREvents.h file.

#pragma pack(push, 1)
struct EtwData_DistortionBegin
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = DistortionBegin_value;
    static const int32_t      Version   = 0;

    uint32_t Id;
    uint32_t FrameIndex;
};

struct EtwData_DistortionEnd
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = DistortionEnd_value;
    static const int32_t      Version = 0;

    uint32_t Id;
    uint32_t FrameIndex;
};

struct EtwData_PoseLatchCpuWrite
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = PoseLatchCPUWrite_value;
    static const int32_t      Version = 0;

    uint32_t Sequence;
    int32_t  Layer;
    float    MotionSensorTime;
    float    PredictedScanlineFirst;
    float    PredictedScanlineLast;
    float    TimeToScanlineFirst;
    float    TimeToScanlineLast;
    float    StartPosition[3];
    float    EndPosition[3];
    float    StartQuat[4];
    float    EndQuat[4];
};

struct EtwData_PoseLatchGpuReadback
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = PoseLatchGPULatchReadback_value;
    static const int32_t      Version = 0;

    uint32_t Sequence;
    int32_t  Layer;
    float    MotionSensorTime;
    float    PredictedScanlineFirst;
    float    PredictedScanlineLast;
    float    TimeToScanlineFirst;
    float    TimeToScanlineLast;
};

struct EtwData_VSync
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = VSync_value;
    static const int32_t      Version   = 0;

    double    VSyncTime;
    uint32_t  FrameIndex;
    double    TwGpuEndTime;
};

struct EtwData_AppCompositorFocus
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = AppCompositorFocus_value;
    static const int32_t      Version   = 0;

    uint64_t    Pid;
};

struct EtwData_AppConnect
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = AppConnect_value;
    static const int32_t      Version   = 0;

    uint64_t    Pid;
};

struct EtwData_AppDisconnect
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = AppDisconnect_value;
    static const int32_t      Version   = 0;

    uint64_t    Pid;
};

struct EtwData_AppNoOp
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = AppNoOp_value;
    static const int32_t      Version   = 0;

    uint64_t    Pid;
};

struct EtwData_LatencyTiming
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = LatencyTiming_value;
    static const int32_t      Version   = 0;

    double LatencyRenderCpuBegin;
    double LatencyRenderCpuEnd;
    double LatencyRenderIMU;
    double LatencyTimewarpCpu;
    double LatencyTimewarpLatched;
    double LatencyTimewarpGpuEnd;
    double LatencyPostPresent;
    double ErrorRender;
    double ErrorTimewarp;
};

struct EtwData_EndFrameAppTiming
{
    static const EtwProviders Provider  = EtwProviders::Sdk;
    static const int32_t      EventId   = EndFrameAppTiming_value;
    static const int32_t      Version   = 0;

    uint32_t AppFrameIndex;
    double AppRenderIMUTime;
    double AppScanoutStartTime;
    double AppGpuRenderDuration;
    double AppBeginRenderingTime;
    double AppEndRenderingTime;
    double QueueAheadSeconds;
    double DistortionGpuDuration;
};
#pragma pack(pop)

//------------------------------------------------------------------------------
// Event Deserialization
//------------------------------------------------------------------------------

struct EtwEventKey
{
    EtwEventKey() {}

    EtwEventKey(EtwProviders provider, UINT id)
      : Provider(provider)
      , Id(id)
    {}

    EtwProviders  Provider;
    UINT          Id;

    // LT operator for ordered insertion into a map.
    bool operator<(const struct EtwEventKey &b) const
    {
        if (this->Provider < b.Provider)
        {
            return true;
        }
        else if (this->Provider > b.Provider)
        {
            return false;
        }
        else
        {
            return this->Id < b.Id;
        }
    }
};

class EtwEventDataConv
{
public:
    EtwEventDataConv();

    /// Deserializes the event given provider and eventId. If the provider and
    /// event Id haven't been registered, then userDataSize is set to 0 and
    /// this function returns false.
    ///
    /// \param[in]  event      Raw ETW event.
    /// \param[out] buffer     Result of deserializing 'event'.
    ///
    /// \return false if event failed deserialization.
    bool Deserialize(PEVENT_RECORD event, EtwEvent& buffer);

    /// Used by the source file to register events for deserialization.
    void registerStruct(EtwProviders provider, UINT eventId, size_t structSize, int version);

private:

    struct EtwStructInfo
    {
        EtwStructInfo() : StructSize(0), Version(-1) {}
        EtwStructInfo(size_t structSize, int version)
          : StructSize(structSize)
          , Version(version)
        { }

        size_t  StructSize;
        int     Version;
    };

    uint64_t PerfFrequency;
    double   PerfFrequencyInverse;

    std::map<EtwEventKey, EtwStructInfo> EventToDeserializeMap;
};

} // namespace Etw
} // namespace OVR

#endif // OVR_EtwFrameEventData_h
