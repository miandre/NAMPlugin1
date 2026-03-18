#define PLUG_NAME "MND-AMP"
#define PLUG_MFR "MND-AMPS"
#define PLUG_VERSION_HEX 0x00000001
#define PLUG_VERSION_STR "0.0.1"
#define PLUG_UNIQUE_ID 'MND1'
#define PLUG_MFR_ID 'MND1'
#define PLUG_URL_STR "https://github.com/sdatkinson/NeuralAmpModelerPlugin"
#define PLUG_EMAIL_STR "spam@me.com"
#define PLUG_COPYRIGHT_STR "Copyright 2022 Steven Atkinson"
#define PLUG_CLASS_NAME NeuralAmpModeler
#define BUNDLE_NAME "MND-AMPS"
#define BUNDLE_MFR "MND-AMPS"
#define BUNDLE_DOMAIN "com"
#define NAM_STARTUP_TMPLOAD_DEFAULTS 1 // Dev/test helper: set to 0 to disable auto-loading tmpLoad defaults on app start.
// Amp workflow profile: 0 = Rig Mode (editable slot model pickers), 1 = Release Mode (slot model edits locked).
#define NAM_RELEASE_MODE 0

#define SHARED_RESOURCES_SUBPATH "MND-AMPS"

#ifdef APP_API
  // Standalone host opens max channel count from this list.
  // Keep stereo input capability available for ASIO wrapper drivers that are unstable on mono input streams.
  #define PLUG_CHANNEL_IO "1-2 2-2"
  // Temporary test toggle: 0 = mono core in standalone, 1 = true stereo core in standalone.
  #define NAM_APP_STEREO_CORE_TEST 1
#else
  #define PLUG_CHANNEL_IO "2-2 1-2 1-1"
#endif

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 1039
#define PLUG_HEIGHT 666
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0
#define PLUG_MAX_WIDTH PLUG_WIDTH * 4
#define PLUG_MAX_HEIGHT PLUG_HEIGHT * 4

#define AUV2_ENTRY NeuralAmpModeler_Entry
#define AUV2_ENTRY_STR "NeuralAmpModeler_Entry"
#define AUV2_FACTORY NeuralAmpModeler_Factory
#define AUV2_VIEW_CLASS NeuralAmpModeler_View
#define AUV2_VIEW_CLASS_STR "NeuralAmpModeler_View"

#define AAX_TYPE_IDS 'ITP1'
#define AAX_TYPE_IDS_AUDIOSUITE 'ITA1'
#define AAX_PLUG_MFR_STR "Acme"
#define AAX_PLUG_NAME_STR "NeuralAmpModeler\nIPEF"
#define AAX_PLUG_CATEGORY_STR "Effect"
#define AAX_DOES_AUDIOSUITE 1

#define VST3_SUBCATEGORY "Fx"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
#define MICHROMA_FN "Michroma-Regular.ttf"

#define GEAR_FN "General/Gear.svg"
#define FILE_FN "General/File.svg"
#define CLOSE_BUTTON_FN "General/Cross.svg"
#define LEFT_ARROW_FN "General/ArrowLeft.svg"
#define RIGHT_ARROW_FN "General/ArrowRight.svg"
#define MODEL_ICON_FN "General/ModelIcon.svg"
#define IR_ICON_ON_FN "General/IRIconOn.svg"
#define IR_ICON_OFF_FN "General/IRIconOff.svg"
#define INPUT_MONO_SVG_FN "General/mono.svg"
#define INPUT_STEREO_SVG_FN "General/stereo.svg"
#define GLOBE_ICON_FN "General/Globe.svg"

