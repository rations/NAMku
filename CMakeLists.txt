# NAMku — Neural Amp Modeler for Haiku, a raw VST3 plug-in (no framework).
#
# The DSP core is the MIT-licensed NeuralAmpModelerCore + AudioDSPTools by
# Steven Atkinson (vendored under deps/, pinned to the versions proven in the
# author's Linux port), with Eigen (MPL2) as the linear-algebra backend.
#
# Builds two ways:
#   - standalone: point VST3_SDK_DIR at a Haiku-patched VST 3 SDK checkout
#     (default: ../VST3-renku/vst3sdk) and configure this directory directly;
#   - as a subdirectory of a project that already builds that SDK (the parent
#     must provide the SDK targets and public_sdk_SOURCE_DIR).

cmake_minimum_required(VERSION 3.25.0)

project(smtg-namku
    VERSION 0.5.0
    DESCRIPTION "NAMku neural amp modeler VST3 for Haiku"
)

if(NOT TARGET sdk)
    set(VST3_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../VST3-renku/vst3sdk"
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

    # The audio backends the standalone app plays on, defined before the SDK
    # (its audiohost sample looks for them too).
    include(${VST3_SDK_DIR}/../haiku/jdbackend.cmake OPTIONAL)

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
    set(NAMKU_HAIKU_SUPPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../VST3-renku/haiku")
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

# ---------------------------------------------------------------------------
# The standalone app: NAMku without a DAW, on the Media Kit or on JACK.
# It hosts the NAMku.vst3 bundle built above through the SDK audiohost's
# AudioClient on JackDAW's audio backends (VST3-renku's haiku/jdbackend): the
# Media Kit built in, JACK as the jack.so add-on, so it never links libjack.
# ---------------------------------------------------------------------------
if(HAIKU)
    include("${NAMKU_HAIKU_SUPPORT_DIR}/jdbackend.cmake")
    set(NAMKU_SDK_DIR "${NAMKU_HAIKU_SUPPORT_DIR}/../vst3sdk")
    set(NAMKU_AUDIOHOST_DIR "${NAMKU_SDK_DIR}/public.sdk/samples/vst-hosting/audiohost")

    add_executable(NAMkuApp
        standalone/main.cpp
        standalone/mainwindow.cpp
        standalone/pluginhost.cpp
        standalone/settings.cpp
        ${NAMKU_AUDIOHOST_DIR}/source/media/audioclient.cpp
        ${NAMKU_AUDIOHOST_DIR}/source/media/jdbackend/jdbackendclient.cpp
        ${NAMKU_SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${NAMKU_SDK_DIR}/public.sdk/source/vst/hosting/module_haiku.cpp
        ${NAMKU_SDK_DIR}/public.sdk/source/common/memorystream.cpp
    )
    target_compile_features(NAMkuApp PRIVATE cxx_std_17)
    # Warnings for our own files; the SDK's are built as the SDK builds them.
    set_source_files_properties(
        standalone/main.cpp standalone/mainwindow.cpp standalone/pluginhost.cpp
        standalone/settings.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
    target_include_directories(NAMkuApp PRIVATE ${NAMKU_SDK_DIR} source)
    target_link_libraries(NAMkuApp PRIVATE sdk_hosting jdbackend be)
    set_target_properties(NAMkuApp
        PROPERTIES
            OUTPUT_NAME NAMku
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>
    )
    # The bundle it hosts, and the JACK add-on it can load.
    add_dependencies(NAMkuApp NAMku)
    if(TARGET jdbackend_jack)
        add_dependencies(NAMkuApp jdbackend_jack)
    endif()

    # Signature, launch flags and version, so Tracker and Deskbar treat it as
    # an application.
    set(NAMKU_RSRC ${CMAKE_CURRENT_BINARY_DIR}/NAMku.rsrc)
    add_custom_command(OUTPUT ${NAMKU_RSRC}
        COMMAND rc -o ${NAMKU_RSRC} ${CMAKE_CURRENT_SOURCE_DIR}/standalone/NAMku.rdef
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/standalone/NAMku.rdef
        COMMENT "Compiling the NAMku app resources"
    )
    add_custom_target(NAMkuAppResources DEPENDS ${NAMKU_RSRC})
    add_dependencies(NAMkuApp NAMkuAppResources)
    add_custom_command(TARGET NAMkuApp POST_BUILD
        COMMAND xres -o $<TARGET_FILE:NAMkuApp> ${NAMKU_RSRC}
        COMMAND mimeset -f $<TARGET_FILE:NAMkuApp>
    )
endif()
