#ifndef AUDIO_SUBSTREAM_H_
#define AUDIO_SUBSTREAM_H_

#include "../codecs/AudioCodec.h"
#include "logger.h"
//#include "boost/date_time/posix_time/posix_time_types.hpp"

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
}


namespace erizo {
	class AudioSubstream {
		DECLARE_LOGGER();
	public:
		bool isInitialized() const;
		int initStream(const AudioCodecInfo &info);
		int decodeSteam(char *buf,int len);
	    int getSampleRate() const{
	    	return audioDec_.aDecoderContext_->sample_rate;
	    }
	    int durationToSamples(const filetime::timestamp &interval) const;
	    filetime::timestamp samplesToDuration(const int &samples) const;
	    int durationToOffset(const filetime::timestamp &interval) const;
	    filetime::timestamp bitrateToDuration(const int &br) const;

	    const AudioCodecInfo &getAudioInfo() const;

		 BUFFER_TYPE 				audioDecodeBuffer_;
	private:
		 AudioDecoder 				audioDec_;
		 AudioCodecInfo			    info_;

		 static int onDecodeAudio_s(void *ctx,AVFrame *ready);
		 int onDecodeAudio_(AVFrame *ready);

	};

}

#endif

