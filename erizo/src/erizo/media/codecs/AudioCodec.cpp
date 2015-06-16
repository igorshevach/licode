/**
 * AudioCodec.pp
 */

#include "../../pchheader.h"
#include "AudioCodec.h"


namespace erizo {

class AutoInitCodecs
{
public:
	AutoInitCodecs()
{
		avcodec_register_all();

}
}g_init_ffmpeg_codecs;

DEFINE_LOGGER(AudioEncoder, "media.codecs.AudioEncoder");
DEFINE_LOGGER(AudioDecoder, "media.codecs.AudioDecoder");
DEFINE_LOGGER(AudioResampler, "media.codecs.AudioResampler");




inline  AVCodecID
AudioCodecID2ffmpegDecoderID(AudioCodecID codec)
{
	switch (codec)
	{
	case AUDIO_CODEC_PCM_S16: return AV_CODEC_ID_PCM_S16LE;
	case AUDIO_CODEC_PCM_U8: return AV_CODEC_ID_PCM_U8;
	case AUDIO_CODEC_VORBIS: return AV_CODEC_ID_VORBIS;
	case AUDIO_CODEC_CN: return AV_CODEC_ID_COMFORT_NOISE;
	case AUDIO_CODEC_ISAC: return AV_CODEC_ID_COMFORT_NOISE;
	case AUDIO_CODEC_OPUS: return AV_CODEC_ID_OPUS;
	default: return AV_CODEC_ID_PCM_U8;
	}
}

AudioEncoder::AudioEncoder(EncoderCallback &cb):cb_(cb){
	aCoder_ = NULL;
	aCoderContext_=NULL;
	aFrame_ = NULL;
}

AudioEncoder::~AudioEncoder(){
	ELOG_DEBUG("AudioEncoder Destructor");
	this->closeEncoder();
}

int AudioEncoder::initEncoder (const AudioCodecInfo &inputCodec,const AudioCodecInfo& mediaInfo){

	ELOG_DEBUG("Init audioEncoder begin");

	closeEncoder();

	//	 for(AVCodec *codec = av_codec_next(NULL);codec != NULL; codec=av_codec_next(codec)){
	//		 ELOG_DEBUG("iterating: codec %s",codec->name);
	//			 if(codec->type == AVMEDIA_TYPE_AUDIO){
	//			    ELOG_DEBUG("Audio codec %s",codec->name);
	//			    if(codec->sample_fmts){
	//			    	for( int i = 0;codec->sample_fmts[i] != -1; i++){
	//			    		ELOG_DEBUG("\tformat %d",codec->sample_fmts[i]);
	//			    	}
	//			    }
	//			 }
	//
	//	 }

	inputFmt_ = AudioCodecToSampleFormat(inputCodec.codec);

	if(inputFmt_ == AV_SAMPLE_FMT_NONE){
		ELOG_WARN("initEncoder. wrong input sample fmt");
		return -1;
	}

	aCoder_ = avcodec_find_encoder(AudioCodecID2ffmpegDecoderID(mediaInfo.codec));
	if (!aCoder_) {
		ELOG_DEBUG("Audio Codec not found");
		return -1;
	}

	aCoderContext_ = avcodec_alloc_context3(aCoder_);
	if (!aCoderContext_) {
		ELOG_DEBUG("Memory error allocating audio coder context");
		return -1;
	}

	aCoderContext_->channel_layout = av_get_default_channel_layout(mediaInfo.channels);
	aCoderContext_->bit_rate = mediaInfo.bitRate;
	aCoderContext_->sample_rate = mediaInfo.sampleRate;
	aCoderContext_->channels = mediaInfo.channels;


	std::vector<AVSampleFormat> sample_fmts;
	sample_fmts.push_back(inputFmt_);
	if(aCoder_->sample_fmts){
		for(int i =0; aCoder_->sample_fmts[i] > 0;i++){
			if(inputFmt_ != aCoder_->sample_fmts[i]){
				sample_fmts.push_back(aCoder_->sample_fmts[i]);
			}
		}
	}
	sample_fmts.push_back(AV_SAMPLE_FMT_NONE);


	int success = -1;
	for(std::vector<AVSampleFormat>::size_type i = 0; i < sample_fmts.size(); i++){
		aCoderContext_->sample_fmt = sample_fmts[i];
		if( 0 == (success = avcodec_open2(aCoderContext_, aCoder_, NULL)) ){
			ELOG_INFO("initEncoder. input fmt=%s srate=%d channels=%d samplefmt=%s channle_layout=%d framesz=%d",
					av_get_sample_fmt_name(inputFmt_),
					aCoderContext_->sample_rate,
					aCoderContext_->channels,
					av_get_sample_fmt_name(aCoderContext_->sample_fmt),
					aCoderContext_->channel_layout,
					aCoderContext_->frame_size);
			break;
		}
	}
	ELOG_DEBUG("Init audioEncoder end");
	return success;
}

int AudioEncoder::encodeAudio (const filetime::timestamp &pts,unsigned char* inBuffer, int nSamples) {


	/* the codec gives us the frame size, in samples,
	 * we calculate the size of the samples buffer in bytes */
	ELOG_DEBUG("encodeAudio. channels %d, frame_size %d, sample_fmt %d input sample # %d pts %ld",
			aCoderContext_->channels, aCoderContext_->frame_size,inputFmt_,nSamples,pts);

	int frame_step = nSamples;
	if(aCoderContext_->frame_size > 0){
		frame_step = aCoderContext_->frame_size;
	}

	AVFrame frame = {0};
	frame.format = aCoderContext_->sample_fmt;

	AVPacket pkt;
	av_init_packet(&pkt);

	int result = 0;
	for(filetime::timestamp ts = pts;nSamples >= frame_step;){

		frame.nb_samples = frame_step;

		int input_buffer_size = av_samples_get_buffer_size(NULL, aCoderContext_->channels,
				frame.nb_samples, inputFmt_, 0);

		if (input_buffer_size <= 0) {
			ELOG_ERROR("wrong input_buffer_size");
			return -1;
		}

		nSamples -= frame.nb_samples;

		/* setup the data pointers in the AVFrame */
		frame.pts = ts;
		_F( avcodec_fill_audio_frame(&frame, aCoderContext_->channels,
				inputFmt_, (const uint8_t*) inBuffer, input_buffer_size,
				0));

		inBuffer += input_buffer_size;
		ts += filetime::SECOND / aCoderContext_->sample_rate * frame.nb_samples;

		int output_buffer_size = av_samples_get_buffer_size(NULL, aCoderContext_->channels,
				frame.nb_samples, aCoderContext_->sample_fmt, 0);

		if (output_buffer_size <= 0) {
			ELOG_ERROR("bad output_buffer_size");
			return -1;
		}

		if(pkt.size < output_buffer_size){
			pkt.data = cb_.allocMemory(output_buffer_size);
			_PTR(pkt.data);
			pkt.size = output_buffer_size;
		}

		int got_output = 0;
		_F( avcodec_encode_audio2(aCoderContext_, &pkt, &frame, &got_output));

		ELOG_DEBUG("encoded %d bytes samples left %d outbufsz=%d",pkt.size,frame.nb_samples,output_buffer_size);

		if (got_output) {
			//fwrite(pkt.data, 1, pkt.size, f);
			ELOG_DEBUG("Got OUTPUT");
			_S(cb_.onEncodePacket(frame.nb_samples,pkt));
			result++;
		}
	}

	return result;
}

int AudioEncoder::closeEncoder (){
	if (aCoderContext_!=NULL){
		avcodec_close(aCoderContext_);
		aCoderContext_ = NULL;
	}
	if(aFrame_)	{
		av_frame_free(&aFrame_);
		aFrame_ = NULL;
	}

	return 0;
}
int AudioEncoder::calculateBufferSize(int samples) {
	if(aCoderContext_){
		return av_samples_get_buffer_size(NULL, aCoderContext_->channels,
				samples, aCoderContext_->sample_fmt, 0);
	}
	return 0;
}


//////////////////////////////////////////////////////////////////////////////
// AudioDecoder
AudioDecoder::AudioDecoder(){
	aDecoder_ = NULL;
	aDecoderContext_ = NULL;
	dFrame_ = NULL;
}

AudioDecoder::~AudioDecoder(){
	ELOG_DEBUG("AudioDecoder Destructor");
	this->closeDecoder();
}

int AudioDecoder::initDecoder (const AudioCodecInfo& info,bool bCheckBestMT){

	ELOG_DEBUG("initDecoder");
	info.Print(logger);

	this->closeDecoder();
	aDecoder_ = avcodec_find_decoder(AudioCodecID2ffmpegDecoderID(info.codec));
	if (!aDecoder_) {
		ELOG_WARN("Audio decoder not found");
		return -1;
	}

	aDecoderContext_ = avcodec_alloc_context3(aDecoder_);
	if (!aDecoderContext_) {
		ELOG_WARN("Error allocating audio decoder context");
		return -1;
	}


	std::vector<AVSampleFormat> connection_candidates;

	if(bCheckBestMT && aDecoder_->sample_fmts){
		AVCodec *encoder = avcodec_find_encoder(AudioCodecID2ffmpegDecoderID(info.codec));
		if(encoder && encoder->sample_fmts){
			ELOG_INFO("found encoder. dec=%p enc=%p",aDecoder_->sample_fmts, encoder->sample_fmts);
			for(const AVSampleFormat *psd = &aDecoder_->sample_fmts[0]; *psd > 0; psd++){
				for(const AVSampleFormat *pse = &encoder->sample_fmts[0];*pse > 0; pse++){
					if(*pse == *psd){
						ELOG_INFO("found candidate for both decoder and encoder:  %s.",av_get_sample_fmt_name(*pse));
						connection_candidates.push_back(*pse);
						break;
					}
				}
			}
		}
	}

	if(connection_candidates.empty()){
		AVSampleFormat fmt = AudioCodecToSampleFormat(info.codec);
		if(fmt != AV_SAMPLE_FMT_NONE){
			connection_candidates.push_back(fmt);
		}
	}

	_S(avcodec_get_context_defaults3(aDecoderContext_,aDecoder_));

	aDecoderContext_->bit_rate = info.bitRate;
	aDecoderContext_->sample_rate = info.sampleRate;
	aDecoderContext_->channels = info.channels;

	if(!connection_candidates.empty()){
		int ret = -1;
		for(std::vector<AVSampleFormat>::iterator it = connection_candidates.begin();
				it != connection_candidates.end(); it++){
			aDecoderContext_->sample_fmt = *it;

			ELOG_INFO("trying to create decoder with sample_fmt: %s.",av_get_sample_fmt_name(*it));

			if( (ret = avcodec_open2(aDecoderContext_, aDecoder_, NULL)) >= 0){
				break;
			}
		}
		if(ret < 0)
			return ret;
	} else {

		ELOG_INFO("trying to create decoder with DEFAULT sample_fmt: %s.",av_get_sample_fmt_name(aDecoderContext_->sample_fmt));

		_S( avcodec_open2(aDecoderContext_, aDecoder_, NULL));
	}

	ELOG_INFO("initDecoder. br=%d srate=%d channels=%d samplefmt=%s",
			aDecoderContext_->bit_rate,
			aDecoderContext_->sample_rate,
			aDecoderContext_->channels,
			av_get_sample_fmt_name(aDecoderContext_->sample_fmt));

	return 0;
}

int AudioDecoder::initDecoder (AVCodecContext* context){
	return 0;
}
int AudioDecoder::decodeAudio(unsigned char* inBuff, int inBuffLen, void *ctx,AudioDecoder::pDecodeCB cb){

	AVPacket avpkt;
	int len = -1;

	av_init_packet(&avpkt);
	avpkt.data = (unsigned char*) inBuff;
	avpkt.size = inBuffLen;

	while (avpkt.size > 0) {

		//Puede fallar. Cogido de libavcodec/utils.c del paso de avcodec_decode_audio3 a avcodec_decode_audio4
		//avcodec_decode_audio3(aDecoderContext, (short*)decBuff, &outSize, &avpkt);

		if(!dFrame_)
		{
			dFrame_ = av_frame_alloc();
		}

		if(!dFrame_)
		{
			ELOG_WARN("av_frame_alloc failed");
			return -1;
		}

		int got_frame = 0;

		//      aDecoderContext->get_buffer = avcodec_default_get_buffer;
		//      aDecoderContext->release_buffer = avcodec_default_release_buffer;

		len = avcodec_decode_audio4(aDecoderContext_, dFrame_, &got_frame,
				&avpkt);
		if (len >= 0 && got_frame) {

			if( (*cb)(ctx,dFrame_)  < 0)
				return -1;

			/* Si hay más de un canal
           if (planar && aDecoderContext->channels > 1) {
           uint8_t *out = ((uint8_t *)decBuff) + plane_size;
           for (int ch = 1; ch < aDecoderContext->channels; ch++) {
           memcpy(out, frame.extended_data[ch], plane_size);
           out += plane_size;
           }
           }
			 */
		}
		else if (len < 0) {
			ELOG_DEBUG("Error al decodificar audio");
			return -1;
		}

		avpkt.size -= len;
		avpkt.data += len;

	}
	return 0;
}
int AudioDecoder::closeDecoder(){
	if (aDecoderContext_!=NULL){
		avcodec_close(aDecoderContext_);
	}
	if (dFrame_!=NULL){
		av_frame_free(&dFrame_);
	}
	return 0;
}

///////////////////////////////////////////////////////////
// AudioResampler
AudioResampler::AudioResampler()
:resampleContext_(NULL)
{}

AudioResampler::~AudioResampler()
{
	freeContext();
}

int AudioResampler::initResampler (const AudioCodecInfo& info){
	info_ = info;
	ELOG_INFO("initResampler. bps=%d channels=%d samplerate=%d codec=%s",
			info.bitsPerSample,
			info.channels,
			info.sampleRate,
			AudioCodecToString(info.codec));
	return 0;
}
int AudioResampler::resample(const filetime::timestamp &pts_in,
		 filetime::timestamp &pts_out,
		 unsigned char *data,
		 int samples,
		 const AudioCodecInfo &info)
{
	if(!needToResample(info)){
		return 0;
	}

	AVCodecID avcodecId = AudioCodecID2ffmpegDecoderID(info.codec);

	AVCodec *pCodec = avcodec_find_decoder(avcodecId);

	AVFrame in = {0};

	AVSampleFormat in_sample_fmt = AudioCodecToSampleFormat(info.codec);
	int bps = av_get_bytes_per_sample(in_sample_fmt);
	if(bps <= 0){
		ELOG_ERROR("resample. could not get bits per sample from sample fmt %d",in_sample_fmt);
		return -1;
	}
	in.pts = pts_in;
	in.format = in_sample_fmt;
	in.nb_samples = samples;
	in.sample_rate = info.sampleRate;
	in.channel_layout = info.channels;

	int expected_buffer_size = 0, input_buffer_size = info.sampleNumToByteOffset(samples);
	do{
		expected_buffer_size = av_samples_get_buffer_size(NULL, in.channel_layout,
				in.nb_samples, in_sample_fmt, 0);

		//		 if (expected_buffer_size > len) {
		//			ELOG_DEBUG("resample. adjusting input size %d expected %d channels=%d nb samples=%d sample fmt=%d",len,expected_buffer_size,
		//					in.channel_layout,
		//					in.nb_samples,
		//					in_sample_fmt);
		//		 }
	}while(expected_buffer_size > input_buffer_size && in.nb_samples-- > 0);

	if(in.nb_samples <= 0){
		ELOG_DEBUG("could not find samples to resample. input len=%d sample type=%s",input_buffer_size,
				av_get_sample_fmt_name(in_sample_fmt));
		return 0;
	} else {
		ELOG_DEBUG("resample. processing %d samples. original sample num %d ",in.nb_samples, samples);
	}

	input_buffer_size = expected_buffer_size;

	/* setup the data pointers in the AVFrame */
	_S(avcodec_fill_audio_frame(&in, in.channel_layout,in_sample_fmt, (const uint8_t*) data, input_buffer_size,	0));

	AVSampleFormat out_sample_fmt = AudioCodecToSampleFormat(info_.codec);

	// int dst_nb_samples = av_rescale_rnd(swr_get_delay(m_resampleContext, in.sample_rate) +
	// in.nb_samples, frame->sample_rate , in.sample_rate, AV_ROUND_UP);
	if(resampleContext_ == NULL
			|| info.channels != last_.channels
			|| info.sampleRate != last_.sampleRate
			|| info.codec != last_.codec ){

		freeContext();

		ELOG_DEBUG("resample. init state from");
		info.Print(logger);
		ELOG_DEBUG(" to ");
		info_.Print(logger);

		last_ = info;

		resampleContext_ = avresample_alloc_context();

		_PTR(resampleContext_);
		/* set options */
		_S(av_opt_set_int(resampleContext_, "in_channel_layout", in.channel_layout, 0));
		_S(av_opt_set_int(resampleContext_, "in_sample_rate", in.sample_rate, 0));
		_S(av_opt_set_int(resampleContext_, "in_sample_fmt", in.format, 0));
		_S(av_opt_set_int(resampleContext_, "out_channel_layout", info_.channels, 0));
		_S(av_opt_set_int(resampleContext_, "out_sample_rate", info_.sampleRate, 0));
		_S(av_opt_set_int(resampleContext_, "out_sample_fmt", out_sample_fmt, 0));
	}

	AVFrame out = {0};

	out.nb_samples = in.nb_samples;
	out.format = out_sample_fmt;
	out.sample_rate = info_.sampleRate;
	out.channel_layout = info_.channels;

	int resample_buffer_size = av_samples_get_buffer_size(NULL, out.channel_layout,
			out.nb_samples, out_sample_fmt, 0);

	if (resample_buffer_size <= 0) {
		ELOG_ERROR("wrong input");
		return -1;
	}

	resampleBuffer_.resize(resample_buffer_size);

	/* setup the data pointers in the AVFrame */
	_S( avcodec_fill_audio_frame(&out, info_.channels,
			out_sample_fmt, (const uint8_t*) &resampleBuffer_.at(0), resampleBuffer_.size(),
			0));


	int ret = avresample_convert_frame(resampleContext_,&out,&in);

	if(!ret){
		pts_out = pts_in;
		int delay = avresample_get_delay(resampleContext_);
		if(delay > 0){
			pts_out -= info_.sampleNumToFiltimeDuration(delay);
		}
		return out.nb_samples;
	}
	ELOG_WARN("resample. avresample_convert_frame errro %d",ret);
	return ret;
}

void AudioResampler::freeContext()
{
	if(resampleContext_)
	{
		avresample_free(&resampleContext_);
		resampleContext_ = NULL;
		last_.codec = AUDIO_CODEC_UNDEFINED;
	}
}
}

