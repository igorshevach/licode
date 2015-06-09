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
	default: return AV_CODEC_ID_PCM_U8;
	}
}

AudioEncoder::AudioEncoder(){
	aCoder_ = NULL;
	aCoderContext_=NULL;
	aFrame_ = NULL;
}

AudioEncoder::~AudioEncoder(){
	ELOG_DEBUG("AudioEncoder Destructor");
	this->closeEncoder();
}

int AudioEncoder::initEncoder (const AudioCodecInfo& mediaInfo){

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

	aCoderContext_->sample_fmt = aCoder_->sample_fmts[0];
	aCoderContext_->bit_rate = mediaInfo.bitRate;
	aCoderContext_->sample_rate = mediaInfo.sampleRate;
	aCoderContext_->channels = 1;
	char errbuff[500];
	int res = avcodec_open2(aCoderContext_, aCoder_, NULL);
	if(res != 0){
		av_strerror(res, (char*)(&errbuff), 500);
		ELOG_WARN("fail when opening input %s", errbuff);
		return -1;
	}
	ELOG_DEBUG("initEncoder. srate=%d channels=%d samplefmt=%d",
			aCoderContext_->sample_rate,
			aCoderContext_->channels,
			aCoderContext_->sample_fmt);
	ELOG_DEBUG("Init audioEncoder end");
	return 0;
}

