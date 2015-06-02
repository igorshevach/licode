/*
 * OneToManyProcessor.h
 */

#ifndef ONETOMANYPROCESSOR_H_
#define ONETOMANYPROCESSOR_H_

#include <map>
#include <string>

#include "MediaDefinitions.h"
#include "media/ExternalOutput.h"
#include "logger.h"
#include "rtp/RtpHeaders.h"
#include "media/mixers/AudioSubStream.h"
#include <deque>

namespace erizo{

class WebRtcConnection;

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
	  AudioMixingStream()
	  {
		  invalidate_times();
	  }

	  int addTimeRange(const time_range &r){
		  ranges_.push_back(r);
		  return 0;
	  }
	  int getBufferAndRange(time_range &r,buffer_range &br){
		  br = empty_range();
		  if(ranges_.empty()){
			  ELOG_DEBUG("getBufferAndRange. no ranges added so far");
			  return -1;
		  }
		  if(!stream_.audioDecodeBuffer_.size()){
			  ELOG_DEBUG("getBufferAndRange. buffer is empty");
			  invalidate_times();
			  return -1;
		  }
		  if(ranges_.front().first > r.second){
			  ELOG_DEBUG("getBufferAndRange. requested range in the past");
			  r.first = ranges_.front().first;
			  return -1;
		  }

		  filetime::timestamp total_duration(0);
		  for(std::deque<time_range>::iterator it = ranges_.begin(); it != ranges_.end(); it++){

			  if(it->second > r.first){
				  if(r.first > it->first)
					  total_duration += it->first - r.first;
				  else
					  r.first = it->first;
				  r.second = std::min(r.second, it->second);

				  BUFFER_TYPE::iterator s = stream_.audioDecodeBuffer_.begin() + stream_.durationToOffset(total_duration),
								  e = s + stream_.durationToOffset(r.second - r.first);

				  br = std::make_pair(s,e);
				  ELOG_DEBUG("getBufferAndRange. found range %lld-%lld byte range %d-%d total data size %d",
						  r.first,r.second, br.first-stream_.audioDecodeBuffer_.begin(),br.second-stream_.audioDecodeBuffer_.begin(),
						  stream_.audioDecodeBuffer_.size());
				  return 0;
			  }
			  total_duration += it->second - it->first;
		  }
		  ELOG_DEBUG("getBufferAndRange. haven't found requested range");
		  return 1;
	  }

	  int updateRange(const time_range &r){
		  while(!ranges_.empty()){
				  if(ranges_.front().second > r.first){
					  filetime::timestamp end = std::min(ranges_.front().second,r.second);
					  stream_.audioDecodeBuffer_.erase(stream_.audioDecodeBuffer_.begin() ,
					  stream_.audioDecodeBuffer_.begin() + stream_.durationToOffset( end - ranges_.front().first));
					  ranges_.front().first = end;
					  if(end < ranges_.front().second )
						  break;
				  }
				  stream_.audioDecodeBuffer_.erase(stream_.audioDecodeBuffer_.begin(),
						  stream_.audioDecodeBuffer_.begin() + stream_.durationToOffset(ranges_.front().second - ranges_.front().first));
				  ranges_.pop_front();
		  }
		  return 0;
	  }
	  RtcpHeader::report_t::senderReport_t sr_;
	  AudioSubstream                       stream_;
private:
	  std::deque<time_range>               ranges_;

//	  long filetimeToOffset(const filetime::timestamp &t){
//	 	  return (t - stream_.start_) * stream_.durationToOffset(t - stream_.start_);
//	   }
	   buffer_range empty_range() {
		  return std::make_pair(stream_.audioDecodeBuffer_.end(),stream_.audioDecodeBuffer_.end());
	  }
	  void invalidate_times(){
		  ranges_.clear();
	  }
};

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
  enum {
	  MAX_AUIDIO_DELAY_MS = 100,
	  RTP_PACKET_SZ = 1500
  };

  BUFFER_TYPE audioMixBuffer_,audioOutputBuffer_;

  filetime::timestamp audioMixerTimestampLow_, audioMixerTimestampHigh_;

  typedef std::map<int,AudioMixingStream> AUDIO_INFO_REP;

  AUDIO_INFO_REP  			audioStreamsInfos_;
  AudioCodecInfo			audioInfo_;
  AudioResampler            resampler_;
  AudioEncoder				audioEnc_;
  uint16_t                  audioSeqNumber_;
  long ptimeToByteOffset(const filetime::timestamp &t){
	  return (t - audioMixerTimestampLow_) * audioInfo_.bitRate / filetime::SECOND;
  }
  long ptimeToSampleNum(const filetime::timestamp &t){
 	  return ptimeToByteOffset(t) / (audioInfo_.bitsPerSample / 8);
   }

 // @igors

  int deliverAudioData_(char* buf, int len);
	int deliverVideoData_(char* buf, int len);
  int deliverFeedback_(char* buf, int len);
  void closeAll();

};


} /* namespace erizo */
#endif /* ONETOMANYPROCESSOR_H_ */
