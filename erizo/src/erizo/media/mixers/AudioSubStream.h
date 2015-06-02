#ifndef AUDIO_SUBSTREAM_H_
#define AUDIO_SUBSTREAM_H_

#include "../codecs/AudioCodec.h"
#include "logger.h"
#include <deque>
#include "../../MediaDefinitions.h"
#include "../../rtp/RtpHeaders.h"

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



typedef std::pair<filetime::timestamp,filetime::timestamp> time_range;
typedef std::pair<BUFFER_TYPE::iterator,BUFFER_TYPE::iterator> buffer_range;

inline
uint32_t rtp_time_to_millisec(const uint32_t &rtp,const uint32_t &scale){
	uint64_t temp = rtp * 1000;
	return temp / scale;
}

inline
uint32_t filetime_to_rtp_time(const filetime::timestamp &t,const uint32_t &scale){
	return filetime::milliseconds_from(t) * scale / 1000;
}


/*
 *  Mixed stream management.
 * */
 class AudioMixingStream
 {
	 DECLARE_LOGGER();
	 //TODO disable assignment operators and copy c-tor
 public:
	 AudioMixingStream();
	 int addTimeRange(const time_range &r);
	 int getBufferAndRange(time_range &r,buffer_range &br);
	 int updateRange(const time_range &r);

	 RtcpHeader::report_t::senderReport_t sr_;
	 AudioSubstream                       stream_;
 private:
	 std::deque<time_range>               ranges_;

	 buffer_range empty_range();
	 void invalidate_times();
 };


}

#endif

