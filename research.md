WebRTC Media Gateway
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

- get as little delay as possible under potentally harsh internet conditions.
- be able to scale to include as many as possible participants.
- each of participants has limited bandwidth for upload and download.
- both upload and download bandwidth may be shared and public or otherwise subject to dynamic changes over time.
- WebRTC is the media protocol adopted by each of the participants.
- media gateway is based on Licode library which adopts start topology where participants communicate with server
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

expected pitfalls:
as always with captured media:
- timestamp jitter affecting audio is expected. 
- timestamps drift between different sources due to different sources (even between same video and microphone).

16/06/15 ishevach:
   discovered problems:
   - for some reasons chrome ntp timestamp in RTCP sender report varies from expected value by the matter of seconds or dozen of seconds.this is a nuisanse since i would expect most of connected to internet machines to be synchronized with time server. this seems to be a chrome bug. so, it looks like i have no choice but calculate streams jitter and apply local system time with jitter correction instead of relying on ntp to rtp mapping for various streams.
   - building with eclipse: for some reasons erizoAPI target (addon.node) is erased after erizo project is rebuilt.
      erizoAPI project does not notice that however runtime crashes which forces me manually clen & rebuild erizoAPI project every time there has been changes to header files in erizo project!
   - debugging with eclipse: i've modified scripts/initlicode.sh not to run init_erizoagent.sh when i intend to debug c++ code on my self. i then run custom debug configuration to debug nodejs with erizoagent.js  as cmd argument.
     another issue is mastering gdb debugging.
     sometimes it works but sometimes it doesn't.
- postmortem core debugging: despite all native code is compiled as debug core does not provide any meaningful information, but that might be due to my misunderstanding.
 
