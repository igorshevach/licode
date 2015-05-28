/*
 * OneToManyProcessor.cpp
 */

#include "OneToManyProcessor.h"
#include "WebRtcConnection.h"
#include "rtp/RtpHeaders.h"
#include "boost/date_time/posix_time/posix_time.hpp"

namespace erizo {
DEFINE_LOGGER(OneToManyProcessor, "OneToManyProcessor");
OneToManyProcessor::OneToManyProcessor() : activeState_(OneToManyProcessor::INACTIVE){
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
	if(subscribers.empty())
		return 0;

	_PTR(publisher);

	RtpHeader* rtp = reinterpret_cast<RtpHeader*>(buf);
	if(publisher->getAudioSourceSSRC() != rtp->getSSRC() ){
		return 0;
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
	if(publisher->getVideoSourceSSRC() != rtp->getSSRC() ){
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

void OneToManyProcessor::activatePublisher_(const OneToManyProcessor::source_ptr& src)
{

	ELOG_DEBUG("OneToManyProcessor::activatePublisher activating publisher %p",src.get());

	if(!src){
		return;
	}

	activeState_ = ACTIVATING;
	publisher = src;
	feedbackSink_ = publisher->getFeedbackSink();
	for (std::map<std::string, sink_ptr >::iterator it = subscribers.begin(); it != subscribers.end(); it++ )
	{
		if ((*it).second != NULL)
		{
			(*it).second->setAudioSinkSSRC(this->publisher->getAudioSourceSSRC());
			(*it).second->setVideoSinkSSRC(this->publisher->getVideoSourceSSRC());
		}
	}

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