int AudioEncoder::encodeAudio (unsigned char* inBuffer, int nSamples, AVPacket* pkt) {
	AVFrame frame = {0};

	frame.nb_samples = nSamples;
	frame.format = aCoderContext_->sample_fmt;
	//	frame->channel_layout = aCoderContext_->channel_layout;

	/* the codec gives us the frame size, in samples,
	 * we calculate the size of the samples buffer in bytes */
	ELOG_DEBUG("channels %d, frame_size %d, sample_fmt %d",
			aCoderContext_->channels, aCoderContext_->frame_size,
			aCoderContext_->sample_fmt);

	int input_buffer_size = av_samples_get_buffer_size(NULL, aCoderContext_->channels,
			frame.nb_samples, AV_SAMPLE_FMT_S16, 0);

	if (input_buffer_size <= 0) {
		ELOG_ERROR("wrong input_buffer_size");
		return -1;
	}


	/* setup the data pointers in the AVFrame */
	int ret = avcodec_fill_audio_frame(&frame, aCoderContext_->channels,
			AV_SAMPLE_FMT_S16, (const uint8_t*) inBuffer, input_buffer_size,
			0);

	if (ret < 0) {
		ELOG_ERROR("could not setup audio frame");
		return ret;
	}

	int output_buffer_size = av_samples_get_buffer_size(NULL, aCoderContext_->channels,
			frame.nb_samples, aCoderContext_->sample_fmt, 0);

	if (output_buffer_size <= 0) {
		ELOG_ERROR("bad output_buffer_size");
		return -1;
	}

	if( pkt->size < output_buffer_size ){
		ELOG_ERROR("not enough storage:  %d need %d",pkt->size, output_buffer_size);
		return -1;
	}

	int got_output = 0;
	ret = avcodec_encode_audio2(aCoderContext_, pkt, &frame, &got_output);
	if (ret < 0) {
		ELOG_ERROR("error encoding audio frame");
		return ret;
	}

	ELOG_DEBUG("encoded %d bytes",pkt->size);

	if (got_output) {
		//fwrite(pkt.data, 1, pkt.size, f);
		ELOG_DEBUG("Got OUTPUT");
		return 0;
	}


	return -1;
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

int AudioDecoder::initDecoder (const AudioCodecInfo& info){
	this->closeDecoder();
	aDecoder_ = avcodec_find_decoder(AudioCodecID2ffmpegDecoderID(info.codec));
	if (!aDecoder_) {
		ELOG_DEBUG("Audio decoder not found");
		return -1;
	}

	aDecoderContext_ = avcodec_alloc_context3(aDecoder_);
	if (!aDecoderContext_) {
		ELOG_DEBUG("Error allocating audio decoder context");
		return -1;
	}

	switch(info.bitsPerSample)
	{
	case 8:
		aDecoderContext_->sample_fmt = AV_SAMPLE_FMT_U8;
		break;
	case 16:
		aDecoderContext_->sample_fmt = AV_SAMPLE_FMT_S16;
		break;
	};
	aDecoderContext_->bit_rate = info.bitRate;
	aDecoderContext_->sample_rate = info.sampleRate;
	aDecoderContext_->channels = info.channels;

	_S(avcodec_open2(aDecoderContext_, aDecoder_, NULL) < 0);

	ELOG_INFO("initDecoder. br=%d srate=%d channels=%d samplefmt=%d",
			aDecoderContext_->bit_rate,
			aDecoderContext_->sample_rate,
			aDecoderContext_->channels,
			aDecoderContext_->sample_fmt);
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
	return 0;
}
int AudioResampler::resample(unsigned char *data,int len,const AudioCodecInfo &info)
{
	if(!needToResample(info)){
		return 0;
	}

	AVCodecID avcodecId = AudioCodecID2ffmpegDecoderID(info.codec);

	AVCodec *pCodec = avcodec_find_decoder(avcodecId);

	AVFrame in = {0};

	AVSampleFormat in_sample_fmt = info.codec == AUDIO_CODEC_PCM_U8 ? AV_SAMPLE_FMT_U8 : AV_SAMPLE_FMT_S16;
	int bps = av_get_bytes_per_sample(in_sample_fmt);
	if(bps <= 0){
		ELOG_ERROR("resample. could not get bits per sample from sample fmt %d",in_sample_fmt);
		return -1;
	}
	in.format = in_sample_fmt;
	in.nb_samples = len / bps;
	in.sample_rate = info.sampleRate;
	in.channel_layout = info.channels;

	int expected_buffer_size = len;
	do{
		expected_buffer_size = av_samples_get_buffer_size(NULL, in.channel_layout,
			in.nb_samples, in_sample_fmt, 0);

//		 if (expected_buffer_size > len) {
//			ELOG_DEBUG("resample. adjusting input size %d expected %d channels=%d nb samples=%d sample fmt=%d",len,expected_buffer_size,
//					in.channel_layout,
//					in.nb_samples,
//					in_sample_fmt);
//		 }
	}while(expected_buffer_size > len && in.nb_samples-- > 0);

	if(in.nb_samples <= 0){
		ELOG_DEBUG("could not find samples to resample. input len=%d sample size=%d",len, bps);
		return 0;
	} else {
		ELOG_DEBUG("resample. processing %d samples ",in.nb_samples);
	}

	len = expected_buffer_size;

	/* setup the data pointers in the AVFrame */
	int ret = avcodec_fill_audio_frame(&in, in.channel_layout,
			in_sample_fmt, (const uint8_t*) data, len,	0);

	AVSampleFormat out_sample_fmt = info_.codec == AUDIO_CODEC_PCM_U8 ? AV_SAMPLE_FMT_U8 : AV_SAMPLE_FMT_S16;

	// int dst_nb_samples = av_rescale_rnd(swr_get_delay(m_resampleContext, in.sample_rate) +
	// in.nb_samples, frame->sample_rate , in.sample_rate, AV_ROUND_UP);
	if(resampleContext_ == NULL
			|| info.channels != info_.channels
			|| info.sampleRate != info_.sampleRate
			|| info.codec != info_.codec ){
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
	AVFrame frame = {0};


	frame.nb_samples = in.nb_samples;
	frame.format = out_sample_fmt;
	frame.sample_rate = info_.sampleRate;
	frame.channel_layout = info_.channels;

	int resample_buffer_size = av_samples_get_buffer_size(NULL, frame.channel_layout,
			frame.nb_samples, out_sample_fmt, 0);

	if (resample_buffer_size <= 0) {
		ELOG_ERROR("wrong input");
		return -1;
	}

	resampleBuffer_.resize(resample_buffer_size);

	/* setup the data pointers in the AVFrame */
	ret = avcodec_fill_audio_frame(&frame, info_.channels,
			out_sample_fmt, (const uint8_t*) &resampleBuffer_.at(0), resampleBuffer_.size(),
			0);

	if (ret < 0) {
		ELOG_ERROR("could not setup audio frame");
		return ret;
	}

	ret = avresample_convert_frame(resampleContext_,&frame,&in);
	if(!ret){
		return frame.nb_samples;
	}
	return ret;
}

void AudioResampler::freeContext()
{
	if(resampleContext_)
	{
		avresample_free(&resampleContext_);
		resampleContext_ = NULL;
	}
}
}

