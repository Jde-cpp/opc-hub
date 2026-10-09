#include "HistEditQLAwait.h"
#include "../UAClient.h"
#include "../async/ValueTypesAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	//A value that can't take its node's type refuses the call, naming its node and value (spec *Pass-through*).
	Ω dataValue( const HistQL::EditValue& e, const NodeId& node, const UA_DataType* type, sv command, SL sl )ε->Value{
		let typed = [&]()ε->Value{
			if( e.Data.is_null() )
				return Value{ e.Status.value_or(UA_STATUSCODE_GOOD) };
			try{
				return Value{ e.Data, type, sl };
			}
			catch( const std::exception& x ){
				if( type )
					throw Exception{ sl, {}, "{}:  {}'s value {} isn't a {}:  {}", command, node.ToString(), serialize(e.Data), type->typeName, x.what() };
				throw Exception{ sl, {}, "{}:  {}'s value {} implies no type:  {}", command, node.ToString(), serialize(e.Data), x.what() };
			}
		};
		Value y = typed();
		y.hasStatus = e.Status.has_value();
		y.status = e.Status.value_or( UA_STATUSCODE_GOOD );
		if( e.Source ){
			y.sourceTimestamp = *e.Source;
			y.hasSourceTimestamp = true;
		}
		if( e.Server ){
			y.serverTimestamp = *e.Server;
			y.hasServerTimestamp = true;
		}
		return y;
	}
	Ω performType( HistQL::EEdit edit )ι->UA_PerformUpdateType{
		using enum HistQL::EEdit;
		return edit==Insert ? UA_PERFORMUPDATETYPE_INSERT : edit==Replace ? UA_PERFORMUPDATETYPE_REPLACE : UA_PERFORMUPDATETYPE_UPDATE;
	}

	α HistEditQLAwait::Execute()ι->TAwait<HistoryUpdateResponse>::Task{
		try{
			using enum HistQL::EEdit;
			const HistQL::EditArgs args{ _edit, _mutation, _sl };
			let count = args.Nodes.size();
			vector<StatusCode> statuses( count, UA_STATUSCODE_GOOD );
			HistoryUpdateRequest request;
			vector<optional<uint>> entries( count );//each node's entry in the request, none for one refused before it.
			if( _edit<Purge ){
				auto [types, typeStatuses] = co_await ValueTypesAwait{ args.Nodes, _client, _sl };//a node it refuses isn't sent.
				statuses = move( typeStatuses );
				vector<vector<Value>> values( count );//each node's, in the order named.
				for( let& v : args.Values ){
					if( !UA_StatusCode_isBad(statuses[v.Slot]) )
						values[v.Slot].push_back( dataValue(v, args.Nodes[v.Slot], types[v.Slot], args.Command(), _sl) );
				}
				for( uint i=0; i<count; ++i ){
					if( UA_StatusCode_isBad(statuses[i]) )
						continue;
					entries[i] = request.Size();
					request.Update( args.Nodes[i], performType(_edit), move(values[i]) );
				}
			}
			else{
				for( uint i=0; i<count; ++i ){
					entries[i] = request.Size();
					if( args.Times.empty() )
						request.DeleteRaw( args.Nodes[i], args.Start, args.End );
					else
						request.DeleteAtTime( args.Nodes[i], args.Times );
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
			Resume( _mutation.ResultRequest ? HistQL::ToJson(*_mutation.ResultRequest, args.Nodes, results, statuses) : jvalue{} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}