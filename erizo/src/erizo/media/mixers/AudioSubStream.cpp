#include "../../pchheader.h"
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
	info_.bitRate = info_.sampleRate  * info_.channels  * info_.bitsPerSample / 8;
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
	int bps = av_get_bytes_per_sample((AVSampleFormat)ready->format);
	ELOG_DEBUG("onDecodeAudio_(%p) %d samples. fmt %d (bps=%d)",this,ready->nb_samples,ready->format, bps	);

	audioDecodeBuffer_.insert(audioDecodeBuffer_.end(),ready->data[0],ready->data[0] + ready->nb_samples * bps);
	return ready->nb_samples;
}

filetime::timestamp AudioSubstream::samplesToDuration(const int &samples) const{
	return filetime::timestamp(samples * filetime::SECOND / getSampleRate());
}
int AudioSubstream::durationToOffset(const filetime::timestamp &interval) const{
	return interval * info_.bitRate / filetime::SECOND;
}
filetime::timestamp AudioSubstream::bitrateToDuration(const int &br) const{
	return filetime::SECOND * br / info_.bitRate;
}
const AudioCodecInfo &AudioSubstream::getAudioInfo() const
{
	return info_;
}

/*
 * AudioMixingStream implementation
 * */

AudioMixingStream::AudioMixingStream(uint32_t ssrc,const filetime::timestamp &tolerance)
:ssrc_(ssrc),
 ntpTime_(filetime::MIN),
 tolerance_(tolerance),
 curTime_(filetime::MIN)
{
	invalidate_times();
}

int AudioMixingStream::decodeStream(const RtpHeader *rtp,char *buf,int len){

	len -= rtp->getHeaderLength();
	buf += rtp->getHeaderLength();

	int decodedSamples;
	_S(decodedSamples = stream_.decodeSteam(buf,len));

	if(decodedSamples == 0)
		return -1;

	uint32_t rtp_diff = rtp->getTimestamp() - getRtpTime();
	uint32_t time_msecs = rtp_time_to_millisec(rtp_diff,stream_.getSampleRate());

	filetime::timestamp tStart = getNtpTime() + filetime::milliseconds( time_msecs );
	curTime_ = tStart + stream_.samplesToDuration(decodedSamples);

	ELOG_DEBUG ("::decodeStream. processing rtp packet stream=%u hdrlen=%d pt=%d time=%u ftime=%ld len=%d decoded samples=%d",
			 rtp->getSSRC(),rtp->getHeaderLength(),
			rtp->getPayloadType(), rtp->getTimestamp(), tStart , len , decodedSamples);

	_S(addTimeRange(std::make_pair(tStart,curTime_)));

	return decodedSamples;
}

int AudioMixingStream::addTimeRange(const time_range &r){
	ELOG_DEBUG("::addTimeRange. %u time_range=%ld-%ld",ssrc_,r.first - getNtpTime(),r.second- getNtpTime());

	if(ranges_.size() > 0){
		if(r.second < ranges_.back().second)
			return -1;
		// handle append / overlap
		if(r.first <= ranges_.back().second){
			ranges_.back().second = r.second;
			return 0;
		}
	}
	ranges_.push_back(r);
	return 0;
}

int AudioMixingStream::getBufferAndRange(time_range &r,buffer_range &br){

	ELOG_DEBUG("::getBufferAndRange. %u",ssrc_);

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

	r = ranges_.front();

	br.first = stream_.audioDecodeBuffer_.begin();
	br.second = br.first + stream_.durationToOffset(r.second - r.first);

	ELOG_DEBUG("::getBufferAndRange.stream=%u found=\t%ld\t%ld\t.in bt=\t%lu\t%lu\t total data size %lu",
			ssrc_,
			r.first- getNtpTime(),r.second- getNtpTime(),
			br.first-stream_.audioDecodeBuffer_.begin(),
			br.second-stream_.audioDecodeBuffer_.begin(),
			stream_.audioDecodeBuffer_.size());

	return 0;
}

int AudioMixingStream::updateRange(const time_range &r){

	ELOG_DEBUG("::updateRange. ssrc=%u range=%ld-%ld available_ranges=%lu", ssrc_,r.first- getNtpTime(),r.second- getNtpTime(),ranges_.size());
	while(!ranges_.empty()){

		if(ranges_.front().first >= r.second){
			break;
		}

		if(ranges_.front().second > r.second){

			ELOG_DEBUG("::updateRange. ssrc=%u found =%ld-%ld",ssrc_,ranges_.front().first- getNtpTime(),ranges_.front().second- getNtpTime() );

			stream_.audioDecodeBuffer_.erase(stream_.audioDecodeBuffer_.begin() ,
					stream_.audioDecodeBuffer_.begin() + stream_.durationToOffset( r.second - ranges_.front().first));

			ranges_.front().first = r.second;
			break;
		}

		ELOG_DEBUG("::updateRange. ssrc=%u remove range =%ld-%ld",ssrc_,ranges_.front().first- getNtpTime(),ranges_.front().second - getNtpTime());

		stream_.audioDecodeBuffer_.erase(stream_.audioDecodeBuffer_.begin(),
				stream_.audioDecodeBuffer_.begin() + stream_.durationToOffset(ranges_.front().second - ranges_.front().first));

		ranges_.pop_front();
	}
	ELOG_DEBUG("::updateRange. ssrc=%u at exit. available_ranges=%lu", ssrc_,ranges_.size());
	return 0;
}
buffer_range AudioMixingStream::empty_range() {
	return std::make_pair(stream_.audioDecodeBuffer_.end(),stream_.audioDecodeBuffer_.end());
}
void AudioMixingStream::invalidate_times(){
	ranges_.clear();
}

void AudioMixingStream::updateSR(const RtcpHeader &head){

	filetime::timestamp ntp = head.getNtpTimestampAsFileTime();
	filetime::timestamp oldNtp = ntpTime_;
	uint32_t oldRtp = rtpTime_;

	// workaround for wrong SR NTP timestamps on chrome
	curTime_ = ntpTime_ = currentSystemTimeAsFileTime(); /*head.getNtpTimestampAsFileTime() +*/

	rtpTime_ = head.getRtpTimestamp();

	filetime::timestamp n = currentSystemTimeAsFileTime();
	//if(oldNtp != filetime::MIN){
	ELOG_DEBUG("::updateSR. ssrc=%u last ntp=%ld(old=%ld,diff=%ld 100ns units) rtp=%u(old=%u,diff=%u msec ntp %ld curtime %ld diff %ld)", ssrc_,ntpTime_,
			oldNtp,ntpTime_-oldNtp,rtpTime_,oldRtp,(rtpTime_-oldRtp) / 8 , ntp, n , ntp - n  );
	//}
}

filetime::timestamp AudioMixingStream::getCurrentTime() const{
	return curTime_;
}

filetime::timestamp AudioMixingStream::currentSystemTimeAsFileTime(){
	struct timeval unix;
	::gettimeofday(&unix, NULL);
	return unix.tv_sec * 10000000LL + unix.tv_usec * 10 + filetime::NTP_TIME_BASE;
}

uint32_t AudioMixingStream::getCurrentTimeAsRtp() const{
	return getRtpTime() + filetime_to_rtp_time(curTime_ - ntpTime_,stream_.getSampleRate());
}
}
