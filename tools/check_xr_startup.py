#!/usr/bin/env python3
"""Exercise the renderer's actual instance creation; optionally probe a runtime."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, help='Opt-in live OpenXR runtime manifest')
    args = parser.parse_args()
    source = (ROOT / 'OpenJK/code/rd-vulkan/vk_backend.cpp').read_text()
    registration = body((ROOT / 'OpenJK/code/rd-vulkan/tr_init.cpp').read_text(),
                        'RE_BeginRegistration')
    # Failed startup must stop before any media registration or window setup.
    guard = registration[:registration.index('R_ImageLoader_Init()')]
    assert 'if ( !VK_Backend_Init() )' in guard
    assert 'ri.Error( ERR_FATAL' in guard and 'return;' in guard
    harness = r'''
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "jkxrl_version.h"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
constexpr int PRINT_WARNING=1, PRINT_ALL=0;
struct {XrInstance xrInstance=XR_NULL_HANDLE;} vk;
struct Imports {
    void Printf(int, const char* format, ...) {
        va_list args; va_start(args,format); std::vprintf(format,args); va_end(args);
        std::fflush(stdout);
    }
} ri;
'''
    if not args.runtime:
        harness += r'''
bool extensionAvailable=true;
XrResult creationResult=XR_SUCCESS;
int createCalls=0, propertyCalls=0;
extern "C" XrResult xrEnumerateInstanceExtensionProperties(const char*,uint32_t capacity,
    uint32_t* count,XrExtensionProperties* extensions) {
    *count=extensionAvailable?1:0;
    if(capacity && extensionAvailable)
        std::strcpy(extensions[0].extensionName,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
    return XR_SUCCESS;
}
extern "C" XrResult xrCreateInstance(const XrInstanceCreateInfo* info,XrInstance* instance) {
    ++createCalls;
    // Emulate a 1.0-only runtime while compiling against newer OpenXR headers.
    if(XR_VERSION_MAJOR(info->applicationInfo.apiVersion)!=1 ||
       XR_VERSION_MINOR(info->applicationInfo.apiVersion)!=0)
        return XR_ERROR_API_VERSION_UNSUPPORTED;
    assert(info->enabledExtensionCount==1);
    assert(!std::strcmp(info->enabledExtensionNames[0],XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME));
    assert(!std::strcmp(info->applicationInfo.applicationName,JKXRL_NAME));
    if(XR_SUCCEEDED(creationResult)) *instance=reinterpret_cast<XrInstance>(1);
    return creationResult;
}
extern "C" XrResult xrGetInstanceProperties(XrInstance instance,XrInstanceProperties* props) {
    assert(instance!=XR_NULL_HANDLE); ++propertyCalls;
    std::strcpy(props->runtimeName,"Test OpenXR 1.0 runtime");
    props->runtimeVersion=XR_MAKE_VERSION(1,0,0);
    return XR_SUCCESS;
}
extern "C" XrResult xrResultToString(XrInstance,XrResult result,char* text) {
    std::snprintf(text,XR_MAX_RESULT_STRING_SIZE,"mock result %d",result); return XR_SUCCESS;
}
'''
    for declaration, name in (
        ('void VK_LogXrFailure(const char* what, XrResult result)', 'VK_LogXrFailure'),
        ('bool VK_CheckXr(XrResult result, const char* what)', 'VK_CheckXr'),
        ('bool VK_HasXrExtension(const char* extensionName)', 'VK_HasXrExtension'),
        ('bool VK_CreateXrInstance()', 'VK_CreateXrInstance'),
    ):
        definition = re.search(r'^static (?:void|bool) ' + name + r'\([^;{]*\)\s*\{',
                               source, re.M)
        assert definition, name
        harness += declaration + '{' + body(source[definition.start():], name) + '}\n'
    if args.runtime:
        harness += '''int main() {
            if (!VK_CreateXrInstance()) return 1;
            return XR_FAILED(xrDestroyInstance(vk.xrInstance)) ? 1 : 0;
        }'''
    else:
        harness += '''int main() {
            assert(VK_CreateXrInstance());
            assert(createCalls==1 && propertyCalls==1);
            vk.xrInstance=XR_NULL_HANDLE;
            creationResult=XR_ERROR_INITIALIZATION_FAILED;
            assert(!VK_CreateXrInstance());
            assert(createCalls==2 && propertyCalls==1);
            creationResult=XR_ERROR_API_VERSION_UNSUPPORTED;
            assert(!VK_CreateXrInstance());
            assert(createCalls==3 && propertyCalls==1);
            extensionAvailable=false;
            assert(!VK_CreateXrInstance());
            assert(createCalls==3 && propertyCalls==1);
        }'''
    with tempfile.TemporaryDirectory(prefix='jkxrl-xr-startup-') as temp:
        executable = str(Path(temp) / 'probe')
        command = ['c++', '-std=c++17', '-I' + str(ROOT / 'OpenJK/shared/qcommon'),
                   '-x', 'c++', '-', '-o', executable]
        if args.runtime:
            command += ['-lopenxr_loader']
        subprocess.run(command, input=harness, text=True, check=True)
        env = os.environ.copy()
        if args.runtime:
            env['XR_RUNTIME_JSON'] = str(args.runtime.resolve(strict=True))
        subprocess.run([executable], env=env, check=True, timeout=30)
    print('PASS: OpenXR startup and fail-fast registration')


if __name__ == '__main__':
    main()
