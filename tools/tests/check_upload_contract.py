#!/usr/bin/env python3
"""Static ownership/order guards. Not a substitute for a Vulkan/PS4 runtime test."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
text = (root / "src/rendering/vk_context.cpp").read_text()


def section(start, end):
    return text.split(start, 1)[1].split(end, 1)[0]


finish = section("void VkContext::finishUploadBatch(", "void VkContext::pollUploadBatches(")
assert finish.index("inFlightBatches_.reserve") < finish.index("vkQueueSubmit")
assert finish.index("inFlightBatches_.push_back") < finish.index("vkQueueSubmit")
assert "const VkResult submitted = vkQueueSubmit" in finish
assert "const VkResult created = vkCreateFence" in finish
assert "const VkResult ended = vkEndCommandBuffer" in finish
assert "batchFence = immFence" not in finish
assert "separateQueue ? transferQueue_ : graphicsQueue" in finish
wait = section("bool VkContext::waitAllUploads(", "void VkContext::deferStagingCleanup(")
assert wait.index("return false;") < wait.index("vkDestroyBuffer")
assert "5000000000ull" in wait
ui = section("VkDescriptorSet VkContext::uploadImGuiTexture(", "void VkContext::releaseSurface(")
assert ui.index("uiTextures_.reserve") < ui.index("vkCreateBuffer")
assert ui.index("vkCreateImageView") < ui.index("immediateSubmit")
assert ui.index("vkAllocateDescriptorSets") < ui.index("immediateSubmit")
assert ui.index("uiTextures_.push_back") < ui.index("immediateSubmit")
assert "if (!uploaded)" in ui
assert "vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS" in ui
assert "vkMapMemory(device, stagingMemory, 0, imageSize, 0, &mapped) != VK_SUCCESS" in ui
app = (root / "src/core/application.cpp").read_text()
callback = app.split("interfaceSource().setArchive(", 1)[1].split("std::string addonsDir", 1)[0]
assert "readFileBounded" in callback and "trimFileCache(0)" not in callback
print("PASS static upload ordering, failed-completion retention, UI ownership and source-cache guards")
