#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

typedef struct SurfaceViewData {
    WGPUSurfaceTexture surfaceTexture;
    WGPUTextureView textureView;
} SurfaceViewData;

typedef struct CoordTransform {
    float coords[16];
} CoordTransform;

typedef struct PipelineSetupOutput {
    WGPUBuffer pointBuffer;
    WGPUBuffer indexBuffer;
    WGPUBuffer transformBuffer;
    WGPUBindGroup bindGroup;
    WGPURenderPipeline renderPipeline;
} PipelineSetupOutput;

void create_buffers(PipelineSetupOutput* output, WGPUDevice* device_ptr, WGPUTextureFormat* preferredFormat_ptr) {
    // Create the buffers we'll be using and put them in a bind group
    WGPUDevice device = *device_ptr;
    WGPUTextureFormat preferred_format = *preferredFormat_ptr;

    // We'll define the shape of our cube here (positions of each point)
    float points[24] = {
        -1.0, -1.0, -1.0,
        -1.0, -1.0, 1.0,
        -1.0, 1.0, -1.0,
        -1.0, 1.0, 1.0,
        1.0, -1.0, -1.0,
        1.0, -1.0, 1.0,
        1.0, 1.0, -1.0,
        1.0, 1.0, 1.0
    };

    // Vertex buffer to hold object we render
    WGPUBufferDescriptor pointBufferDesc = {};
    pointBufferDesc.label = {"Vertex buffer"};
    pointBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    pointBufferDesc.nextInChain = nullptr;
    pointBufferDesc.size = sizeof(points);
    pointBufferDesc.mappedAtCreation = true;
    WGPUBuffer pointBuffer = wgpuDeviceCreateBuffer(device,&pointBufferDesc);

    // Map the buffer --- get the pointer to the buffer data and memcpy our data to it
    void* pointBufferAddr = wgpuBufferGetMappedRange(pointBuffer,0,sizeof(points));
    memcpy(pointBufferAddr,points,sizeof(points));
    wgpuBufferUnmap(pointBuffer);

    // Index buffer --- identifies which points are different vertices
    // using fixed size int to ensure GPU gets the right size int from us
    // Need 36 = 3 per triangle * 2 triangles per face * 6 faces
    uint32_t indices[36] = {
        1, 5, 7, 1, 7, 3,
        0, 2, 6, 0, 6, 4,
        0, 1, 3, 0, 3, 2,
        4, 6, 7, 4, 7, 5,
        2, 3, 7, 2, 7, 6,
        0, 4, 5, 0, 5, 1
    };

    WGPUBufferDescriptor indexBufferDesc = {};
    indexBufferDesc.label = {"Index buffer"};
    indexBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    indexBufferDesc.nextInChain = nullptr;
    indexBufferDesc.size = sizeof(indices);
    indexBufferDesc.mappedAtCreation = true;
    WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device,&indexBufferDesc);

    // Map the buffer --- get the pointer to the buffer data and memcpy our data to it
    void* indexBufferAddr = wgpuBufferGetMappedRange(indexBuffer,0,sizeof(indices));
    memcpy(indexBufferAddr,points,sizeof(indices));
    wgpuBufferUnmap(indexBuffer);

    // Uniform buffer for coordinate transformations
    // Initial transforms:
    CoordTransform tf_object = {{
        1., 0., 0., 0.,
        0., 1., 0., 0.,
        0., 1., 0., 0.,
        0., 0., 0., 1.
    }};

    CoordTransform tf_light = {{
        1., 0., 0., 0.,
        0., 1., 0., 0.,
        0., 1., 0., 0.,
        0., 0., 0., 1.
    }};

    WGPUBufferDescriptor transformBufferDesc = {};
    transformBufferDesc.label = {"Coordinate transform buffer"};
    transformBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    transformBufferDesc.nextInChain = nullptr;
    transformBufferDesc.size = 2 * sizeof(CoordTransform); // we need to hold transform of object and light
    transformBufferDesc.mappedAtCreation = true;
    WGPUBuffer transformBuffer = wgpuDeviceCreateBuffer(device,&transformBufferDesc);

    void* tfBufferAddr = wgpuBufferGetMappedRange(transformBuffer,0,sizeof(CoordTransform));
    void* tfBufferAddrHalf = wgpuBufferGetMappedRange(transformBuffer,sizeof(CoordTransform),sizeof(CoordTransform));
    memcpy(tfBufferAddr,tf_object.coords,sizeof(CoordTransform));
    memcpy(tfBufferAddrHalf,tf_light.coords,sizeof(CoordTransform));
    wgpuBufferUnmap(transformBuffer);

    // Uniform buffer for lighting information
    /*
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.label = {"Lighting buffer"};
    vertexBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    vertexBufferDesc.nextInChain = nullptr;
    vertexBufferDesc.size = 16;
    vertexBufferDesc.mappedAtCreation = true;
    WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device,&vertexBufferDesc);
    */
    // Create bind group to hold buffers
    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.label = {"Bind group layout"};
    bglDesc.nextInChain = nullptr;
    WGPUBindGroupLayoutEntry layoutEntries[3];

    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntries[1].buffer.type = WGPUBufferBindingType_Uniform;

    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntries[2].buffer.type = WGPUBufferBindingType_Uniform;

    bglDesc.entries = layoutEntries;
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device,&bglDesc);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.entryCount = 3; // 3 buffers
    bgDesc.nextInChain = nullptr;
    bgDesc.label = {"Bind group"};
    bgDesc.layout = layout;

    WGPUBindGroupEntry entries[3];
    entries[0].binding = 0;
    entries[0].buffer = pointBuffer;
    entries[0].offset = 0;
    entries[0].size = WGPU_WHOLE_SIZE;

    entries[1].binding = 1;
    entries[1].buffer = indexBuffer;
    entries[1].offset = 0;
    entries[1].size = WGPU_WHOLE_SIZE;

    entries[2].binding = 2;
    entries[2].buffer = transformBuffer;
    entries[2].offset = 0;
    entries[2].size = WGPU_WHOLE_SIZE;
    
    bgDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device,&bgDesc);

    // Create pipeline
    WGPUPipelineLayoutDescriptor pipelineLayoutDescRender{};
    WGPUBindGroupLayout layoutsRender[] = {layout}; // only one bind group
    pipelineLayoutDescRender.bindGroupLayouts = layoutsRender;
    pipelineLayoutDescRender.bindGroupLayoutCount = 1;
    WGPUPipelineLayout pipelineLayoutRender = wgpuDeviceCreatePipelineLayout(device,&pipelineLayoutDescRender);

    WGPURenderPipelineDescriptor renderDesc;
    renderDesc.label = {"particle-render-pipeline"};

    //renderDesc.vertex.module = renderShader; TODO SHADER GOES HERE
    //renderDesc.vertex.entryPoint = "vs_main";

    renderDesc.layout = pipelineLayoutRender;
    renderDesc.vertex.bufferCount = 0;
    renderDesc.vertex.buffers = nullptr;
    renderDesc.vertex.constantCount = 0;
    renderDesc.vertex.constants = nullptr;

    WGPUFragmentState fragment{};
    //fragment.module = renderShader; TODO SHADER GOES HERE
    //fragment.entryPoint = "fs_main";
    fragment.constantCount = 0;
    fragment.constants = nullptr;

    renderDesc.depthStencil = nullptr; 
    renderDesc.fragment = &fragment;

    WGPUBlendState blendState;
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;

    blendState.alpha.srcFactor = WGPUBlendFactor_Zero;
    blendState.alpha.dstFactor = WGPUBlendFactor_One;
    blendState.alpha.operation = WGPUBlendOperation_Add;
    
    WGPUColorTargetState colorTarget;
    colorTarget.format = preferred_format;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    renderDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;

    renderDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    renderDesc.primitive.frontFace = WGPUFrontFace_CCW;
    renderDesc.primitive.cullMode = WGPUCullMode_None;
    renderDesc.multisample.count = 1;
    renderDesc.multisample.mask = ~0u;
    renderDesc.multisample.alphaToCoverageEnabled = false;

    WGPURenderPipeline renderPipeline = wgpuDeviceCreateRenderPipeline(device,&renderDesc); 

    // Write created pipeline components to struct passed as input
    *output = {
        .pointBuffer=pointBuffer,
        .indexBuffer=indexBuffer,
        .transformBuffer=transformBuffer,
        .bindGroup=bindGroup,
        .renderPipeline=renderPipeline
    };
}

