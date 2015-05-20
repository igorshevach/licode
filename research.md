media-gateway
============

abbreviations:
-------------
media gateway = MG
bandwidth = BW
upload/download = U/D
peer-to-peer connection = P2P

abstract
--------
This doc is created in process of trying to estimate design of mediagateway.
It is based on observations and requirements of a would be webrtc mediagateway.

system requirements
-------------------

  The whole thing is pushed forward to meet several requirements:
A. get as little delay as possible under potentally harsh internet conditions.
B. be able to scale to include as many as possible participants.
C. each of participants has limited bandwidth for upload and download.
D. both upload and download bandwidth may be shared and public or otherwise subject to dynamic changes over time.
E. WebRTC is the media protocol adopted by each of the participants.
F. media gateway is based on Licode library which adopts start topology where participants communicate with server
 and server is used to multiplex streams to the rest thereby minimizing the upload bandwidth and roughly doubling the delay
 in general case.
 
 based on the above the media gateway must be flexible enough to adopt itself to varying number of participants.
 it is assumed that in general case both upload and download bandwidth owned by MG is at least as sum of bandwidthes of participants.
 
 general operation can then be as follows:
 participants begin joining the conference one by one and is assigned P2P.
 if total number of participants does not exceed their respective U/D BW then MG does not have to duplicate any of them.
 instead, it is first to join the conference in order to upload the content for live/recording using existing Kaltura technology.
 in the process the number of participants reaches the threshold of either upload or download at one  of them.
 to ammend the situation several approaches can be employed:
 1) WebRTC congestion control. participant suffering the U/D problems can gracefully reduce it's bitrate as per individual stream.
 2) upload BW: specific participant can be marked as P2P no more and from now on any newly joined member will get the stream from MG.
 3) download BW: 
  3.1 in case of VP8/9 codec existing stream BW could be reduced by dropping the frames.
  3.2 otherwise, MG could transcode the input stream for that purpose.
  
additional efforts
------------------

in addition to existing ones there is requirement to share all media in single Kaltura media entry.
all audio streams should be mixed together and video should be switched between participants according to who is talking 
at specific moment (active). the logic of assignment the role is beyond the scope of this doc.
current design makes use of distinct process for each of the participants. for the clarity the participants are called writers
and the code responsible for mixing is called mixer. 
the writer can be in one of the states: active, error.
active writer delivers samples without delay periodically once in every X ms, where X < 500 ms.
------
writer media type can be either audio or video. 
idle writer is either in state transition (joining, departing, adjusting it's BW, or just muted or switched off it's camera etc).
error writer is the one expreiencing problems beyond the scope of this doc.
both error and idle writers should not disrupt normal operation of the mixer, however must be accounted for by introducing some tiemout
upon which they can be safely assumed as not contributing to the current chunk of mixed data.
after this the process repeats itself.this model will allow for multiple contributers to access some shared resource in order
to update it during some established period of time long enough to avoid races. the mixer can lock the buffer and write it out.
the shared space is envisioned as circular buffer with base timestamp and duration. 
for audio the buffer is PCM with established number of channels and samplerate.
for video it's number of pictures corresponding to max framerate of 30 fps of preestablished resolution.
the rules of updating are as such:
at avery given moment of time a writer has latest decoded frame with timestamp and duration.
the mixer operates at some preestablished pace of ~500 ms.
writer fills in the window and blocks on condition to be raised by mixer.
it is useful to provide some sort of limits on the input queue in case of errors etc.
a mixer waits for timeout before it attempts to write data out according to configuration.
once the window is filled the base timestamp is updated as well as read offset and the writer's condition is fired.
then the mixer begins writing the window away.


