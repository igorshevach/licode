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
	void activatePublisher_(const source_ptr& src);

  typedef boost::shared_ptr<MediaSink> sink_ptr;
  FeedbackSink* feedbackSink_;
	
  std::map<std::string, source_ptr > publishers;
  typedef std::map<std::string, source_ptr >::iterator publisher_it;

  int deliverAudioData_(char* buf, int len);
	int deliverVideoData_(char* buf, int len);
  int deliverFeedback_(char* buf, int len);
  void closeAll();

  enum {INACTIVE,ACTIVATING,ACTIVATE} activeState_;

};

} /* namespace erizo */
#endif /* ONETOMANYPROCESSOR_H_ */
