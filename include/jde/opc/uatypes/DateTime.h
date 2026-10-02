#pragma once

namespace Jde::Opc{
	struct UADateTime{
		UADateTime( const UA_DateTime& dt )ι;
		UADateTime( TimePoint time )ι:_time{time}{}
		UADateTime( const jvalue& json, SRCE )ε;
		UADateTime( const google::protobuf::Timestamp& timestamp )ι;//saturating, not throwing - see the json ctor for the loud counterpart.
		α ToJson()Ι->jobject;
		α ToProto()Ι->google::protobuf::Timestamp;
		α Time()Ι->TimePoint{ return _time; }
		α UA()Ι->UA_DateTime;
	private:
		α ToParts()Ι->tuple<_int,int>;
		TimePoint _time;
	};
}