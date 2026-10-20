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
				vector<string> names; names.reserve( count );//each node's, once for all its values.
				for( let& node : nodes )
					names.push_back( node.ToString() );
				for( let& v : args.Values ){
					if( !UA_StatusCode_isBad(statuses[v.Slot]) )
						values[v.Slot].push_back( HistQL::ToValue(v, names[v.Slot], types[v.Slot], args.Command(), _sl) );
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
			vector<std::span<const StatusCode>> operations( count );
			for( uint i=0; i<count; ++i ){
				if( !entries[i] )//refused before the request:  ValueTypesAwait's status stands.
					continue;
				let& result = response.Result( *entries[i] );
				statuses[i] = result.statusCode;
				operations[i] = { result.operationResults, result.operationResultsSize };
			}
			Resume( _mutation.ResultRequest ? HistQL::ToJson(*_mutation.ResultRequest, HistQL::NodesJson(nodes), HistQL::EditRows(args, operations, statuses), statuses) : jvalue{} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}