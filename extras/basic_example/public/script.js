var serverUrl = "/";
var localStream, room, recording, recordingId;

function getParameterByName(name) {
  name = name.replace(/[\[]/, "\\\[").replace(/[\]]/, "\\\]");
  var regex = new RegExp("[\\?&]" + name + "=([^&#]*)"),
      results = regex.exec(location.search);
  return results == null ? "" : decodeURIComponent(results[1].replace(/\+/g, " "));
}

function startRecording () {

    var rtmpUrl = document.getElementById('rtmpurl').value;

    if (room !== undefined && rtmpUrl !== undefined && rtmpUrl.startsWith('rtmp')){
        if (!recording){
            //alert("script rtmp="+rtmpUrl);
            room.startRecording(localStream, function(id) {

                recording = true;
                recordingId = id;
            }, rtmpUrl);

        } else {
            room.stopRecording(recordingId);
            recording = false;
        }
    }
    else{
        alert("room not initialized or invalid rtmp url");
    }
}

function activateStream(){
    room.activateStream(localStream);
}


function sendShual() {
    var req = new XMLHttpRequest();
    var url = serverUrl + 'shual/';
    var body = {username: "aaa", role: "bbb"};

    req.onreadystatechange = function () {
        if (req.readyState === 4) {
            alert(req.responseText);
        }
    };

    req.open('POST', url, true);
    req.setRequestHeader('Content-Type', 'application/json');
    req.send(JSON.stringify(body));
}


window.onload = function () {
  recording = false;
  var screen = getParameterByName("screen");
  //var config = {audio: true, video: true, data: true, screen: screen, videoSize: [640, 480, 640, 480]};
  var config = {audio: true, video: true, data: true, screen: screen, videoSize: [320, 240, 320, 240]};
  // If we want screen sharing we have to put our Chrome extension id. The default one only works in our Lynckia test servers.
  // If we are not using chrome, the creation of the stream will fail regardless.
  if (screen){
    config.extensionId = "okeephmleflklcdebijnponpabbmmgeo";
  }
  localStream = Erizo.Stream(config);
  var createToken = function(userName, role, callback) {

    var req = new XMLHttpRequest();
    var url = serverUrl + 'createToken/';
    var body = {username: userName, role: role};

    req.onreadystatechange = function () {
      if (req.readyState === 4) {
        callback(req.responseText);
      }
    };

    req.open('POST', url, true);
    req.setRequestHeader('Content-Type', 'application/json');
    req.send(JSON.stringify(body));
  };

  createToken("user", "presenter", function (response) {
    var token = response;
    console.log(token);
    room = Erizo.Room({token: token});

    localStream.addEventListener("access-accepted", function () {
      var subscribeToStreams = function (streams) {
        for (var index in streams) {
          var stream = streams[index];
          if (localStream.getID() !== stream.getID()) {
            room.subscribe(stream);
          }
        }
      };

      room.addEventListener("room-connected", function (roomEvent) {

        room.publish(localStream, {maxVideoBW: 300});
        subscribeToStreams(roomEvent.streams);
      });

      room.addEventListener("stream-subscribed", function(streamEvent) {
        var stream = streamEvent.stream;
        var div = document.createElement('div');
        div.setAttribute("style", "width: 320px; height: 240px;");
        div.setAttribute("id", "test" + stream.getID());

        document.body.appendChild(div);
        stream.show("test" + stream.getID());

          stream.play()

      });


      room.addEventListener("stream-added", function (streamEvent) {
        var streams = [];
        streams.push(streamEvent.stream);
        subscribeToStreams(streams);
	if(document.getElementById('rtmpurl').value !== ""){
		document.getElementById("recordButton").visible = false;
        	startRecording();
	} else {
		document.getElementById("recordButton").visible = true;
		document.getElementById("recordButton").disabled = false;
	}
      });

      room.addEventListener("stream-removed", function (streamEvent) {
        // Remove stream from DOM
        var stream = streamEvent.stream;
        if (stream.elementID !== undefined) {
          var element = document.getElementById(stream.elementID);
          document.body.removeChild(element);
        }
      });

      room.addEventListener("stream-failed", function (streamEvent){
          console.log("STREAM FAILED, DISCONNECTION");
          room.disconnect();
      });

       //igorS
       room.addEventListener("stream-activated", function (streamEvent){

           console.log("stream activated, stream id " + streamEvent.stream.getID());
            if( localStream.getID() != streamEvent.stream.getID() ){
                localStream.video.disable();
            } else {
                localStream.video.enable();
            }

        });

      room.connect();

      localStream.show("myVideo");

    });
    localStream.init();
  });
};
