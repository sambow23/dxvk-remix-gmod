threads_dep = dependency('threads')

includes = [
    'TracyC.h',
    'Tracy.hpp',
    'TracyCUDA.hpp',
    'TracyD3D11.hpp',
    'TracyD3D12.hpp',
    'TracyLua.hpp',
    'TracyOpenCL.hpp',
    'TracyOpenGL.hpp',
    'TracyVulkan.hpp'
]

client_includes = [
    'client/tracy_concurrentqueue.h',
    'client/tracy_rpmalloc.hpp',
    'client/tracy_SPSCQueue.h',
    'client/TracyArmCpuTable.hpp',
    'client/TracyCallstack.h',
    'client/TracyCallstack.hpp',
    'client/TracyCpuid.hpp',
    'client/TracyDebug.hpp',
    'client/TracyDxt1.hpp',
    'client/TracyFastVector.hpp',
    'client/TracyKCore.hpp',
    'client/TracyLock.hpp',
    'client/TracyProfiler.hpp',
    'client/TracyRingBuffer.hpp',
    'client/TracyScoped.hpp',
    'client/TracyStringHelpers.hpp',
    'client/TracySysPower.hpp',
    'client/TracySysTime.hpp',
    'client/TracySysTrace.hpp',
    'client/TracyThread.hpp'
]

common_includes = [
    'common/tracy_lz4.hpp',
    'common/tracy_lz4hc.hpp',
    'common/TracyAlign.hpp',
    'common/TracyAlloc.hpp',
    'common/TracyApi.h',
    'common/TracyColor.hpp',
    'common/TracyForceInline.hpp',
    'common/TracyMutex.hpp',
    'common/TracyProtocol.hpp',
    'common/TracyQueue.hpp',
    'common/TracySocket.hpp',
    'common/TracyStackFrames.hpp',
    'common/TracySystem.hpp',
    'common/TracyVersion.hpp',
    'common/TracyWinFamily.hpp',
    'common/TracyYield.hpp'
]

libbacktrace_includes = [
    'libbacktrace/backtrace.hpp',
    'libbacktrace/config.h',
    'libbacktrace/filenames.hpp',
    'libbacktrace/internal.hpp'
]

tracy_header_files = includes + client_includes + common_includes + libbacktrace_includes

tracy_src = [
    'TracyClient.cpp'
]

tracy_public_include_dirs = include_directories('.')

compiler = meson.get_compiler('cpp')
override_options = []

if compiler.has_define('_MSC_VER')
  override_options += 'cpp_std=c++14'
else
  override_options += 'cpp_std=c++11'
endif

tracy = static_library('tracy', tracy_src, tracy_header_files,
dependencies        : [ threads_dep ],
include_directories : tracy_public_include_dirs,
override_options    : override_options)

tracy_dep_compile_args = []

tracy_dep = declare_dependency(
    compile_args        : tracy_dep_compile_args,
    link_with           : tracy,
    include_directories : tracy_public_include_dirs)

tracy_dep_dynamic = declare_dependency(
    include_directories : tracy_public_include_dirs)

meson.override_dependency('tracy', tracy_dep)