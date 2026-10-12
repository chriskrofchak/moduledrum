# JUCE 8.0.9 (AGPLv3 or the commercial JUCE licence; used here under the AGPLv3), downloaded at configure time as
# Monomodule does. FETCHCONTENT_SOURCE_DIR_JUCE=<an existing JUCE 8.0.9 checkout> builds offline.
include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.9
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
