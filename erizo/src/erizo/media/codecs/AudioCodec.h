/**
 * VideoCodec.h
 */

#ifndef AUDIOCODEC_H_
#define AUDIOCODEC_H_

#include "Codecs.h"
#include "logger.h"



namespace erizo {

#define _F(x) {\
		int res=x;\
		if( res < 0){\
			char errbuff[1024*10];\
			if(!av_strerror(res, (char*)(&errbuff), sizeof(errbuff))){\
				ELOG_WARN( #x " error %d : %s",res, errbuff);\
			} else {\
				ELOG_WARN( #x "error %d", res);\
			}\
			return res;\
		}\
}


inline
AVSampleFormat
AudioCodecToSampleFormat(const AudioCodecID &codec){
	switch(codec){
	case AUDIO_CODEC_PCM_U8: return AV_SAMPLE_FMT_U8;
	case AUDIO_CODEC_PCM_S16: return AV_SAMPLE_FMT_S16;
	case AUDIO_CODEC_PCM_FLTP: return AV_SAMPLE_FMT_FLTP;
	default:
		return AV_SAMPLE_FMT_NONE;
		break;
	};
}

inline
AudioCodecID
SampleFormatToAudioCodec(const AVSampleFormat &fmt){
	switch(fmt){
	case AV_SAMPLE_FMT_U8: return AUDIO_CODEC_PCM_U8;
	case AV_SAMPLE_FMT_S16: return AUDIO_CODEC_PCM_S16;
	case AV_SAMPLE_FMT_FLTP: return AUDIO_CODEC_PCM_FLTP;
	default:
		return AUDIO_CODEC_UNDEFINED;
		break;
	};
}


class EncoderCallback {
public:
	virtual ~EncoderCallback(){}
	 virtual int onEncodePacket(int samples,AVPacket &pkt) = 0;
	 virtual unsigned char *allocMemory(const size_t &size) = 0;
};

class AudioEncoder {
	DECLARE_LOGGER();
public:
	AudioEncoder(EncoderCallback &cb);
	virtual ~AudioEncoder();
	int initEncoder (const AudioCodecInfo &input,const AudioCodecInfo& info);
	int encodeAudio (const filetime::timestamp &pts,unsigned char* inBuffer, int nSamples);
	int closeEncoder ();
	int calculateBufferSize(int samples);
	AVCodec* aCoder_;
	AVCodecContext* aCoderContext_;
private:
	AVFrame *aFrame_;
	AVSampleFormat 	inputFmt_;
	EncoderCallback &cb_;
};


class AudioDecoder {
	DECLARE_LOGGER();
public:
	AudioDecoder();
	virtual ~AudioDecoder();
	int initDecoder (const AudioCodecInfo& info,bool bCheckBestMT = false);
	int initDecoder (AVCodecContext* context);
	typedef int (*pDecodeCB)(void *ctx,AVFrame *ready);
	int decodeAudio(unsigned char* inBuff, int inBuffLen,void *pCtx,pDecodeCB decCb);
	int closeDecoder();

	AVCodec* aDecoder_;
	AVCodecContext* aDecoderContext_;
private:
	AVFrame* dFrame_;
};



class AudioResampler {
	AudioResampler(const AudioResampler&);
	void operator=(const AudioResampler&);
	DECLARE_LOGGER();
public:
	AudioResampler();
	~AudioResampler();
	/*
	 *  > 0 - number of samples resampled for success
	 *  0 if no need to resample samples
	 *  < 0  in case of error
	 * */
	 int resample(const filetime::timestamp &pts_in,
			 filetime::timestamp &pts_out,
			 unsigned char *data,
			 int len,
			 const AudioCodecInfo &info);
	 /*
	  * */
	 int initResampler (const AudioCodecInfo& info);

	 bool needToResample(const AudioCodecInfo &info) const {
		 ELOG_DEBUG("needToResample. them=%s/%d/%d/%d us=%s/%d/%d/%d",
				 AudioCodecToString(info.codec),info.sampleRate,info.channels,info.bitsPerSample,
				 AudioCodecToString(info_.codec),info_.sampleRate,info_.channels,info_.bitsPerSample);
		 if(info.channels == info_.channels &&
				 info.sampleRate == info_.sampleRate &&
				 info.bitsPerSample == info_.bitsPerSample){

			 if(IsRawAudio(info.codec)){
				 return (info.codec != info_.codec);
			 }
			 return false;
		 }
		 return true;
	 }

	 BUFFER_TYPE resampleBuffer_;
private:
	 void freeContext();
	 struct AVAudioResampleContext *resampleContext_;
	 AudioCodecInfo info_, last_;
};

}
#endif /* AUDIOCODEC_H_ */
