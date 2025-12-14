#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <cmath>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <unistd.h>
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
    WGPUTexture depthTexture;
    WGPUTextureView depthTextureView;
    uint32_t height;
    uint32_t width;
} PipelineSetupOutput;

void error_callback(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2) {
    // Handle the error scope result here
    if (message.length > 0) {
        printf("Status: %d, Error type: %d, Message: %.*s\n", (int)status, (int)type, (int)message.length, message.data);
    }
}

static void on_uncaptured_error(const WGPUDevice* device, WGPUErrorType type, WGPUStringView msg, void*, void*) {
  fprintf(stderr, "UNCAPTURED %d: %.*s\n", (int)type, (int)msg.length, msg.data);
}
static void on_device_lost(const WGPUDevice* device, WGPUDeviceLostReason reason, WGPUStringView msg, void*, void*) {
  fprintf(stderr, "DEVICE LOST %d: %.*s\n", (int)reason, (int)msg.length, msg.data);
}

void setDefault(WGPUStencilFaceState &stencilFaceState) {
    stencilFaceState.compare = WGPUCompareFunction_Always;
    stencilFaceState.failOp = WGPUStencilOperation_Keep;
    stencilFaceState.depthFailOp = WGPUStencilOperation_Keep;
    stencilFaceState.passOp = WGPUStencilOperation_Keep;
}

void setDefault(WGPUDepthStencilState &depthStencilState) {
    depthStencilState.format = WGPUTextureFormat_Undefined;
    depthStencilState.depthWriteEnabled = WGPUOptionalBool_False;
    depthStencilState.depthCompare = WGPUCompareFunction_Always;
    depthStencilState.stencilReadMask = 0xFFFFFFFF;
    depthStencilState.stencilWriteMask = 0xFFFFFFFF;
    depthStencilState.depthBias = 0;
    depthStencilState.depthBiasSlopeScale = 0;
    depthStencilState.depthBiasClamp = 0;
    setDefault(depthStencilState.stencilFront);
    setDefault(depthStencilState.stencilBack);
}

void setDefault(WGPUBindGroupLayoutEntry &bindingLayout) {
    bindingLayout.buffer.nextInChain = nullptr;
    bindingLayout.buffer.type = WGPUBufferBindingType_Undefined;
    bindingLayout.buffer.hasDynamicOffset = false;

    bindingLayout.sampler.nextInChain = nullptr;
    bindingLayout.sampler.type = WGPUSamplerBindingType_BindingNotUsed;

    bindingLayout.storageTexture.nextInChain = nullptr;
    bindingLayout.storageTexture.access = WGPUStorageTextureAccess_BindingNotUsed;
    bindingLayout.storageTexture.format = WGPUTextureFormat_Undefined;
    bindingLayout.storageTexture.viewDimension = WGPUTextureViewDimension_Undefined;

    bindingLayout.texture.nextInChain = nullptr;
    bindingLayout.texture.multisampled = false;
    bindingLayout.texture.sampleType = WGPUTextureSampleType_BindingNotUsed;
    bindingLayout.texture.viewDimension = WGPUTextureViewDimension_Undefined;
}

// Adapted from tutorial, thus why it uses C++ functions instead of fopen()/fgets()
std::string LoadWGSLShader(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filepath);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();  // Read entire file
    return buffer.str();
}

