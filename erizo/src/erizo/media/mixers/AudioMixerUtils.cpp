/*
 * AudioMixerUtils.cpp
 *
 *  Created on: Jun 9, 2015
 *      Author: igors
 */
#include "../../pchheader.h"
#include "AudioMixerUtils.h"


namespace erizo{

DEFINE_LOGGER(AudioMixerStateManager, "media.mixers.AudioMixerStateManager");


AudioMixerStateManager::AudioMixerStateManager(const filetime::timestamp &min_buffer,const filetime::timestamp &max_buffer)
:start_(filetime::MAX),  end_(filetime::MIN),
 min_buffer_sz_(min_buffer), max_buffer_sz_(max_buffer),mixedBase_(filetime::MIN){
}

inline bool sortFunc(const AudioMixingStream &l,const AudioMixingStream &r){
	return l.getCurrentTime() < r.getCurrentTime();
}

int AudioMixerStateManager::onStreamData(AudioMixingStream &subs,const time_range &t){

	ELOG_DEBUG("onStreamData. stream id =%u. range %ld-%ld",subs.getSSRC(), t.first,t.second);

	start_ = std::max(mixedBase_,std::min(t.first,start_));

	ratings_.sort(sortFunc);

	return 0;
}

int AudioMixerStateManager::getAvailableTime(time_range &t) const {
	if(isReady()){
		t.first = start_;
		t.second = ratings_.front().get().getCurrentTime();
		ELOG_DEBUG("getAvailableTime. ready. range %ld-%ld",t.first,t.second);
		return 0;
	} else if (isTimeout()) {
		t.first = start_;
		t.second = start_ + (ratings_.back().get().getCurrentTime() - start_)/2;
		ELOG_DEBUG("getAvailableTime. overflow. range %ld-%ld",t.first,t.second);
		return 0;
	}
	return -1;
}

void AudioMixerStateManager::updateMixerLowBound(const filetime::timestamp &t){
	ELOG_DEBUG("updateMixerLowBound.  %ld",t);

	if(t < start_){
		ELOG_WARN("updateMixerLowBound.  t=%ld < start_=%ld",t,start_);
		return;
	}
	mixedBase_ = start_ = t;
}

inline bool is_same_stream(const AudioMixingStream &l,const AudioMixingStream &r){
	return l.getSSRC() == r.getSSRC();
}

void AudioMixerStateManager::addStream(AudioMixingStream &r){
	ELOG_DEBUG("add stream %u",r.getSSRC());
	ratings_.push_front(r);
	ratings_.unique(is_same_stream);
}


void AudioMixerStateManager::removeSubstream(AudioMixingStream &r){
	ELOG_DEBUG("removeSubstream stream %u",r.getSSRC());

	struct lookup{
		const AudioMixingStream &val_;
		lookup(const AudioMixingStream& val): val_(val){}
	 bool operator()(const AudioMixingStream& value) const {
		 return is_same_stream(value,val_);
	 }
	}lookup(r);

	ratings_.remove_if(lookup);
}

bool AudioMixerStateManager::isReady() const{
		return ratings_.front().get().getCurrentTime() - start_ >= min_buffer_sz_;
}

bool AudioMixerStateManager::isTimeout() const{
	return (ratings_.back().get().getCurrentTime() - start_ >= max_buffer_sz_);
}

}



