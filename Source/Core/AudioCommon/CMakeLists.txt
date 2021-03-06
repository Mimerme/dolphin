set(SRCS	AudioCommon.cpp
			DPL2Decoder.cpp
			Mixer.cpp
			WaveFile.cpp
			NullSoundStream.cpp)

set(LIBS "")

if(OPENSLES_FOUND)
	set(SRCS ${SRCS} OpenSLESStream.cpp)
	set(LIBS ${LIBS} ${OPENSLES_LIBRARIES})
endif()

if(ALSA_FOUND)
	set(SRCS ${SRCS} AlsaSoundStream.cpp)
	set(LIBS ${LIBS} ${ALSA_LIBRARIES})
endif()

if(AO_FOUND)
	set(SRCS ${SRCS} AOSoundStream.cpp)
	set(LIBS ${LIBS} ${AO_LIBRARIES})
endif()

if(OPENAL_FOUND)
	set(SRCS ${SRCS} OpenALStream.cpp aldlist.cpp)
	set(LIBS ${LIBS} ${OPENAL_LIBRARY} SoundTouch )
endif()

if(PULSEAUDIO_FOUND)
	set(SRCS ${SRCS} PulseAudioStream.cpp)
	set(LIBS ${LIBS} ${PULSEAUDIO_LIBRARIES})
endif()

if(WIN32)
	set(SRCS ${SRCS} XAudio2Stream.cpp)

	add_dolphin_library(audiocommon_xaudio27 "XAudio2_7Stream.cpp" "${LIBS}")
	target_include_directories(audiocommon_xaudio27 PRIVATE
		${PROJECT_SOURCE_DIR}/Externals
		${PROJECT_SOURCE_DIR}/Externals/XAudio2_7
	)
	list(APPEND LIBS audiocommon_xaudio27)

elseif(APPLE)
	set(SRCS ${SRCS} CoreAudioSoundStream.cpp)
endif()


add_dolphin_library(audiocommon "${SRCS}" "${LIBS}")
