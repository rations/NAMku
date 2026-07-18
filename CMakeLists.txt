# NAMku — Neural Amp Modeler for Haiku, a raw VST3 plug-in (no framework).
#
# The DSP core is the MIT-licensed NeuralAmpModelerCore + AudioDSPTools by
# Steven Atkinson (vendored under deps/, pinned to the versions proven in the
# author's Linux port), with Eigen (MPL2) as the linear-algebra backend.
#
# Builds two ways:
#   - standalone: point VST3_SDK_DIR at a Haiku-patched VST 3 SDK checkout
#     (default: ../VST3-haiku/vst3sdk) and configure this directory directly;
#   - as a subdirectory of a project that already builds that SDK (the parent
#     must provide the SDK targets and public_sdk_SOURCE_DIR).

cmake_minimum_required(VERSION 3.25.0)

project(smtg-namku
    VERSION 0.1.0
    DESCRIPTION "NAMku neural amp modeler VST3 for Haiku"
)

if(NOT TARGET sdk)
    set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../VST3-haiku/vst3sdk"
        CACHE PATH "Haiku-patched VST 3 SDK source tree")
    if(NOT EXISTS ${VST3_SDK_DIR}/CMakeLists.txt)
        message(FATAL_ERROR
            "VST 3 SDK not found at '${VST3_SDK_DIR}' - set -DVST3_SDK_DIR=<path>")
    endif()

    # Headless SDK: core libraries plus the validator (part of the hosting
    # examples), no VSTGUI (no Haiku backend yet), no plug-in examples.
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "VSTGUI has no Haiku backend yet" FORCE)
    set(SMTG_ENABLE_WAYLAND_SUPPORT OFF CACHE BOOL "no Wayland on Haiku" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "validator" FORCE)
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "plug-in examples not needed" FORCE)
    set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "run manually instead" FORCE)
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "install manually instead" FORCE)

    add_subdirectory(${VST3_SDK_DIR} vst3sdk)

    # The SDK's plugin helper functions read these directory-scoped variables
    # (smtg_target_add_library_main silently adds no module entry point without
    # public_sdk_SOURCE_DIR); the SDK sets them only in its own scope.
    set(public_sdk_SOURCE_DIR "${VST3_SDK_DIR}/public.sdk")
    set(pluginterfaces_SOURCE_DIR "${VST3_SDK_DIR}/pluginterfaces")
endif()

set(NAMKU_DEPS ${CMAKE_CURRENT_SOURCE_DIR}/deps)

# ---------------------------------------------------------------------------
# NAM DSP core as a static library, linked --whole-archive: each network
# architecture (WaveNet, LSTM, ConvNet) self-registers via a file-scope static
# initialiser, and the linker would otherwise drop those object files from the
# plug-in .so, breaking model loading at runtime.
# ---------------------------------------------------------------------------
add_library(NamDsp STATIC
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/activations.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/container.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/conv1d.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/convnet.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/dsp.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/get_dsp.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/lstm.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/ring_buffer.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/util.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/wavenet/model.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/wavenet/a2_fast.cpp
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM/wavenet/slimmable.cpp
    ${NAMKU_DEPS}/AudioDSPTools/dsp/dsp.cpp
    ${NAMKU_DEPS}/AudioDSPTools/dsp/ImpulseResponse.cpp
    ${NAMKU_DEPS}/AudioDSPTools/dsp/NoiseGate.cpp
    ${NAMKU_DEPS}/AudioDSPTools/dsp/RecursiveLinearFilter.cpp
    ${NAMKU_DEPS}/AudioDSPTools/dsp/wav.cpp
    ${NAMKU_DEPS}/tonestack/ToneStack.cpp
)

target_compile_definitions(NamDsp PUBLIC NAM_ENABLE_A2_FAST)

target_include_directories(NamDsp PUBLIC
    ${NAMKU_DEPS}/NeuralAmpModelerCore
    ${NAMKU_DEPS}/NeuralAmpModelerCore/NAM
    ${NAMKU_DEPS}/NeuralAmpModelerCore/Dependencies/nlohmann
    ${NAMKU_DEPS}/AudioDSPTools/dsp
    ${NAMKU_DEPS}/eigen
    ${NAMKU_DEPS}/tonestack
)

# C++20: NAM's slimmable wavenet uses std::atomic<std::shared_ptr> (a C++20
# library feature), matching the Linux port which also builds as C++20.
target_compile_features(NamDsp PUBLIC cxx_std_20)
target_compile_options(NamDsp PRIVATE -ffast-math -Wno-unused-parameter)

# ---------------------------------------------------------------------------
# The plug-in
# ---------------------------------------------------------------------------
smtg_add_vst3plugin(NAMku
    source/inamfileloader.h
    source/namids.h
    source/namgeometry.h
    source/version.h
    source/resamplingnam.h
    source/namprocessor.h
    source/namprocessor.cpp
    source/namcontroller.h
    source/namcontroller.cpp
    source/namview.h
    source/namview.cpp
    source/namfactory.cpp
)

smtg_target_configure_version_file(NAMku)

# Editor art layers (imported by gui/make_assets.sh, committed) -> bundle
# Contents/Resources/gui/.
file(GLOB NAMKU_GUI_PNGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resource/gui/*.png")
smtg_target_add_plugin_resources(NAMku
    RESOURCES ${NAMKU_GUI_PNGS}
    OUTPUT_SUBDIRECTORY gui
)

# Title (Michroma) and body (Roboto) fonts, installed into the user font
# directory on first editor open (see namview.cpp) so BFont can select them.
smtg_target_add_plugin_resources(NAMku
    RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/fonts/Michroma-Regular.ttf"
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/fonts/Roboto-Regular.ttf"
    OUTPUT_SUBDIRECTORY fonts
)

target_compile_features(NAMku
    PUBLIC
        cxx_std_17
)
target_compile_options(NAMku PRIVATE -ffast-math)

# Native Haiku editor: the HaikuPlugView helper header lives next to the
# Haiku-patched SDK tree (VST3-haiku/haiku), and the view links the Be API.
if(DEFINED VST3_SDK_DIR)
    set(NAMKU_HAIKU_SUPPORT_DIR "${VST3_SDK_DIR}/../haiku")
else()
    set(NAMKU_HAIKU_SUPPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../VST3-haiku/haiku")
endif()
if(NOT EXISTS "${NAMKU_HAIKU_SUPPORT_DIR}/haikuplugview.h")
    message(FATAL_ERROR
        "HaikuPlugView helper not found at '${NAMKU_HAIKU_SUPPORT_DIR}' - "
        "check out VST3-haiku next to this repository")
endif()
target_include_directories(NAMku PRIVATE "${NAMKU_HAIKU_SUPPORT_DIR}")

target_link_libraries(NAMku
    PRIVATE
        sdk
        -Wl,--whole-archive NamDsp -Wl,--no-whole-archive
        be
        tracker # BFilePanel
        translation # BTranslationUtils
)
