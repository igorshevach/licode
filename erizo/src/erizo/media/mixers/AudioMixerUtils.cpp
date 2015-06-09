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
:streamsMask_(0),start_(filetime::MAX / 2),  end_(filetime::MIN / 2),
 min_buffer_sz_(min_buffer), max_buffer_sz_(max_buffer),mixedBase_(filetime::MIN / 2) {
	reset(end_);
}

void AudioMixerStateManager::onStreamData(uint32_t id,const time_range &t){

	ELOG_DEBUG("onStreamData. stream id =%d. range %ld-%ld",id, t.first,t.second);

	if(sentinel_ < 0){
		sentinel_ = t.first + min_buffer_sz_;
	}
	if(t.second >= sentinel_){
		readyMask_ |= (1 << id);
	} else {
		readyMask_ &= ~(1 << id);
	}
	end_ = std::max(t.second,end_);
	start_ = std::max(mixedBase_,std::min(t.first,start_));
}

int AudioMixerStateManager::getAvailableTime(time_range &t) const {
	if(isReady()){
		t.first = start_;
		t.second = sentinel_;
		ELOG_DEBUG("getAvailableTime. ready. range %ld-%ld",t.first,t.second);
		return 0;
	} else if (isTimeout()) {
		t.first = start_;
		t.second = start_ + (end_-start_)/2;
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
	end_ = std::max(end_,start_);
	reset(start_ + min_buffer_sz_);
}

uint32_t AudioMixerStateManager::generateId() {
	for(uint32_t id = 0 ; id < sizeof(streamsMask_) * 8; id++){
		uint64_t bit = 1 << id;
		if(~streamsMask_ & bit){
			streamsMask_ |= bit;
			return id;
		}
	}
	return -1;
}

void AudioMixerStateManager::removeId(const uint32_t &id){
	streamsMask_ &= ~(1 << id);
}

bool AudioMixerStateManager::isReady() const{
	return (readyMask_ == streamsMask_);
}

bool AudioMixerStateManager::isTimeout() const{
	return (end_ - start_ >= max_buffer_sz_);
}

void AudioMixerStateManager::reset(const filetime::timestamp &t){
	sentinel_ = t;
	readyMask_ = 0ULL;
}
}



