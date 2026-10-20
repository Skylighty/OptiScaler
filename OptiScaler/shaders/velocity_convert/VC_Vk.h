#pragma once
#include <vulkan/vulkan.h>
#include <string>

#include <shaders/Shader_Vk.h>

// Converts R16G16_SNORM / R16G16_UNORM motion vectors into an R16G16_SFLOAT
// intermediate image, because the FFX Vulkan backend has no SNORM/UNORM
// R16G16 surface format.
class VelocityConvert_Vk : public Shader_Vk
{
  private:
    VkImageLayout _intermediateImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    VelocityConvert_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~VelocityConvert_Vk() = default;

    bool Convert(VkCommandBuffer InCmdList, VkImageView InResourceView, VkExtent2D InExtent);

    VkImage IntermediateImage() const { return _intermediateImage; }
    VkImageView IntermediateView() const { return _intermediateImageView; }
};
