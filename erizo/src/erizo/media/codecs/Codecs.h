
#ifndef CODECS_H_
#define CODECS_H_

#include "erizo_common.h"

namespace erizo{

enum VideoCodecID{
	VIDEO_CODEC_VP8,
	VIDEO_CODEC_H264,
	VIDEO_CODEC_MPEG4
};

enum AudioCodecID{
	AUDIO_CODEC_UNDEFINED = -1,
	AUDIO_CODEC_PCM_S16,
	AUDIO_CODEC_PCM_U8,
	AUDIO_CODEC_PCM_FLTP,
	AUDIO_CODEC_OPUS,
	AUDIO_CODEC_VORBIS,
	AUDIO_CODEC_ISAC,
	AUDIO_CODEC_CN
};

#define TO_STR(x) #x

inline const char *AudioCodecToString(const AudioCodecID &id){
	switch(id){
	case AUDIO_CODEC_UNDEFINED: return TO_STR(AUDIO_CODEC_UNDEFINED);
	case AUDIO_CODEC_PCM_S16: return TO_STR(AUDIO_CODEC_PCM_S16);
	case AUDIO_CODEC_PCM_U8: return TO_STR(AUDIO_CODEC_PCM_U8);
	case AUDIO_CODEC_PCM_FLTP: return TO_STR(AUDIO_CODEC_PCM_FLTP);
	case AUDIO_CODEC_OPUS: return TO_STR(AUDIO_CODEC_OPUS);
	case AUDIO_CODEC_VORBIS: return TO_STR(AUDIO_CODEC_VORBIS);
	case AUDIO_CODEC_ISAC: return TO_STR(AUDIO_CODEC_ISAC);
	case AUDIO_CODEC_CN: return TO_STR(AUDIO_CODEC_CN);
	default:		return TO_STR(AUDIO_CODEC_UNDEFINED);
	};
}

inline bool IsRawAudio(const AudioCodecID &c){
	return (c == AUDIO_CODEC_PCM_S16 || c == AUDIO_CODEC_PCM_U8 || c == AUDIO_CODEC_PCM_FLTP);
}

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
	:sampleRate(0),
	 channels(0),
	 bitsPerSample(0)
	{
		this->codec = AUDIO_CODEC_UNDEFINED;
	}

	 long ftimeDurationToBitrate(const filetime::timestamp &ival) const{
		  return (ival * bitRate / filetime::SECOND);
	  }
	  long ftimeDurationToSampleNum(const filetime::timestamp &duration) const{
	 	  return duration * sampleRate / filetime::SECOND / channels;
	   }
	  long bitrateToSampleNum(const size_t &br) const {
	 	  return br * sampleRate / bitRate;
	  }
	  filetime::timestamp bitrateToDuration(const uint32_t &br) const{
	 	  return br * filetime::SECOND / bitRate;
	   }
	  filetime::timestamp sampleNumToFiltimeDuration(const int &samples) const{
	 	  return samples * filetime::SECOND / sampleRate;
	   }
	  long sampleNumToByteOffset(const int &samples) const{
	 	  return samples * bitRate / sampleRate;
	   }
	  const char *CodecName() const{
		  return AudioCodecToString(codec);
	  }
	  void Print(log4cxx::LoggerPtr logger) const{
		  ELOG_INFO(" AudioCodecInfo codec:\tcodec=%d\t(%s)\tsample_rate=%d\tchannels=%d\tbps=%d\tbitrate=%d",
				  codec, CodecName(), sampleRate, channels, bitsPerSample, bitRate );
	  }

	AudioCodecID codec;
	int bitRate; // in bytes!!!!
	int sampleRate;
	int channels;
	int bitsPerSample;
};

}
#endif /* CODECS_H_ */