void create_buffers(PipelineSetupOutput* output, WGPUDevice* device_ptr, WGPUTextureFormat* preferredFormat_ptr) {
    // Create the buffers we'll be using and put them in a bind group
    WGPUDevice device = *device_ptr;
    WGPUTextureFormat preferred_format = *preferredFormat_ptr;

    // Push the error scope to catch Validation errors
    wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);

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
    pointBufferDesc.label = {"Vertex buffer",WGPU_STRLEN};
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
    indexBufferDesc.label = {"Index buffer",WGPU_STRLEN};
    indexBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
    indexBufferDesc.nextInChain = nullptr;
    indexBufferDesc.size = sizeof(indices);
    indexBufferDesc.mappedAtCreation = true;
    WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device,&indexBufferDesc);

    // Map the buffer --- get the pointer to the buffer data and memcpy our data to it
    void* indexBufferAddr = wgpuBufferGetMappedRange(indexBuffer,0,sizeof(indices));
    memcpy(indexBufferAddr,indices,sizeof(indices));
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
    transformBufferDesc.label = {"Coordinate transform buffer",WGPU_STRLEN};
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

    // Create bind group to hold buffers
    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.label = {"Bind group layout",WGPU_STRLEN};
    bglDesc.nextInChain = nullptr;
    bglDesc.entryCount = 1;
    WGPUBindGroupLayoutEntry layoutEntries[1] = {};

    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].nextInChain = nullptr;

    bglDesc.entries = layoutEntries;
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device,&bglDesc);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.entryCount = 1;
    bgDesc.nextInChain = nullptr;
    bgDesc.label = {"Bind group",WGPU_STRLEN};
    bgDesc.layout = layout;

    WGPUBindGroupEntry entries[1] = {};

    entries[0].binding = 0;
    entries[0].buffer = transformBuffer;
    entries[0].offset = 0;
    entries[0].size = WGPU_WHOLE_SIZE;
    entries[0].nextInChain = nullptr;
    
    bgDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device,&bgDesc);

    // Create pipeline
    WGPUPipelineLayoutDescriptor pipelineLayoutDescRender = {};
    WGPUBindGroupLayout layoutsRender[] = {layout}; // only one bind group
    pipelineLayoutDescRender.bindGroupLayouts = layoutsRender;
    pipelineLayoutDescRender.bindGroupLayoutCount = 1;
    WGPUPipelineLayout pipelineLayoutRender = wgpuDeviceCreatePipelineLayout(device,&pipelineLayoutDescRender);

    WGPURenderPipelineDescriptor renderDesc = {};
    renderDesc.label = {"particle-render-pipeline",WGPU_STRLEN};

    // Load our shader for rendering

    std::string shaderString = LoadWGSLShader("src/simple_shader.wgsl");
    WGPUStringView shaderStringView = {};
    shaderStringView.data = shaderString.c_str();
    shaderStringView.length = shaderString.length();

    WGPUShaderModuleWGSLDescriptor shaderCodeDesc = {};
    shaderCodeDesc.code = shaderStringView;
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = WGPUSType_ShaderSourceWGSL;

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &shaderCodeDesc.chain;
    WGPUShaderModule renderShader = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    renderDesc.vertex.module = renderShader;
    renderDesc.vertex.entryPoint = {"vs_main",WGPU_STRLEN};

    // How our vertex data is stored in the buffer
    WGPUVertexBufferLayout vertexBufLayout = {};
    vertexBufLayout.arrayStride = sizeof(float) * 3; // 3 floats per vertex
    vertexBufLayout.nextInChain = nullptr;
    vertexBufLayout.attributeCount = 1;
    vertexBufLayout.stepMode = WGPUVertexStepMode_Vertex;

    WGPUVertexAttribute vertexAttr;
    vertexAttr.format = WGPUVertexFormat_Float32x3;
    vertexAttr.offset = 0;
    vertexAttr.nextInChain = nullptr;
    vertexAttr.shaderLocation = 0; // corresponds to @location(0) in the shader
    vertexBufLayout.attributes = &vertexAttr;

    renderDesc.layout = pipelineLayoutRender;
    renderDesc.vertex.bufferCount = 1;
    renderDesc.vertex.buffers = &vertexBufLayout;
    renderDesc.vertex.constantCount = 0;
    renderDesc.vertex.constants = nullptr;

    WGPUFragmentState fragment = {};
    fragment.module = renderShader;
    fragment.entryPoint = {"fs_main",WGPU_STRLEN};
    fragment.constantCount = 0;
    fragment.constants = nullptr;
    renderDesc.fragment = &fragment;

    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;

    blendState.alpha.srcFactor = WGPUBlendFactor_Zero;
    blendState.alpha.dstFactor = WGPUBlendFactor_One;
    blendState.alpha.operation = WGPUBlendOperation_Add;
    
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = preferred_format;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    renderDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;

    // Depth stencil is necessary to figure out which fragments are drawn in 3D
    // This configuration is taken from https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/3d-meshes/depth-buffer.html
    WGPUDepthStencilState depthStencilState = {};
    setDefault(depthStencilState);

    // Blend fragment only if depth is less than current Z buffer
    depthStencilState.depthCompare = WGPUCompareFunction_Less;
    // Update depth in Z buffer once fragment is drawn
    depthStencilState.depthWriteEnabled = WGPUOptionalBool_True;
    // Store the depth format in a variable
    WGPUTextureFormat depthTextureFormat = WGPUTextureFormat_Depth24Plus;
    depthStencilState.format = depthTextureFormat;
    depthStencilState.stencilReadMask = 0;
    depthStencilState.stencilWriteMask = 0;
    renderDesc.depthStencil = &depthStencilState;

    WGPUTextureDescriptor depthTextureDesc = {};
    depthTextureDesc.dimension = WGPUTextureDimension_2D;
    depthTextureDesc.format = depthTextureFormat;
    depthTextureDesc.mipLevelCount = 1;
    depthTextureDesc.sampleCount = 1;

    // We pass height and width with our setup params struct
    uint32_t height = output->height;
    uint32_t width = output->width;
    depthTextureDesc.size = {width, height, 1};
    depthTextureDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthTextureDesc.viewFormatCount = 1;
    depthTextureDesc.viewFormats = &depthTextureFormat;
    WGPUTexture depthTexture = wgpuDeviceCreateTexture(device, &depthTextureDesc);

    WGPUTextureViewDescriptor depthTextureViewDesc = {};
    depthTextureViewDesc.nextInChain = nullptr;
    depthTextureViewDesc.aspect = WGPUTextureAspect_DepthOnly;
    depthTextureViewDesc.baseArrayLayer = 0;
    depthTextureViewDesc.arrayLayerCount = 1;
    depthTextureViewDesc.baseMipLevel = 0;
    depthTextureViewDesc.mipLevelCount = 1;
    depthTextureViewDesc.dimension = WGPUTextureViewDimension_2D;
    depthTextureViewDesc.format = depthTextureFormat;
    WGPUTextureView depthTextureView = wgpuTextureCreateView(depthTexture, &depthTextureViewDesc);

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
        .renderPipeline=renderPipeline,
        .depthTexture=depthTexture,
        .depthTextureView=depthTextureView
    };

    // Pop error scope to see any errors
    WGPUPopErrorScopeCallbackInfo cbInfo = {};
    cbInfo.callback = &error_callback;
    cbInfo.nextInChain = nullptr;
    cbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    wgpuDevicePopErrorScope(device,cbInfo);
}

