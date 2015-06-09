#ifndef ERIZO_COMMON_H_
#define ERIZO_COMMON_H_

namespace erizo{

#define _PTR(x)\
	if(!(x))\
	{\
		ELOG_WARN(#x " is NULL!");\
		return -1;\
	}

#define _S(x){\
		int ret = (x);\
		if(ret < 0)\
		{\
			ELOG_WARN(#x " failed error %d",ret);\
			return -1;\
		}\
}
typedef std::vector<unsigned char> BUFFER_TYPE;

}

#endif
