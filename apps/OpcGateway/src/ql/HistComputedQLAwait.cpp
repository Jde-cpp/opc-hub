#include "HistComputedQLAwait.h"
#include <jde/fwk/str.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include "../AggregateFunctions.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α HistComputedQLAwait::Execute()ι->TAwait<HistoryReadResponse>::Task{
		jvalue y;
		std::exception_ptr failed;
		try{
			_args.emplace( _query, _sl );
			_nodes.resize( _args->Nodes.size() );
			for( uint i=0; i<_nodes.size(); ++i )
				_nodes[i].Had = _args->Continuation && _args->Continuation->counts( i )!=0;
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
			_from = _stop = First();
			Extend( _args->Limit, (uint)std::ranges::count_if(_nodes, &Node::Had) );//every node is Caught at the call's first.
			for( bool fresh=true;; ){
				auto response = co_await HistoryReadAwait{ Request(fresh, false), _client, _sl };
				for( uint i=0; i<_asked.size(); ++i )
					Take( _asked[i], response.Result(i) );
				let walked = Walk( nullptr );
				if( walked.Values>=_args->Limit || !walked.At )
					break;
				fresh = !std::ranges::any_of( _nodes, &Node::More );
				if( fresh && !Extend(_args->Limit-walked.Values) )
					break;
			}
			vector<HistQL::ReadValue> page;
			let walked = Walk( &page );
			let continuation = Continuation( walked.At );
			vector<StatusCode> statuses; statuses.reserve( _nodes.size() );
			for( let& node : _nodes )
				statuses.push_back( node.Status );
			y = HistQL::ToJson( _query, _args->Nodes, move(page), continuation, statuses );
		}
		catch( runtime_error& ){//a round that fails leaves the points the rounds before took, which the release below frees.
			failed = std::current_exception();
		}
		if( std::ranges::any_of(_nodes, &Node::More) ){
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

	α HistComputedQLAwait::Request( bool fresh, bool release )ι->HistoryReadRequest{
		let from = _from, stop = _stop;
		let back = Back();
		auto request = [&]{
			if( _args->Mode==HistQL::EMode::AtTime )
				return HistoryReadRequest{ vector<UA_DateTime>{_args->Times.begin()+from, _args->Times.begin()+stop}, true, UA_TIMESTAMPSTORETURN_BOTH, release };
			//From the round's first interval's start to its stop's:  the intervals between are the read's own, the last
			//uneven one included while another comes before it in the request (Back), since only the read's end makes one.
			let at = [&]( uint64_t point ){
				let offset = (UA_DateTime)( point*_args->Width() );
				return _args->Reverse() ? *_args->Start-offset : *_args->Start+offset;
			};
			let end = stop==_args->Count() ? *_args->End : at( stop );
			return HistoryReadRequest{ at(from-(back ? 1 : 0)), end, _args->Interval, *_aggregate, UA_TIMESTAMPSTORETURN_BOTH, release };
		}();
		//Among the nodes the server has more for, the ones holding the merge back:  those with the fewest values.
		optional<uint> fewest;
		for( let& node : _nodes ){
			if( node.More && (!fewest || node.Values.size()<*fewest) )
				fewest = node.Values.size();
		}
		_asked.clear();
		for( uint i=0; i<_nodes.size(); ++i ){
			auto& node = _nodes[i];
			if( fresh ? Caught(node) : node.More && (release || node.Values.size()==*fewest) ){
				request.Add( _args->Nodes[i], node.Point );
				_asked.push_back( i );
				if( fresh )
					node.Skip = back;
			}
		}
		return request;
	}

	α HistComputedQLAwait::Take( uint slot, UA_HistoryReadResult& result )ι->void{
		auto& node = _nodes[slot];
		node.Status = result.statusCode;
		node.Point.assign( (const char*)result.continuationPoint.data, result.continuationPoint.length );
		node.More = node.Point.size() && !UA_StatusCode_isBad( result.statusCode );
		if( UA_StatusCode_isBad(result.statusCode) ){//the node's answer, as the server gave it.
			DBG( "[{}]history {} answered {}.", hex(_client->Handle()), _args->Nodes[slot].ToString(), UAException::Message(result.statusCode) );
			return;
		}
		let decoded = result.historyData.encoding==UA_EXTENSIONOBJECT_DECODED || result.historyData.encoding==UA_EXTENSIONOBJECT_DECODED_NODELETE;
		if( !decoded || result.historyData.content.decoded.type!=&UA_TYPES[UA_TYPES_HISTORYDATA] )
			return;
		auto& history = *static_cast<UA_HistoryData*>( result.historyData.content.decoded.data );
		for( uint i=0; i<history.dataValuesSize; ++i ){
			if( std::exchange(node.Skip, false) )
				continue;
			node.Values.push_back( {slot, Value{move(history.dataValues[i])}} );
		}
	}

	α HistComputedQLAwait::Back()Ι->bool{
		return _args->Mode==HistQL::EMode::Processed && _from && _from+1==_args->Count() && _args->Uneven();
	}
	α HistComputedQLAwait::Extend( uint missing, uint had )ι->bool{
		_from = _stop;
		let nodes = (uint)std::ranges::count_if( _nodes, [this]( let& node ){ return Caught(node); } );
		if( !nodes )
			return false;
		//⌈(missing+had)/nodes⌉, split so an unlimited readLimit doesn't overflow.
		let points = missing/nodes+( missing%nodes+had+nodes-1 )/nodes;
		_stop = _from+std::min<uint64_t>( points, _args->Count()-_from );
		return true;
	}

	α HistComputedQLAwait::Walk( vector<HistQL::ReadValue>* page )ι->Walked{
		Walked y;
		let from = First(), stop = _stop;
		uint64_t last{};//the points after the values, where a node with more cuts.
		for( let& node : _nodes )
			last = std::max<uint64_t>( last, node.Values.size() );
		for( uint64_t p=0; p<=last && from+p<stop; ++p ){
			vector<bool> done( _nodes.size() );
			for( uint i=0; i<_nodes.size(); ++i ){
				auto& node = _nodes[i];
				if( p==0 && node.Had ){
					done[i] = true;
					continue;
				}
				if( p<node.Values.size() ){
					if( y.Values==_args->Limit ){
						y.At = Cut{ from+p, move(done) };
						return y;
					}
					if( page )
						page->push_back( move(node.Values[p]) );
					++y.Values;
					done[i] = true;
				}
				else if( node.More ){//the point waits for it.
					y.At = Cut{ from+p, move(done) };
					return y;
				}
			}
		}
		//The request answered, with points past it:  the next page's, unless the server refused every node.
		if( stop<_args->Count() && std::ranges::any_of(_nodes, []( let& node ){ return !UA_StatusCode_isBad(node.Status); }) )
			y.At = Cut{ stop, vector<bool>(_nodes.size()) };
		return y;
	}

	α HistComputedQLAwait::Continuation( const optional<Cut>& cut )Ι->string{
		if( !cut )
			return {};
		Hist::Proto::Continuation next;
		next.set_crc( _args->Crc() );
		next.set_next( cut->Point );
		for( let done : cut->Done )
			next.add_counts( done ? 1 : 0 );
		return HistQL::Encode( next );
	}
}