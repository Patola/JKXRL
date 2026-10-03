#!/usr/bin/env python3
"""Source-contract guard for shared-pool uploads, not a GPU integration test."""
import pathlib
import re
import unittest


SOURCE = (pathlib.Path(__file__).resolve().parents[1] /
          'OpenJK/code/rd-vulkan/vk_backend.cpp').read_text()


def body(source, name):
    # Ignore braces inside C++ strings, character literals and comments.
    cleaned = re.sub(r"""//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""",
                     lambda m: ' ' * len(m[0]), source, flags=re.S)
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', cleaned)
    if not match:
        raise AssertionError('Function not found: ' + name)
    start = match.end()
    depth = 1
    for end in range(start, len(cleaned)):
        depth += (cleaned[end] == '{') - (cleaned[end] == '}')
        if depth == 0:
            return source[start:end]
    raise AssertionError('Unclosed function: ' + name)


class UploadBoundary(unittest.TestCase):
    def check_weather_draw(self, source):
        for function in ('VK_BuildWeatherBatches', 'VK_RecordWeather'):
            self.assertNotRegex(body(source, function),
                                r'VK_(?:Backend_RegisterTexture|CreateTextureFromPixels|PrepareWeatherResources)\s*\(')

    def test_weather_draw_does_not_upload(self):
        self.check_weather_draw(SOURCE)

    def test_rejects_original_lazy_upload_regression(self):
        changed = SOURCE.replace('static void VK_BuildWeatherBatches()\n{',
                                 'static void VK_BuildWeatherBatches()\n{\n'
                                 'VK_Backend_RegisterTexture("rain");', 1)
        self.assertNotEqual(changed, SOURCE)
        with self.assertRaises(AssertionError):
            self.check_weather_draw(changed)

    def test_preflight_precedes_both_eyes_and_guard_spans_them(self):
        render = body(SOURCE, 'VK_RenderEyes')
        order = [render.index(token) for token in (
            'VK_PrepareWeatherResources()', 'vkResetCommandPool(',
            'vk.stereoCommandsRecording = true', 'VK_RecordTestPattern(',
            'vk.stereoCommandsRecording = false', 'vkQueueSubmit(')]
        self.assertEqual(order, sorted(order))
        self.assertIn('VK_Backend_RegisterTexture(', body(SOURCE, 'VK_PrepareWeatherResources'))

    def test_every_shared_pool_upload_checks_guard_first(self):
        for function in ('VK_UploadBuffer', 'VK_CreateTextureFromPixels', 'VK_UpdateTexturePixels'):
            upload = body(SOURCE, function)
            self.assertRegex(upload, r'^\s*if\s*\(!VK_AllowSharedPoolUpload\(')
            self.assertLess(upload.index('VK_AllowSharedPoolUpload('), upload.index('vkResetCommandPool('))
        guard = body(SOURCE, 'VK_AllowSharedPoolUpload')
        self.assertIn('if (!vk.stereoCommandsRecording) return true;', guard)
        self.assertIn('return false;', guard)


if __name__ == '__main__':
    unittest.main()
