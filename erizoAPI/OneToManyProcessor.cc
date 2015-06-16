#ifndef BUILDING_NODE_EXTENSION
#define BUILDING_NODE_EXTENSION
#endif
#include <pchheader.h>
#include <node.h>
#include "OneToManyProcessor.h"
#include <sstream>

using namespace v8;

OneToManyProcessor::OneToManyProcessor() {};
OneToManyProcessor::~OneToManyProcessor() {};

void OneToManyProcessor::Init(Handle<Object> target) {
  // Prepare constructor template
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  tpl->SetClassName(String::NewSymbol("OneToManyProcessor"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  // Prototype
  tpl->PrototypeTemplate()->Set(String::NewSymbol("close"), FunctionTemplate::New(close)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("addPublisher"), FunctionTemplate::New(addPublisher)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("removePublisher"), FunctionTemplate::New(removePublisher)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("addExternalOutput"), FunctionTemplate::New(addExternalOutput)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("setExternalPublisher"), FunctionTemplate::New(setExternalPublisher)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("getPublisherState"), FunctionTemplate::New(getPublisherState)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("hasPublisher"), FunctionTemplate::New(hasPublisher)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("addSubscriber"), FunctionTemplate::New(addSubscriber)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("removeSubscriber"), FunctionTemplate::New(removeSubscriber)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("activatePublisher"), FunctionTemplate::New(activatePublisher)->GetFunction());

  Persistent<Function> constructor = Persistent<Function>::New(tpl->GetFunction());
  target->Set(String::NewSymbol("OneToManyProcessor"), constructor);
}

Handle<Value> OneToManyProcessor::New(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = new OneToManyProcessor();
  obj->me = new erizo::OneToManyProcessor();
  obj->msink = obj->me;

  obj->Wrap(args.This());

  return args.This();
}

Handle<Value> OneToManyProcessor::close(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

  delete me;

  return scope.Close(Null());
}

Handle<Value> OneToManyProcessor::addPublisher(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

  WebRtcConnection* param = ObjectWrap::Unwrap<WebRtcConnection>(args[0]->ToObject());
  erizo::WebRtcConnection* wr = (erizo::WebRtcConnection*)param->me;

  erizo::MediaSource* ms = dynamic_cast<erizo::MediaSource*>(wr);

  v8::String::Utf8Value param1(args[1]->ToString());

  // convert it to string
  std::string peerId = std::string(*param1);

  me->addPublisher(ms,peerId);

  return scope.Close(Null());
}
Handle<Value> OneToManyProcessor::setExternalPublisher(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

  ExternalInput* param = ObjectWrap::Unwrap<ExternalInput>(args[0]->ToObject());
  erizo::ExternalInput* wr = (erizo::ExternalInput*)param->me;

  erizo::MediaSource* ms = dynamic_cast<erizo::MediaSource*>(wr);

  std::stringstream rndname;
  rndname << "external input " << std::rand();
  me->addPublisher(ms,rndname.str());

  return scope.Close(Null());
}

Handle<Value> OneToManyProcessor::getPublisherState(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

  erizo::MediaSource * ms = me->publisher.get();

  erizo::WebRtcConnection* wr = (erizo::WebRtcConnection*)ms;

  int state = wr->getCurrentState();

  return scope.Close(Number::New(state));
}

Handle<Value> OneToManyProcessor::hasPublisher(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

  bool p = true;

  if(me->publisher == NULL) {
    p = false;
  }
  
  return scope.Close(Boolean::New(p));
}

Handle<Value> OneToManyProcessor::addSubscriber(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

  WebRtcConnection* param = ObjectWrap::Unwrap<WebRtcConnection>(args[0]->ToObject());
  erizo::WebRtcConnection* wr = param->me;

  erizo::MediaSink* ms = dynamic_cast<erizo::MediaSink*>(wr);

// get the param
  v8::String::Utf8Value param1(args[1]->ToString());

// convert it to string
  std::string peerId = std::string(*param1);
  me->addSubscriber(ms, peerId);

  return scope.Close(Null());
}

Handle<Value> OneToManyProcessor::addExternalOutput(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

  ExternalOutput* param = ObjectWrap::Unwrap<ExternalOutput>(args[0]->ToObject());
  erizo::ExternalOutput* wr = param->me;

  erizo::MediaSink* ms = dynamic_cast<erizo::MediaSink*>(wr);

// get the param
  v8::String::Utf8Value param1(args[1]->ToString());

// convert it to string
  std::string peerId = std::string(*param1);
  me->addSubscriber(ms, peerId);

  return scope.Close(Null());
}

Handle<Value> OneToManyProcessor::removeSubscriber(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

// get the param
  v8::String::Utf8Value param1(args[0]->ToString());

// convert it to string
  std::string peerId = std::string(*param1);
  me->removeSubscriber(peerId);

  return scope.Close(Null());
}

Handle<Value> OneToManyProcessor::removePublisher(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

// get the param
  v8::String::Utf8Value param1(args[0]->ToString());

// convert it to string
  std::string peerId = std::string(*param1);
  me->removePublisher(peerId);

  return scope.Close(Null());
}

Handle<Value> OneToManyProcessor::activatePublisher(const Arguments& args) {
  HandleScope scope;

  OneToManyProcessor* obj = ObjectWrap::Unwrap<OneToManyProcessor>(args.This());
  erizo::OneToManyProcessor *me = (erizo::OneToManyProcessor*)obj->me;

// get the param
  v8::String::Utf8Value param1(args[0]->ToString());

// convert it to string
  std::string peerId = std::string(*param1);
  me->activatePublisher(peerId);

  return scope.Close(Null());
}
