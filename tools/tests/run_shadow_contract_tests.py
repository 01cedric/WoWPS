#!/usr/bin/env python3
"""Compile the production projection and terrain shadow submission with fake GPU calls.
This checks geometry, offsets and submission only; it cannot confirm GPU depth writes.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]

def function(path, signature):
    text = (root / path).read_text()
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

with tempfile.TemporaryDirectory() as temporary:
    temp = Path(temporary)
    (temp / 'shadow_production.inc').write_text(
        function('src/rendering/renderer.cpp', 'glm::mat4 Renderer::computeLightSpaceMatrix()') + '\n' +
        function('src/rendering/terrain_renderer.cpp', 'void TerrainRenderer::renderShadow('))
    (temp / 'viewport_production.inc').write_text(function(
        'ps4/third_party/ps4_vulkan/source/vulkan-ps4/src/vk_ps4_command.c',
        'VKAPI_ATTR void VKAPI_CALL\nvk_ps4_CmdSetViewport('))
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-g',
               '-DGLM_FORCE_DEPTH_ZERO_TO_ONE', '-I' + str(temp),
               '-I' + str(root / 'include'), '-I' + str(root / 'extern/glm'),
               '-I' + str(root / 'ps4/third_party/ps4_vulkan/include')]
    if os.environ.get('SANITIZE') == '1':
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        os.environ.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
        os.environ.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
    command += [str(root / 'tools/tests/shadow_contract_test.cpp'),
                str(root / 'src/rendering/frustum.cpp'), '-o', str(temp / 'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(temp / 'test')], check=True)
