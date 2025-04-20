#include "dxvk_device_filter.h"

namespace dxvk {
  
  DxvkDeviceFilter::DxvkDeviceFilter(DxvkDeviceFilterFlags flags)
  : m_flags(flags) {
    m_matchDeviceName = env::getEnvVar("DXVK_FILTER_DEVICE_NAME");
    
    if (m_matchDeviceName.size() != 0)
      m_flags.set(DxvkDeviceFilterFlag::MatchDeviceName);
  }
  
  
  DxvkDeviceFilter::~DxvkDeviceFilter() {
    
  }
  
  
  bool DxvkDeviceFilter::testAdapter(const VkPhysicalDeviceProperties& properties) const {
    if (properties.apiVersion < VK_MAKE_VERSION(1, 1, 0)) {
      Logger::warn(str::format("Skipping Vulkan 1.0 adapter: ", properties.deviceName));
      return false;
    }
  
    // We want to include both AMD and NVIDIA GPUs in the list
    // but prioritize AMD for actual rendering
    
    // Skip CPU virtual devices
    if (m_flags.test(DxvkDeviceFilterFlag::SkipCpuDevices)) {
      if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
        Logger::warn(str::format("Skipping CPU adapter: ", properties.deviceName));
        return false;
      }
    }
  
    // Apply name matching if requested
    if (m_flags.test(DxvkDeviceFilterFlag::MatchDeviceName)) {
      if (std::string(properties.deviceName).find(m_matchDeviceName) == std::string::npos) {
        Logger::info(str::format("Adapter doesn't match name filter: ", properties.deviceName));
        return false;
      }
    }
  
    // Accept all GPU adapters that pass the above checks
    return true;
  }
  
}
