// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// VK_LAYER_tubelight_overlay — Vulkan instance + device layer.
//
// Hooks the loader chain for vkQueuePresentKHR. The hook is currently a
// passthrough plus counter; F7 inserts the Tubelight 8-pass pipeline before
// the present completes (using a per-swapchain auxiliary command buffer to
// blit through the CRT shaders).
//
// References:
//   - https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md
//   - https://renderdoc.org/vulkan-layer-guide.html
//
// Build-time the Vulkan headers must be available (find_package(Vulkan)).
// Runtime activation:
//   - Manifest VkLayer_tubelight_overlay.json must be on VK_LAYER_PATH or
//     VK_ADD_LAYER_PATH.
//   - VK_INSTANCE_LAYERS=VK_LAYER_tubelight_overlay, or implicit if app opts in.

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {

struct InstanceData {
    VkInstance instance = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr next_gipa = nullptr;
    PFN_vkDestroyInstance     next_destroy_instance = nullptr;
};

struct DeviceData {
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr next_gdpa = nullptr;
    PFN_vkDestroyDevice     next_destroy_device = nullptr;
    PFN_vkQueuePresentKHR   next_present = nullptr;
};

std::mutex g_lock;
std::unordered_map<void*, InstanceData> g_instances;
std::unordered_map<void*, DeviceData>   g_devices;
std::unordered_map<VkQueue, VkDevice>   g_queue_to_device;
std::atomic<unsigned long long> g_present_calls{0};

// Each opaque dispatchable handle is a pointer-to-pointer (the loader's
// dispatch table). Using the first sizeof(void*) bytes as the key is the
// canonical way layers identify a dispatchable handle.
template <typename T>
void* dispatch_key(T handle) {
    return *reinterpret_cast<void**>(handle);
}

VkLayerInstanceCreateInfo* get_chain_info(const VkInstanceCreateInfo* pCreateInfo, VkLayerFunction func) {
    auto* chain_info = reinterpret_cast<const VkLayerInstanceCreateInfo*>(pCreateInfo->pNext);
    while (chain_info != nullptr) {
        if (chain_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
            chain_info->function == func) {
            return const_cast<VkLayerInstanceCreateInfo*>(chain_info);
        }
        chain_info = reinterpret_cast<const VkLayerInstanceCreateInfo*>(chain_info->pNext);
    }
    return nullptr;
}

VkLayerDeviceCreateInfo* get_chain_info(const VkDeviceCreateInfo* pCreateInfo, VkLayerFunction func) {
    auto* chain_info = reinterpret_cast<const VkLayerDeviceCreateInfo*>(pCreateInfo->pNext);
    while (chain_info != nullptr) {
        if (chain_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
            chain_info->function == func) {
            return const_cast<VkLayerDeviceCreateInfo*>(chain_info);
        }
        chain_info = reinterpret_cast<const VkLayerDeviceCreateInfo*>(chain_info->pNext);
    }
    return nullptr;
}

void log_once() {
    static std::once_flag once;
    std::call_once(once, [](){
        std::fprintf(stderr, "[tubelight-vk-layer] active (hooking vkQueuePresentKHR)\n");
    });
}

} // namespace

