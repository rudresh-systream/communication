/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/
#ifndef SCORE_MW_COM_SYSTREAM_DEMO_DEMO_TYPES_H
#define SCORE_MW_COM_SYSTREAM_DEMO_DEMO_TYPES_H

// score/mw/com/types.h provides:
//   - AsSkeleton<>  / AsProxy<>   template helpers
//   - SampleAllocateePtr<T>       zero-copy send handle
//   - SamplePtr<T>                zero-copy receive handle
#include "score/mw/com/types.h"

// ---------------------------------------------------------------------------
//  SensorData — the data type exchanged over shared memory
//
//  Rules for types used with LoLa:
//    - Must be trivially copyable (plain struct / POD)
//    - No heap allocation inside the struct (no std::string, std::vector, …)
//    - Fixed size at compile time
// ---------------------------------------------------------------------------
struct SensorData
{
    // Monotonically increasing counter — lets the client detect missed samples
    std::uint32_t sequence_number;

    // Simulated sensor readings (°C and hPa)
    float temperature_celsius;
    float pressure_hpa;

    // CPU timestamp in milliseconds since process start — latency measurement
    std::uint64_t timestamp_ms;
};

// ---------------------------------------------------------------------------
//  Service interface definition
//
//  The template trick below is the standard S-Core COM pattern for declaring
//  a service with events.  The same class is used for both Skeleton and Proxy
//  by switching the Trait (AsSkeleton / AsProxy) — identical to how the
//  existing ipc_bridge example works.
//
//  Each member "typename Trait::template Event<DataType>" becomes:
//    - SkeletonEvent<SensorData>  when Trait = AsSkeleton<SensorInterface>
//    - ProxyEvent<SensorData>     when Trait = AsProxy<SensorInterface>
// ---------------------------------------------------------------------------
template <typename Trait>
class SensorInterface : public Trait::Base
{
  public:
    using Trait::Base::Base;  // inherit all constructors from SkeletonBase/ProxyBase

    // Event name MUST match the "eventName" field in mw_com_config.json exactly
    typename Trait::template Event<SensorData> sensor_reading_{*this, "sensor_reading"};
};

// Convenience type aliases — use these in your application code
using SensorSkeleton = score::mw::com::AsSkeleton<SensorInterface>;
using SensorProxy    = score::mw::com::AsProxy<SensorInterface>;

#endif  // SCORE_MW_COM_SYSTREAM_DEMO_DEMO_TYPES_H
