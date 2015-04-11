#include <iostream>
#include <fstream>
#include <vector>
#include <cassert>

#include <SDL.h>

#include "mantle.h"

const int WIDTH = 1280;
const int HEIGHT = 720;

GR_VOID GR_STDCALL debugCallback(GR_ENUM type, GR_ENUM level, GR_BASE_OBJECT obj, GR_SIZE location, GR_ENUM msgCode, const GR_CHAR* msg, GR_VOID* userData) {
	std::cerr << "DEBUG: " << msg << std::endl;
}

std::vector<char> loadShader(const std::string& filename) {
	std::ifstream file(filename, std::ios::binary | std::ios::ate);

	auto size = file.tellg();
	file.seekg(std::ios::beg, 0);

	std::vector<char> data;
	data.resize(size);

	file.read(&data[0], size);

	file.close();

	return data;
}

int main(int argc, char *args[]) {
	// Initialize function pointers, much like GLEW in OpenGL
	mantleLoadFunctions();

	// Receive debug messages
	grDbgRegisterMsgCallback(debugCallback, nullptr);

	// Find Mantle compatible GPU handles
	GR_APPLICATION_INFO appInfo = {};
	appInfo.apiVersion = GR_API_VERSION;

	GR_PHYSICAL_GPU gpus[GR_MAX_PHYSICAL_GPUS] = {};
	GR_UINT gpuCount = 0;

	grInitAndEnumerateGpus(&appInfo, nullptr, &gpuCount, gpus);

	// Create device from first compatible GPU
	GR_DEVICE_QUEUE_CREATE_INFO queueInfo = {};
	queueInfo.queueType = GR_QUEUE_UNIVERSAL;
	queueInfo.queueCount = 1;

	assert(grGetExtensionSupport(gpus[0], "GR_WSI_WINDOWS") == GR_SUCCESS);

	static const GR_CHAR* const ppExtensions[] = {
		"GR_WSI_WINDOWS",
	};

	GR_DEVICE_CREATE_INFO deviceInfo = {};
	deviceInfo.queueRecordCount = 1;
	deviceInfo.pRequestedQueues = &queueInfo;
	deviceInfo.extensionCount = 1;
	deviceInfo.ppEnabledExtensionNames = ppExtensions;
	deviceInfo.flags |= GR_DEVICE_CREATE_VALIDATION;
	deviceInfo.maxValidationLevel = GR_VALIDATION_LEVEL_4;

	GR_DEVICE device;
	grCreateDevice(gpus[0], &deviceInfo, &device);

	// Create image that can be presented
	GR_WSI_WIN_PRESENTABLE_IMAGE_CREATE_INFO imageCreateInfo = {};
	imageCreateInfo.format = {
		GR_CH_FMT_R8G8B8A8,
		GR_NUM_FMT_UNORM
	};
	imageCreateInfo.usage = GR_IMAGE_USAGE_COLOR_TARGET;
	imageCreateInfo.extent = {WIDTH, HEIGHT};

	GR_IMAGE image;
	GR_GPU_MEMORY imageMemory;
	grWsiWinCreatePresentableImage(device, &imageCreateInfo, &image, &imageMemory);

	GR_MEMORY_REF imageMemoryRef = {};
	imageMemoryRef.mem = imageMemory;

	GR_IMAGE_SUBRESOURCE_RANGE imageColorRange;
	imageColorRange.aspect = GR_IMAGE_ASPECT_COLOR;
	imageColorRange.baseMipLevel = 0;
	imageColorRange.mipLevels = 1;
	imageColorRange.baseArraySlice = 0;
	imageColorRange.arraySize = 1;

	// Get handle to universal queue
	GR_QUEUE universalQueue;
	grGetDeviceQueue(device, GR_QUEUE_UNIVERSAL, 0, &universalQueue);

	// Create and submit command buffer that transitions the image to being presentable
	GR_CMD_BUFFER_CREATE_INFO bufferCreateInfo = {};
	bufferCreateInfo.queueType = GR_QUEUE_UNIVERSAL;

	GR_CMD_BUFFER initCmdBuffer;
	grCreateCommandBuffer(device, &bufferCreateInfo, &initCmdBuffer);

	grBeginCommandBuffer(initCmdBuffer, 0);

		GR_IMAGE_STATE_TRANSITION initTransition = {};
		initTransition.image = image;
		initTransition.oldState = GR_IMAGE_STATE_UNINITIALIZED;
		initTransition.newState = GR_WSI_WIN_IMAGE_STATE_PRESENT_WINDOWED;
		initTransition.subresourceRange = imageColorRange;

		grCmdPrepareImages(initCmdBuffer, 1, &initTransition);

	grEndCommandBuffer(initCmdBuffer);

	grQueueSubmit(universalQueue, 1, &initCmdBuffer, 1, &imageMemoryRef, 0);

	// Create window to present to and retrieve its handle
	SDL_Init(SDL_INIT_VIDEO);

	SDL_Window* window = SDL_CreateWindow("Mantle Hello Triangle", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);

	// Describe presenting from image to window
	GR_WSI_WIN_PRESENT_INFO presentInfo = {};
	presentInfo.hWndDest = GetActiveWindow();
	presentInfo.srcImage = image;
	presentInfo.presentMode = GR_WSI_WIN_PRESENT_MODE_WINDOWED;

	// Create fence for CPU synchronization with rendering
	GR_FENCE fence;
	GR_FENCE_CREATE_INFO fenceCreateInfo = {};
	grCreateFence(device, &fenceCreateInfo, &fence);

	// Create color target view
	GR_COLOR_TARGET_VIEW colorTargetView;
	GR_COLOR_TARGET_VIEW_CREATE_INFO colorTargetViewCreateInfo = {};
	colorTargetViewCreateInfo.image = image;
	colorTargetViewCreateInfo.arraySize = 1;
	colorTargetViewCreateInfo.baseArraySlice = 0;
	colorTargetViewCreateInfo.mipLevel = 0;
	colorTargetViewCreateInfo.format.channelFormat = GR_CH_FMT_R8G8B8A8;
	colorTargetViewCreateInfo.format.numericFormat = GR_NUM_FMT_UNORM;

	grCreateColorTargetView(device, &colorTargetViewCreateInfo, &colorTargetView);

	// Create target states
	GR_MSAA_STATE_OBJECT msaaState;
	GR_MSAA_STATE_CREATE_INFO msaaStateCreateInfo = {};
	msaaStateCreateInfo.samples = 1;
	msaaStateCreateInfo.sampleMask = 0xF; // RGBA bits

	grCreateMsaaState(device, &msaaStateCreateInfo, &msaaState);

	GR_VIEWPORT_STATE_OBJECT viewportState;
	GR_VIEWPORT_STATE_CREATE_INFO viewportStateCreateInfo = {};
	viewportStateCreateInfo.viewportCount = 1;
	viewportStateCreateInfo.scissorEnable = GR_FALSE;
	viewportStateCreateInfo.viewports[0].width = 1280;
	viewportStateCreateInfo.viewports[0].height = 720;
	viewportStateCreateInfo.viewports[0].minDepth = 0;
	viewportStateCreateInfo.viewports[0].maxDepth = 1;

	grCreateViewportState(device, &viewportStateCreateInfo, &viewportState);

	GR_COLOR_BLEND_STATE_OBJECT colorBlendState;
	GR_COLOR_BLEND_STATE_CREATE_INFO blendStateCreateInfo = {};
	blendStateCreateInfo.target[0].blendEnable = GR_TRUE;
	blendStateCreateInfo.target[0].srcBlendColor = GR_BLEND_SRC_ALPHA;
	blendStateCreateInfo.target[0].destBlendColor = GR_BLEND_ONE_MINUS_SRC_ALPHA;
	blendStateCreateInfo.target[0].blendFuncColor = GR_BLEND_FUNC_ADD;

	blendStateCreateInfo.target[0].srcBlendAlpha = GR_BLEND_ONE;
	blendStateCreateInfo.target[0].destBlendAlpha = GR_BLEND_ONE;
	blendStateCreateInfo.target[0].blendFuncAlpha = GR_BLEND_FUNC_ADD;

	grCreateColorBlendState(device, &blendStateCreateInfo, &colorBlendState);

	GR_DEPTH_STENCIL_STATE_OBJECT depthStencilState;
	GR_DEPTH_STENCIL_STATE_CREATE_INFO depthStencilStateCreateInfo = {};
	depthStencilStateCreateInfo.depthEnable = GR_FALSE;
	depthStencilStateCreateInfo.stencilEnable = GR_FALSE;
	depthStencilStateCreateInfo.depthFunc = GR_COMPARE_LESS;

	depthStencilStateCreateInfo.front.stencilDepthFailOp = GR_STENCIL_OP_KEEP;
	depthStencilStateCreateInfo.front.stencilFailOp = GR_STENCIL_OP_KEEP;
	depthStencilStateCreateInfo.front.stencilPassOp = GR_STENCIL_OP_KEEP;
	depthStencilStateCreateInfo.front.stencilFunc = GR_COMPARE_ALWAYS;
	depthStencilStateCreateInfo.front.stencilRef = 0;

	depthStencilStateCreateInfo.back.stencilDepthFailOp = GR_STENCIL_OP_KEEP;
	depthStencilStateCreateInfo.back.stencilFailOp = GR_STENCIL_OP_KEEP;
	depthStencilStateCreateInfo.back.stencilPassOp = GR_STENCIL_OP_KEEP;
	depthStencilStateCreateInfo.back.stencilFunc = GR_COMPARE_ALWAYS;
	depthStencilStateCreateInfo.back.stencilRef = 0;

	grCreateDepthStencilState(device, &depthStencilStateCreateInfo, &depthStencilState);

	GR_RASTER_STATE_OBJECT rasterState;
	GR_RASTER_STATE_CREATE_INFO rasterStateCreateInfo = {};
	rasterStateCreateInfo.fillMode = GR_FILL_SOLID;
	rasterStateCreateInfo.cullMode = GR_CULL_NONE;
	rasterStateCreateInfo.frontFace = GR_FRONT_FACE_CCW;

	grCreateRasterState(device, &rasterStateCreateInfo, &rasterState);

	// Load shaders
	GR_SHADER vertexShader;
	GR_SHADER_CREATE_INFO vertexShaderCreateInfo = {};

	auto vertexShaderCode = loadShader("shaders/vs.bin");

	vertexShaderCreateInfo.pCode = vertexShaderCode.data();
	vertexShaderCreateInfo.codeSize = vertexShaderCode.size();

	grCreateShader(device, &vertexShaderCreateInfo, &vertexShader);

	GR_SHADER fragShader;
	GR_SHADER_CREATE_INFO fragShaderCreateInfo = {};

	auto fragShaderCode = loadShader("shaders/ps.bin");

	fragShaderCreateInfo.pCode = fragShaderCode.data();
	fragShaderCreateInfo.codeSize = fragShaderCode.size();

	grCreateShader(device, &fragShaderCreateInfo, &fragShader);

	// Create graphics pipeline
	GR_PIPELINE pipeline;
	GR_GRAPHICS_PIPELINE_CREATE_INFO pipelineCreateInfo = {};

	pipelineCreateInfo.vs.shader = vertexShader;
	pipelineCreateInfo.vs.linkConstBufferCount = 0;
	pipelineCreateInfo.vs.dynamicMemoryViewMapping.slotObjectType = GR_SLOT_UNUSED;

	GR_DESCRIPTOR_SLOT_INFO vsDescriptorSlot;
	vsDescriptorSlot.slotObjectType = GR_SLOT_SHADER_RESOURCE;
	vsDescriptorSlot.shaderEntityIndex = 0;

	pipelineCreateInfo.vs.descriptorSetMapping[0].descriptorCount = 1;
	pipelineCreateInfo.vs.descriptorSetMapping[0].pDescriptorInfo = &vsDescriptorSlot;

	pipelineCreateInfo.ps.shader = fragShader;
	pipelineCreateInfo.ps.linkConstBufferCount = 0;
	pipelineCreateInfo.ps.dynamicMemoryViewMapping.slotObjectType = GR_SLOT_UNUSED;

	GR_DESCRIPTOR_SLOT_INFO psDescriptorSlot;
	psDescriptorSlot.slotObjectType = GR_SLOT_UNUSED;
	psDescriptorSlot.shaderEntityIndex = 0;

	pipelineCreateInfo.ps.descriptorSetMapping[0].descriptorCount = 1;
	pipelineCreateInfo.ps.descriptorSetMapping[0].pDescriptorInfo = &psDescriptorSlot;

	pipelineCreateInfo.iaState.topology = GR_TOPOLOGY_TRIANGLE_LIST;
	pipelineCreateInfo.iaState.disableVertexReuse = GR_FALSE;

	pipelineCreateInfo.rsState.depthClipEnable = GR_FALSE;

	pipelineCreateInfo.cbState.logicOp = GR_LOGIC_OP_COPY;
	pipelineCreateInfo.cbState.target[0].blendEnable = GR_TRUE;
	pipelineCreateInfo.cbState.target[0].channelWriteMask = 0xF; // RGBA bits
	pipelineCreateInfo.cbState.target[0].format.channelFormat = GR_CH_FMT_R8G8B8A8;
	pipelineCreateInfo.cbState.target[0].format.numericFormat = GR_NUM_FMT_UNORM;

	pipelineCreateInfo.dbState.format.channelFormat = GR_CH_FMT_R4G4B4A4;
	pipelineCreateInfo.dbState.format.numericFormat = GR_NUM_FMT_UNDEFINED;

	grCreateGraphicsPipeline(device, &pipelineCreateInfo, &pipeline);

	// Allocate memory for pipeline
	GR_MEMORY_REQUIREMENTS memReqs = {};
	GR_SIZE memReqsSize = sizeof(memReqs);
	grGetObjectInfo(pipeline, GR_INFO_TYPE_MEMORY_REQUIREMENTS, &memReqsSize, &memReqs);

	GR_MEMORY_HEAP_PROPERTIES heapProps = {};
	GR_SIZE heapPropsSize = sizeof(heapProps);
	grGetMemoryHeapInfo(device, memReqs.heaps[0], GR_INFO_TYPE_MEMORY_HEAP_PROPERTIES, &heapPropsSize, &heapProps);

	GR_GPU_MEMORY pipelineMemory;
	GR_MEMORY_ALLOC_INFO allocInfo = {};
	allocInfo.size = max(1, memReqs.size / heapProps.pageSize) * heapProps.pageSize;
	allocInfo.alignment = 0;
	allocInfo.memPriority = GR_MEMORY_PRIORITY_HIGH;
	allocInfo.heapCount = 1;
	allocInfo.heaps[0] = memReqs.heaps[0];
	grAllocMemory(device, &allocInfo, &pipelineMemory);

	grBindObjectMemory(pipeline, pipelineMemory, 0);

	GR_MEMORY_REF pipelineMemoryRef = {};
	pipelineMemoryRef.mem = pipelineMemory;

	// Create descriptor set for vertex shader input
	GR_DESCRIPTOR_SET descriptorSet;
	GR_DESCRIPTOR_SET_CREATE_INFO descriptorCreateInfo = {};
	descriptorCreateInfo.slots = 1;

	grCreateDescriptorSet(device, &descriptorCreateInfo, &descriptorSet);

	// Allocate memory for descriptor set
	grGetObjectInfo(descriptorSet, GR_INFO_TYPE_MEMORY_REQUIREMENTS, &memReqsSize, &memReqs);
	grGetMemoryHeapInfo(device, memReqs.heaps[0], GR_INFO_TYPE_MEMORY_HEAP_PROPERTIES, &heapPropsSize, &heapProps);

	GR_GPU_MEMORY descriptorMemory;
	allocInfo.size = max(1, memReqs.size / heapProps.pageSize) * heapProps.pageSize;
	allocInfo.alignment = 0;
	allocInfo.memPriority = GR_MEMORY_PRIORITY_HIGH;
	allocInfo.heapCount = 1;
	allocInfo.heaps[0] = memReqs.heaps[0];
	grAllocMemory(device, &allocInfo, &descriptorMemory);

	grBindObjectMemory(descriptorSet, descriptorMemory, 0);

	GR_MEMORY_REF descriptorMemoryRef = {};
	descriptorMemoryRef.mem = descriptorMemory;

	// Allocate memory for vertex data
	float colors[] = {
		1.0f, 0.0f, 0.0f, 1.0f,
		0.0f, 1.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f
	};

	// TODO: Find heap that is guaranteed to be CPU accessible instead of piggybacking
	GR_GPU_MEMORY vertexDataMemory;
	allocInfo.size = max(1, sizeof(colors) / heapProps.pageSize) * heapProps.pageSize;
	allocInfo.alignment = 0;
	allocInfo.memPriority = GR_MEMORY_PRIORITY_HIGH;
	allocInfo.heapCount = 1;
	allocInfo.heaps[0] = memReqs.heaps[0];
	grAllocMemory(device, &allocInfo, &vertexDataMemory);

	void* bufferPointer;
	grMapMemory(vertexDataMemory, 0, &bufferPointer);

	memcpy(bufferPointer, colors, sizeof(colors));

	grUnmapMemory(vertexDataMemory);

	GR_MEMORY_REF vertexDataMemRef = {};
	vertexDataMemRef.mem = vertexDataMemory;

	// Create and submit command buffer that transitions vertex data memory to being shader accessible
	GR_CMD_BUFFER initDataCmdBuffer;
	grCreateCommandBuffer(device, &bufferCreateInfo, &initDataCmdBuffer);

	grBeginCommandBuffer(initDataCmdBuffer, 0);

		GR_MEMORY_STATE_TRANSITION dataTransition = {};
		dataTransition.mem = vertexDataMemory;
		dataTransition.oldState = GR_MEMORY_STATE_DATA_TRANSFER;
		dataTransition.newState = GR_MEMORY_STATE_GRAPHICS_SHADER_READ_ONLY;
		dataTransition.offset = 0;
		dataTransition.regionSize = sizeof(colors);

		grCmdPrepareMemoryRegions(initDataCmdBuffer, 1, &dataTransition);

	grEndCommandBuffer(initDataCmdBuffer);

	grQueueSubmit(universalQueue, 1, &initDataCmdBuffer, 1, &vertexDataMemRef, 0);

	// Attach a view to the vertex data to the descriptor set
	GR_MEMORY_VIEW_ATTACH_INFO memoryViewAttachInfo = {};
	memoryViewAttachInfo.mem = vertexDataMemory;
	memoryViewAttachInfo.offset = 0;
	memoryViewAttachInfo.stride = sizeof(colors) / 3;
	memoryViewAttachInfo.range = sizeof(colors);
	memoryViewAttachInfo.format.channelFormat = GR_CH_FMT_R32G32B32A32;
	memoryViewAttachInfo.format.numericFormat = GR_NUM_FMT_FLOAT;
	memoryViewAttachInfo.state = GR_MEMORY_STATE_GRAPHICS_SHADER_READ_ONLY;

	grBeginDescriptorSetUpdate(descriptorSet);
		grAttachMemoryViewDescriptors(descriptorSet, 0, 1, &memoryViewAttachInfo);
	grEndDescriptorSetUpdate(descriptorSet);

	// Create command buffer that prepares the color target image for rendering
	GR_CMD_BUFFER bufferPrepareRender;
	grCreateCommandBuffer(device, &bufferCreateInfo, &bufferPrepareRender);

	grBeginCommandBuffer(bufferPrepareRender, 0);

		GR_IMAGE_STATE_TRANSITION transition = {};
		transition.image = image;
		transition.oldState = GR_WSI_WIN_IMAGE_STATE_PRESENT_WINDOWED;
		transition.newState = GR_IMAGE_STATE_TARGET_RENDER_ACCESS_OPTIMAL;
		transition.subresourceRange = imageColorRange;

		grCmdPrepareImages(bufferPrepareRender, 1, &transition);

	grEndCommandBuffer(bufferPrepareRender);

	// Create command buffer that clears the color target to black
	GR_CMD_BUFFER bufferDrawTrianglelear;
	grCreateCommandBuffer(device, &bufferCreateInfo, &bufferDrawTrianglelear);

	grBeginCommandBuffer(bufferDrawTrianglelear, 0);

		transition.image = image;
		transition.oldState = GR_IMAGE_STATE_TARGET_RENDER_ACCESS_OPTIMAL;
		transition.newState = GR_IMAGE_STATE_CLEAR;
		transition.subresourceRange = imageColorRange;

		grCmdPrepareImages(bufferDrawTrianglelear, 1, &transition);

		float clearColor[] = {0.0, 0.0, 0.0, 1.0};
		grCmdClearColorImage(bufferDrawTrianglelear, image, clearColor, 1, &imageColorRange);

		transition.image = image;
		transition.oldState = GR_IMAGE_STATE_CLEAR;
		transition.newState = GR_IMAGE_STATE_TARGET_RENDER_ACCESS_OPTIMAL;
		transition.subresourceRange = imageColorRange;

		grCmdPrepareImages(bufferDrawTrianglelear, 1, &transition);

	grEndCommandBuffer(bufferDrawTrianglelear);

	// Create command buffer that renders the triangle
	GR_CMD_BUFFER bufferDrawTriangle;
	grCreateCommandBuffer(device, &bufferCreateInfo, &bufferDrawTriangle);

	grBeginCommandBuffer(bufferDrawTriangle, 0);

		// Bind render target
		GR_COLOR_TARGET_BIND_INFO colorTargetBindInfo;
		colorTargetBindInfo.view = colorTargetView;
		colorTargetBindInfo.colorTargetState = GR_IMAGE_STATE_TARGET_RENDER_ACCESS_OPTIMAL;

		grCmdBindTargets(bufferDrawTriangle, 1, &colorTargetBindInfo, nullptr);

		// Set up dynamic draw state
		grCmdBindStateObject(bufferDrawTriangle, GR_STATE_BIND_MSAA, msaaState);
		grCmdBindStateObject(bufferDrawTriangle, GR_STATE_BIND_VIEWPORT, viewportState);
		grCmdBindStateObject(bufferDrawTriangle, GR_STATE_BIND_COLOR_BLEND, colorBlendState);
		grCmdBindStateObject(bufferDrawTriangle, GR_STATE_BIND_DEPTH_STENCIL, depthStencilState);
		grCmdBindStateObject(bufferDrawTriangle, GR_STATE_BIND_RASTER, rasterState);

		// Bind descriptor set
		grCmdBindDescriptorSet(bufferDrawTriangle, GR_PIPELINE_BIND_POINT_GRAPHICS, 0, descriptorSet, 0);

		// Set graphics pipeline
		grCmdBindPipeline(bufferDrawTriangle, GR_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// Render triangle
		grCmdDraw(bufferDrawTriangle, 0, 3, 0, 1);

	grEndCommandBuffer(bufferDrawTriangle);

	// Create command buffer that transitions color target image back to a presentable state
	GR_CMD_BUFFER bufferFinish;
	grCreateCommandBuffer(device, &bufferCreateInfo, &bufferFinish);

	grBeginCommandBuffer(bufferFinish, 0);

		transition.image = image;
		transition.oldState = GR_IMAGE_STATE_TARGET_RENDER_ACCESS_OPTIMAL;
		transition.newState = GR_WSI_WIN_IMAGE_STATE_PRESENT_WINDOWED;
		transition.subresourceRange = imageColorRange;

		grCmdPrepareImages(bufferFinish, 1, &transition);

	grEndCommandBuffer(bufferFinish);

	// Main loop
	while (true) {
		SDL_Event windowEvent;
		if (SDL_PollEvent(&windowEvent)) {
			if (windowEvent.type == SDL_QUIT) break;
		}

		// Wait for previous frame to end
		grWaitForFences(device, 1, &fence, true, 1);

		// Submit command buffers along with memory references
		GR_MEMORY_REF memoryRefs[] = {imageMemoryRef, pipelineMemoryRef, descriptorMemoryRef, vertexDataMemRef};
		GR_CMD_BUFFER commandBuffers[] = {bufferPrepareRender, bufferDrawTrianglelear, bufferDrawTriangle, bufferFinish};

		grQueueSubmit(universalQueue, 4, commandBuffers, 4, memoryRefs, fence);

		// Present image to the window
		grWsiWinQueuePresent(universalQueue, &presentInfo);
	}

	return 0;
}