cmake_minimum_required(VERSION 3.22)
project(A1Revival VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "macOS target")
if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "14.0")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "macOS target" FORCE)
endif()
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/JUCE" CACHE PATH "Pinned JUCE checkout")
add_subdirectory("${JUCE_PATH}" JUCE)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(A1_JUCE_VST3 "${JUCE_PATH}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
set(A1_VST3_ADAPTER "${CMAKE_CURRENT_BINARY_DIR}/vst3-adapter/A1VST3Wrapper.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${A1_JUCE_VST3}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare-vst3-wrapper.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare-vst3-wrapper.py"
    --source "${A1_JUCE_VST3}" --output "${A1_VST3_ADAPTER}"
    RESULT_VARIABLE A1_VST3_ADAPTER_RESULT ERROR_VARIABLE A1_VST3_ADAPTER_ERROR)
if(NOT A1_VST3_ADAPTER_RESULT EQUAL 0)
    message(FATAL_ERROR "${A1_VST3_ADAPTER_ERROR}")
endif()
set(A1_WINE_SOURCE "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-unix/wine")
set(A1_NTDLL_SOURCE "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/x86_64-unix/ntdll.so")
file(SHA256 "${A1_WINE_SOURCE}" A1_WINE_SHA256)
file(SHA256 "${A1_NTDLL_SOURCE}" A1_NTDLL_SHA256)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${A1_WINE_SOURCE}" "${A1_NTDLL_SOURCE}")
configure_file(src/RuntimeBuild.h.in "${CMAKE_CURRENT_BINARY_DIR}/RuntimeBuild.h" @ONLY)
set(A1_X87_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/local/tools/x87sidecar-a1")
set(A1_X87_BINARY "${CMAKE_CURRENT_BINARY_DIR}/x87-runtime/bin/x87sidecar")
add_custom_command(OUTPUT "${A1_X87_BINARY}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare-x87-runtime.py"
        --build-dir "${CMAKE_CURRENT_BINARY_DIR}/x87-runtime"
    DEPENDS tools/prepare-x87-runtime.py patches/x87sidecar-a1.patch VERBATIM)
add_custom_target(A1Runtime ALL DEPENDS "${A1_X87_BINARY}")
set(A1_PRIVATE_WINE "${CMAKE_CURRENT_BINARY_DIR}/a1-wineloader")
add_custom_command(OUTPUT "${A1_PRIVATE_WINE}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${A1_WINE_SOURCE}" "${A1_PRIVATE_WINE}"
    COMMAND /usr/bin/codesign --force --sign - --options runtime --timestamp=none
        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/src/RuntimeEntitlements.plist" "${A1_PRIVATE_WINE}"
    DEPENDS "${A1_WINE_SOURCE}" src/RuntimeEntitlements.plist VERBATIM)
add_custom_target(A1WineRuntime ALL DEPENDS "${A1_PRIVATE_WINE}")
add_executable(A1EngineLauncher src/EngineLauncher.c)
set_target_properties(A1EngineLauncher PROPERTIES OUTPUT_NAME a1-engine-launcher C_STANDARD 11)
target_include_directories(A1EngineLauncher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(A1EngineLauncher PRIVATE -O2 -Wall -Wextra -Werror -mmacosx-version-min=14.0)
target_link_options(A1EngineLauncher PRIVATE -mmacosx-version-min=14.0)
add_library(A1Coop SHARED src/CoopHandshake.c)
set_target_properties(A1Coop PROPERTIES OUTPUT_NAME a1-coop PREFIX "" C_STANDARD 11
    OSX_ARCHITECTURES x86_64)
target_include_directories(A1Coop PRIVATE "${A1_X87_SOURCE}/rosetta_loader/src")
target_compile_options(A1Coop PRIVATE -O2 -Wall -Wextra -Werror -mmacosx-version-min=14.0)
target_link_options(A1Coop PRIVATE -mmacosx-version-min=14.0)
add_dependencies(A1Coop A1Runtime)
foreach(runtime_target IN ITEMS A1EngineLauncher A1Coop)
    add_custom_command(TARGET ${runtime_target} POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - --timestamp=none "$<TARGET_FILE:${runtime_target}>" VERBATIM)
endforeach()
set(ZIG "${CMAKE_CURRENT_SOURCE_DIR}/local/tools/zig-aarch64-macos-0.15.2/zig")
set(HELPER "${CMAKE_CURRENT_BINARY_DIR}/a1-helper.exe")
add_custom_command(OUTPUT "${HELPER}"
    COMMAND "${CMAKE_COMMAND}" -E env "ZIG_GLOBAL_CACHE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/local/zig-cache"
        "${ZIG}" cc -target x86-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror
        "${CMAKE_CURRENT_SOURCE_DIR}/src/WinHelper.c" -o "${HELPER}"
    DEPENDS src/WinHelper.c src/Protocol.h VERBATIM)
add_custom_target(A1Helper ALL DEPENDS "${HELPER}")
set(A1_HELPER_CONTRACTS "${CMAKE_CURRENT_BINARY_DIR}/a1-helper-contracts.exe")
add_custom_command(OUTPUT "${A1_HELPER_CONTRACTS}"
    COMMAND "${CMAKE_COMMAND}" -E env "ZIG_GLOBAL_CACHE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/local/zig-cache"
        "${ZIG}" cc -target x86-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/WinHelperTests.c" -o "${A1_HELPER_CONTRACTS}"
    DEPENDS tests/WinHelperTests.c src/WinHelper.c src/Protocol.h VERBATIM)
add_custom_target(A1HelperContracts ALL DEPENDS "${A1_HELPER_CONTRACTS}")
add_test(NAME WindowsHelperContracts COMMAND "${CMAKE_COMMAND}" -E env
    "CX_BOTTLE_PATH=$ENV{HOME}/Library/Application Support/A1RevivalPreview/bottles"
    "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
    --bottle A1RevivalPreview --no-gui --debugmsg -all,+msgbox "${A1_HELPER_CONTRACTS}")
set_tests_properties(WindowsHelperContracts PROPERTIES TIMEOUT 25)
juce_add_plugin(A1Revival COMPANY_NAME "Richard Peace" BUNDLE_ID "uk.richardpeace.a1revival"
    PLUGIN_MANUFACTURER_CODE RpRv PLUGIN_CODE A1Rv PRODUCT_NAME "A1 Revival"
    DESCRIPTION "Original A1 synth for Cubase"
    PLIST_TO_MERGE "<plist><dict><key>LSMinimumSystemVersion</key><string>14.0</string></dict></plist>"
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE VST3_CATEGORIES "Instrument;Synth"
    FORMATS VST3 COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(A1Revival)
set_source_files_properties("${JUCE_PATH}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.mm"
    PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(A1Revival_VST3 PRIVATE src/VST3Wrapper.mm)
target_include_directories(A1Revival_VST3 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/vst3-adapter")
target_sources(A1Revival PRIVATE src/Processor.cpp src/Bridge.cpp src/Editor.cpp)
set_property(SOURCE src/Editor.cpp APPEND PROPERTY COMPILE_OPTIONS -Wall -Wextra -Werror)
target_include_directories(A1Revival PRIVATE src "${JUCE_PATH}/modules/juce_audio_processors_headless/format_types/VST3_SDK")
target_compile_definitions(A1Revival PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(A1Revival PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
option(A1_UI_COVERAGE "Instrument the restored editor for local coverage checks" OFF)
if(A1_UI_COVERAGE)
    set_property(SOURCE src/Editor.cpp APPEND PROPERTY COMPILE_OPTIONS -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(A1Revival PUBLIC -fprofile-instr-generate)
endif()
add_dependencies(A1Revival_VST3 A1Helper A1EngineLauncher A1Coop A1Runtime A1WineRuntime)
set_property(TARGET A1Revival_VST3 APPEND PROPERTY LINK_DEPENDS "${HELPER}" "${A1_X87_BINARY}"
    "${A1_PRIVATE_WINE}" "$<TARGET_FILE:A1EngineLauncher>" "$<TARGET_FILE:A1Coop>")
file(GLOB A1_UI_ARTWORK CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/local/original-ui/BITMAP_*.png")
list(LENGTH A1_UI_ARTWORK A1_UI_ARTWORK_COUNT)
if(NOT A1_UI_ARTWORK_COUNT EQUAL 21)
    message(FATAL_ERROR "The 21 recovered original A1 UI images are required in local/original-ui")
endif()
set_property(TARGET A1Revival_VST3 APPEND PROPERTY LINK_DEPENDS ${A1_UI_ARTWORK})
add_custom_command(TARGET A1Revival_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:A1EngineLauncher>" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/a1-engine-launcher"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:A1Coop>" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/a1-coop.dylib"
    COMMAND "${CMAKE_COMMAND}" -E copy "${A1_X87_BINARY}" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/x87sidecar"
    COMMAND "${CMAKE_COMMAND}" -E copy "${A1_PRIVATE_WINE}" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/a1-wineloader"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/ThirdParty"
    COMMAND "${CMAKE_COMMAND}" -E copy "${A1_X87_SOURCE}/LICENSE" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/ThirdParty/x87sidecar-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${HELPER}" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/a1-helper.exe"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/local/original/a1.dll" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/a1.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/local/original/A1_Default_Bank2.fxb" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/A1_Default_Bank2.fxb"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/local/original/A1.vstxml" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/A1.vstxml"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/OriginalUI"
    COMMAND "${CMAKE_COMMAND}" -E copy ${A1_UI_ARTWORK} "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/OriginalUI"
    COMMAND /usr/bin/codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:A1Revival_VST3>" VERBATIM)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/local/original-mac/a1_default_bank1.fxb")
    add_custom_command(TARGET A1Revival_VST3 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/local/original-mac/a1_default_bank1.fxb" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources/A1_Default_Bank1.fxb"
        COMMAND /usr/bin/codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:A1Revival_VST3>" VERBATIM)
endif()
add_executable(A1HostContextTests tests/HostContextTests.cpp)
target_compile_options(A1HostContextTests PRIVATE -UNDEBUG)
add_test(NAME CubaseHostContext COMMAND A1HostContextTests)
add_executable(A1BankTests tests/BankTests.cpp)
target_compile_options(A1BankTests PRIVATE -UNDEBUG)
add_test(NAME OriginalBankParsing COMMAND A1BankTests)
add_executable(A1ProcessorTests tests/ProcessorTests.cpp)
target_include_directories(A1ProcessorTests PRIVATE src "${JUCE_PATH}/modules" "${CMAKE_CURRENT_BINARY_DIR}/A1Revival_artefacts/JuceLibraryCode")
target_link_libraries(A1ProcessorTests PRIVATE A1Revival)
target_compile_options(A1ProcessorTests PRIVATE -fno-access-control)
target_include_directories(A1ProcessorTests PRIVATE "${JUCE_PATH}/modules/juce_audio_processors_headless/format_types/VST3_SDK")
add_dependencies(A1ProcessorTests A1Revival_VST3)
add_test(NAME StateAndGenericHost COMMAND A1ProcessorTests "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources")
add_executable(A1ChildProcessTests tests/ChildProcessTests.cpp src/Bridge.cpp)
# This fixture injects only its own native child into a resource-less Bridge.
target_compile_options(A1ChildProcessTests PRIVATE -fno-access-control -Wall -Wextra -Werror)
add_test(NAME BoundedChildShutdown COMMAND A1ChildProcessTests)
set_tests_properties(BoundedChildShutdown PROPERTIES TIMEOUT 5)
add_executable(A1RenderPipelineTests tests/RenderPipelineTests.cpp)
target_include_directories(A1RenderPipelineTests PRIVATE src "${JUCE_PATH}/modules" "${CMAKE_CURRENT_BINARY_DIR}/A1Revival_artefacts/JuceLibraryCode")
target_link_libraries(A1RenderPipelineTests PRIVATE A1Revival)
add_test(NAME NonblockingRenderPipeline COMMAND A1RenderPipelineTests)
set_tests_properties(NonblockingRenderPipeline PROPERTIES TIMEOUT 10)
add_executable(A1PrefetchSupportTests tests/PrefetchSupportTests.cpp)
target_include_directories(A1PrefetchSupportTests PRIVATE "${JUCE_PATH}/modules/juce_audio_processors_headless/format_types/VST3_SDK")
target_compile_options(A1PrefetchSupportTests PRIVATE -Wall -Wextra -Werror)
add_test(NAME RealtimePrefetchContract COMMAND A1PrefetchSupportTests)
add_executable(A1BridgeProcessTests tests/BridgeProcessTests.cpp src/Bridge.cpp)
target_compile_options(A1BridgeProcessTests PRIVATE -fno-access-control -Wall -Wextra -Werror)
add_test(NAME BufferedHelperRecovery COMMAND A1BridgeProcessTests)
set_tests_properties(BufferedHelperRecovery PROPERTIES TIMEOUT 5)
add_executable(A1EditorTests tests/EditorTests.cpp)
target_include_directories(A1EditorTests PRIVATE src "${JUCE_PATH}/modules" "${CMAKE_CURRENT_BINARY_DIR}/A1Revival_artefacts/JuceLibraryCode")
target_include_directories(A1EditorTests PRIVATE "${JUCE_PATH}/modules/juce_audio_processors_headless/format_types/VST3_SDK")
target_link_libraries(A1EditorTests PRIVATE A1Revival)
add_dependencies(A1EditorTests A1Revival_VST3)
add_test(NAME OriginalEditor COMMAND A1EditorTests "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources" "${CMAKE_CURRENT_SOURCE_DIR}/local/ui-restoration/verification/editor.png")
set_tests_properties(OriginalEditor PROPERTIES TIMEOUT 15)
set(A1_TRANSPORT_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/engine-transport-fixture.exe")
add_custom_command(OUTPUT "${A1_TRANSPORT_FIXTURE}"
    COMMAND "${CMAKE_COMMAND}" -E env "ZIG_GLOBAL_CACHE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/local/zig-cache"
        "${ZIG}" cc -target x86-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineTransportFixture.c" -o "${A1_TRANSPORT_FIXTURE}"
    DEPENDS tests/EngineTransportFixture.c VERBATIM)
add_custom_target(A1TransportFixture ALL DEPENDS "${A1_TRANSPORT_FIXTURE}")
add_library(A1SlowCoopFixture SHARED tests/SlowCoopFixture.c)
set_target_properties(A1SlowCoopFixture PROPERTIES OSX_ARCHITECTURES x86_64)
add_test(NAME AcceleratedEngineStartup COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/EngineLauncherTests.py"
    --launcher "$<TARGET_FILE:A1EngineLauncher>" --shim "$<TARGET_FILE:A1Coop>"
    --sidecar "${A1_X87_BINARY}" --wine-loader "${A1_PRIVATE_WINE}"
    --fixture "${A1_TRANSPORT_FIXTURE}" --slow-shim "$<TARGET_FILE:A1SlowCoopFixture>"
    --work-dir "${CMAKE_CURRENT_SOURCE_DIR}/local/performance/chords/launcher-verification")
set_tests_properties(AcceleratedEngineStartup PROPERTIES TIMEOUT 75)
add_test(NAME PinnedRuntimeSource COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/RuntimeSourcePinTests.py")
set_tests_properties(PinnedRuntimeSource PROPERTIES TIMEOUT 10)
add_test(NAME ReleasePackageContracts COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/PackageContractTests.py" "$<TARGET_BUNDLE_DIR:A1Revival_VST3>")
set_tests_properties(ReleasePackageContracts PROPERTIES TIMEOUT 15)
add_executable(A1VST3ProgramTests tests/VST3ProgramTests.mm)
target_link_libraries(A1VST3ProgramTests PRIVATE A1Revival)
target_include_directories(A1VST3ProgramTests PRIVATE src "${JUCE_PATH}/modules"
    "${JUCE_PATH}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
    "${CMAKE_CURRENT_BINARY_DIR}/A1Revival_artefacts/JuceLibraryCode")
target_compile_definitions(A1VST3ProgramTests PRIVATE JucePlugin_Build_VST3=1 DONT_SET_USING_JUCE_NAMESPACE=1
    A1_VST3_WRAPPER_FILE="${A1_VST3_ADAPTER}")
target_compile_options(A1VST3ProgramTests PRIVATE -fno-access-control)
set_target_properties(A1VST3ProgramTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vst3-program-fixture/Contents/MacOS")
add_dependencies(A1VST3ProgramTests A1Revival_VST3)
add_custom_command(TARGET A1VST3ProgramTests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E create_symlink "$<TARGET_BUNDLE_DIR:A1Revival_VST3>/Contents/Resources"
        "${CMAKE_CURRENT_BINARY_DIR}/vst3-program-fixture/Contents/Resources")
add_test(NAME VST3ProgramAutomation COMMAND A1VST3ProgramTests)
set_tests_properties(VST3ProgramAutomation PROPERTIES TIMEOUT 10)
