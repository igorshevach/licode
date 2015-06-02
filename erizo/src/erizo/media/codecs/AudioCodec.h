/**
 * VideoCodec.h
 */

#ifndef AUDIOCODEC_H_
#define AUDIOCODEC_H_

#include "Codecs.h"
#include "logger.h"


extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavresample/avresample.h>
}

namespace erizo {




  class AudioEncoder {
    DECLARE_LOGGER();
    public:
      AudioEncoder();
      virtual ~AudioEncoder();
      int initEncoder (const AudioCodecInfo& info);
      int encodeAudio (unsigned char* inBuffer, int nSamples, AVPacket* pkt);
      int closeEncoder ();
      int calculateBufferSize(int samples);
      AVCodec* aCoder_;
       AVCodecContext* aCoderContext_;
     private:
       AVFrame *aFrame_;
  };


  class AudioDecoder {
    DECLARE_LOGGER();
    public:
      AudioDecoder();
      virtual ~AudioDecoder();
      int initDecoder (const AudioCodecInfo& info);
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
	  int resample(unsigned char *data,int len,const AudioCodecInfo &info);
	 /*
	  * */
	  int initResampler (const AudioCodecInfo& info);

	  BUFFER_TYPE resampleBuffer_;
  private:
	  void freeContext();
	  struct AVAudioResampleContext *resampleContext_;
	  AudioCodecInfo info_;
  };

}
#endif /* AUDIOCODEC_H_ */
