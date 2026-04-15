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
//  DEMO SERVER — Systream S-Core COM Getting-Started Demo
//  Role: Skeleton (Provider / Publisher)
//
//  This process:
//    1. Reads the mw_com_config.json to find the SHM deployment parameters
//    2. Creates a SensorSkeleton and allocates shared memory
//    3. Publishes SensorData every --cycle-time milliseconds
//    4. Runs for --num-cycles cycles (0 = run forever) then exits cleanly
//
//  Build:
//    bazel build //score/mw/com/example/systream_demo:demo_server
//
//  Run:
//    ./bazel-bin/score/mw/com/example/systream_demo/demo_server \
//      --service_instance_manifest \
//      score/mw/com/example/systream_demo/etc/mw_com_config.json \
//      --cycle-time 500 \
//      --num-cycles 20
// =============================================================================

#include "demo_types.h"

// Runtime init: parses mw_com_config.json and sets up LoLa infrastructure
#include "score/mw/com/runtime.h"

// mw::log: structured logging (replaces printf in production code)
#include "score/mw/log/logging.h"

#include <boost/program_options.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
namespace po = boost::program_options;

// ---------------------------------------------------------------------------
//  SimulateSensor()
//  Fills a SensorData sample with plausible-looking values.
//  In real code this is where you'd read from hardware registers / drivers.
// ---------------------------------------------------------------------------
static void SimulateSensor(SensorData& data, std::uint32_t cycle)
{
    // Monotonic sequence number — the client uses this to count missed samples
    data.sequence_number = cycle;

    // Simulate temperature oscillating around 25 °C
    data.temperature_celsius = 25.0f + 5.0f * std::sin(static_cast<float>(cycle) * 0.1f);

    // Simulate pressure oscillating around 1013 hPa
    data.pressure_hpa = 1013.0f + 10.0f * std::cos(static_cast<float>(cycle) * 0.05f);

    // Capture a wall-clock timestamp for latency measurement on the client side
    using namespace std::chrono;
    data.timestamp_ms = static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
//  main()
// ---------------------------------------------------------------------------
int main(const int argc, const char** argv)
{
    // ── Parse command-line arguments ─────────────────────────────────────────
    po::options_description opts("Demo Server (Skeleton) Options");
    // clang-format off
    opts.add_options()
        ("help,h",                                      "Show this help message")
        ("service_instance_manifest,s",
            po::value<std::string>()->required(),       "Path to mw_com_config.json")
        ("cycle-time,t",
            po::value<std::size_t>()->default_value(500),
                                                        "Publishing interval in milliseconds")
        ("num-cycles,n",
            po::value<std::size_t>()->default_value(20),"Number of samples to send (0 = infinite)");
    // clang-format on

    po::variables_map args;
    po::store(po::parse_command_line(argc, argv, opts), args);

    if (args.count("help") > 0U)
    {
        std::cout << opts << "\n";
        return EXIT_SUCCESS;
    }
    po::notify(args);  // throws if required options are missing

    const std::string manifest_path = args["service_instance_manifest"].as<std::string>();
    const std::size_t cycle_time_ms = args["cycle-time"].as<std::size_t>();
    const std::size_t num_cycles    = args["num-cycles"].as<std::size_t>();
    const std::chrono::milliseconds cycle_time{cycle_time_ms};

    // ── Step 1: Initialize the LoLa runtime ──────────────────────────────────
    //
    //  InitializeRuntime() does three things:
    //    a) Opens and parses mw_com_config.json with nlohmann_json
    //    b) Validates the JSON against mw_com_config_schema.json (fatal on error)
    //    c) Sets up the Configuration singleton used by all subsequent calls
    //
    //  The arguments must be passed in this exact format (StringLiteral array).
    //  The key is "-service_instance_manifest" (single dash, not double dash).
    {
        score::StringLiteral runtime_args[2U] = {"-service_instance_manifest", manifest_path.c_str()};
        score::mw::com::runtime::InitializeRuntime(2, runtime_args);
    }
    score::mw::log::LogInfo("demo") << "Runtime initialized from: " << manifest_path;

    // ── Step 2: Create the Skeleton ───────────────────────────────────────────
    //
    //  SensorSkeleton::Create() resolves the InstanceSpecifier string
    //  "systream/demo/SensorService" (defined in mw_com_config.json) to a
    //  concrete InstanceIdentifier, then:
    //    - Creates a SkeletonBinding for the LoLa (SHM) backend
    //    - Calls the SensorInterface constructor which registers sensor_reading_
    //      with the SkeletonBase via SkeletonBaseView::RegisterEvent()
    //
    //  Create() returns a score::Result — always check it before proceeding.
    const auto specifier_result =
        score::mw::com::InstanceSpecifier::Create(std::string{"systream/demo/SensorService"});
    if (!specifier_result.has_value())
    {
        std::cerr << "[demo_server] Invalid InstanceSpecifier. "
                     "Check mw_com_config.json instanceSpecifier field.\n";
        return EXIT_FAILURE;
    }
    const auto& specifier = specifier_result.value();

    auto skeleton_result = SensorSkeleton::Create(specifier);
    if (!skeleton_result.has_value())
    {
        std::cerr << "[demo_server] Failed to create skeleton: " << skeleton_result.error() << "\n"
                  << "  Check: serviceTypeName and instanceSpecifier in mw_com_config.json\n";
        return EXIT_FAILURE;
    }
    auto& skeleton = skeleton_result.value();
    score::mw::log::LogInfo("demo") << "SensorSkeleton created successfully";

    // ── Step 3: Offer the service ─────────────────────────────────────────────
    //
    //  OfferService() does:
    //    a) Opens (creates) the shared memory region in /dev/shm/score_lola_*
    //    b) Creates the control SHM region (subscriber slot tracking)
    //    c) Registers the service in the service discovery directory
    //       (/tmp/score_lola_service_discovery/) so Proxies can find it
    //    d) Calls PrepareOffer() on each registered event (sensor_reading_)
    //
    //  After this call, the SHM regions exist on disk and Proxies can call
    //  FindService() to discover this skeleton.
    const auto offer_result = skeleton.OfferService();
    if (!offer_result.has_value())
    {
        std::cerr << "[demo_server] OfferService() failed: " << offer_result.error() << "\n";
        return EXIT_FAILURE;
    }
    score::mw::log::LogInfo("demo")
        << "Service offered! Shared memory active. Starting to publish sensor data...";

    std::cout << "\n[demo_server] Publishing SensorData every " << cycle_time_ms << " ms\n";
    if (num_cycles == 0U)
    {
        std::cout << "[demo_server] Running indefinitely — press Ctrl+C to stop\n\n";
    }
    else
    {
        std::cout << "[demo_server] Will send " << num_cycles << " samples then exit\n\n";
    }

    // ── Step 4: Publish loop ──────────────────────────────────────────────────
    for (std::size_t cycle = 0U; (num_cycles == 0U) || (cycle < num_cycles); ++cycle)
    {
        // Allocate() reserves one slot in the SHM ring buffer and returns a
        // SampleAllocateePtr<SensorData>.  This pointer is a handle into the
        // shared memory — writing to *sample writes directly into SHM with
        // zero copies.  No heap allocation occurs.
        auto sample_result = skeleton.sensor_reading_.Allocate();
        if (!sample_result.has_value())
        {
            // This typically means all ring buffer slots are held by subscribers.
            // Increase numberOfSampleSlots in mw_com_config.json if this happens often.
            std::cerr << "[demo_server] Allocate() failed (ring buffer full?): "
                      << sample_result.error() << "\n";
            std::this_thread::sleep_for(cycle_time);
            continue;
        }
        auto sample = std::move(sample_result).value();

        // Fill the sample with sensor data — writing directly into SHM
        SimulateSensor(*sample, static_cast<std::uint32_t>(cycle));

        std::cout << "[demo_server] Sending sample #" << cycle
                  << "  temp=" << sample->temperature_celsius << " °C"
                  << "  pres=" << sample->pressure_hpa << " hPa\n";

        // Send() transfers ownership of the slot to the ring buffer and
        // notifies all subscribed Proxies.  After Send(), the slot is
        // immutable from the Skeleton side — do not use `sample` after this.
        const auto send_result = skeleton.sensor_reading_.Send(std::move(sample));
        if (!send_result.has_value())
        {
            std::cerr << "[demo_server] Send() failed: " << send_result.error() << "\n";
        }

        std::this_thread::sleep_for(cycle_time);
    }

    // ── Step 5: Clean shutdown ────────────────────────────────────────────────
    //
    //  StopOfferService() removes the service from the discovery directory and
    //  signals all subscribed Proxies that the service is going away.
    //  They will receive a SubscriptionState::kSubscriptionPending notification.
    //  The SHM regions are unmapped and unlinked after this call.
    std::cout << "\n[demo_server] Finished sending. Stopping service offer...\n";
    skeleton.StopOfferService();
    score::mw::log::LogInfo("demo") << "Service stopped cleanly. Goodbye.";

    return EXIT_SUCCESS;
}
