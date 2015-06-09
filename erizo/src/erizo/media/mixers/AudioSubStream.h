#ifndef AUDIO_SUBSTREAM_H_
#define AUDIO_SUBSTREAM_H_

#include "../codecs/AudioCodec.h"

//#include "boost/date_time/posix_time/posix_time_types.hpp"



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



/*
 *  Mixed stream management.
 * */
 class AudioMixingStream
 {
	 DECLARE_LOGGER();
	 //TODO disable assignment operators and copy c-tor
 public:
	 AudioMixingStream(uint32_t ssrc, const filetime::timestamp &tolerance,uint32_t id);
	 int addTimeRange(const time_range &r);
	 int getBufferAndRange(time_range &r,buffer_range &br);
	 int updateRange(const time_range &r);
	 int decodeStream(const RtpHeader *rtp,char *buf,int data);

	 inline filetime::timestamp getNtpTime() const{
		 return ntpTime_;
	 }

	 inline uint32_t getRtpTime() const {
		 return rtpTime_;
	 }

	 inline uint32_t getSSRC() const{
		 return ssrc_;
	 }

	 void updateSR(const RtcpHeader &head);

	 filetime::timestamp getCurrentTime() const;

	 uint32_t getCurrentTimeAsRtp() const;

	 uint32_t getId() const{
		 return id_;
	 }
	 AudioSubstream                       stream_;
 private:
	 std::deque<time_range>               ranges_;
	 uint32_t							  ssrc_;
	 filetime::timestamp                  ntpTime_;
	 uint32_t							  rtpTime_;
	 const filetime::timestamp 			  tolerance_;
	 filetime::timestamp 				  curTime_;
	 uint32_t							  id_;

	 static filetime::timestamp currentSystemTimeAsFileTime();

	 buffer_range empty_range();
	 void invalidate_times();
 };


}

#endif

