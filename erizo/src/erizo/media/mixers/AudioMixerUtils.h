/*
 * MixerUtils.h
 *
 *  Created on: Jun 8, 2015
 *      Author: igors
 */

#ifndef MIXERUTILS_H_
#define MIXERUTILS_H_

#include "erizo_common.h"
#include "AudioSubStream.h"

namespace erizo
{
template <typename SAMPLE_TRAITS>
class audio_mix_utils{

public:

	static BUFFER_TYPE::difference_type mix(BUFFER_TYPE::iterator srcIt,
			BUFFER_TYPE::iterator srcItEnd,
			BUFFER_TYPE::iterator mixItStart,
			BUFFER_TYPE::iterator mixItEnd,
			int div_factor)
	{
		return _Mix((typename SAMPLE_TRAITS::iterator)&*srcIt,
				(typename SAMPLE_TRAITS::iterator)&*srcItEnd,
				(typename SAMPLE_TRAITS::iterator)&*mixItStart,
				(typename SAMPLE_TRAITS::iterator)&*mixItEnd,
				div_factor);
	}

	static BUFFER_TYPE::difference_type _Mix(typename SAMPLE_TRAITS::iterator srcIt,
			typename SAMPLE_TRAITS::iterator srcItEnd,
			typename SAMPLE_TRAITS::iterator mixItStart,
			typename SAMPLE_TRAITS::iterator mixItEnd,
			int div_factor)
	{
		typename SAMPLE_TRAITS::iterator src = srcIt;

		struct ScaleDown{
			int by_;
			ScaleDown(const uint32_t &by):by_(by)
			{}
			void operator () (typename SAMPLE_TRAITS::reference r) const						{
				//r /= by_;
				//r /= 2;
			}
		}scaler(div_factor);

		std::for_each(srcIt,srcItEnd, scaler);

		//mix
		typename SAMPLE_TRAITS::sum_type sum;
		for(; mixItStart < mixItEnd && srcIt < srcItEnd; mixItStart++,srcIt++){
			sum = *mixItStart + *srcIt;
			// saturate
//			while(sum >= std::numeric_limits<typename SAMPLE_TRAITS::the_type>::max()){
//				sum /= 3;
//				sum *= 2;
//			}
			*mixItStart = (typename SAMPLE_TRAITS::the_type)sum;
		}
		// check for new data reaching beyond current high marker
		return (srcIt  - src) * sizeof(typename SAMPLE_TRAITS::the_type);
	}
};

template <typename T>
struct MixTraits{
	typedef T * iterator;
	typedef T & reference;
	typedef uint64_t sum_type;
	typedef T the_type;
};

template <typename T>
class SynthWave
{
public:
	SynthWave(int samplerate)
:samplerate_(samplerate),
 phase_(0.0)
{}

	static const int volume_factor = 1;

	int filldata(unsigned char *pdata,int samples,int freq = 440){
		T *data = (T*)pdata;
		int energy = 0;
		for(int i = 0; i < samples ; i++){
			double dSample = osc(freq);
			if(std::numeric_limits<T>::is_signed){
				data[i] = (dSample - 0.5) * std::numeric_limits<T>::max() / volume_factor;
			} else {
				data[i] = dSample * std::numeric_limits<T>::max() / volume_factor;
			}
			energy += data[i];
		}
		return energy;
	}
private:
	double phase_;
	int samplerate_;
	double step_;

	double osc(int freq){
		//		phase_ = std::fmod(phase_ + 1.0 / samplerate_ ,1.0);
		//		return std::sin(phase_ * 2 * M_PI * freq);
		phase_ += 2 * M_PI / samplerate_;
		if(phase_ >  2 * M_PI)
			phase_ -=  2 * M_PI;
		return std::sin(phase_ * freq);
	}
};

/*
 *   Mixer state manager.
 *   states are: underflow , overflow, optimal
 *    u-flow is when less than optimal mixer buffer space are available
 *    ovflow is when there has been enough data however not all streams have contributed
 *    optimal - can safely write some amount of mixed buffer
 * */
class AudioMixerStateManager {
	DECLARE_LOGGER();
 public:

	  AudioMixerStateManager(const filetime::timestamp &min_buffer,
			  const filetime::timestamp &max_buffer);

	  /*
	   * called when a stream with <id> has been updated with data in range <t>
	   */
	  int onStreamData(AudioMixingStream &subs,const time_range &t);

	   /*
	    *  get available range to write out.
	    *  return values:
	    *  0 for success
	    *  < 0 on
	    * */
	  int getAvailableTime(time_range &t) const;

	  /*
	   * 	notify about lower bound being successfully written out up to <t>
	   * */
	  void updateMixerLowBound(const filetime::timestamp &t);

	  /*
	   * add a stream
	   */
	  void addStream(AudioMixingStream &subs);
	  /*
	   * remove stream
	   * */
	  void removeSubstream(AudioMixingStream &subs);


	  const filetime::timestamp &startTime() const {
		  return start_;
	  }

	  const filetime::timestamp &mixedTime() const {
		  return mixedBase_;
	  }

 private:

	  bool isReady() const;
	  bool isTimeout() const;

	  //TODO manage time discontinuities i.e. when a stream report range starting with > end_

	  filetime::timestamp start_, // lower bound of mixer available time range
	  	  	  	  	  	  end_,   // upper bound ---
	  	  	  	  	  	  min_buffer_sz_, // min buffer size before we can send
	  	  	  	  	  	  max_buffer_sz_, // max buffer range available
	  	  	  	  	  	  mixedBase_; // mixed basetime - low threshold cannot go lower than that

	  typedef std::list< std::reference_wrapper<const AudioMixingStream> > RATING_LIST;

	  RATING_LIST ratings_;
 };


}

#endif /* MIXERUTILS_H_ */
