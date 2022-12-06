/*
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "framework/l0/levelzero.h"
#include "framework/l0/utility/buffer_contents_helper_l0.h"
#include "framework/l0/utility/usm_helper.h"
#include "framework/test_case/register_test_case.h"
#include "framework/utility/timer.h"

#include "definitions/usm_fill_immediate.h"

#include <gtest/gtest.h>

static TestResult run(const UsmFillImmediateArguments &arguments, Statistics &statistics) {
    MeasurementFields typeSelector(MeasurementUnit::GigabytesPerSecond, arguments.useEvents ? MeasurementType::Gpu : MeasurementType::Cpu);

    if (isNoopRun()) {
        statistics.pushUnitAndType(typeSelector.getUnit(), typeSelector.getType());
        return TestResult::Nooped;
    }

    if (arguments.usmMemoryPlacement == UsmMemoryPlacement::NonUsmMapped) {
        return TestResult::ApiNotCapable;
    }

    QueueProperties queueProperties = QueueProperties::create().setForceBlitter(arguments.forceBlitter).allowCreationFail();
    ContextProperties contextProperties = ContextProperties::create();
    ExtensionProperties extensionProperties = ExtensionProperties::create().setImportHostPointerFunctions(
        arguments.usmMemoryPlacement == UsmMemoryPlacement::NonUsmImported);

    LevelZero levelzero(queueProperties, contextProperties, extensionProperties);
    if (levelzero.commandQueue == nullptr || arguments.patternSize > levelzero.commandQueueMaxFillSize) {
        return TestResult::DeviceNotCapable;
    }

    Timer timer;
    const uint64_t timerResolution = levelzero.getTimerResolution(levelzero.device);

    // Create buffer
    void *buffer{};
    ASSERT_ZE_RESULT_SUCCESS(UsmHelper::allocate(arguments.usmMemoryPlacement, levelzero, arguments.bufferSize, &buffer));
    ASSERT_ZE_RESULT_SUCCESS(zeContextMakeMemoryResident(levelzero.context, levelzero.device, buffer, arguments.bufferSize));

    // Create event
    ze_event_pool_flags_t eventPoolFlags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;
    if (arguments.useEvents) {
        eventPoolFlags |= ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP;
    }
    ze_event_pool_handle_t eventPool{};
    ze_event_handle_t event{};
    ze_event_pool_desc_t eventPoolDesc{ZE_STRUCTURE_TYPE_EVENT_POOL_DESC};
    eventPoolDesc.flags = eventPoolFlags;
    eventPoolDesc.count = 1;
    ASSERT_ZE_RESULT_SUCCESS(zeEventPoolCreate(levelzero.context, &eventPoolDesc, 1, &levelzero.device, &eventPool));
    ze_event_desc_t eventDesc{ZE_STRUCTURE_TYPE_EVENT_DESC};
    eventDesc.index = 0;
    eventDesc.signal = ZE_EVENT_SCOPE_FLAG_DEVICE;
    eventDesc.wait = ZE_EVENT_SCOPE_FLAG_HOST;
    ASSERT_ZE_RESULT_SUCCESS(zeEventCreate(eventPool, &eventDesc, &event));

    // Create pattern
    const auto pattern = std::make_unique<uint8_t[]>(arguments.patternSize);
    if (arguments.patternContents == BufferContents::Random) {
        BufferContentsHelperL0::fillWithRandomBytes(pattern.get(), arguments.patternSize);
    }
    if (arguments.usmMemoryPlacement == UsmMemoryPlacement::NonUsmImported) {
        ASSERT_ZE_RESULT_SUCCESS(levelzero.importHostPointer.importExternalPointer(
            levelzero.driver, pattern.get(), arguments.patternSize));
        ASSERT_ZE_RESULT_SUCCESS(zeContextMakeMemoryResident(levelzero.context, levelzero.device,
                                                             pattern.get(), arguments.patternSize));
    }

    // Create an immediate command list
    ze_command_list_handle_t cmdList{};
    auto commandQueueDesc = QueueFamiliesHelper::getPropertiesForSelectingEngine(levelzero.device, queueProperties.selectedEngine);
    ASSERT_ZE_RESULT_SUCCESS(zeCommandListCreateImmediate(levelzero.context, levelzero.device, &commandQueueDesc->desc, &cmdList));

    // Warmup
    ASSERT_ZE_RESULT_SUCCESS(zeCommandListAppendMemoryFill(cmdList, buffer, pattern.get(), arguments.patternSize, arguments.bufferSize, event, 0, nullptr));
    ASSERT_ZE_RESULT_SUCCESS(zeEventHostSynchronize(event, std::numeric_limits<uint64_t>::max()));
    ASSERT_ZE_RESULT_SUCCESS(zeEventHostReset(event));

    // Benchmark
    for (auto i = 0u; i < arguments.iterations; i++) {
        ASSERT_ZE_RESULT_SUCCESS(BufferContentsHelperL0::fillBuffer(levelzero, buffer, arguments.bufferSize, arguments.contents, true));

        timer.measureStart();
        ASSERT_ZE_RESULT_SUCCESS(zeCommandListAppendMemoryFill(cmdList, buffer, pattern.get(), arguments.patternSize, arguments.bufferSize, event, 0, nullptr));
        ASSERT_ZE_RESULT_SUCCESS(zeEventHostSynchronize(event, std::numeric_limits<uint64_t>::max()));
        timer.measureEnd();

        if (arguments.useEvents) {
            ze_kernel_timestamp_result_t timestampResult{};
            ASSERT_ZE_RESULT_SUCCESS(zeEventQueryKernelTimestamp(event, &timestampResult));
            auto commandTime = std::chrono::nanoseconds(timestampResult.global.kernelEnd - timestampResult.global.kernelStart);
            commandTime *= timerResolution;
            statistics.pushValue(commandTime, arguments.bufferSize, typeSelector.getUnit(), typeSelector.getType());
        } else {
            statistics.pushValue(timer.get(), arguments.bufferSize, typeSelector.getUnit(), typeSelector.getType());
        }

        ASSERT_ZE_RESULT_SUCCESS(zeEventHostReset(event));
    }

    // Evict buffer
    ASSERT_ZE_RESULT_SUCCESS(zeContextEvictMemory(levelzero.context, levelzero.device, buffer, arguments.bufferSize));

    // Cleanup
    ASSERT_ZE_RESULT_SUCCESS(zeCommandListDestroy(cmdList));
    ASSERT_ZE_RESULT_SUCCESS(zeEventPoolDestroy(eventPool));
    ASSERT_ZE_RESULT_SUCCESS(zeEventDestroy(event));

    ASSERT_ZE_RESULT_SUCCESS(UsmHelper::deallocate(arguments.usmMemoryPlacement, levelzero, buffer));
    if (arguments.usmMemoryPlacement == UsmMemoryPlacement::NonUsmImported) {
        ASSERT_ZE_RESULT_SUCCESS(zeContextEvictMemory(levelzero.context, levelzero.device,
                                                      pattern.get(), arguments.patternSize));
        ASSERT_ZE_RESULT_SUCCESS(levelzero.importHostPointer.releaseExternalPointer(
            levelzero.driver, pattern.get()));
    }
    return TestResult::Success;
}

static RegisterTestCaseImplementation<UsmFillImmediate> registerTestCase(run, Api::L0);
