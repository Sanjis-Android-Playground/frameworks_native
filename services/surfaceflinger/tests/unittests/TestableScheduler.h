/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <Scheduler/Scheduler.h>
#include <ftl/fake_guard.h>
#include <gmock/gmock.h>
#include <gui/ISurfaceComposer.h>

#include "Scheduler/EventThread.h"
#include "Scheduler/LayerHistory.h"
#include "Scheduler/Scheduler.h"
#include "Scheduler/VSyncTracker.h"
#include "Scheduler/VsyncController.h"
#include "mock/MockVSyncTracker.h"
#include "mock/MockVsyncController.h"

namespace android::scheduler {

class TestableScheduler : public Scheduler, private ICompositor {
public:
    TestableScheduler(RefreshRateSelectorPtr selectorPtr, ISchedulerCallback& callback)
          : TestableScheduler(std::make_unique<mock::VsyncController>(),
                              std::make_unique<mock::VSyncTracker>(), std::move(selectorPtr),
                              callback) {}

    TestableScheduler(std::unique_ptr<VsyncController> controller,
                      std::unique_ptr<VSyncTracker> tracker, RefreshRateSelectorPtr selectorPtr,
                      ISchedulerCallback& callback)
          : Scheduler(*this, callback, Feature::kContentDetection) {
        mVsyncSchedule.emplace(VsyncSchedule(std::move(tracker), nullptr, std::move(controller)));

        const auto displayId = selectorPtr->getActiveMode().modePtr->getPhysicalDisplayId();
        registerDisplay(displayId, std::move(selectorPtr));

        ON_CALL(*this, postMessage).WillByDefault([](sp<MessageHandler>&& handler) {
            // Execute task to prevent broken promise exception on destruction.
            handler->handleMessage(Message());
        });
    }

    MOCK_METHOD(void, scheduleConfigure, (), (override));
    MOCK_METHOD(void, scheduleFrame, (), (override));
    MOCK_METHOD(void, postMessage, (sp<MessageHandler>&&), (override));

    // Used to inject mock event thread.
    ConnectionHandle createConnection(std::unique_ptr<EventThread> eventThread) {
        return Scheduler::createConnection(std::move(eventThread));
    }

    /* ------------------------------------------------------------------------
     * Read-write access to private data to set up preconditions and assert
     * post-conditions.
     */
    auto& mutablePrimaryHWVsyncEnabled() { return mPrimaryHWVsyncEnabled; }
    auto& mutableHWVsyncAvailable() { return mHWVsyncAvailable; }

    auto refreshRateSelector() { return leaderSelectorPtr(); }

    const auto& refreshRateSelectors() const NO_THREAD_SAFETY_ANALYSIS {
        return mRefreshRateSelectors;
    }

    bool hasRefreshRateSelectors() const { return !refreshRateSelectors().empty(); }

    void registerDisplay(PhysicalDisplayId displayId, RefreshRateSelectorPtr selectorPtr) {
        ftl::FakeGuard guard(kMainThreadContext);
        Scheduler::registerDisplay(displayId, std::move(selectorPtr));
    }

    void unregisterDisplay(PhysicalDisplayId displayId) {
        ftl::FakeGuard guard(kMainThreadContext);
        Scheduler::unregisterDisplay(displayId);
    }

    void setLeaderDisplay(PhysicalDisplayId displayId) {
        ftl::FakeGuard guard(kMainThreadContext);
        Scheduler::setLeaderDisplay(displayId);
    }

    auto& mutableLayerHistory() { return mLayerHistory; }

    size_t layerHistorySize() NO_THREAD_SAFETY_ANALYSIS {
        return mLayerHistory.mActiveLayerInfos.size() + mLayerHistory.mInactiveLayerInfos.size();
    }

    size_t getNumActiveLayers() NO_THREAD_SAFETY_ANALYSIS {
        return mLayerHistory.mActiveLayerInfos.size();
    }

    void replaceTouchTimer(int64_t millis) {
        if (mTouchTimer) {
            mTouchTimer.reset();
        }
        mTouchTimer.emplace(
                "Testable Touch timer", std::chrono::milliseconds(millis),
                [this] { touchTimerCallback(TimerState::Reset); },
                [this] { touchTimerCallback(TimerState::Expired); });
        mTouchTimer->start();
    }

    bool isTouchActive() {
        std::lock_guard<std::mutex> lock(mPolicyLock);
        return mPolicy.touch == Scheduler::TouchState::Active;
    }

    void setTouchStateAndIdleTimerPolicy(GlobalSignals globalSignals) {
        std::lock_guard<std::mutex> lock(mPolicyLock);
        mPolicy.touch = globalSignals.touch ? TouchState::Active : TouchState::Inactive;
        mPolicy.idleTimer = globalSignals.idle ? TimerState::Expired : TimerState::Reset;
    }

    void setContentRequirements(std::vector<RefreshRateSelector::LayerRequirement> layers) {
        std::lock_guard<std::mutex> lock(mPolicyLock);
        mPolicy.contentRequirements = std::move(layers);
    }

    using Scheduler::DisplayModeChoice;
    using Scheduler::DisplayModeChoiceMap;

    DisplayModeChoiceMap chooseDisplayModes() NO_THREAD_SAFETY_ANALYSIS {
        return Scheduler::chooseDisplayModes();
    }

    void dispatchCachedReportedMode() {
        std::lock_guard<std::mutex> lock(mPolicyLock);
        Scheduler::dispatchCachedReportedMode();
    }

    void clearCachedReportedMode() {
        std::lock_guard<std::mutex> lock(mPolicyLock);
        mPolicy.cachedModeChangedParams.reset();
    }

    void onNonPrimaryDisplayModeChanged(ConnectionHandle handle, const FrameRateMode& mode) {
        Scheduler::onNonPrimaryDisplayModeChanged(handle, mode);
    }

private:
    // ICompositor overrides:
    void configure() override {}
    bool commit(TimePoint, VsyncId, TimePoint) override { return false; }
    void composite(TimePoint, VsyncId) override {}
    void sample() override {}
};

} // namespace android::scheduler
