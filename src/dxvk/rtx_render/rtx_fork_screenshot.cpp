#include "rtx_fork_screenshot.h"

namespace dxvk::fork_hooks {
  namespace {
    PresentedScreenshotQueue g_presentedScreenshotQueue;
  }

  bool PresentedScreenshotQueue::request(const char* absolutePath) {
    if (absolutePath == nullptr || absolutePath[0] == '\0') {
      return false;
    }

    return request(std::string(absolutePath));
  }

  bool PresentedScreenshotQueue::request(const std::string& absolutePath) {
    if (absolutePath.empty()) {
      return false;
    }

    std::lock_guard lock(mutex_);
    if (pendingPath_.has_value()) {
      return false;
    }

    pendingPath_ = absolutePath;
    return true;
  }

  std::optional<std::string> PresentedScreenshotQueue::consume() {
    std::lock_guard lock(mutex_);
    std::optional<std::string> result = std::move(pendingPath_);
    pendingPath_.reset();
    return result;
  }

  void PresentedScreenshotQueue::clear() {
    std::lock_guard lock(mutex_);
    pendingPath_.reset();
  }

  remixapi_ErrorCode requestPresentedScreenshot(const char* absolutePath) {
    if (absolutePath == nullptr || absolutePath[0] == '\0') {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    return g_presentedScreenshotQueue.request(absolutePath)
      ? REMIXAPI_ERROR_CODE_SUCCESS
      : REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
  }

  std::optional<std::string> consumePresentedScreenshotRequest() {
    return g_presentedScreenshotQueue.consume();
  }

  void clearPresentedScreenshotRequest() {
    g_presentedScreenshotQueue.clear();
  }

} // namespace dxvk::fork_hooks
