#include "rtx_fork_screenshot.h"

#include "../dxvk_objects.h"
#include "rtx_asset_exporter.h"
#include "rtx_context.h"
#include "../../util/log/log.h"

namespace dxvk::fork_hooks {

  void exportPresentedScreenshot(RtxContext& ctx, Rc<DxvkImage> targetImage, const std::string& absolutePath) {
    if (!targetImage.ptr()) {
      Logger::err("RTX: Cannot export requested presented screenshot because target image is null");
      return;
    }

    ctx.getCommonObjects()->metaExporter().dumpImageToExactPath(&ctx, absolutePath, targetImage);
  }

} // namespace dxvk::fork_hooks
