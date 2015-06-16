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
  audioMixerManager_(MIN_BUCKET_SIZE,filetime::milliseconds(max_audio_mixed_buffer_size_)),
  audioEnc_(*this)
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
			// TODO (igors) need to update seqnumbers for all RTCP traffic!!
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
			//TODO (igors) reply with RR to all streams but publisher
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

			ELOG_DEBUG ("[%p] deliverAudio. processing rtp packet stream=%u pt=%d time=%u sr time=%u",
					this,
					rtp->getSSRC(),
					rtp->getPayloadType(), rtp->getTimestamp(), subs.getRtpTime() );

			if(!subs.stream_.isInitialized())
			{
				AudioCodecInfo info;

				WebRtcConnection *pConn = dynamic_cast<WebRtcConnection*>(publisher.get());
				if(pConn) {
					const SdpInfo &sdp = pConn->getRemoteSdpInfo();
					const std::vector<RtpMap>& infos =sdp.getPayloadInfos();
					for(std::vector<RtpMap>::const_iterator it = infos.begin(); it != infos.end(); it++ ){
						if( rtp->getPayloadType() == it->payloadType ){
							info.sampleRate = it->clockRate;
							info.channels = it->channels;
							ELOG_DEBUG ("[%p] deliverAudio. found rtp map for ssrc %u yload pt=%d sample_rate=%d channels=%d encname=%s",
									this,
									rtp->getSSRC(),
									rtp->getPayloadType(),
									it->clockRate,
									it->channels,
									it->encodingName.c_str());
							break;
						}
					}
				}
				switch(rtp->getPayloadType())
				{
				case OPUS_48000_PT:
					info.codec = AUDIO_CODEC_OPUS;
					info.bitsPerSample = 16;
					break;
				case PCMU_8000_PT:
					info.codec = AUDIO_CODEC_PCM_U8;
					info.bitsPerSample = 8;
					break;
				case CN_8000_PT:
				case CN_16000_PT:
				case CN_32000_PT:
				case CN_48000_PT:
					info.codec = AUDIO_CODEC_CN;
					info.bitsPerSample = 16;
					break;
				case ISAC_16000_PT:
				case ISAC_32000_PT:
					info.codec = AUDIO_CODEC_ISAC;
					info.bitsPerSample = 16;
					break;
				default:
					ELOG_WARN("::deliverAudioData_. unsupported audio codec %d",rtp->getPayloadType());
					return -1;
				};

				ELOG_DEBUG ( "[%p] deliverAudio. initializing mixing state . codec %d", this, info.codec );
				_S(subs.stream_.initStream(info));

				if(audioEnc_.aCoderContext_ == NULL)
				{
					ELOG_DEBUG ("[%p] deliverAudio. initialize encoder and resampler.", this );
					audioInfo_ = subs.stream_.getAudioInfo();
					_S(audioEnc_.initEncoder(audioInfo_,info));
					audioInfo_.codec = SampleFormatToAudioCodec(audioEnc_.aCoderContext_->sample_fmt);
					_S(resampler_.initResampler(audioInfo_));
					//TODO bind payload type to encoded codec based on SdpInfo
					payloadType_= rtp->getPayloadType();
				}
			}

			if(subs.stream_.isInitialized() && audioEnc_.aCoderContext_)
			{
				// a) decode packet and mix it with the buffer
				ELOG_DEBUG ( "[%p] deliverAudio. decoding raw %d bytes", this, len - rtp->getHeaderLength());

				if(subs.decodeStream(rtp,(char*)buf,len))
				{
					_S(mixAudioWith_(subs));

					// b) make sure we've waited enough to get all streams mixed
					time_range t;
					if(audioMixerManager_.getAvailableTime(t) >= 0){
						int samples = audioInfo_.ftimeDurationToSampleNum(t.second - t.first);

						ELOG_INFO("sending %d samples",samples);

						_S(sendMixedAudio_(activeSubs,samples));
					}
				}
				return 0;

			}
		}
		else
		{
			if(head->isRtcp() && publisher->getAudioSourceSSRC() != head->getSSRC()){
				return 0;
			}

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

		time_range tr_mix(t);
		if(resampler_.needToResample(subs.stream_.getAudioInfo())){

			const AudioCodecInfo &aci = subs.stream_.getAudioInfo();

			//resample: some samples can be put back due to delay introduced by resample
			int samples = aci.bitrateToSampleNum(r.second - r.first);
			int resampled = resampler_.resample(tr_mix.first,tr_mix.first,&*r.first,samples,aci);
			if(resampled < 0){
				ELOG_WARN("mixAudioWith_(stream=%u) resample failed",subs.getSSRC());
				return -1;
			}

			if(resampled == 0){
				ELOG_DEBUG("no samples resampled",subs.getSSRC());
				break;
			}

			tr_mix.second = tr_mix.first + audioInfo_.sampleNumToFiltimeDuration(resampled);

			ELOG_DEBUG("mixAudioWith_(stream=%u) resampled %d out of %d in pts %ld out pts %ld", subs.getSSRC(),resampled,	samples,
					t.first,tr_mix.first);

			r.first = resampler_.resampleBuffer_.begin();
			r.second = resampler_.resampleBuffer_.end();
		}

		BUFFER_TYPE::difference_type range_sz = std::distance(r.first,r.second);

		// mix
		long offStart = ptimeToByteOffset(tr_mix.first),	offEnd = offStart + range_sz;

		// gap checking
		if(offEnd > audioMixBuffer_.size()){
			ELOG_DEBUG("mixAudioWith_(stream=%u) mixItEnd(%d) > audioMixBuffer_.size(%d)",subs.getSSRC(),offEnd , audioMixBuffer_.size());

			BUFFER_TYPE::difference_type cnt = offEnd - audioMixBuffer_.size();

			audioMixBuffer_.insert(audioMixBuffer_.end(),cnt,0);
			_S(fill_silence(audioInfo_.codec,audioMixBuffer_.end() - cnt,cnt));
		}
		if(offStart < 0){
			ELOG_DEBUG("mixAudioWith_(stream=%u) mixItStart < audioMixBuffer_.begin() by %d",subs.getSSRC(),-offStart);
			BUFFER_TYPE::difference_type cnt = -offStart;
			audioMixBuffer_.insert(audioMixBuffer_.begin(),cnt,0);
			_S(fill_silence(audioInfo_.codec,audioMixBuffer_.begin(),cnt));
			offStart = 0;
		}


		BUFFER_TYPE::iterator mixItStart = audioMixBuffer_.begin() + offStart,mixItEnd = audioMixBuffer_.begin() + offEnd;

		assert(std::distance(audioMixBuffer_.begin(),mixItStart) <= audioMixBuffer_.size());
		assert(std::distance(audioMixBuffer_.begin(),mixItEnd) <= audioMixBuffer_.size());

		ELOG_DEBUG("mixAudioWith_(stream=%u) offsets: mixItStart=%u mixItEnd=%u total mix buf sz=%u",
				subs.getSSRC(),
				std::distance(audioMixBuffer_.begin(),mixItStart),
				std::distance(audioMixBuffer_.begin(),mixItEnd),
				audioMixBuffer_.size());

		BUFFER_TYPE::difference_type advanced = do_mix(audioInfo_.codec,r.first,r.second,mixItStart,mixItEnd,publishers.size());

		if(-1 == advanced) {
			ELOG_WARN("mixAudioWith_. unsupported raw audio type %s", audioInfo_.CodecName());
			return -1;
		};

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

	// discontinuity check
	samples = audioInfo_.bitrateToSampleNum(audioMixBuffer_.size());

	//TODO make sure encoder goes with current payloadType_

	ELOG_DEBUG ("OneToManyProcessor sendMixedAudio_. encoding %d mixed samples. mix buf sz %d",
			samples,audioMixBuffer_.size());

	_S(audioEnc_.encodeAudio(audioMixerManager_.startTime(),&audioMixBuffer_.at(0),samples));

	return 0;

}

