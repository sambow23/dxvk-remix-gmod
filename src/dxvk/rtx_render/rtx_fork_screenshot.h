#pragma once

#include <mutex>
#include <optional>
#include <string>

#include <remix/remix_c.h>

namespace dxvk {

  class DxvkImage;
  class RtxContext;

  template <typename T>
  class Rc;

  namespace fork_hooks {

    class PresentedScreenshotQueue {
    public:
      bool request(const char* absolutePath);
      bool request(const std::string& absolutePath);
      std::optional<std::string> consume();
      void clear();

    private:
      std::mutex mutex_;
      std::optional<std::string> pendingPath_;
    };

    remixapi_ErrorCode requestPresentedScreenshot(const char* absolutePath);
    std::optional<std::string> consumePresentedScreenshotRequest();
    void clearPresentedScreenshotRequest();
    void exportPresentedScreenshot(RtxContext& ctx, Rc<DxvkImage> targetImage, const std::string& absolutePath);

  } // namespace fork_hooks

} // namespace dxvk
