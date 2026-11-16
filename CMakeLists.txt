cmake_minimum_required(VERSION 3.22)
project(AsterHost VERSION 0.3.18 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory("${JUCE_DIR}" JUCE)
juce_add_gui_app(AsterHost PRODUCT_NAME "aster" COMPANY_NAME "Aster Audio" VERSION "${PROJECT_VERSION}" ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/aster.png" ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/aster.png")
juce_generate_juce_header(AsterHost)
target_sources(AsterHost PRIVATE StandaloneHost.cpp Capture.h ApplicationCapture.h HostPanel.h Vst2Host.h PeakHold.h)
target_compile_options(AsterHost PRIVATE /utf-8)
target_compile_definitions(AsterHost PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(AsterHost PRIVATE juce::juce_audio_utils mmdevapi ole32 shell32 dwmapi)

# JUCE's resource command otherwise depends only on the icon, leaving the
# Windows file version stale after a version-only reconfigure.
if(WIN32)
  get_target_property(aster_generated AsterHost JUCE_GENERATED_SOURCES_DIRECTORY)
  get_target_property(aster_info AsterHost JUCE_INFO_FILE)
  add_custom_command(OUTPUT "${aster_generated}/resources.rc" APPEND DEPENDS "${aster_info}")
endif()

# Explicit local dependencies also work with localized MSVC include output.
set_source_files_properties(StandaloneHost.cpp PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/HostPanel.h;${CMAKE_CURRENT_SOURCE_DIR}/InputPicker.h;${CMAKE_CURRENT_SOURCE_DIR}/InputTests.h;${CMAKE_CURRENT_SOURCE_DIR}/BundledPlugins.h;${CMAKE_CURRENT_SOURCE_DIR}/BundledPluginTests.h;${CMAKE_CURRENT_SOURCE_DIR}/Capture.h;${CMAKE_CURRENT_SOURCE_DIR}/ApplicationCapture.h;${CMAKE_CURRENT_SOURCE_DIR}/PeakHold.h;${CMAKE_CURRENT_SOURCE_DIR}/Vst2Host.h")
