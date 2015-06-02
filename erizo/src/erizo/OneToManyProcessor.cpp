/*
 * OneToManyProcessor.cpp
 */

#include "OneToManyProcessor.h"
#include "WebRtcConnection.h"
#include "rtp/RtpHeaders.h"
#include "boost/date_time/posix_time/posix_time.hpp"

namespace erizo {

DEFINE_LOGGER(OneToManyProcessor, "OneToManyProcessor");
OneToManyProcessor::OneToManyProcessor()
: audioMixerTimestampLow_(filetime::MIN),
  audioMixerTimestampHigh_(filetime::MIN)
{
	ELOG_DEBUG ("OneToManyProcessor constructor");
	feedbackSink_ = NULL;
}

OneToManyProcessor::~OneToManyProcessor() {
	ELOG_DEBUG ("OneToManyProcessor destructor");
	this->closeAll();
}

int OneToManyProcessor::deliverAudioData_(char* buf, int len) {
	//   ELOG_DEBUG ("OneToManyProcessor deliverAudio");
	if (len <= 0)
		return 0;

	boost::unique_lock<boost::mutex> lock(myMonitor_);

	RtpHeader* rtp = reinterpret_cast<RtpHeader*>(buf);

	AUDIO_INFO_REP::iterator streamIt = audioStreamsInfos_.find(rtp->getSSRC());

	RtcpHeader* head = reinterpret_cast<RtcpHeader*>(buf);
	if(head->isRtcp() && head->packettype == RTCP_Sender_PT){
		// TODO need to update seqnumbers for all RTCP traffic!!
		ELOG_DEBUG ("OneToManyProcessor deliverAudio. update SR for stream %u", head->getSSRC() );
		if(streamIt == audioStreamsInfos_.end()){
			streamIt = audioStreamsInfos_.insert(std::make_pair(head->getSSRC(),AudioMixingStream())).first;
		}
		streamIt->second.sr_ = head->report.senderReport;
	}

	if(subscribers.empty())
		return 0;

	_PTR(publisher);


	AUDIO_INFO_REP::iterator activePub = audioStreamsInfos_.find(publisher->getAudioSourceSSRC());

	if( streamIt != audioStreamsInfos_.end() && activePub != audioStreamsInfos_.end() )
	{
		ELOG_DEBUG ("OneToManyProcessor deliverAudio. begin mix. stream %u payload type %d", rtp->getSSRC(), rtp->getPayloadType() );

		if(!streamIt->second.stream_.isInitialized())
		{
			AudioCodecInfo info;
			switch(rtp->getPayloadType())
			{
			case PCMU_8000_PT:
				info.codec = AUDIO_CODEC_PCM_U8;
				info.sampleRate = 8000;
				break;
			default:
				ELOG_WARN("OneToManyProcessor::deliverAudioData_. unsupported audio codec %d",rtp->getPayloadType());
				break;
			};

			if(info.codec != AUDIO_CODEC_UNDEFINED)
			{
				ELOG_DEBUG ("OneToManyProcessor deliverAudio. initializing mixing state . codec %d", info.codec );
				_S(streamIt->second.stream_.initStream(info));

				if(audioEnc_.aCoderContext_ == NULL)
				{
					info.channels = audioInfo_.channels = 1;
					info.sampleRate = audioInfo_.sampleRate = 8000;
					info.bitsPerSample = audioInfo_.bitsPerSample = 16;
					audioInfo_.bitRate = audioInfo_.bitsPerSample / 2 * audioInfo_.channels * audioInfo_.sampleRate;
					audioInfo_.codec = AUDIO_CODEC_PCM_S16;
					_S(resampler_.initResampler(audioInfo_));
					_S(audioEnc_.initEncoder(info));
					audioMixBuffer_.resize(audioInfo_.bitRate);
				}

			}
		}

		if(streamIt->second.stream_.isInitialized() && audioEnc_.aCoderContext_)
		{
			AudioMixingStream &subs = streamIt->second;
			// a) decode packet and mix it with the buffer
			int decodedSamples = subs.stream_.decodeSteam((char*)buf,len);
			if(decodedSamples > 0)
			{
				ELOG_DEBUG ("OneToManyProcessor deliverAudio. decoded %d audio samples", decodedSamples );

				// find offset to write buffer in
				uint32_t rtp_diff = rtp->getTimestamp() - subs.sr_.getRtpTimestamp();
				uint32_t time_msecs = rtp_time_to_millisec(rtp_diff,subs.stream_.getSampleRate());

				filetime::timestamp tStart = subs.sr_.getNtpTimestampAsFileTime() +	filetime::milliseconds( time_msecs ),
						tEnd = tStart + subs.stream_.samplesToDuration(decodedSamples);

				// handle wrap / init
				if(audioMixerTimestampLow_ > 0)
				{
					audioMixerTimestampLow_ = audioMixerTimestampHigh_ = tStart;
				}
				_S(subs.addTimeRange(std::make_pair(tStart,tEnd)));

				audioMixerTimestampHigh_ = std::max(audioMixerTimestampHigh_,tEnd );

				time_range t = std::make_pair(audioMixerTimestampLow_,audioMixerTimestampHigh_);
				while(true){

					buffer_range r;
					if( subs.getBufferAndRange(t,r) == -1 )
						break;

					// mix
					BUFFER_TYPE::iterator mixItStart = audioMixBuffer_.begin() + ptimeToByteOffset(t.first),
							mixItEnd = audioMixBuffer_.begin() + ptimeToByteOffset(t.second);

					if(mixItStart >= audioMixBuffer_.end())
						break;
					BUFFER_TYPE::size_type distance = audioMixBuffer_.end()-mixItStart;
					if(r.second-r.first > distance){
						r.second = r.first + distance;
					}

					//resample
					if(resampler_.resample(&*r.first,r.second - r.first,subs.stream_.getAudioInfo()) > 0)
					{
						r.first = resampler_.resampleBuffer_.begin();
						r.second = resampler_.resampleBuffer_.end();
					}

					if(mixItStart < mixItEnd){
						//mix
						for(; mixItStart < mixItEnd ; mixItStart++){
							*mixItStart = (*mixItStart + *r.first++) / 2;
						}
					} else {
						// check for new data reaching beyond current high marker
						audioMixBuffer_.insert(mixItStart,r.first,r.second);
					}
					subs.updateRange(t);
				}

				// b) make sure we've waited enough to get all streams mixed
				if( audioMixerTimestampHigh_ - audioMixerTimestampLow_ >= filetime::milliseconds(MAX_AUIDIO_DELAY_MS) ){

					int samples = ptimeToSampleNum(audioMixerTimestampHigh_ - audioMixerTimestampLow_);
					if(samples <= 0){
						ELOG_WARN ("OneToManyProcessor deliverAudio. bsd number of samples - most probably indicates a bug");
					}
					int output_buffer_size = audioEnc_.calculateBufferSize(samples);
					if(output_buffer_size <= 0){
						ELOG_WARN ("OneToManyProcessor deliverAudio. av_samples_get_buffer_size failed ");
						return -1;
					}
					audioOutputBuffer_.resize(output_buffer_size + rtp->getHeaderLength());

					// c) encode part of the buffer and send it out

					int rtpHdrLen = RtpHeader::MIN_SIZE;

					AVPacket avpkt;
					av_init_packet(&avpkt);

					avpkt.data = &audioOutputBuffer_.at(rtpHdrLen);
					avpkt.size = audioOutputBuffer_.size() - rtpHdrLen;

					ELOG_DEBUG ("OneToManyProcessor deliverAudio. encoding %d mixed samples", samples);

					_S(audioEnc_.encodeAudio(&audioMixBuffer_.at(0),samples,&avpkt));

					ELOG_DEBUG ("OneToManyProcessor deliverAudio. sending away %d bytes", avpkt.size);

					filetime::timestamp encodedChunkDuration =  filetime::SECOND / audioInfo_.sampleRate * samples;

					//packetize
					for( int offset = 0; offset < avpkt.size + rtpHdrLen; offset += RTP_PACKET_SZ )
					{
						RtpHeader *rtpOut = new (&audioOutputBuffer_.at(offset)) RtpHeader();

						rtpOut->setPayloadType(rtp->getPayloadType());
						rtpOut->setSeqNumber(audioSeqNumber_++);
						rtpOut->setSSRC(publisher->getAudioSourceSSRC());
						filetime::timestamp ftime_off = audioMixerTimestampLow_ - activePub->second.sr_.getNtpTimestampAsFileTime();
						uint32_t rtp_timeoff = filetime_to_rtp_time( ftime_off, audioInfo_.sampleRate);
						rtpOut->setTimestamp( activePub->second.sr_.getRtpTimestamp() + rtp_timeoff );
						rtpOut->setMarker(true);

						int payloadSize = std::min((int)RTP_PACKET_SZ,avpkt.size - offset);
						// calculate timestamp for a packet
						audioMixerTimestampLow_ += (payloadSize - rtpHdrLen) * encodedChunkDuration / avpkt.size;

						std::map<std::string, sink_ptr>::iterator it;
						for (it = subscribers.begin(); it != subscribers.end(); ++it) {
							(*it).second->deliverAudioData((char*)&audioOutputBuffer_.at(offset),payloadSize);
						}
					}
					int leftOver = std::max(0,filetime::milliseconds_from(audioMixerTimestampHigh_ - audioMixerTimestampLow_)) / 10 * audioInfo_.sampleRate / 100;

					ELOG_DEBUG ("OneToManyProcessor deliverAudio. compacting decode buffer to %d bytes", leftOver );

					audioOutputBuffer_.erase(audioOutputBuffer_.begin(),audioMixBuffer_.begin()+ ptimeToByteOffset(audioMixerTimestampLow_));

					return 0;
				}
			}
		}
	}
	else
	{
		ELOG_DEBUG ("OneToManyProcessor deliverAudio. no SR for stream %u", rtp->getSSRC() );

		if(publisher->getAudioSourceSSRC() != rtp->getSSRC() ){
			return 0;
		}
	}

	//ELOG_WARN("OneToManyProcessor::deliverAudioData_(this=%p)_ pt=%d,ssrc=%x,seqn=%u subs=%d/pubs=%d", this,rtp->getPayloadType(),rtp->getSSRC(),rtp->getSeqNumber(),
	//subscribers.size(), publishers.size() );

	std::map<std::string, sink_ptr>::iterator it;
	for (it = subscribers.begin(); it != subscribers.end(); ++it) {
		(*it).second->deliverAudioData(buf, len);
	}

	return 0;
}



int OneToManyProcessor::deliverVideoData_(char* buf, int len) {
	if (len <= 0)
		return 0;
	RtcpHeader* head = reinterpret_cast<RtcpHeader*>(buf);
	if(head->isFeedback()){
		ELOG_WARN("Receiving Feedback in wrong path: %d", head->packettype);
		if (feedbackSink_!=NULL){
			head->ssrc = htonl(publisher->getVideoSourceSSRC());
			feedbackSink_->deliverFeedback(buf,len);
		}
		return 0;
	}
	boost::unique_lock<boost::mutex> lock(myMonitor_);

	if (subscribers.empty())
		return 0;

	_PTR(publisher);

	RtpHeader* rtp = reinterpret_cast<RtpHeader*>(buf);

	if(lastPublisher_ && lastPublisher_->getVideoSourceSSRC() != rtp->getSSRC() )
	{
		//TODO check availability of key frame
		if(publisher->getVideoSourceSSRC() != rtp->getSSRC() ){
			return 0;
		} else {
			ELOG_INFO("OneToManyProcessor::deliverVideoData_(this=%p). performing transition to new publisher. pt=%d,ssrc=%x,seqn=%u subs=%d/pubs=%d", this,rtp->getPayloadType(),rtp->getSSRC(),rtp->getSeqNumber(),
					subscribers.size(), publishers.size() );

			lastPublisher_.reset();
		}
	}
	else if(publisher->getVideoSourceSSRC() != rtp->getSSRC() ){
		return 0;
	}

	//ELOG_WARN("OneToManyProcessor::deliverVideoData(this=%p)_ pt=%d,ssrc=%x,seqn=%u subs=%d/pubs=%d", this,rtp->getPayloadType(),rtp->getSSRC(),rtp->getSeqNumber(),
	//subscribers.size(), publishers.size() );


	//	using namespace boost::posix_time;
	//
	//	static  ptime s_last(second_clock::universal_time());
	//
	//	ptime now(second_clock::universal_time());
	//	time_duration diff = now - s_last;
	//
	//	if(diff.total_seconds() > 7){
	//		for(publisher_it it = publishers.begin(); it != publishers.end(); it++ ){
	//			if(it->second && it->second != publisher){
	//				ELOG_WARN("switching to publisher %s. elapsed %d sec", it->first.c_str(),diff.total_seconds());
	//				activatePublisher_(it->second);
	//				s_last = now;
	//				break;
	//			}
	//		}
	//	}

	std::map<std::string, sink_ptr>::iterator it;
	for (it = subscribers.begin(); it != subscribers.end(); ++it) {
		if((*it).second != NULL) {
			(*it).second->deliverVideoData(buf, len);
		}
	}
	return 0;
}

void OneToManyProcessor::addPublisher(MediaSource* webRtcConn,const std::string& peerId) {
	ELOG_DEBUG("addPublisher %s",peerId.c_str());
	boost::mutex::scoped_lock lock(myMonitor_);
	publishers.insert(std::make_pair(peerId,source_ptr(webRtcConn)));
}

void OneToManyProcessor::removePublisher(const std::string& peerId) {
	boost::mutex::scoped_lock lock(myMonitor_);
	std::map<std::string, source_ptr >::iterator found = publishers.find(peerId);
	if(found != publishers.end())
	{
		if( publisher == found->second ){
			publisher.reset();
			feedbackSink_ = NULL;
		}
		publishers.erase(found);
	}
	if(!publisher && !publishers.empty()){
		activatePublisher_(publishers.begin()->second);
	}
}

int OneToManyProcessor::deliverFeedback_(char* buf, int len){
	if (feedbackSink_ != NULL){
		feedbackSink_->deliverFeedback(buf,len);
	}
	return 0;
}

void OneToManyProcessor::addSubscriber(MediaSink* webRtcConn,
		const std::string& peerId) {
	ELOG_DEBUG("Adding subscriber");
	boost::mutex::scoped_lock lock(myMonitor_);

	if(publishers.empty()){
		ELOG_WARN("no publishers available.");
		return;
	}

	if(!publisher){
		this->activatePublisher_(publishers.begin()->second);
	}

	ELOG_DEBUG("From %u, %u to %u, %u ", publisher->getAudioSourceSSRC() , publisher->getVideoSourceSSRC(),
			webRtcConn->getVideoSinkSSRC(),webRtcConn->getAudioSinkSSRC());
	webRtcConn->setAudioSinkSSRC(this->publisher->getAudioSourceSSRC());
	webRtcConn->setVideoSinkSSRC(this->publisher->getVideoSourceSSRC());
	ELOG_DEBUG("Subscribers ssrcs: Audio %u, video, %u from %u, %u ", webRtcConn->getAudioSinkSSRC(), webRtcConn->getVideoSinkSSRC(), this->publisher->getAudioSourceSSRC() , this->publisher->getVideoSourceSSRC());
	FeedbackSource* fbsource = webRtcConn->getFeedbackSource();

	if (fbsource!=NULL){
		ELOG_DEBUG("adding fbsource");
		fbsource->setFeedbackSink(this);
	}
	this->subscribers[peerId] = sink_ptr(webRtcConn);
}

void OneToManyProcessor::removeSubscriber(const std::string& peerId) {
	ELOG_DEBUG("Remove subscriber");
	boost::mutex::scoped_lock lock(myMonitor_);
	if (this->subscribers.find(peerId) != subscribers.end()) {
		this->subscribers.erase(peerId);
	}
}

void OneToManyProcessor::activatePublisher(const std::string& peerId)
{
	ELOG_DEBUG("activate publisher %s",peerId.c_str());

	boost::mutex::scoped_lock lock(myMonitor_);

	std::map<std::string, source_ptr >::iterator found = publishers.find(peerId);
	if(found != publishers.end())
	{
		activatePublisher_(found->second);
	}
	else
	{
		ELOG_INFO("OneToManyProcessor::activatePublisher publisher %s not exists!",peerId.c_str());
	}
}

int OneToManyProcessor::activatePublisher_(const OneToManyProcessor::source_ptr& src)
{
	ELOG_DEBUG("OneToManyProcessor::activatePublisher activating publisher %p",src.get());

	lastPublisher_ = publisher;
	publisher = src;
	if(publisher)
	{
		feedbackSink_ = publisher->getFeedbackSink();
		publisher->sendPLI();
		for (std::map<std::string, sink_ptr >::iterator it = subscribers.begin(); it != subscribers.end(); it++ )
		{
			if ((*it).second != NULL)
			{
				(*it).second->setAudioSinkSSRC(this->publisher->getAudioSourceSSRC());
				(*it).second->setVideoSinkSSRC(this->publisher->getVideoSourceSSRC());
			}
		}
	}
	else
	{
		feedbackSink_ = NULL;
		lastPublisher_.reset();
	}
	return 0;
}

void OneToManyProcessor::closeAll() {
	ELOG_DEBUG ("OneToManyProcessor closeAll");
	feedbackSink_ = NULL;
	publisher.reset();
	boost::unique_lock<boost::mutex> lock(myMonitor_);
	std::map<std::string, boost::shared_ptr<MediaSink> >::iterator it = subscribers.begin();
	while (it != subscribers.end()) {
		if ((*it).second != NULL) {
			FeedbackSource* fbsource = (*it).second->getFeedbackSource();
			if (fbsource!=NULL){
				fbsource->setFeedbackSink(NULL);
			}
		}
		subscribers.erase(it++);
	}
	publishers.clear();
	subscribers.clear();
	ELOG_DEBUG ("ClosedAll media in this OneToMany");
}

}/* namespace erizo */

