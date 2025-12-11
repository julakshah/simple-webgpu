#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <webgpu/webgpu.h>

// These callbacks are used for the async functions getting adapter and device
void adapter_callback(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2) {
    if (status != WGPURequestAdapterStatus_Success) {
            fprintf(stderr,"Failed to get an adapter: %s",message.data);
            return;
        }
        *(WGPUAdapter*)(userdata1) = adapter;
}

void device_callback(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2) {
    if (status != WGPURequestDeviceStatus_Success) {
            fprintf(stderr,"Failed to get a device: %s",message.data);
            return;
        }
        *(WGPUDevice*)(userdata1) = device;
}

int main(int argc, char** argv) {

    WGPUInstanceDescriptor instanceDesc{};
    instanceDesc.nextInChain = nullptr;

    // Thread panic if WaitAny isn't supported
    //instanceDesc.features.timedWaitAnyEnable = false; // used in WGPU
    instanceDesc.capabilities.timedWaitAnyEnable = false; // used in Dawn

    WGPUInstance instance = wgpuCreateInstance(&instanceDesc);
    if (!instance) {
        fprintf(stderr, "Failed to create instance\n");
        return 1;
    }
    printf("Created wgpu instance\n");

    // Create adapter
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.nextInChain = nullptr;

    WGPUAdapter adapter = nullptr;

    auto callbackMode = WGPUCallbackMode_AllowProcessEvents;
    WGPURequestAdapterCallbackInfo callbackInfo;
    callbackInfo.callback = &adapter_callback;
    callbackInfo.mode = callbackMode;
    callbackInfo.userdata1 = &adapter;

    // Start async adapter request
    wgpuInstanceRequestAdapter(instance, &adapterOpts, callbackInfo);

    while (adapter == NULL) {
        // Wait for the instance to give us the adapter
        wgpuInstanceProcessEvents(instance);
    }

    printf("Got adapter\n");

    WGPUAdapterInfo info{};
    WGPUStatus s = wgpuAdapterGetInfo(adapter,&info);
    if (s != WGPUStatus_Success) {
        fprintf(stderr, "wgpuAdapterGetInfo failed (status %d)\n", (int)s);
    }
    printf("VendorID: %x\n", info.vendorID);
    printf("Vendor: %.*s\n", (int)info.vendor.length, info.vendor.data);
    printf("Architecture: %.*s\n", (int)info.vendor.length, info.architecture.data);
    printf("DeviceID: %x\n", info.deviceID);
    printf("Name: %.*s\n", (int)info.vendor.length, info.device.data);
    printf("Driver description: %.*s\n", (int)info.vendor.length, info.description.data);

    // Get device
    WGPUDeviceDescriptor deviceDesc = {};
    WGPUDevice device = nullptr;

    WGPURequestDeviceCallbackInfo deviceCallbackInfo;
    deviceCallbackInfo.callback = &device_callback;
    deviceCallbackInfo.mode = callbackMode;
    deviceCallbackInfo.userdata1 = &device;

    // Start an async device request
    wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallbackInfo);

    while (device == NULL) {
        // Wait for the instance to give us the device
        wgpuInstanceProcessEvents(instance);
    }

    printf("Got device!\n");

    // Once we have the device, we no longer need the adater
    wgpuAdapterRelease(adapter);

    // Queue holds a series of operations to run
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    // Command encoder writes instructions
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain = nullptr;
    encoderDesc.label = WGPUStringView{"Command encoder", WGPU_STRLEN};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    // Cleanup
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);

    return 0;
    
}