// Get the next surface texture and target view
SurfaceViewData get_next_surface_view_data(WGPUSurface* surface) {
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(*surface, &surfaceTexture);
    //printf("Surface status: %d\n",surfaceTexture.status);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        printf("SurfaceTexture is not success! Skipping iteration\n");
        return {surfaceTexture, nullptr};
    }

    WGPUTextureViewDescriptor viewDescriptor = {};
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.label = {"Surface texture view",WGPU_STRLEN};
    viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect = WGPUTextureAspect_All;
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

void main_loop(WGPUSurface* surface_ptr, WGPUDevice* device_ptr, WGPUQueue* queue_ptr, PipelineSetupOutput* pipeline_setup_ptr) {
    // Main rendering loop to run
    WGPUSurface surface = *surface_ptr;
    WGPUDevice device = *device_ptr;
    WGPUQueue queue = *queue_ptr;
    PipelineSetupOutput setup_params = *pipeline_setup_ptr;

    // Push the error scope to catch Validation errors
    wgpuDevicePushErrorScope(device,WGPUErrorFilter_Validation);

    // Command encoder writes instructions
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain = nullptr;
    encoderDesc.label = WGPUStringView{"Command encoder", WGPU_STRLEN};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    SurfaceViewData surfViewData = get_next_surface_view_data(&surface);
    WGPUSurfaceTexture surface_texture = surfViewData.surfaceTexture;

    printf("Surface texture status: %d\n", surface_texture.status);

    WGPUTextureView targetView = surfViewData.textureView;
    if (!targetView) {
        printf("Target view is NULL! Skipping iteration\n");
        wgpuCommandEncoderRelease(encoder);
        return;
    }

    // Texture can be released after getting texture view if backend isn't WGPU
#ifndef WEBGPU_BACKEND_WGPU
    //wgpuTextureRelease(surface_texture.texture);
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

    WGPURenderPassDepthStencilAttachment depthStencilAttachment = {};

    // The view of the depth texture
    depthStencilAttachment.view = setup_params.depthTextureView;

    // The initial value of the depth buffer, meaning "far"
    depthStencilAttachment.depthClearValue = 1.0f;
    // Operation settings comparable to the color attachment
    depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
    // we could turn off writing to the depth buffer globally here
    depthStencilAttachment.depthReadOnly = false;

    // Stencil setup, mandatory but unused
    depthStencilAttachment.stencilClearValue = 0;
    // Set LoadOp and StoreOp to Undefined in dawn
    depthStencilAttachment.stencilLoadOp = WGPULoadOp_Undefined; 
    depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthStencilAttachment.stencilReadOnly = true;

    renderPassDesc.depthStencilAttachment = &depthStencilAttachment;
    //renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites = nullptr;

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    wgpuRenderPassEncoderSetPipeline(renderPass,setup_params.renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass,0,setup_params.bindGroup,0,nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass,0,setup_params.pointBuffer,0,24*sizeof(float));
    wgpuRenderPassEncoderSetIndexBuffer(renderPass,setup_params.indexBuffer,WGPUIndexFormat_Uint32,0,36*sizeof(uint32_t));

    wgpuRenderPassEncoderDrawIndexed(renderPass,36,1,0,0,0);

    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);

    WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.nextInChain = nullptr;
    cmdBufferDescriptor.label = {"Command buffer",WGPU_STRLEN};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder,&cmdBufferDescriptor);

    printf("SurfaceTexture: %p\n",surface_texture);
    printf("Texture: %p\n",surface_texture.texture);
    printf("Target View: %p\n",targetView);

    wgpuQueueSubmit(queue,1,&command);
    wgpuCommandBufferRelease(command);
    wgpuSurfacePresent(surface);
    
    wgpuTextureViewRelease(targetView);
    wgpuTextureRelease(surface_texture.texture);
    wgpuCommandEncoderRelease(encoder);

