#pragma once
#include <jde/opc/uatypes/Value.h>

//A flow's message (docs/flows/spec.md *Messages*):  a json object under Node-RED's conventions, which the engine owns and
//hands from node to node.
namespace Jde::Opc::Flows{
	using MessageId = uint64_t;

	//The ids of one run of the gateway, or of one replay:  the run's start in seconds in the high half and a counter in the
	//low, so none repeats across restarts.  A counter past its half carries into the seconds, which a run's own length
	//keeps ahead of.  A replay's count from the start of its range, so two replays of a range give the same ids, which are
	//then unique within the replay's trace alone (Replay.h).
	struct MessageIds final{
		explicit MessageIds( TimePoint start )ι;
		α Next()ι->MessageId{ return _next.fetch_add( 1, std::memory_order_relaxed ); }
	private:
		atomic<MessageId> _next;
	};

	//A property of a message by Node-RED's path:  `payload`, `payload.speed`, `payload.rows[2].id`.
	struct Path final{
		explicit Path( sv text )ε;//refuses an empty path, a name that is missing and an index that is not a number.
		α Find( const jobject& o )Ι->const jvalue*;
		//Makes the objects on the way.  Throws through a value that is neither an object nor null, and at an index past its array's end.
		α Set( jobject& o, jvalue value )Ε->void;
		α Erase( jobject& o )Ι->bool;
		α Text()Ι->str{ return _text; }
		α Root()Ι->str{ return std::get<string>( _steps.front() ); }//its first name.
	private:
		α Parent( const jobject& o )Ι->const jvalue*;//what holds the last step:  null for a path of one name, whose holder is `o`.
		string _text;
		vector<std::variant<string,uint>> _steps;
	};

	//`_id`, `_parent` when it was cloned or made from another, `_time` when its source made it, on the flow's clock in ISO
	//8601, `topic` and `payload`;  from a data change `node`, `status`, `sourceTime` and `serverTime`.  The rest is the flow's.
	struct Message final{
		Message()ι=default;
		explicit Message( jobject&& json )ι:Json{ move(json) }{}
		α Id()Ι->MessageId;//0 for one moved from.
		α Parent()Ι->MessageId;//0 at a source.
		α Topic()Ι->sv;
		α Payload()Ι->const jvalue&;//null when it has none.
		//A data change's fields:  `payload` from ToPayload, `node` the connection's slug and the node's id, `status` the
		//code, and each timestamp the server sent.
		α SetDataChange( sv connection, sv nodeId, const Value& value, const ITypes* types=nullptr )ι->void;
		jobject Json;
	};

	//A value as a message carries it:  what Value::ToJson writes, but for a 64-bit integer, an integer here and the socket's
	//Long there, so the nodes compare and compute on it.  Null for a Bad or an empty value.
	α ToPayload( const Value& value, const ITypes* types=nullptr )ι->jvalue;
	//A value as the web reads it, the inverse:  every integer past JavaScript's safe range, ±(2^53-1), as the protobufjs
	//Long `{high, low, unsigned}` Value::ToJson writes, at any depth, signed below 2^63 however it is held, as its json's
	//text reads back.  What the trace's reads and the live push render.
	α ToWire( jvalue& v )ι->void;
	α ToLong( MessageId id )ι->jobject;//a message's id as the web reads it.
}