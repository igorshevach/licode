/*
 * pchheader.h
 *
 *  Created on: Jun 7, 2015
 *      Author: igors
 */

#ifndef PCHHEADER_H_
#define PCHHEADER_H_

#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <stddef.h> // size_t, ptrdiff_t
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
#include <cstdio>
#include <cstring>
#include <stdio.h>
#include <cstddef>


#include <boost/cstdint.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread.hpp>

#include <string>
#include <queue>
#include <vector>
#include <map>
#include <deque>
#include <list>
#include <algorithm>
#include <limits>
#include <sstream>


extern "C" {
#ifndef INT64_C
#define INT64_C(c) (c ## LL)
#define UINT64_C(c) (c ## ULL)
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavresample/avresample.h>
}

#include "logger.h"
#include "erizo_common.h"
#include "MediaDefinitions.h"
#include "StringUtil.h"
#include "rtp/RtpHeaders.h"

#endif //#ifdef __cplusplus

#endif /* PCHHEADER_H_ */
