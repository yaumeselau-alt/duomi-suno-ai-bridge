if(DEFINED ENV{AAX_SDK_PATH})
    set(AAX_SDK_PATH $ENV{AAX_SDK_PATH})
endif()
if(DEFINED AAX_SDK_PATH)
    set(AAX_BUILD_FLAG AAX)
    file(TO_CMAKE_PATH "${AAX_SDK_PATH}" AAX_SDK_CMAKE_PATH)
    message(STATUS "AAX Build is enabled. AAX SDK = " ${AAX_SDK_CMAKE_PATH})
    juce_set_aax_sdk_path(${AAX_SDK_CMAKE_PATH})
else()
    message(STATUS "AAX Build is disabled. To enable, set AAX_SDK_PATH to your environment variable or specify -DAAX_SDK_PATH to CMake configure option.")
endif()

juce_add_plugin(DuomiSunoAIBridge
    # VERSION ...                                     # Set this if the plugin version is different to the project version
    # ICON_BIG ""   # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    #   ICON_SMALL "${CMAKE_SOURCE_DIR}/Assets/icon16.png "
    COMPANY_NAME "MrDream"                          # Specify the name of the plugin's author
    COMPANY_COPYRIGHT "MrDream"
    COMPANY_WEBSITE "https://example.com/"
    COMPANY_EMAIL "mail:mrdreamstaff@126.com"
    IS_SYNTH TRUE                        # 内部合成/预览音频，需要输出总线（无输入）
    NEEDS_MIDI_INPUT TRUE                     # Live instruments require an event input bus
    # NEEDS_MIDI_OUTPUT TRUE/FALSE              # Does the plugin need midi output?
    # IS_MIDI_EFFECT TRUE/FALSE                 # Is this plugin a MIDI effect?
    # EDITOR_WANTS_KEYBOARD_FOCUS TRUE/FALSE    # Does the editor need keyboard focus?
    # COPY_PLUGIN_AFTER_BUILD TRUE/FALSE        # Should the plugin be installed to a default location after building?
    PLUGIN_MANUFACTURER_CODE Dumi               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE Dsab                            # A unique four-character plugin id with at least one upper-case character
    FORMATS
    # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
            Standalone
            AU
            # AUv3
            VST3
            # Unity
            ${AAX_BUILD_FLAG}
    VST3_CATEGORIES "Synth"
    AU_MAIN_TYPE "kAudioUnitType_MusicDevice"
    # AU_SANDBOX_SAFE TRUE
    # AAX_CATEGORY ""
    # HARDENED_RUNTIME_ENABLED # macOS app settings
    # HARDENED_RUNTIME_OPTIONS
    # APP_SANDBOX_ENABLED
    # APP_SANDBOX_INHERIT
    # DESCRIPTION "" #when i set this option, the name of AAX plugin became this description(bug?)
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "This applicaion requires a permission to use an audio input device of your computer. By Default, Built-In microphone will be used."

    PRODUCT_NAME "Duomi Suno AI Bridge")        # The name of the final

target_compile_features(DuomiSunoAIBridge PUBLIC cxx_std_17)

# MSVC 默认按系统代码页(936/GBK)解析源码，强制按 UTF-8 解析以避免中文字符串乱码/报错
if(MSVC)
    target_compile_options(DuomiSunoAIBridge PRIVATE /utf-8)
endif()


target_compile_definitions(DuomiSunoAIBridge
    PUBLIC
    # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
    JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
    JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
    JUCE_VST3_CAN_REPLACE_VST2=0
    # JUCE_DISPLAY_SPLASH_SCREEN=0 #if your plugin is distributed with GPL license or paid
)

juce_add_binary_data(JuceLogoBinary
SOURCES
${CMAKE_SOURCE_DIR}/assets/juce-logo.svg
)

target_sources(DuomiSunoAIBridge PRIVATE
    PluginEditor.cpp
    PluginProcessor.cpp
    DuomiClient.cpp
    GenerationJob.cpp
    MidiPreviewPlayer.cpp
    MidiExportJob.cpp
    AudioPreviewPlayer.cpp
    ApiKeyStore.cpp
    ChordExtractionJob.cpp
    MidiTrackJob.cpp
    AudioStemJob.cpp
)

target_link_libraries(DuomiSunoAIBridge PUBLIC
JuceLogoBinary
juce::juce_audio_basics
juce::juce_audio_devices
juce::juce_audio_formats
juce::juce_audio_plugin_client
juce::juce_audio_processors
juce::juce_audio_utils
juce::juce_core
juce::juce_cryptography
juce::juce_data_structures
juce::juce_dsp
juce::juce_events
juce::juce_graphics
juce::juce_gui_basics
juce::juce_gui_extra
juce::juce_opengl
juce::juce_product_unlocking
)

juce_generate_juce_header(DuomiSunoAIBridge)

option(DUOMI_BUILD_TESTS "Build offline client checks and explicit live API probe" OFF)
if(DUOMI_BUILD_TESTS)
    add_executable(DuomiBridgeCheck ../tests/DuomiClientTests.cpp ../tests/MidiPreviewTests.cpp ../tests/MidiTransferTests.cpp ../tests/AudioPreviewTests.cpp ../tests/ApiKeyStoreTests.cpp ../tests/ChordStateTests.cpp ../tests/TrackSelectionTests.cpp)
    target_link_libraries(DuomiBridgeCheck PRIVATE DuomiSunoAIBridge)
    target_include_directories(DuomiBridgeCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(DuomiBridgeCheck PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/DuomiSunoAIBridge_artefacts/JuceLibraryCode")
    if(MSVC)
        target_compile_options(DuomiBridgeCheck PRIVATE /utf-8)
    endif()
    add_test(NAME DuomiClientOffline COMMAND DuomiBridgeCheck)
    add_test(NAME MidiPreviewOffline COMMAND DuomiBridgeCheck --preview-tests)
    add_test(NAME MidiTransferOffline COMMAND DuomiBridgeCheck --transfer-tests)
    add_test(NAME AudioPreviewOffline COMMAND DuomiBridgeCheck --audio-tests)
    add_test(NAME ApiKeyStoreOffline COMMAND DuomiBridgeCheck --key-tests)
    add_test(NAME ChordExtractionOffline COMMAND DuomiBridgeCheck --chord-tests)
    add_test(NAME SessionStateOffline COMMAND DuomiBridgeCheck --session-tests)
    add_test(NAME TrackSelectionOffline COMMAND DuomiBridgeCheck --track-tests)
endif()

# Validate the exported VST3 ABI, not only the shared Processor implementation.
if(DUOMI_BUILD_TESTS AND WIN32)
    add_executable(Vst3BusCheck ../tests/Vst3BusCheck.cpp)
    target_compile_features(Vst3BusCheck PRIVATE cxx_std_17)
    target_include_directories(Vst3BusCheck PRIVATE
        "${CMAKE_SOURCE_DIR}/lib/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK")
    add_dependencies(Vst3BusCheck DuomiSunoAIBridge_VST3)
    add_test(NAME Vst3BinaryBusCheck COMMAND Vst3BusCheck "$<TARGET_FILE:DuomiSunoAIBridge_VST3>")
endif()

if(WIN32)
    target_link_libraries(DuomiSunoAIBridge PUBLIC Crypt32)
endif()
