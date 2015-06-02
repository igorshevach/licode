#include "AudioSubStream.h"

namespace erizo{

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
	return AudioSubstream.aDecoderContext_ != NULL;
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
int AudioSubstream::copySamples(BUFFER_TYPE &to,BUFFER_TYPE::iterator where)

	to.insert(where,audioDecodeBuffer_.begin(),audioDecodeBuffer_.end());
	audioDecodeBuffer_.clear();
	return 0;
}

int AudioSubstream::onDecodeAudio_s(void *ctx,AVFrame *ready)
{
	AudioSubstream *dc = reinterpret_cast<AudioSubstream*>(ctx);
	return dc->onDecodeAudio_(ready);
}

int AudioSubstream::onDecodeAudio_(AVFrame *ready)
{
	audioDecodeBuffer_.insert(audioDecodeBuffer_.end(),ready->data[0],ready->data[0] + ready->nb_samples * av_get_bytes_per_sample(ready->format));
	return ready->nb_samples;
}

int AudioSubstream::calcNumOfSamples(const filetime::timestamp &interval)const {
	return filetime::millisecons_from(interval) * getSampleRate() / 1000;
}
filetime::timestamp AudioSubstream::samplesToDuration(const int &samples) const{
	 return filetime::timestamp(samples * filetime::SECOND / getSampleRate());
}
int AudioSubstream::durationToOffset(const filetime::timestamp &interval) const{
	return interval * audioDecodeBuffer_.aDecoderContext_->bit_rate / filetime::SECOND;
}
filetime::timestamp AudioSubstream::bitrateToDuration(const int &br) const{
	return filetime::SECOND * br / audioDecodeBuffer_.aDecoderContext_->bit_rate;
}
const AudioCodecInfo &AudioSubstream::getAudioInfo() const
{
	return info_;
	}

}
