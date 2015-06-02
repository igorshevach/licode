
#ifndef CODECS_H_
#define CODECS_H_

#include <boost/cstdint.hpp>
#include "erizo_common.h"

namespace erizo{

enum VideoCodecID{
	VIDEO_CODEC_VP8,
	VIDEO_CODEC_H264,
	VIDEO_CODEC_MPEG4
};

enum AudioCodecID{
	AUDIO_CODEC_UNDEFINED = 0,
	AUDIO_CODEC_PCM_S16,
	AUDIO_CODEC_PCM_U8,
	AUDIO_CODEC_OPUS,
	AUDIO_CODEC_VORBIS
};

struct VideoCodecInfo {
	VideoCodecID codec;
	int payloadType;
	int width;
	int height;
	int bitRate;
	int frameRate;
};

struct AudioCodecInfo {
	AudioCodecInfo()
	{
		this->codec = AUDIO_CODEC_UNDEFINED;
	}
	AudioCodecID codec;
	int bitRate;
	int sampleRate;
	int channels;
	int bitsPerSample;
};

}
#endif /* CODECS_H_ */
