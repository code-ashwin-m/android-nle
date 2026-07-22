# CMakeLists.txt
#
# Builds every subsystem into one shared library, libnleengine.so, loaded
# once by Kotlin's NativeEditorEngine (System.loadLibrary("nleengine")).
# A single .so rather than one-per-subsystem is deliberate: the render
# thread, decoder thread, and JNI entry points all need to share the same
# EditorEngine instance in-process, which is trivial within one library
# and only adds friction (stable C ABI boundaries between libraries) if
# split up -- there is no deployment or team-ownership reason here to pay
# that cost.

cmake_minimum_required(VERSION 3.22)
project(nleengine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NDK media (NdkMediaCodec/Extractor/Muxer), EGL/GLES3, and android/log are
# all part of the platform NDK sysroot -- no external dependency fetching
# needed for Phase 1.
find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles3-lib GLESv3)
find_library(mediandk-lib mediandk)

add_library(nleengine SHARED
    include/nle/core/Types.h

    core/Keyframe.h
    core/Property.h
    core/PropertyBag.h
    core/MathTypes.h
    core/MediaSource.h
    core/Clip.h
    core/Track.h
    core/Timeline.h
    core/Project.h
    core/PlaybackClock.h

    command/Command.h
    command/CommandStack.h
    command/commands/AddClipCommand.h
    command/commands/SetBrightnessCommand.h
    command/commands/TimelineCommands.h

    util/Json.h
    util/Json.cpp

    effects/EffectGraphDef.h
    effects/EffectPackageLoader.h
    effects/EffectPackageLoader.cpp
    effects/EffectNodeRegistry.h
    effects/EffectNodeRegistry.cpp
    effects/EffectRuntime.h
    effects/EffectRuntime.cpp
    effects/nodes/ExternalInputNode.h
    effects/nodes/ColorNode.h
    effects/nodes/TransformNode.h
    effects/nodes/BlurNode.h

    rendergraph/Frame.h
    rendergraph/RenderNode.h
    rendergraph/RenderGraph.h
    rendergraph/nodes/DecoderSourceNode.h
    rendergraph/nodes/DecoderSourceNode.cpp
    rendergraph/nodes/ColorConvertNode.h
    rendergraph/nodes/BrightnessNode.h
    rendergraph/nodes/ClipTransformNode.h
    rendergraph/nodes/PackagedEffectSlotNode.h
    rendergraph/nodes/CompositeNode.h
    rendergraph/nodes/OutputNode.h

    render/GLContext.h
    render/GLContext.cpp
    render/Texture.h
    render/Texture.cpp
    render/TexturePool.h
    render/GraphicsDevice.h
    render/OpenGLRenderer.h
    render/OpenGLRenderer.cpp

    decode/FrameQueue.h
    decode/Decoder.h
    decode/Decoder.cpp
    decode/DecoderThread.h
    decode/DecoderThread.cpp

    encode/Encoder.h
    encode/Encoder.cpp

    playback/PlaybackEngine.h
    playback/PlaybackEngine.cpp

    engine/EditorEngine.h
    engine/EditorEngine.cpp

    util/SpscRingBuffer.h

    jni/JniUtils.h
    jni/JniUtils.cpp
    jni/jni_bridge.cpp
)

target_include_directories(nleengine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nleengine
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${gles3-lib}
    ${mediandk-lib}
)

# Warnings-as-errors is intentionally OFF for this scaffold so the project
# builds cleanly while features are still being fleshed out; flip this on
# once Phase 1 stabilizes -- a professional codebase should not tolerate
# warnings indefinitely, but gating every scaffold commit on zero warnings
# during initial buildout slows iteration for no real benefit yet.
target_compile_options(nleengine PRIVATE -Wall -Wextra)