#ifdef WEBGPU_BACKEND_WGPU
    wgpuTextureRelease(surface_texture.texture);
#endif  

    // Pop the error scope to print errors that were caught
    WGPUPopErrorScopeCallbackInfo cbInfo = {};
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

    deviceDesc.uncapturedErrorCallbackInfo.callback = &on_uncaptured_error;
    deviceDesc.uncapturedErrorCallbackInfo.nextInChain = nullptr;
    deviceDesc.uncapturedErrorCallbackInfo.userdata1 = nullptr;
    deviceDesc.uncapturedErrorCallbackInfo.userdata2 = nullptr;

    deviceDesc.deviceLostCallbackInfo.callback = on_device_lost;
    deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceDesc.deviceLostCallbackInfo.nextInChain = nullptr;
    deviceDesc.deviceLostCallbackInfo.userdata1 = nullptr;
    deviceDesc.deviceLostCallbackInfo.userdata2 = nullptr;

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

    WGPUTextureFormat preferredFormat = capabilities.formats[0];
    config.format = preferredFormat;

    config.viewFormatCount = 0;
    config.viewFormats = nullptr;

    config.usage = WGPUTextureUsage_RenderAttachment;
    config.device = device;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;

    PipelineSetupOutput setup_params = {.height=(uint32_t)fbHeight,.width=(uint32_t)fbWidth};
    create_buffers(&setup_params,&device,&preferredFormat);

    wgpuSurfaceConfigure(surface,&config);

    int fbW, fbH;
    while (!glfwWindowShouldClose(window)) {
        main_loop(&surface,&device,&queue,&setup_params);
        glfwPollEvents();
        wgpuInstanceProcessEvents(instance);
        glfwGetFramebufferSize(window, &fbW, &fbH);
        printf("fb: %dx%d\n", fbW, fbH);
        sleep(1);


#ifdef WEBGPU_BACKEND_DAWN
        wgpuDeviceTick(device);
#endif
#ifdef WEBGPU_BACKEND_WGPU
        wgpuDevicePoll(device, false, nullptr);
#endif
    }

    // Cleanup
    glfwDestroyWindow(window);
    glfwTerminate();
    wgpuTextureViewRelease(setup_params.depthTextureView);
    wgpuTextureRelease(setup_params.depthTexture);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);

    return 0;
    
}
