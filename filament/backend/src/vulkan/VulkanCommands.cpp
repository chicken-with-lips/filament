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

    // Internally we use the VK_INCOMPLETE status to mean "not yet submitted".
    // When this fence gets submitted, its status changes to VK_NOT_READY.
    status.store(VK_INCOMPLETE);
}

 VulkanCmdFence::~VulkanCmdFence() {
    vkDestroyFence(device, fence, VKALLOC);
}

VulkanCommands::VulkanCommands(VkDevice device, uint32_t queueFamilyIndex, VulkanBinder& binder,
                VulkanDisposer& disposer) : mDevice(device), mBinder(binder), mDisposer(disposer) {
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
    const VkCommandBufferBeginInfo binfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkAllocateCommandBuffers(mDevice, &allocateInfo, &mCurrent->cmdbuffer);

    // Create a submission semaphore.
    VkSemaphoreCreateInfo createInfo { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    vkCreateSemaphore(mDevice, &createInfo, nullptr, &mCurrent->renderingFinished);

    // Create a submission fence.
    mCurrent->fence = std::make_shared<VulkanCmdFence>(mDevice);

    // Begin writing into the command buffer.
    vkBeginCommandBuffer(mCurrent->cmdbuffer, &binfo);

    // vkCmdBindPipeline and vkCmdBindDescriptorSets establish bindings to a specific command
    // buffer; they are not global to the device. Since VulkanBinder doesn't have context about the
    // current command buffer, we need to reset its bindings after swapping over to a new command
    // buffer. Note that the following reset causes us to issue a few more vkBind* calls than
    // strictly necessary, but only in the first draw call of the frame. Alteneratively we could
    // enhance VulkanBinder by adding a mapping from command buffers to bindings, but this would
    // introduce complexity that doesn't seem worthwhile. Yet another design would be to instance a
    // separate VulkanBinder for each element in the swap chain, which would also have the benefit
    // of allowing us to safely mutate descriptor sets. For now we're avoiding that strategy in the
    // interest of maintaining a small memory footprint.
    mBinder.resetBindings();

    return *mCurrent;
}

void VulkanCommands::flush(VkSemaphore* pSemaphore) {
    // It's perfectly fine to call flush when no commands have been written.
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

    // If we are given an image availability semaphore that has not yet been signaled, it's fine to
    // start executing commands anyway as along as we stall the GPU at the color output stage until
    // the image becomes available.
    const VkPipelineStageFlags waitDestStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSemaphore imageAvailable = pSemaphore ? *pSemaphore : VK_NULL_HANDLE;

    auto& cmdfence = mCurrent->fence;

    if (imageAvailable) {
        printf("flushing with semaphore fence = %p\n", cmdfence->fence);
        submitInfo.waitSemaphoreCount = 1u;
        submitInfo.pWaitSemaphores = &imageAvailable;
        submitInfo.pWaitDstStageMask = &waitDestStageMask;

        // This gets set to null to prevent a subsequent submission from waiting on the image.
        // It is illegal in Vulkan for a single semaphore to waiting on by multiple submissions.
        *pSemaphore = VK_NULL_HANDLE;
    } else {
        printf("flushing without semaphore fence = %p\n", cmdfence->fence);
    }

    std::unique_lock<utils::Mutex> lock(cmdfence->mutex);
    cmdfence->status.store(VK_NOT_READY);
    vkQueueSubmit(mQueue, 1, &submitInfo, cmdfence->fence);
    lock.unlock();
    cmdfence->condition.notify_all();

    mLatestSubmission = mCurrent;
    mCurrent = nullptr;
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

VkSemaphore VulkanCommands::latestSemaphore() {
    return mLatestSubmission ? mLatestSubmission->renderingFinished : VK_NULL_HANDLE;
}

void VulkanCommands::gc() {

    int count = 0;
    for (auto& wrapper : mStorage)  if (wrapper.cmdbuffer != VK_NULL_HANDLE) ++count;

    for (auto& wrapper : mStorage) {
        if (wrapper.cmdbuffer != VK_NULL_HANDLE) {
            VkResult result = vkWaitForFences(mDevice, 1, &wrapper.fence->fence, VK_TRUE, 0);
            if (result == VK_SUCCESS) {
                if (mLatestSubmission == &wrapper) {
                    mLatestSubmission = nullptr;
                }

                mDisposer.release(wrapper.resources);

                vkDestroySemaphore(mDevice, wrapper.renderingFinished, VKALLOC);
                wrapper.renderingFinished = VK_NULL_HANDLE;

                vkFreeCommandBuffers(mDevice, mPool, 1, &wrapper.cmdbuffer);
                wrapper.cmdbuffer = VK_NULL_HANDLE;

                printf("        releasing fence %p\n", wrapper.fence->fence);

                wrapper.fence.reset();
            }
        }
    }

    int after = 0;
    for (auto& wrapper : mStorage)  if (wrapper.cmdbuffer != VK_NULL_HANDLE) ++after;

    printf("gc %d => %d\n", count, after);

}

void VulkanCommands::updateFences() {
    for (auto& wrapper : mStorage) {
        if (wrapper.cmdbuffer != VK_NULL_HANDLE) {
            VulkanCmdFence* fence = wrapper.fence.get();
            if (fence) {
                VkResult status = vkGetFenceStatus(mDevice, fence->fence);
                // This is either VK_SUCCESS, VK_NOT_READY, or VK_ERROR_DEVICE_LOST.
                fence->status.store(status, std::memory_order_relaxed);
            }
        }
    }
}

void VulkanCommands::addReference(void* resource) {
    for (auto& wrapper : mStorage) {
        if (wrapper.cmdbuffer != VK_NULL_HANDLE) {
            mDisposer.acquire(resource, wrapper.resources);
        }
    }
}

} // namespace filament
} // namespace backend