void error_callback(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2) {
    // Handle the error scope result here
    if (message.length > 0) {
        printf("Status: %d, Error type: %d, Message: %.*s\n", (int)status, (int)type, (int)message.length, message.data);
    }
}

// Get the next surface texture and target view
SurfaceViewData get_next_surface_view_data(WGPUSurface* surface) {
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(*surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal) {
        return {surfaceTexture, nullptr};
    }

    WGPUTextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.label = {"Surface texture view"};
    viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect = WGPUTextureAspect_All;
    viewDescriptor.usage = WGPUTextureUsage_RenderAttachment; // Use for rendering
    WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

    // Return texture view
    return {surfaceTexture, targetView};
}

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

void main_loop(WGPUSurface* surface_ptr, WGPUDevice* device_ptr) {
    // Main rendering loop to run
    WGPUSurface surface = *surface_ptr;
    WGPUDevice device = *device_ptr;

    // Push the error scope to catch Validation errors
    wgpuDevicePushErrorScope(device,WGPUErrorFilter_Internal);

    // Command encoder writes instructions
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain = nullptr;
    encoderDesc.label = WGPUStringView{"Command encoder", WGPU_STRLEN};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    SurfaceViewData surfViewData = get_next_surface_view_data(&surface);
    WGPUSurfaceTexture surface_texture = surfViewData.surfaceTexture;
    WGPUTextureView targetView = surfViewData.textureView;

    // Texture can be released after getting texture view if backend isn't WGPU
#ifndef WEBGPU_BACKEND_WGPU
    wgpuTextureRelease(surface_texture.texture);
#endif

    // Build render pass encoder
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain = nullptr;

    WGPURenderPassColorAttachment renderPassColorAttachment = {};
    renderPassColorAttachment.view = targetView; // render directly on screen
    renderPassColorAttachment.resolveTarget = nullptr;
    renderPassColorAttachment.loadOp = WGPULoadOp_Clear; // load default color clear
    renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
    renderPassColorAttachment.clearValue = WGPUColor{0.0, 0.6, 0.9, 1.0};

// depth slice option not supported by wgpu-native
#ifndef WEBGPU_BACKEND_WGPU
    renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites = nullptr;

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    wgpuRenderPassEncoderEnd(renderPass);
    
    wgpuRenderPassEncoderRelease(renderPass);

    wgpuSurfacePresent(surface);

#ifdef WEBGPU_BACKEND_WGPU
    wgpuTextureRelease(surface_texture.texture);
#endif

    // Pop the error scope to print errors that were caught
    WGPUPopErrorScopeCallbackInfo cbInfo;
    cbInfo.callback = &error_callback;
    cbInfo.nextInChain = nullptr;
    cbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    wgpuDevicePopErrorScope(device,cbInfo);
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

    // Use GLFW for windows
    if (!glfwInit()) {
        fprintf(stderr,"Failed to initialize GLFW!\n");
        return 1;
    }
    glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(640,480,"Simple WebGPU test",nullptr,nullptr);
    if (!window) {
        fprintf(stderr,"Failed to initialize window!\n");
        glfwTerminate();
        return 1;
    }

    WGPUSurface surface = glfwGetWGPUSurface(instance, window);
    WGPUSurfaceConfiguration config = {};
    config.nextInChain = nullptr;

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    config.width = fbWidth;
    config.height = fbHeight;

    WGPUSurfaceCapabilities capabilities;
    wgpuSurfaceGetCapabilities(surface,adapter,&capabilities);

    if (capabilities.formatCount == 0) {
        fprintf(stderr,"No supported surface formats!\n");
        return 1;
    }

    WGPUTextureFormat preferredForamt = capabilities.formats[0];
    config.format = preferredForamt;

    config.viewFormatCount = 0;
    config.viewFormats = nullptr;

    config.usage = WGPUTextureUsage_RenderAttachment;
    config.device = device;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;

    wgpuSurfaceConfigure(surface,&config);

    while (!glfwWindowShouldClose(window)) {
        main_loop(&surface,&device);
        glfwPollEvents();
    }

    // Cleanup
    glfwDestroyWindow(window);
    glfwTerminate();
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);

    return 0;
    
}
