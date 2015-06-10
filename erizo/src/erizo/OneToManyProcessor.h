/*
 * OneToManyProcessor.h
 */

#ifndef ONETOMANYPROCESSOR_H_
#define ONETOMANYPROCESSOR_H_

#include "media/ExternalOutput.h"
#include "media/mixers/AudioSubStream.h"
#include "media/mixers/AudioMixerUtils.h"

namespace erizo{

class WebRtcConnection;


/**
 * Represents a One to Many connection.
 * Receives media from one publisher and retransmits it to every subscriber.
 */
class OneToManyProcessor : public MediaSink, public FeedbackSink {
	DECLARE_LOGGER();

public:
	  typedef boost::shared_ptr<MediaSource> source_ptr;

	  std::map<std::string, boost::shared_ptr<MediaSink> > subscribers;

    source_ptr publisher;

	OneToManyProcessor();
	virtual ~OneToManyProcessor();
	/**
	 * Adds the Publisher
	 * @param webRtcConn The WebRtcConnection of the Publisher
	 * @param peerId An unique Id for the publisher
	 */
	void addPublisher(MediaSource* webRtcConn, const std::string& peerId);
	/**
		 * Removes the Publisher
		 * @param peerId An unique Id for the publisher
		 */
	void removePublisher(const std::string& peerId);
	/**
	 * Sets the subscriber
	 * @param webRtcConn The WebRtcConnection of the subscriber
	 * @param peerId An unique Id for the subscriber
	 */
	void addSubscriber(MediaSink* webRtcConn, const std::string& peerId);
	/**
	 * Eliminates the subscriber given its peer id
	 * @param peerId the peerId
	 */
	void removeSubscriber(const std::string& peerId);

	/**
		 * Eliminates the subscriber given its peer id
		 * @param peerId the peerId
		 */
	void activatePublisher(const std::string& peerId);

private:
	int activatePublisher_(const source_ptr& src);

  typedef boost::shared_ptr<MediaSink> sink_ptr;
  FeedbackSink* feedbackSink_;
	
  std::map<std::string, source_ptr > publishers;
  typedef std::map<std::string, source_ptr >::iterator publisher_it;
  source_ptr lastPublisher_;

  // @igors@ audio mixer stuff
  //hardcoded PCMU/8000/1 ch audio codec info


  const int max_audio_mixed_buffer_size_;
  static const int RTP_PACKET_SZ = 1500;

  BUFFER_TYPE audioMixBuffer_,audioOutputBuffer_;

  typedef std::map<int,AudioMixingStream> AUDIO_INFO_REP;

  AUDIO_INFO_REP  			audioStreamsInfos_;
  AudioCodecInfo			audioInfo_;
  AudioResampler            resampler_;
  AudioEncoder				audioEnc_;
  uint16_t                  audioSeqNumber_;
  uint8_t				    payloadType_;

  static const int MIN_BUCKET_SIZE = 20 * 10000;

  AudioMixerStateManager audioMixerManager_;

  long ptimeToByteOffset(const filetime::timestamp &t) const{
	  return ptimeToDuratiom(t - audioMixerManager_.startTime());
  }
  long ptimeToDuratiom(const filetime::timestamp &ival) const{
	  return (ival * audioInfo_.bitRate / filetime::SECOND);
  }
  long ftimeDurationToSampleNum(const filetime::timestamp &duration) const{
 	  return duration * audioInfo_.sampleRate / filetime::SECOND ;
   }
  long bitrateToDuration(const uint32_t &br) const{
 	  return br * filetime::SECOND / audioInfo_.bitRate;
   }
  filetime::timestamp sampleNumToFiltimeDuration(const int &samples) const{
 	  return samples * filetime::SECOND / audioInfo_.sampleRate;
   }
  int sendMixedAudio_(AudioMixingStream &provider,int samples);
  int mixAudioWith_(AudioMixingStream &subs);

  filetime::timestamp getCurrentTime() const;

  // @igors

  int deliverAudioData_(char* buf, int len);
  int deliverVideoData_(char* buf, int len);
  int deliverFeedback_(char* buf, int len);
  void closeAll();

};


} /* namespace erizo */
#endif /* ONETOMANYPROCESSOR_H_ */