#define BACKGROUND_FN "Backgrounds/SettingsBackground.jpg"
#define BACKGROUND2X_FN "Backgrounds/SettingsBackground@2x.jpg"
#define BACKGROUND3X_FN "Backgrounds/SettingsBackground@3x.jpg"
#define AMP1BACKGROUND_FN "Backgrounds/Amp1Background.png"
#define AMP1BACKGROUND2X_FN "Backgrounds/Amp1Background@2x.png"
#define AMP1BACKGROUND3X_FN "Backgrounds/Amp1Background@3x.png"
#define AMP1BACKGROUND_OFF_FN "Backgrounds/Amp1Background_OFF.png"
#define AMP1BACKGROUND_OFF2X_FN "Backgrounds/Amp1Background_OFF@2x.png"
#define AMP1BACKGROUND_OFF3X_FN "Backgrounds/Amp1Background_OFF@3x.png"
#define AMP2BACKGROUND_FN "Backgrounds/Amp2Background.png"
#define AMP2BACKGROUND2X_FN "Backgrounds/Amp2Background@2x.png"
#define AMP2BACKGROUND3X_FN "Backgrounds/Amp2Background@3x.png"
#define AMP2BACKGROUND_OFF_FN "Backgrounds/Amp2Background_OFF.png"
#define AMP2BACKGROUND_OFF2X_FN "Backgrounds/Amp2Background_OFF@2x.png"
#define AMP2BACKGROUND_OFF3X_FN "Backgrounds/Amp2Background_OFF@3x.png"
#define AMP3BACKGROUND_FN "Backgrounds/Amp3Background.png"
#define AMP3BACKGROUND2X_FN "Backgrounds/Amp3Background@2x.png"
#define AMP3BACKGROUND3X_FN "Backgrounds/Amp3Background@3x.png"
#define AMP3BACKGROUND_OFF_FN "Backgrounds/Amp3Background_OFF.png"
#define AMP3BACKGROUND_OFF2X_FN "Backgrounds/Amp3Background_OFF@2x.png"
#define AMP3BACKGROUND_OFF3X_FN "Backgrounds/Amp3Background_OFF@3x.png"
#define AMP1KNOB_FN "Hardware/Amp1Knob.png"
#define AMP1KNOBBACKGROUND_FN "Hardware/Amp1KnobBackground.png"
#define AMP2KNOB_FN "Hardware/Amp2Knob.png"
#define AMP2KNOBBACKGROUND_FN "Hardware/Amp2KnobBackground.png"
#define AMP3KNOB_FN "Hardware/Amp3Knob.png"
#define AMP3KNOBBACKGROUND_FN "Hardware/Amp3KnobBackground.png"
#define STOMPBACKGROUND_FN "Backgrounds/StompBackground.jpg"
#define STOMPBACKGROUND2X_FN "Backgrounds/StompBackground@2x.jpg"
#define STOMPBACKGROUND3X_FN "Backgrounds/StompBackground@3x.jpg"
#define CABBACKGROUND_FN "Backgrounds/CabBackground.jpg"
#define CABBACKGROUND2X_FN "Backgrounds/CabBackground@2x.jpg"
#define CABBACKGROUND3X_FN "Backgrounds/CabBackground@3x.jpg"
#define FXBACKGROUND_FN "Backgrounds/FXBackground.png"
#define FXBACKGROUND2X_FN "Backgrounds/FXBackground@2x.png"
#define FXBACKGROUND3X_FN "Backgrounds/FXBackground@3x.png"
#define EQBACKGROUND_FN "Backgrounds/EQBackground.jpg"
#define EQBACKGROUND2X_FN "Backgrounds/EQBackground@2x.jpg"
#define EQBACKGROUND3X_FN "Backgrounds/EQBackground@3x.jpg"
#define SETTINGSBACKGROUND_FN "Backgrounds/SettingsBackground.jpg"
#define SETTINGSBACKGROUND2X_FN "Backgrounds/SettingsBackground@2x.jpg"
#define SETTINGSBACKGROUND3X_FN "Backgrounds/SettingsBackground@3x.jpg"
#define KNOBBACKGROUND_FN "Hardware/KnobBackground.png"
#define KNOBBACKGROUND2X_FN "Hardware/KnobBackground@2x.png"
#define KNOBBACKGROUND3X_FN "Hardware/KnobBackground@3x.png"
#define FLATKNOBBACKGROUND_SVG_FN "General/FlatKnobBackground.svg"
#define PEDALKNOB_FN "Hardware/PedalKnob.png"
#define PEDALKNOBSHADOW_FN "Hardware/PedalKnobShadow.png"
#define EQFADERKNOB_FN "Hardware/EqFaderKnob.png"
#define STOMPBUTTONUP_FN "Hardware/StompButtonUp.png"
#define STOMPBUTTONDOWN_FN "Hardware/StompButtonDown.png"
#define GREENLEDON_FN "Hardware/GreenLedOn.png"
#define GREENLEDOFF_FN "Hardware/GreenLedOff.png"
#define REDLEDON_FN "Hardware/RedLedOn.png"
#define REDLEDOFF_FN "Hardware/RedLedOff.png"
#define SMALLONOFF_OFF_FN "Hardware/SmallOnOff_OFF.png"
#define SMALLONOFF_ON_FN "Hardware/SmallOnOff_ON.png"
#define AP_KNOP_OFFSET 23.0f
#define AMP1SWITCH_OFF_FN "Hardware/Amp1Switch_OFF.png"
#define AMP1SWITCH_ON_FN "Hardware/Amp1Switch_ON.png"
#define SWITCH_OFF_FN "Hardware/Switch_OFF.png"
#define SWITCH_OFF2X_FN "Hardware/Switch_OFF@2x.png"
#define SWITCH_ON_FN "Hardware/Switch_ON.png"
#define SWITCH_ON2X_FN "Hardware/Switch_ON@2x.png"
#define FILEBACKGROUND_FN "General/FileBackground.png"
#define FILEBACKGROUND2X_FN "General/FileBackground@2x.png"
#define FILEBACKGROUND3X_FN "General/FileBackground@3x.png"
#define MIC57_FN "Mic/57.png"
#define MIC121_FN "Mic/121.png"
#define INPUTLEVELBACKGROUND_FN "General/InputLevelBackground.png"
#define INPUTLEVELBACKGROUND2X_FN "General/InputLevelBackground@2x.png"
#define INPUTLEVELBACKGROUND3X_FN "General/InputLevelBackground@3x.png"
#define SLIDESWITCHHANDLE_FN "General/SlideSwitchHandle.png"
#define SLIDESWITCHHANDLE2X_FN "General/SlideSwitchHandle@2x.png"
#define SLIDESWITCHHANDLE3X_FN "General/SlideSwitchHandle@3x.png"

