/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "VulkanCommands.h"

#include <utils/Panic.h>

using namespace bluevk;

namespace filament {
namespace backend {

// All vkCreate* functions take an optional allocator. For now we select the default allocator by
// passing in a null pointer, and we highlight the argument by using the VKALLOC constant.
constexpr VkAllocationCallbacks* VKALLOC = nullptr;

VulkanCmdFence::VulkanCmdFence(VkDevice device, bool signaled) : device(device) {
    VkFenceCreateInfo fenceCreateInfo { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (signaled) {
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }
    vkCreateFence(device, &fenceCreateInfo, VKALLOC, &fence);
}

 VulkanCmdFence::~VulkanCmdFence() {
    vkDestroyFence(device, fence, VKALLOC);
}

VulkanCommands::VulkanCommands(VkDevice device, uint32_t queueFamilyIndex) : mDevice(device) {
    VkCommandPoolCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.flags =
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    createInfo.queueFamilyIndex = queueFamilyIndex;
    vkCreateCommandPool(device, &createInfo, VKALLOC, &mPool);
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &mQueue);
}

VulkanCommands::~VulkanCommands() {
    wait();
    gc();
    vkDestroyCommandPool(mDevice, mPool, VKALLOC);
}

VulkanCommandBuffer& VulkanCommands::get() {
    if (mCurrent) {
        return *mCurrent;
    }

    // Find an available slot.
    for (auto& wrapper : mStorage) {
        if (wrapper.cmdbuffer == VK_NULL_HANDLE) {
            mCurrent = &wrapper;
            break;
        }
    }
    ASSERT_POSTCONDITION(mCurrent, "Too many in-flight command buffers.");

    // Create the low-level command buffer.
    const VkCommandBufferAllocateInfo allocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = mPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    const VkCommandBufferBeginInfo binfo { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkAllocateCommandBuffers(mDevice, &allocateInfo, &mCurrent->cmdbuffer);

    // Create a submission semaphore.
    VkSemaphoreCreateInfo createInfo { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    vkCreateSemaphore(mDevice, &createInfo, nullptr, &mCurrent->renderingFinished);

    // Create a submission fence.
    mCurrent->fence = std::make_shared<VulkanCmdFence>(mDevice);

    // Begin writing into the command buffer.
    vkBeginCommandBuffer(mCurrent->cmdbuffer, &binfo);

    return *mCurrent;
}

void VulkanCommands::submit(VkSemaphore imageAvailable) {
    // It's perfectly fine to call submit when no commands have been written.
    if (mCurrent == nullptr) {
        return;
    }

    vkEndCommandBuffer(mCurrent->cmdbuffer);

    VkSubmitInfo submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &mCurrent->cmdbuffer,
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &mCurrent->renderingFinished,
    };

    VkPipelineStageFlags waitDestStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (imageAvailable) {
        submitInfo.waitSemaphoreCount = 1u;
        submitInfo.pWaitSemaphores = &imageAvailable;
        submitInfo.pWaitDstStageMask = &waitDestStageMask;
    }

    auto& cmdfence = mCurrent->fence;
    std::unique_lock<utils::Mutex> lock(cmdfence->mutex);
    vkQueueSubmit(mQueue, 1, &submitInfo, cmdfence->fence);
    lock.unlock();
    cmdfence->condition.notify_all();

    mLatestSubmission = mCurrent;
    mCurrent = nullptr;
}

VkSemaphore VulkanCommands::latestSemaphore() {
    return mLatestSubmission ? mLatestSubmission->renderingFinished : VK_NULL_HANDLE;
}

void VulkanCommands::gc() {
    for (auto& wrapper : mStorage) {
        if (wrapper.cmdbuffer != VK_NULL_HANDLE) {
            VkResult result = vkWaitForFences(mDevice, 1, &wrapper.fence->fence, VK_TRUE, 0);
            if (result == VK_SUCCESS) {
                if (mLatestSubmission == &wrapper) {
                    mLatestSubmission = nullptr;
                }

                vkDestroySemaphore(mDevice, wrapper.renderingFinished, VKALLOC);
                wrapper.renderingFinished = VK_NULL_HANDLE;

                vkFreeCommandBuffers(mDevice, mPool, 1, &wrapper.cmdbuffer);
                wrapper.cmdbuffer = VK_NULL_HANDLE;

                wrapper.fence.reset();
            }
        }
    }
}

void VulkanCommands::wait() {
    VkFence fences[CAPACITY];
    uint32_t count = 0;
    for (auto& wrapper : mStorage) {
        if (wrapper.cmdbuffer != VK_NULL_HANDLE) {
            fences[count++] = wrapper.fence->fence;
        }
    }
    if (count > 0) {
        vkWaitForFences(mDevice, count, fences, VK_TRUE, UINT64_MAX);
    }
}

} // namespace filament
} // namespace backend
