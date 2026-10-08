#include "ReadValueAwait.h"
#include <jde/fwk/process/execution.h>
#include <jde/opc/uatypes/Value.h>
#include "../UAClient.h"

#define let const auto

namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)IotReadTag };
	Ω onResponse( UA_Client* /*ua*/, void* userdata, RequestId requestId, StatusCode sc, UA_DataValue* val )ι->void{
		ReadValueAwait& await = *( ReadValueAwait* )userdata;
		await.OnComplete( requestId, sc, val );
	}

	ReadValueAwait::ReadValueAwait( flat_set<NodeId> x, sp<UAClient> c, SL sl )ι:
		ReadValueAwait{ move(x), move(c), UA_TIMESTAMPSTORETURN_NEITHER, sl }
	{}
	ReadValueAwait::ReadValueAwait( flat_set<NodeId> x, sp<UAClient> c, UA_TimestampsToReturn timestamps, SL sl )ι:
		base{ sl },
		_nodes{ move(x) },
		_client{ move(c) },
		_timestamps{ timestamps }
	{}

	//Suspend's closure and OnComplete (invoked inside run_iterate) both run on the client's strand, so
	//_requests/_results are strand-confined - no lock needed, and a response can't be processed before its
	//requestId is registered.
	α ReadValueAwait::Suspend()ι->void{
		_client->PostUA( [this]{
			for( auto&& nodeId : _nodes ){
				RequestId requestId{};
				try{
					UA_ReadValueId rvi; UA_ReadValueId_init( &rvi );
					rvi.nodeId = nodeId;
					rvi.attributeId = UA_ATTRIBUTEID_VALUE;
					UAε( UA_Client_readAttribute_async(_client->UAPointer(), &rvi, _timestamps, onResponse, this, &requestId) );
					_requests.emplace( requestId, move(nodeId) );
					_client->Process( requestId, "readValueAttribute" );
				}
				catch( UAException& e ){
					_results.emplace( nodeId, Value{(StatusCode)e.Code()} );
					if( _results.size()==_nodes.size() )
						_client->PostStrand( [this]{ Resume(move(_results)); } );//defer: resuming inline lets the awaiting caller destroy `this`/_nodes while this loop is still iterating them (invalidated-iterator crash on ++).
				}
			}
		});
	}

	α ReadValueAwait::OnComplete( RequestId requestId, StatusCode sc, UA_DataValue* val )ι->void{
		_client->ClearRequest( requestId );
		auto logPrefix = [&](){ return Ƒ("[{}.{}]", hex(_client->Handle()), requestId); };
		auto nodeIdIt = _requests.find( requestId );
		if( nodeIdIt==_requests.end() ){
			CRITICAL( "{}ReadValueAwait::OnComplete - could not find requestId", logPrefix() );
			return;
		}
		Value value = sc || !val ? Value{ sc } : Value{ move(*val) };
		DBG( "{} Value: {}", logPrefix(), serialize(value.ToJson()) );
		_results.emplace( nodeIdIt->second, move(value) );
		if( _results.size()==_nodes.size() )
			_client->PostStrand( [this]{ Resume(move(_results)); } );//defer resume off the current strand handler (which may be a run_iterate driven re-entrantly from Suspend's loop) - see Suspend.
	}
}