/*
 * OneToManyProcessor.cpp
 */
#include "pchheader.h"
#include "OneToManyProcessor.h"
#include "WebRtcConnection.h"
#include "media/mixers/AudioMixerUtils.h"

//#include "boost/date_time/posix_time/posix_time.hpp"

namespace erizo {


DEFINE_LOGGER(OneToManyProcessor, "OneToManyProcessor");
OneToManyProcessor::OneToManyProcessor()
: max_audio_mixed_buffer_size_(100),
  audioMixerManager_(MIN_BUCKET_SIZE,filetime::milliseconds(max_audio_mixed_buffer_size_))
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

	try
	{
		if (len <= 0)
			return 0;

		boost::unique_lock<boost::mutex> lock(myMonitor_);

		RtpHeader* rtp = reinterpret_cast<RtpHeader*>(buf);

		AUDIO_INFO_REP::iterator streamIt = audioStreamsInfos_.find(rtp->getSSRC());

		RtcpHeader* head = reinterpret_cast<RtcpHeader*>(buf);
		if(head->isRtcp() && head->packettype == RTCP_Sender_PT){
			// TODO need to update seqnumbers for all RTCP traffic!!
			ELOG_DEBUG ("::deliverAudioData_. update SR for stream %u", head->getSSRC() );

			if(streamIt == audioStreamsInfos_.end()){

				streamIt = audioStreamsInfos_.find(head->getSSRC());

				if(streamIt == audioStreamsInfos_.end()){

					ELOG_INFO("deliverAudioData_. adding mixer stream %u",head->getSSRC());

					streamIt = audioStreamsInfos_.insert(std::make_pair(head->getSSRC(),
							AudioMixingStream(head->getSSRC(),
									filetime::milliseconds(max_audio_mixed_buffer_size_)))).first;
					audioMixerManager_.addStream(streamIt->second);
				}
			}
			streamIt->second.updateSR(*head);
			//TODO reply with RR to all streams but publisher
		}

		if(subscribers.empty())
			return 0;

		_PTR(publisher);

		//	if(publisher->getAudioSourceSSRC() != rtp->getSSRC())
		//	return 0;


		AUDIO_INFO_REP::iterator activePub = audioStreamsInfos_.find(publisher->getAudioSourceSSRC());

		//	for(streamIt = audioStreamsInfos_.begin(); streamIt != audioStreamsInfos_.end() && activePub != audioStreamsInfos_.end()
		//	; streamIt++)
		if( !head->isRtcp() && streamIt != audioStreamsInfos_.end() && activePub != audioStreamsInfos_.end() )
		{
			AudioMixingStream &subs = streamIt->second, &activeSubs = activePub->second;

			ELOG_DEBUG ("::deliverAudio. processing rtp packet stream=%u pt=%d time=%u sr time=%u", rtp->getSSRC(),
					rtp->getPayloadType(), rtp->getTimestamp(), subs.getRtpTime() );

			if(!subs.stream_.isInitialized())
			{
				WebRtcConnection *pConn = dynamic_cast<WebRtcConnection*>(publisher.get());

				AudioCodecInfo info;
				switch(rtp->getPayloadType())
				{
				case PCMU_8000_PT:
					info.codec = AUDIO_CODEC_PCM_U8;
					info.bitsPerSample = 8;
					info.sampleRate = 8000;
					info.channels = 1;
					info.bitRate = info.sampleRate * info.channels * info.bitsPerSample / 8;
					break;
				default:
					if(pConn) {
						const SdpInfo &sdp = pConn->getRemoteSdpInfo();
						const std::vector<RtpMap>& infos =sdp.getPayloadInfos();
						for(std::vector<RtpMap>::const_iterator it = infos.begin(); it != infos.end(); it++ ){
							if( rtp->getPayloadType() == it->payloadType ){
								info.sampleRate = it->clockRate;
								info.channels = it->channels;
								break;
							}
						}
						ELOG_DEBUG ("::deliverAudio. processing rtp packet stream=%u pt=%d time=%u sr time=%u", rtp->getSSRC(),
								rtp->getPayloadType(), rtp->getTimestamp(), subs.getRtpTime() );
					} else {
						ELOG_WARN("::deliverAudioData_. unsupported audio codec %d",rtp->getPayloadType());
						return -1;
					}
					break;
				};

				ELOG_DEBUG ("OneToManyProcessor deliverAudio. initializing mixing state . codec %d", info.codec );
				_S(subs.stream_.initStream(info));

				if(audioEnc_.aCoderContext_ == NULL)
				{
					info.channels = audioInfo_.channels = 1;
					info.sampleRate = audioInfo_.sampleRate = 8000;
					info.bitsPerSample = audioInfo_.bitsPerSample = 8;
					info.bitRate = audioInfo_.bitRate = audioInfo_.bitsPerSample / 8 * audioInfo_.channels * audioInfo_.sampleRate;

					switch(audioInfo_.bitsPerSample){
					case 8:
						audioInfo_.codec = AUDIO_CODEC_PCM_U8;
						break;
					case 16:
						audioInfo_.codec = AUDIO_CODEC_PCM_S16;
						break;
					default:
						ELOG_WARN("OneToManyProcessor::deliverAudioData_. cannot infer audio codec - unsupported bps");
						return -1;
						break;
					};
					_S(resampler_.initResampler(audioInfo_));
					_S(audioEnc_.initEncoder(info));
					//TODO bind payload type to encoded codec based on SdpInfo
					payloadType_= rtp->getPayloadType();
				}
			}

			if(subs.stream_.isInitialized() && audioEnc_.aCoderContext_)
			{
				// a) decode packet and mix it with the buffer
				if(subs.decodeStream(rtp,(char*)buf,len))
				{
					_S(mixAudioWith_(subs));

					// b) make sure we've waited enough to get all streams mixed
					time_range t;
					if(audioMixerManager_.getAvailableTime(t) >= 0){
						int samples = ftimeDurationToSampleNum(t.second - t.first);

						ELOG_INFO("sending %d samples",samples);

						_S(sendMixedAudio_(activeSubs,samples));
					}
				}
				return 0;

			}
		}
		else
		{
			ELOG_DEBUG ("deliverAudio. no SR for stream %u", rtp->getSSRC() );

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
	}
	catch(std::exception &e){
		ELOG_ERROR("::deliverAudio. exception %s",e.what());
		throw;
	}
	return 0;
}



int OneToManyProcessor::mixAudioWith_(AudioMixingStream &subs){

	ELOG_DEBUG("::mixAudioWith_(stream=%u). ",subs.getSSRC());

	time_range too_old = std::make_pair(filetime::MIN,std::max(audioMixerManager_.mixedTime(),getCurrentTime() - filetime::milliseconds(max_audio_mixed_buffer_size_)));
	subs.updateRange(too_old);

	while(true){

		time_range t;
		buffer_range r;
		if( subs.getBufferAndRange(t,r) == -1 )
			break;

		ELOG_DEBUG("::mixAudioWith_(stream=%u). range %lld-%lld ",subs.getSSRC(), t.first - audioMixerManager_.startTime(),
				t.second - audioMixerManager_.startTime());

		if(r.second <= r.first)
			break;

		_S(audioMixerManager_.onStreamData(subs,t));

		if(resampler_.needToResample(subs.stream_.getAudioInfo())){

			//resample: some samples can be put back due to delay introduced by resample
			int resampled = resampler_.resample(&*r.first,r.second - r.first,subs.stream_.getAudioInfo());
			if(resampled < 0){
				ELOG_WARN("mixAudioWith_(stream=%u) resample failed",subs.getSSRC());
				return -1;
			}

			if(resampled == 0){
				break;
			}

			if(resampled > 0)
			{
				BUFFER_TYPE::difference_type d=r.second - r.first;
				if(d > resampled){
					// adjust actual number of samples processed by resampler
					filetime::timestamp update = subs.stream_.samplesToDuration(d-resampled);
					ELOG_WARN("mixAudioWith_(stream=%u) resampled less samples than provided. adjust end time by %lld",subs.getSSRC(),update);
					t.second -= update;
				}
				r.first = resampler_.resampleBuffer_.begin();
				r.second = resampler_.resampleBuffer_.end();
			}
		}

		BUFFER_TYPE::difference_type range_sz = std::distance(r.first,r.second);

		// mix
		long offStart = ptimeToByteOffset(t.first),	offEnd = offStart + range_sz;

		// gap checking
		if(offEnd > audioMixBuffer_.size()){
			ELOG_DEBUG("mixAudioWith_(stream=%u) mixItEnd > audioMixBuffer_.end() by %d",subs.getSSRC(),offEnd - (long)audioMixBuffer_.size());

			audioMixBuffer_.insert(audioMixBuffer_.end(),offEnd - audioMixBuffer_.size(),0);
		}
		if(offStart < 0){
			ELOG_DEBUG("mixAudioWith_(stream=%u) mixItStart < audioMixBuffer_.begin() by %d",subs.getSSRC(),-offStart);

			audioMixBuffer_.insert(audioMixBuffer_.begin(),-offStart,0);
			offStart = 0;
		}

		BUFFER_TYPE::iterator mixItStart = audioMixBuffer_.begin() + offStart,mixItEnd = audioMixBuffer_.begin() + offEnd;

		ELOG_DEBUG("mixAudioWith_(stream=%u) offsets: mixItStart=%u mixItEnd=%u total mix buf sz=%u",
				subs.getSSRC(),
				std::distance(audioMixBuffer_.begin(),mixItStart),
				std::distance(audioMixBuffer_.begin(),mixItEnd),
				audioMixBuffer_.size());

		BUFFER_TYPE::difference_type advanced(0);
		switch(audioInfo_.bitsPerSample){
		case 8:
			advanced = audio_mix_utils< MixTraits<uint8_t> >::mix(r.first,r.second,mixItStart,mixItEnd,publishers.size());
			break;
		case 16:
			advanced = audio_mix_utils< MixTraits<uint16_t> >::mix(r.first,	r.second,mixItStart,mixItEnd,publishers.size());
			break;
		default:
			ELOG_WARN("mixAudioWith_. samples other than 8 and 16 bps aren't supported so far");
			return -1;
		};

		// check for new data reaching beyond current high marker
		audioMixBuffer_.insert(mixItStart + advanced,r.first + advanced,r.second);

		subs.updateRange(t);
		t.first = t.second;
		t.second += filetime::SECOND;
	}
	return 0;
}

filetime::timestamp OneToManyProcessor::getCurrentTime() const{
	if(!publisher){
		return filetime::MIN;
	}
	return audioStreamsInfos_.find(publisher->getAudioSourceSSRC())->second.getCurrentTime();
}

int OneToManyProcessor::sendMixedAudio_(AudioMixingStream &provider,int samples) {

	filetime::timestamp mixerLowTime = audioMixerManager_.startTime();

	// discontinuity check
	int checkedForGapSamples = audioMixBuffer_.size() / (audioInfo_.bitsPerSample / 8);
	if(checkedForGapSamples < samples){
		mixerLowTime += sampleNumToFiltimeDuration(samples - checkedForGapSamples);
		samples = checkedForGapSamples;
	}

	int rtpHdrLen = RtpHeader::MIN_SIZE;

	//TODO make sure encoder goes with current payloadType_
	int output_buffer_size = audioEnc_.calculateBufferSize(samples);
	if(output_buffer_size <= 0){
		ELOG_WARN ("OneToManyProcessor sendMixedAudio_. av_samples_get_buffer_size failed ");
		return -1;
	}
	audioOutputBuffer_.resize(output_buffer_size + rtpHdrLen);

	AVPacket avpkt;
	av_init_packet(&avpkt);

	avpkt.data = &audioOutputBuffer_.at(rtpHdrLen);
	avpkt.size = audioOutputBuffer_.size() - rtpHdrLen;

	ELOG_DEBUG ("OneToManyProcessor sendMixedAudio_. encoding %d mixed samples. enc buf sz %d mix buf sz %d", samples,audioOutputBuffer_.size(),
			audioMixBuffer_.size());

	_S(audioEnc_.encodeAudio(&audioMixBuffer_.at(0),samples,&avpkt));

	ELOG_DEBUG ("OneToManyProcessor sendMixedAudio_. sending away %d bytes", avpkt.size);

	filetime::timestamp encodedChunkDuration =  filetime::SECOND / audioInfo_.sampleRate * samples;

	BUFFER_TYPE::size_type processed = 0;

	BUFFER_TYPE::iterator itEnd = audioOutputBuffer_.begin() + avpkt.size + rtpHdrLen;


	const RtpHeader ctor;
	//packetize
	for( BUFFER_TYPE::iterator it = audioOutputBuffer_.begin() ; it < itEnd; it += RTP_PACKET_SZ ){

		int payloadSize = std::min((BUFFER_TYPE::difference_type )RTP_PACKET_SZ,itEnd - it);

		RtpHeader *rtpOut = reinterpret_cast<RtpHeader*>(&*it);
		memcpy(rtpOut,&ctor,RtpHeader::MIN_SIZE);

		rtpOut->setPayloadType(payloadType_);
		rtpOut->setSeqNumber(audioSeqNumber_++);
		rtpOut->setSSRC(publisher->getAudioSourceSSRC());
		rtpOut->setMarker(true);

		// calculate timestamp for a packet
		filetime::timestamp ftime_off = mixerLowTime - provider.getNtpTime();
		uint32_t rtp_timeoff = filetime_to_rtp_time( ftime_off, audioInfo_.sampleRate);
		rtpOut->setTimestamp( provider.getRtpTime() + rtp_timeoff );

		mixerLowTime += (payloadSize - rtpHdrLen) * encodedChunkDuration / avpkt.size;
		processed += payloadSize - rtpHdrLen;

		ELOG_DEBUG ("::sendMixedAudio_. sending rtp packet: ssrc=%u  time=%u seqn=%u pt=%u datalen=%d providerbb_clock=%u", rtpOut->getSSRC(),
				rtpOut->getTimestamp(),
				rtpOut->getSeqNumber(),
				rtpOut->getPayloadType(),
				payloadSize - rtpHdrLen,
				provider.getCurrentTimeAsRtp());

		for (std::map<std::string, sink_ptr>::iterator sendIt  = subscribers.begin(); sendIt != subscribers.end(); ++sendIt) {
			(*sendIt).second->deliverAudioData((char*)&*it,payloadSize);
		}
	}

	ELOG_DEBUG ("::sendMixedAudio_. shrink buffer sz %lu by %d bytes.", audioMixBuffer_.size(),processed);

	audioMixerManager_.updateMixerLowBound(mixerLowTime);

	audioMixBuffer_.erase(audioMixBuffer_.begin(),audioMixBuffer_.begin() + processed);

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
	if(true == publishers.insert(std::make_pair(peerId,source_ptr(webRtcConn))).second){
	}
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
		AUDIO_INFO_REP::iterator mixStream = audioStreamsInfos_.find(found->second->getAudioSourceSSRC());
		if(mixStream != audioStreamsInfos_.end()){
			ELOG_INFO("removePublisher. mixer stream %u is removed",found->second->getAudioSourceSSRC());
			audioMixerManager_.removeSubstream(mixStream->second);
			audioStreamsInfos_.erase(mixStream);
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

