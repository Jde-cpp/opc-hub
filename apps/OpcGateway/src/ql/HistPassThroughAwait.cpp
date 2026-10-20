#include "HistPassThroughAwait.h"
#include <jde/opc/UAException.h>
#include "../AggregateFunctions.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α HistPassThroughAwait::Execute()ι->TAwait<HistoryReadResponse>::Task{
		jvalue y;
		std::exception_ptr failed;
		try{
			_args.emplace( _query, _sl );
			if( _args->Mode==HistQL::EMode::Processed ){
				_aggregate = AggregateFunctions::Part13( _args->Aggregate );
				if( !_aggregate ){//AnyAwait - legal from this TAwait<HistoryReadResponse>::Task.
					auto folder = co_await _client->Aggregates().Get( _client, _sl );
					let p = folder->find( _args->Aggregate );
					if( p==folder->end() )
						throw UAException{ UA_STATUSCODE_BADAGGREGATENOTSUPPORTED, Ƒ("The server lists no aggregate '{}'.", _args->Aggregate), {ELogLevel::Debug}, _sl };
					_aggregate = p->second;
				}
			}
			Begin();
			for( bool first=true;; ){
				auto response = co_await HistoryReadAwait{ Request(first, false), _client, _sl };
				for( uint i=0; i<_asked.size(); ++i )
					Take( _asked[i], response.Result(i) );
				let next = Next();
				if( !next )
					break;
				first = *next;
			}
			auto [page, continuation] = Answer();
			vector<StatusCode> statuses; statuses.reserve( NodeCount() );
			for( uint i=0; i<NodeCount(); ++i )
				statuses.push_back( State(i).Status );
			y = HistQL::ToJson( _query, _args->Nodes, move(page), continuation, statuses );
		}
		catch( runtime_error& ){//a round that fails leaves the points the rounds before took, which the release below frees.
			failed = std::current_exception();
		}
		if( AnyMore() ){
			try{
				co_await HistoryReadAwait{ Request(false, true), _client, _sl };
			}
			catch( const std::exception& e ){//the server's to drop with the session, then.
				DBG( "[{}]Releasing the history continuation points failed:  {}", hex(_client->Handle()), e.what() );
			}
		}
		if( failed ){
			try{
				std::rethrow_exception( failed );
			}
			catch( runtime_error& e ){
				ResumeExp( move(e) );
			}
		}
		else
			Resume( move(y) );
	}

	α HistPassThroughAwait::AnyMore()ι->bool{
		for( uint i=0; i<NodeCount(); ++i ){
			if( State(i).More )
				return true;
		}
		return false;
	}

	α HistPassThroughAwait::TakeHeader( uint slot, UA_HistoryReadResult& result, bool status )ι->bool{
		auto& node = State( slot );
		if( status )
			node.Status = result.statusCode;
		node.Point.assign( (const char*)result.continuationPoint.data, result.continuationPoint.length );
		node.More = node.Point.size() && !UA_StatusCode_isBad( result.statusCode );
		if( UA_StatusCode_isBad(result.statusCode) ){
			DBG( "[{}]history {} answered {}.", hex(_client->Handle()), _args->Nodes[slot].ToString(), UAException::Message(result.statusCode) );
			return false;
		}
		return true;
	}
}