//EncoderCallback
int OneToManyProcessor::onEncodePacket(int samples,AVPacket &avpkt){

	ELOG_DEBUG ("onEncodePacket. encoded %d mixed samples => packet: len=%d pts=%ld dur=%d", samples, avpkt.size,
			avpkt.pts,avpkt.duration);

	_PTR(publisher.get());

	AudioMixingStream &provider = audioStreamsInfos_.find(publisher->getAudioSourceSSRC())->second;

	filetime::timestamp mixerLowTime = audioMixerManager_.startTime();

	int rtpHdrLen = RTP_HDR_SZ;

	filetime::timestamp encodedChunkDuration =  audioInfo_.sampleNumToFiltimeDuration(samples);

	BUFFER_TYPE::iterator itEnd = audioOutputBuffer_.begin() + avpkt.size + rtpHdrLen;

	const RtpHeader ctor;
	//packetize
	filetime::timestamp ftime_off = avpkt.pts - provider.getNtpTime();
	uint32_t rtp_timeoff = filetime_to_rtp_time( ftime_off, audioInfo_.sampleRate);

	for( BUFFER_TYPE::iterator it = audioOutputBuffer_.begin() ; it < itEnd; it += RTP_PACKET_SZ ){

		int payloadSize = std::min((BUFFER_TYPE::difference_type )RTP_PACKET_SZ,itEnd - it);

		RtpHeader *rtpOut = reinterpret_cast<RtpHeader*>(&*it);
		memcpy(rtpOut,&ctor,RTP_HDR_SZ);

		rtpOut->setPayloadType(payloadType_);
		rtpOut->setSeqNumber(audioSeqNumber_++);
		rtpOut->setSSRC(publisher->getAudioSourceSSRC());
		rtpOut->setMarker(payloadSize <= RTP_PACKET_SZ);

		// calculate timestamp for a packet
		rtpOut->setTimestamp( provider.getRtpTime() + rtp_timeoff );

		FillRtpExtData(*rtpOut);

		ELOG_DEBUG ("::onEncodePacket. sending rtp packet: ssrc=%u  time=%u seqn=%u pt=%u datalen=%d providerbb_clock=%u", rtpOut->getSSRC(),
				rtpOut->getTimestamp(),
				rtpOut->getSeqNumber(),
				rtpOut->getPayloadType(),
				payloadSize - rtpHdrLen,
				provider.getCurrentTimeAsRtp() );

		for (std::map<std::string, sink_ptr>::iterator sendIt  = subscribers.begin(); sendIt != subscribers.end(); ++sendIt) {
			(*sendIt).second->deliverAudioData((char*)&*it,payloadSize);
		}
	}

	BUFFER_TYPE::size_type processed = audioInfo_.sampleNumToByteOffset(samples);

	ELOG_DEBUG ("::onEncodePacket. shrink buffer sz %lu by %d bytes.", audioMixBuffer_.size(),processed);

	audioMixerManager_.updateMixerLowBound(audioMixerManager_.startTime() + audioInfo_.sampleNumToFiltimeDuration(samples));

	audioMixBuffer_.erase(audioMixBuffer_.begin(),audioMixBuffer_.begin() + processed);

	return 0;
}

unsigned char *OneToManyProcessor::allocMemory(const size_t &size){

	audioOutputBuffer_.resize(size+RTP_HDR_SZ);
	return &audioOutputBuffer_.at(RTP_HDR_SZ);
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