extern "C" {

// ---- vkCreateInstance ---------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL tubelight_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {

    VkLayerInstanceCreateInfo* chain = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr fp_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance fp_create =
        reinterpret_cast<PFN_vkCreateInstance>(fp_gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!fp_create) return VK_ERROR_INITIALIZATION_FAILED;

    // Advance the chain for the next layer / driver.
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    VkResult res = fp_create(pCreateInfo, pAllocator, pInstance);
    if (res != VK_SUCCESS) return res;

    InstanceData data{};
    data.instance = *pInstance;
    data.next_gipa = fp_gipa;
    data.next_destroy_instance =
        reinterpret_cast<PFN_vkDestroyInstance>(fp_gipa(*pInstance, "vkDestroyInstance"));

    {
        std::lock_guard<std::mutex> lk(g_lock);
        g_instances[dispatch_key(*pInstance)] = data;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL tubelight_DestroyInstance(
    VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    InstanceData data;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        auto it = g_instances.find(dispatch_key(instance));
        if (it == g_instances.end()) return;
        data = it->second;
        g_instances.erase(it);
    }
    if (data.next_destroy_instance) data.next_destroy_instance(instance, pAllocator);
}

// ---- vkCreateDevice -----------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL tubelight_CreateDevice(
    VkPhysicalDevice physical_device,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {

    VkLayerDeviceCreateInfo* chain = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr fp_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   fp_gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;

    PFN_vkCreateDevice fp_create = reinterpret_cast<PFN_vkCreateDevice>(
        fp_gipa(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!fp_create) return VK_ERROR_INITIALIZATION_FAILED;

    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    VkResult res = fp_create(physical_device, pCreateInfo, pAllocator, pDevice);
    if (res != VK_SUCCESS) return res;

    DeviceData data{};
    data.device = *pDevice;
    data.next_gdpa = fp_gdpa;
    data.next_destroy_device =
        reinterpret_cast<PFN_vkDestroyDevice>(fp_gdpa(*pDevice, "vkDestroyDevice"));
    data.next_present =
        reinterpret_cast<PFN_vkQueuePresentKHR>(fp_gdpa(*pDevice, "vkQueuePresentKHR"));

    {
        std::lock_guard<std::mutex> lk(g_lock);
        g_devices[dispatch_key(*pDevice)] = data;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL tubelight_DestroyDevice(
    VkDevice device, const VkAllocationCallbacks* pAllocator) {
    DeviceData data;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        auto it = g_devices.find(dispatch_key(device));
        if (it == g_devices.end()) return;
        data = it->second;
        g_devices.erase(it);
    }
    if (data.next_destroy_device) data.next_destroy_device(device, pAllocator);
}

// ---- vkGetDeviceQueue (track queue → device) ---------------------------

VKAPI_ATTR void VKAPI_CALL tubelight_GetDeviceQueue(
    VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) {
    DeviceData data;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        auto it = g_devices.find(dispatch_key(device));
        if (it == g_devices.end()) return;
        data = it->second;
    }
    auto fp = reinterpret_cast<PFN_vkGetDeviceQueue>(data.next_gdpa(device, "vkGetDeviceQueue"));
    if (fp) {
        fp(device, queueFamilyIndex, queueIndex, pQueue);
        std::lock_guard<std::mutex> lk(g_lock);
        g_queue_to_device[*pQueue] = device;
    }
}

// ---- vkQueuePresentKHR — the actual hook -------------------------------

VKAPI_ATTR VkResult VKAPI_CALL tubelight_QueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {

    log_once();
    g_present_calls.fetch_add(1, std::memory_order_relaxed);

    PFN_vkQueuePresentKHR next = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        auto qit = g_queue_to_device.find(queue);
        if (qit != g_queue_to_device.end()) {
            auto dit = g_devices.find(dispatch_key(qit->second));
            if (dit != g_devices.end()) next = dit->second.next_present;
        }
    }
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;

    // F7: render the Tubelight 8-pass pipeline into the swapchain image here
    // via a per-swapchain auxiliary command buffer, then call next().
    return next(queue, pPresentInfo);
}

// ---- procaddr dispatch -------------------------------------------------

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL tubelight_GetDeviceProcAddr(VkDevice device, const char* pName);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL tubelight_GetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&tubelight_GetInstanceProcAddr);
    if (std::strcmp(pName, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&tubelight_CreateInstance);
    if (std::strcmp(pName, "vkDestroyInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&tubelight_DestroyInstance);
    if (std::strcmp(pName, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&tubelight_CreateDevice);
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&tubelight_GetDeviceProcAddr);

    if (!instance) return nullptr;
    std::lock_guard<std::mutex> lk(g_lock);
    auto it = g_instances.find(dispatch_key(instance));
    if (it == g_instances.end()) return nullptr;
    return it->second.next_gipa(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL tubelight_GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&tubelight_GetDeviceProcAddr);
    if (std::strcmp(pName, "vkDestroyDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&tubelight_DestroyDevice);
    if (std::strcmp(pName, "vkGetDeviceQueue") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&tubelight_GetDeviceQueue);
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&tubelight_QueuePresentKHR);

    if (!device) return nullptr;
    std::lock_guard<std::mutex> lk(g_lock);
    auto it = g_devices.find(dispatch_key(device));
    if (it == g_devices.end()) return nullptr;
    return it->second.next_gdpa(device, pName);
}

// ---- Loader-required negotiation entry point ---------------------------
//
// Some loaders call vkNegotiateLoaderLayerInterfaceVersion to negotiate the
// layer interface version. Returning 2 is the modern recommendation.

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* pVersionStruct) {
    if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
        pVersionStruct->loaderLayerInterfaceVersion = 2;
        pVersionStruct->pfnGetInstanceProcAddr = &tubelight_GetInstanceProcAddr;
        pVersionStruct->pfnGetDeviceProcAddr   = &tubelight_GetDeviceProcAddr;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    }
    return VK_SUCCESS;
}

} // extern "C"
