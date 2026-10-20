#include <pch.h>
#include "VC_Vk.h"
#include <Config.h>
#include "precompile/vc_Shader_Vk.h"

VelocityConvert_Vk::VelocityConvert_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Setup descriptor layouts
    std::vector<VkDescriptorSetLayoutBinding> bindings = { CreateBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
                                                           CreateBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) };
    CreateLayouts(bindings);

    // Setup descriptor pool sizes and allocate sets
    std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, _maxFramesInFlight },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _maxFramesInFlight } };
    CreateDescriptorPool(poolSizes, _maxFramesInFlight);
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);

    // Load precompiled shader (GLSL, entry point "main") and create compute pipeline
    std::vector<char> shaderCode(vc_spv, vc_spv + sizeof(vc_spv));
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode, "main"))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        _init = false;
        return;
    }

    _init = true;
}

bool VelocityConvert_Vk::Convert(VkCommandBuffer InCmdList, VkImageView InResourceView, VkExtent2D InExtent)
{
    if (!_init || _device == VK_NULL_HANDLE || InCmdList == VK_NULL_HANDLE || InResourceView == VK_NULL_HANDLE)
        return false;

    // Create or resize intermediate texture if needed
    if (_intermediateImage == VK_NULL_HANDLE || _width != InExtent.width || _height != InExtent.height)
    {
        if (!CreateImageResource(InExtent.width, InExtent.height, VK_FORMAT_R16G16_SFLOAT,
                                 VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
        {
            LOG_ERROR("[{0}] Failed to create intermediate image!", _name);
            return false;
        }

        _intermediateImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    // Transition intermediate image to GENERAL for compute shader writing
    VkImageMemoryBarrier preBarrier {};
    preBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    preBarrier.oldLayout = _intermediateImageLayout;
    preBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.image = _intermediateImage;
    preBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    preBarrier.subresourceRange.baseMipLevel = 0;
    preBarrier.subresourceRange.levelCount = 1;
    preBarrier.subresourceRange.baseArrayLayer = 0;
    preBarrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (_intermediateImageLayout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        preBarrier.srcAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    else
    {
        preBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }

    preBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(InCmdList, srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &preBarrier);

    // Update descriptors and bind pipeline
    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;
    VkDescriptorSet descriptorSet = _descriptorSets[_currentSetIndex];

    VkDescriptorImageInfo sourceInfo { VK_NULL_HANDLE, InResourceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destInfo { VK_NULL_HANDLE, _intermediateImageView, VK_IMAGE_LAYOUT_GENERAL };

    std::vector<VkWriteDescriptorSet> descriptorWrites = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          &sourceInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          &destInfo, nullptr, nullptr }
    };

    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0,
                           nullptr);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &descriptorSet, 0,
                            nullptr);

    uint32_t dispatchWidth = (InExtent.width + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t dispatchHeight = (InExtent.height + InNumThreadsY - 1) / InNumThreadsY;
    vkCmdDispatch(InCmdList, dispatchWidth, dispatchHeight, 1);

    // Transition intermediate image to SHADER_READ_ONLY_OPTIMAL for FSR consumption
    VkImageMemoryBarrier postBarrier = preBarrier;
    postBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    postBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    postBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    postBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(InCmdList, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &postBarrier);

    _intermediateImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return true;
}
