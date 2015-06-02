#include "AudioSubStream.h"

namespace erizo{

DEFINE_LOGGER(AudioSubstream, "media.mixers.AudioSubstream");
DEFINE_LOGGER(AudioMixingStream, "media.mixers.AudioMixingStream");

int AudioSubstream::initStream(const AudioCodecInfo &info){
	_S(audioDec_.initDecoder(info));
	info_ = info;
	//TODO make sure all these are unknown (taken from SDP or elsewhere)
	info_.bitsPerSample = av_get_bytes_per_sample(audioDec_.aDecoderContext_->sample_fmt) * 8;
	info_.bitsPerSample = info_.bitsPerSample ? info_.bitsPerSample : 8;
	info_.channels = audioDec_.aDecoderContext_->channel_layout ? audioDec_.aDecoderContext_->channel_layout : 1;
	info_.sampleRate = audioDec_.aDecoderContext_->sample_rate ? audioDec_.aDecoderContext_->sample_rate : 8000;
	info_.codec = info_.bitsPerSample == 8 ? AUDIO_CODEC_PCM_U8 : AUDIO_CODEC_PCM_S16;

	return 0;
}

bool AudioSubstream::isInitialized() const{
	return audioDec_.aDecoderContext_ != NULL;
}

int AudioSubstream::decodeSteam(char *buf,int len){
	_PTR(audioDec_.aDecoderContext_);
	// a) decode packet and mix it with the buffer
	_S(audioDec_.decodeAudio((unsigned char*)buf,len,this,&onDecodeAudio_s));
	if(audioDecodeBuffer_.size() > 0)
	{
		return audioDecodeBuffer_.size() / av_get_bytes_per_sample(audioDec_.aDecoderContext_->sample_fmt);
	}
	return 0;
}

int AudioSubstream::onDecodeAudio_s(void *ctx,AVFrame *ready)
{
	AudioSubstream *dc = reinterpret_cast<AudioSubstream*>(ctx);
	return dc->onDecodeAudio_(ready);
}

int AudioSubstream::onDecodeAudio_(AVFrame *ready)
{
	audioDecodeBuffer_.insert(audioDecodeBuffer_.end(),ready->data[0],ready->data[0] + ready->nb_samples * av_get_bytes_per_sample((AVSampleFormat)ready->format));
	return ready->nb_samples;
}

filetime::timestamp AudioSubstream::samplesToDuration(const int &samples) const{
	return filetime::timestamp(samples * filetime::SECOND / getSampleRate());
}
int AudioSubstream::durationToOffset(const filetime::timestamp &interval) const{
	return interval * audioDec_.aDecoderContext_->bit_rate / filetime::SECOND;
}
filetime::timestamp AudioSubstream::bitrateToDuration(const int &br) const{
	return filetime::SECOND * br / audioDec_.aDecoderContext_->bit_rate;
}
const AudioCodecInfo &AudioSubstream::getAudioInfo() const
{
	return info_;
}

/*
 * AudioMixingStream implementation
 * */


AudioMixingStream::AudioMixingStream()
{
	invalidate_times();
}

int AudioMixingStream::addTimeRange(const time_range &r){
	ranges_.push_back(r);
	return 0;
}
int AudioMixingStream::getBufferAndRange(time_range &r,buffer_range &br){
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

int AudioMixingStream::updateRange(const time_range &r){
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
buffer_range AudioMixingStream::empty_range() {
	return std::make_pair(stream_.audioDecodeBuffer_.end(),stream_.audioDecodeBuffer_.end());
}
void AudioMixingStream::invalidate_times(){
	ranges_.clear();
}

}
