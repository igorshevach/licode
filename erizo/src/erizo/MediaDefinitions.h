/*
 * mediadefinitions.h
 */

#ifndef MEDIADEFINITIONS_H_
#define MEDIADEFINITIONS_H_

#include "erizo_common.h"

namespace erizo{

class NiceConnection;

enum packetType{
    VIDEO_PACKET,
    AUDIO_PACKET,
    OTHER_PACKET
};

struct dataPacket{
    int comp;
    char data[1500];
    int length;
    packetType type;
};

class Monitor {
protected:
    mutable boost::mutex myMonitor_;
};

class FeedbackSink {
public:
    virtual ~FeedbackSink() {}
    int deliverFeedback(char* buf, int len){
        return this->deliverFeedback_(buf,len);
    }
private:
    virtual int deliverFeedback_(char* buf, int len)=0;
};


class FeedbackSource {
protected:
    FeedbackSink* fbSink_;
public:
    void setFeedbackSink(FeedbackSink* sink) {
        fbSink_ = sink;
    }
};

/*
 * A MediaSink
 */
class MediaSink: public virtual Monitor{
protected:
    //SSRCs received by the SINK
    unsigned int audioSinkSSRC_;
    unsigned int videoSinkSSRC_;
    //Is it able to provide Feedback
    FeedbackSource* sinkfbSource_;
public:
    int deliverAudioData(char* buf, int len){
        return this->deliverAudioData_(buf, len);
    }
    int deliverVideoData(char* buf, int len){
        return this->deliverVideoData_(buf, len);
    }
    unsigned int getVideoSinkSSRC (){
        boost::mutex::scoped_lock lock(myMonitor_);
        return videoSinkSSRC_;
    }
    void setVideoSinkSSRC (unsigned int ssrc){
        boost::mutex::scoped_lock lock(myMonitor_);
        videoSinkSSRC_ = ssrc;
    }
    unsigned int getAudioSinkSSRC (){
        boost::mutex::scoped_lock lock(myMonitor_);
        return audioSinkSSRC_;
    }
    void setAudioSinkSSRC (unsigned int ssrc){
        boost::mutex::scoped_lock lock(myMonitor_);
        audioSinkSSRC_ = ssrc;
    }
    FeedbackSource* getFeedbackSource(){
        boost::mutex::scoped_lock lock(myMonitor_);
        return sinkfbSource_;
    }
    MediaSink() : audioSinkSSRC_(0), videoSinkSSRC_(0), sinkfbSource_(NULL) {}
    virtual ~MediaSink() {}
private:
    virtual int deliverAudioData_(char* buf, int len)=0;
    virtual int deliverVideoData_(char* buf, int len)=0;

};


/**
 * A MediaSource is any class that produces audio or video data.
 */
class MediaSource: public virtual Monitor{
protected: 
    //SSRCs coming from the source
    unsigned int videoSourceSSRC_;
    unsigned int audioSourceSSRC_;
    MediaSink* videoSink_;
    MediaSink* audioSink_;
    //can it accept feedback
    FeedbackSink* sourcefbSink_;
public:
    void setAudioSink(MediaSink* audioSink){
        boost::mutex::scoped_lock lock(myMonitor_);
        this->audioSink_ = audioSink;
    }
    void setVideoSink(MediaSink* videoSink){
        boost::mutex::scoped_lock lock(myMonitor_);
        this->videoSink_ = videoSink;
    }

    FeedbackSink* getFeedbackSink(){
        boost::mutex::scoped_lock lock(myMonitor_);
        return sourcefbSink_;
    }
    virtual int sendPLI()=0;
    unsigned int getVideoSourceSSRC () const{
        boost::mutex::scoped_lock lock(myMonitor_);
        return videoSourceSSRC_;
    }
    void setVideoSourceSSRC (unsigned int ssrc){
        boost::mutex::scoped_lock lock(myMonitor_);
        videoSourceSSRC_ = ssrc;
    }
    unsigned int getAudioSourceSSRC () const{
        boost::mutex::scoped_lock lock(myMonitor_);
        return audioSourceSSRC_;
    }
    void setAudioSourceSSRC (unsigned int ssrc){
        boost::mutex::scoped_lock lock(myMonitor_);
        audioSourceSSRC_ = ssrc;
    }
    virtual ~MediaSource(){}
};

/**
 * A NiceReceiver is any class that can receive data from a nice connection.
 */
class NiceReceiver{
public:
    virtual int receiveNiceData(char* buf, int len, NiceConnection* nice)=0;
    virtual ~NiceReceiver(){}
};

namespace filetime
{
	typedef int64_t timestamp;

	const timestamp SECOND = 10000000LL;
	const timestamp MILLISEC = 10000LL;
	const timestamp MIN = std::numeric_limits<int64_t>::min();
	const timestamp MAX = std::numeric_limits<int64_t>::max();
	const timestamp NTP_TIME_BASE = 116444736000000000LL;

	inline timestamp milliseconds(const int32_t & milli)
	{
		return milli * MILLISEC;
	}
	inline timestamp nanoseconds(const timestamp & nsec)
	{
		return nsec * 100;
	}
	inline int32_t milliseconds_from(const timestamp & ft)
	{
		return ft / MILLISEC;
	}
	inline timestamp ntp_time(const uint64_t &ntp){
		return ((ntp >> 32) * (ntp & 0xFFFFFFFF) / (double)0xFFFFFFFF) * SECOND;
	}
}

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



} /* namespace erizo */


#endif /* MEDIADEFINITIONS_H_ */