#define METERBACKGROUND_FN "General/MeterBackground.png"
#define METERBACKGROUND2X_FN "General/MeterBackground@2x.png"
#define METERBACKGROUND3X_FN "General/MeterBackground@3x.png"
#define AMP_ACTIVE_SVG_FN "General/Amp_ACTIVE.svg"
#define AMPPICKER_ACTIVE_SVG_FN "General/AmpPicker_ACTIVE.svg"
#define STOMP_ACTIVE_SVG_FN "General/Stomp_ACTIVE.svg"
#define EQ_ACTIVE_SVG_FN "General/Eq_ACTIVE.svg"
#define FX_ACTIVE_SVG_FN "General/Fx_ACTIVE.svg"
#define CAB_ACTIVE_SVG_FN "General/Cab_ACTIVE.svg"
#define TUNER_ON_FN "General/Tuner_ON.png"
#define TUNER_ACTIVE_SVG_FN "General/Tuner_ACTIVE.svg"
#define TUNER_OFF_FN "General/Tuner_OFF.png"

// Issue 291
// On the macOS standalone, we might not have permissions to traverse the file directory, so we have the app ask the
// user to pick a directory instead of the file in the directory.
// Everyone else is fine though.
#if defined(APP_API) && defined(__APPLE__)
  #define NAM_PICK_DIRECTORY
#endif
