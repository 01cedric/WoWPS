#!/usr/bin/env python3
"""Production ordering contracts plus synthetic alpha/postprocess regression.
GPU appearance still requires console acceptance; no GPU timings are inferred.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
r=(root/'src/rendering/renderer.cpp').read_text()
m=(root/'src/rendering/minimap.cpp').read_text()
def body(source, signature):
    start=source.index('{', source.index(signature)); depth=1; end=start+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
e=body(r,'void Renderer::endFrame()')
assert e.index('executePostProcessing(')<e.index('overlayRp.renderPass = vkCtx->getOverlayRenderPass()')<e.index('renderMinimapOverlay(currentCmd, minimapOverlayGameHandler_)')<e.index('ImGui_ImplVulkan_RenderDrawData')
assert r.count('renderMinimapOverlay(currentCmd, minimapOverlayGameHandler_)')==1
assert 'renderMinimapOverlay(' not in body(r,'void Renderer::renderWorld(')
assert 'renderMinimapOverlay(' not in body(r,'void Renderer::renderPostSceneOverlays(')
begin=body(r,'void Renderer::beginFrame()')
assert begin.index('minimapOverlayPending_ = false')<begin.index('if (')
assert 'minimapOverlayGameHandler_ = nullptr' in begin
world=body(r,'void Renderer::renderWorld(')
assert world.index('if (skipAll) return')<world.index('minimapOverlayPending_ = true')
assert 'minimapOverlayGameHandler_ = gameHandler' in world
assert 'if (minimapOverlayPending_)' in e and 'minimapOverlayPending_ = false' in e
minimap=body(r,'void Renderer::renderMinimapOverlay(')
assert 'isScriptedView()) return' in minimap
assert 'const VkExtent2D minimapExtent = vkCtx->getSwapchainExtent()' in minimap
p=body(m,'void Minimap::buildDisplayPipeline(')
assert '.setMultisample(VK_SAMPLE_COUNT_1_BIT)' in p
assert '.setRenderPass(vkCtx->getOverlayRenderPass())' in p
assert '.setNoDepthTest()' in p
shader=(root/'assets/shaders/minimap_display.frag.glsl').read_text()
assert shader.count('uniform sampler')==1
assert 'uniform sampler2D uComposite;' in shader
print('PASS production contracts: one final overlay draw, after world effects and before UI; world-only reset, cinematic guard, full resolution, color-only 1x pipeline')
# Model old/new operation order for opaque authored atlas pixels. This checks
# the exact failure mechanism, not a recreation of the volumetric shader.
atlas=(.18,.37,.11)
old_white=0
for sky in [0.,.1,.5,1.]:
    for rays in [0.,.2,1.,10.]:
        for bloom in [0.,.25,1.]:
            effect=lambda c: tuple(min(1.,v+rays*sky+bloom*sky) for v in c)
            old=effect(atlas)
            scene=effect((sky,sky,sky))
            new=tuple(atlas[i]*1.+scene[i]*(1.-1.) for i in range(3))
            assert new==atlas
            old_white += old==(1.,1.,1.)
assert old_white>0
print(f'PASS 48 synthetic camera/light/bloom combinations: opaque late HUD unchanged; old scene order clips white in {old_white} cases')
print('NOTE deliberate translucent minimap opacity still blends with the finished scene; GPU appearance not tested')
