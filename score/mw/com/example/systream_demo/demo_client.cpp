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

// =============================================================================
//  DEMO CLIENT -- Systream S-Core COM Getting-Started Demo
//  Role: Proxy (Consumer / Subscriber)
//
//  Build:
//    bazel build //score/mw/com/example/systream_demo:demo_client
//
//  Run (in a second terminal, after demo_server is running):
//    ./bazel-bin/score/mw/com/example/systream_demo/demo_client
//      --service_instance_manifest
//      score/mw/com/example/systream_demo/etc/mw_com_config.json
//      --num-samples 20 --poll-interval 100
// =============================================================================

#include "demo_types.h"

// Runtime init: parses mw_com_config.json and sets up LoLa infrastructure
#include "score/mw/com/runtime.h"

// mw::log: structured logging
#include "score/mw/log/logging.h"

#include <boost/program_options.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
namespace po = boost::program_options;

// ---------------------------------------------------------------------------
//  main()
// ---------------------------------------------------------------------------
int main(const int argc, const char** argv)
{
    // -- Parse command-line arguments -----------------------------------------
    po::options_description opts("Demo Client (Proxy) Options");
    // clang-format off
    opts.add_options()
        ("help,h",                                         "Show this help message")
        ("service_instance_manifest,s",
            po::value<std::string>()->required(),          "Path to mw_com_config.json")
        ("num-samples,n",
            po::value<std::size_t>()->default_value(20),   "Number of samples to receive (0 = infinite)")
        ("poll-interval,t",
            po::value<std::size_t>()->default_value(100),  "Polling interval in milliseconds");
    // clang-format on

    po::variables_map args;
    po::store(po::parse_command_line(argc, argv, opts), args);

    if (args.count("help") > 0U)
    {
        std::cout << opts << "\n";
        return EXIT_SUCCESS;
    }
    po::notify(args);

    const std::string manifest_path = args["service_instance_manifest"].as<std::string>();
    const std::size_t num_samples   = args["num-samples"].as<std::size_t>();
    const std::size_t poll_ms       = args["poll-interval"].as<std::size_t>();
    const std::chrono::milliseconds poll_interval{poll_ms};

    // -- Step 1: Initialize the LoLa runtime ----------------------------------
    //
    //  InitializeRuntime() parses mw_com_config.json and stores the result in
    //  a process-wide Configuration singleton.  Call exactly once at startup.
    //  The key "-service_instance_manifest" uses a single dash (LoLa format).
    {
        score::StringLiteral runtime_args[2U] = {"-service_instance_manifest", manifest_path.c_str()};
        score::mw::com::runtime::InitializeRuntime(2, runtime_args);
    }
    score::mw::log::LogInfo("demo") << "Runtime initialized from: " << manifest_path;

    // -- Step 2: Create the InstanceSpecifier ---------------------------------
    //
    //  The string MUST match "instanceSpecifier" in mw_com_config.json exactly
    //  (case-sensitive).
    const auto specifier_result =
        score::mw::com::InstanceSpecifier::Create(std::string{"systream/demo/SensorService"});
    if (!specifier_result.has_value())
    {
        std::cerr << "[demo_client] Invalid InstanceSpecifier. "
                     "Check mw_com_config.json instanceSpecifier field.\n";
        return EXIT_FAILURE;
    }
    const auto& specifier = specifier_result.value();

    // -- Step 3: Discover the service -----------------------------------------
    //
    //  FindService() reads the service discovery directory written by the
    //  Skeleton's OfferService().  Returns a ServiceHandleContainer; if empty
    //  the server is not up yet -- we retry every 500 ms.
    score::mw::com::ServiceHandleContainer<score::mw::com::HandleType> handles;
    std::cout << "[demo_client] Searching for SensorService...\n";
    do
    {
        auto result = SensorProxy::FindService(specifier);
        if (!result.has_value())
        {
            std::cerr << "[demo_client] FindService() error: " << result.error() << "\n";
            return EXIT_FAILURE;
        }
        handles = std::move(result).value();
        if (handles.empty())
        {
            std::cout << "[demo_client] Not found yet, retrying in 500 ms...\n";
            std::this_thread::sleep_for(500ms);
        }
    } while (handles.empty());
    std::cout << "[demo_client] SensorService found!\n";

    // -- Step 4: Instantiate the Proxy ----------------------------------------
    //
    //  SensorProxy::Create() opens the SHM regions in read-only mode (mmap)
    //  and returns a proxy object that holds the file descriptors open.
    auto proxy_result = SensorProxy::Create(std::move(handles.front()));
    if (!proxy_result.has_value())
    {
        std::cerr << "[demo_client] Failed to create proxy: " << proxy_result.error() << "\n";
        return EXIT_FAILURE;
    }
    auto& proxy = proxy_result.value();
    score::mw::log::LogInfo("demo") << "SensorProxy created -- SHM region mapped read-only";

    // -- Step 5: Subscribe ----------------------------------------------------
    //
    //  Subscribe(maxSampleCount) registers this process as a subscriber in the
    //  control SHM region.  maxSampleCount is the local cache: how many
    //  SamplePtr<SensorData> objects the Proxy holds before the oldest is
    //  dropped.  Set it >= numberOfSampleSlots in the config for lossless recv.
    constexpr std::size_t kMaxSampleCount = 5U;
    const auto subscribe_result = proxy.sensor_reading_.Subscribe(kMaxSampleCount);
    if (!subscribe_result.has_value())
    {
        std::cerr << "[demo_client] Subscribe() failed: " << subscribe_result.error() << "\n";
        return EXIT_FAILURE;
    }
    score::mw::log::LogInfo("demo") << "Subscribed to sensor_reading";
    std::cout << "\n[demo_client] Subscribed. Polling every " << poll_ms
              << " ms for sensor data...\n\n";

    // -- Step 6: Receive loop (polling) ---------------------------------------
    //
    //  Pattern taken directly from ipc_bridge (sample_sender_receiver.cpp):
    //    - Sleep for poll_interval
    //    - Call GetNewSamples() to drain all new samples from the ring buffer
    //    - Process each sample inside the lambda
    //    - Count total received; stop when num_samples is reached
    //
    //  GetNewSamples() is always non-blocking.  If no new data has arrived
    //  since the last call it returns 0 without invoking the lambda.
    //
    //  SamplePtr<SensorData> is a smart pointer directly into the SHM ring
    //  buffer slot.  The slot is locked (unavailable to the Skeleton for reuse)
    //  for as long as the SamplePtr lives -- let it go out of scope quickly.
    std::size_t  total_received = 0U;
    std::uint32_t last_seq      = 0U;
    std::size_t  missed_samples = 0U;
    bool         first_sample   = true;

    while ((num_samples == 0U) || (total_received < num_samples))
    {
        // Sleep first -- identical to ipc_bridge cycle_time path
        std::this_thread::sleep_for(poll_interval);

        // Drain all newly arrived samples (up to kMaxSampleCount per call)
        const auto result = proxy.sensor_reading_.GetNewSamples(
            [&](score::mw::com::SamplePtr<SensorData> sample) noexcept {
                const SensorData& data = *sample;

                // Detect sequence number gaps (= missed samples)
                if (!first_sample && data.sequence_number > last_seq + 1U)
                {
                    missed_samples += data.sequence_number - last_seq - 1U;
                }
                last_seq     = data.sequence_number;
                first_sample = false;

                // Measure one-way SHM latency
                using namespace std::chrono;
                const auto now_ms = static_cast<std::uint64_t>(
                    duration_cast<milliseconds>(
                        steady_clock::now().time_since_epoch()).count());
                const std::uint64_t latency_ms =
                    (now_ms > data.timestamp_ms) ? (now_ms - data.timestamp_ms) : 0U;

                std::cout << "[demo_client] Received #" << data.sequence_number
                          << "  temp=" << data.temperature_celsius << " C"
                          << "  pres=" << data.pressure_hpa << " hPa"
                          << "  latency=" << latency_ms << " ms\n";

                ++total_received;
                // SamplePtr released here -- slot returned to ring buffer
            },
            kMaxSampleCount);

        if (!result.has_value())
        {
            std::cerr << "[demo_client] GetNewSamples() error: " << result.error() << "\n";
            break;
        }
    }

    // -- Step 7: Summary and clean shutdown -----------------------------------
    std::cout << "\n[demo_client] --- Reception Summary ---\n"
              << "  Total received : " << total_received << "\n"
              << "  Missed samples : " << missed_samples << "\n";

    if (total_received + missed_samples > 0U)
    {
        const double loss_pct =
            100.0 * static_cast<double>(missed_samples) /
            static_cast<double>(total_received + missed_samples);
        std::cout << "  Loss rate      : " << loss_pct << " %\n";
    }

    // Unsubscribe() removes this process from the subscriber list in the
    // control SHM.  Always call before the proxy is destroyed.
    proxy.sensor_reading_.Unsubscribe();
    score::mw::log::LogInfo("demo") << "Unsubscribed. Demo client finished.";

    return EXIT_SUCCESS;
}
