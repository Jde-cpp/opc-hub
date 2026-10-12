#include "HistEditQLAwait.h"
#include "../UAClient.h"
#include "../async/ValueTypesAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α HistEditQLAwait::Execute()ι->TAwait<HistoryUpdateResponse>::Task{
		try{
			using enum HistQL::EEdit;
			const HistQL::EditArgs args{ _edit, _mutation, _sl };
			if( args.Target!=HistQL::ETarget::Opc )//GatewayQLMAwait takes no group's.
				throw HistQL::TargetRefused( _edit, _sl );
			let nodes = args.NodeIds();
			let count = nodes.size();
			vector<StatusCode> statuses( count, UA_STATUSCODE_GOOD );
			HistoryUpdateRequest request;
			vector<optional<uint>> entries( count );//each node's entry in the request, none for one refused before it.
			if( _edit<Purge ){
				auto typed = co_await ValueTypesAwait{ nodes, _client, _sl };//a node it refuses isn't sent.
				statuses = move( typed.Statuses );
				let& types = typed.Types;
				vector<vector<Value>> values( count );//each node's, in the order named.
				for( let& v : args.Values ){
					if( !UA_StatusCode_isBad(statuses[v.Slot]) )
						values[v.Slot].push_back( HistQL::ToValue(v, nodes[v.Slot].ToString(), types[v.Slot], args.Command(), _sl) );
				}
				for( uint i=0; i<count; ++i ){
					if( UA_StatusCode_isBad(statuses[i]) )
						continue;
					entries[i] = request.Size();
					request.Update( nodes[i], HistQL::PerformType(_edit), move(values[i]) );
				}
			}
			else{
				for( uint i=0; i<count; ++i ){
					entries[i] = request.Size();
					if( args.Times.empty() )
						request.DeleteRaw( nodes[i], args.Start, args.End );
					else
						request.DeleteAtTime( nodes[i], args.Times );
				}
			}
			let response = co_await HistoryUpdateAwait{ move(request), _client, _sl };
			//A node's k-th value or time takes the server's operation result for it, or the node's status where the entry
			//carries none, refused whole, or wasn't sent.
			vector<uint> expected( count, args.Times.size() );
			for( let& v : args.Values )
				++expected[v.Slot];
			vector<const UA_StatusCode*> operations( count );
			for( uint i=0; i<count; ++i ){
				if( !entries[i] )
					continue;
				let& result = response.Result( *entries[i] );
				statuses[i] = result.statusCode;
				if( expected[i] && result.operationResultsSize==expected[i] )
					operations[i] = result.operationResults;
			}
			vector<HistQL::EditResult> results;
			let add = [&]( uint slot, uint k, optional<UA_DateTime> time ){ results.push_back( {slot, time, operations[slot] ? operations[slot][k] : statuses[slot]} ); };
			if( _edit<Purge ){
				vector<uint> at( count );
				for( let& v : args.Values )
					add( v.Slot, at[v.Slot]++, v.Source );
			}
			else if( args.Times.size() ){
				for( uint i=0; i<count; ++i ){
					for( uint k=0; k<args.Times.size(); ++k )
						add( i, k, args.Times[k] );
				}
			}
			Resume( _mutation.ResultRequest ? HistQL::ToJson(*_mutation.ResultRequest, HistQL::NodesJson(nodes), results, statuses) : jvalue{} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}