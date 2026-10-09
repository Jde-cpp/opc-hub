#include "HistEditQLAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include "../UAClient.h"
#include "../async/ReadAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	Ω bad( const UA_DataValue& v )ι->bool{ return v.hasStatus && UA_StatusCode_isBad( v.status ); }
	//A built-in type for a DataType node of namespace 0, as updateVariable finds it;  null for any other, or an abstract one.
	Ω builtIn( const UA_DataValue& dataType )ι->const UA_DataType*{
		if( !dataType.hasValue || dataType.value.type!=&UA_TYPES[UA_TYPES_NODEID] )
			return nullptr;
		const NodeId id{ *(UA_NodeId*)dataType.value.data };
		return id.namespaceIndex ? nullptr : FindDataType( id );
	}
	Ω dataValue( const HistQL::EditValue& e, const UA_DataType* type, SL sl )ε->Value{
		Value y = e.Data.is_null() ? Value{ e.Status.value_or(UA_STATUSCODE_GOOD) } : Value{ e.Data, type, sl };
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
			if( _edit<Delete ){
				vector<const UA_DataType*> types( count );
				vector<NodeId> untyped; vector<uint> untypedSlots;
				let dataTypes = co_await Any( ReadAwait{ReadRequest{args.Nodes, UA_ATTRIBUTEID_DATATYPE}, _client, false, _sl} );
				for( uint i=0; i<count; ++i ){
					if( i>=dataTypes.resultsSize )
						statuses[i] = UA_STATUSCODE_BADUNEXPECTEDERROR;
					else if( bad(dataTypes.results[i]) )
						statuses[i] = dataTypes.results[i].status;
					else if( !(types[i] = builtIn(dataTypes.results[i])) ){//the type the node's value has.
						untyped.push_back( args.Nodes[i] );
						untypedSlots.push_back( i );
					}
				}
				if( untyped.size() ){
					let values = co_await Any( ReadAwait{ReadRequest{untyped, UA_ATTRIBUTEID_VALUE}, _client, false, _sl} );
					for( uint k=0; k<untyped.size(); ++k ){
						let slot = untypedSlots[k];
						if( k>=values.resultsSize )
							statuses[slot] = UA_STATUSCODE_BADUNEXPECTEDERROR;
						else if( bad(values.results[k]) )
							statuses[slot] = values.results[k].status;
						else if( !(types[slot] = values.results[k].hasValue ? values.results[k].value.type : nullptr) )
							statuses[slot] = UA_STATUSCODE_BADDATATYPEIDUNKNOWN;
					}
				}
				for( uint i=0; i<count; ++i ){
					if( !types[i] )
						continue;
					vector<Value> values;
					for( let& v : args.Values ){
						if( v.Slot==i )
							values.push_back( dataValue(v, types[i], _sl) );
					}
					entries[i] = request.Size();
					request.Update( args.Nodes[i], performType(_edit), move(values) );
				}
			}
			else{
				for( uint i=0; i<count; ++i ){
					entries[i] = request.Size();
					if( _edit==Delete )
						request.DeleteRaw( args.Nodes[i], args.Start, args.End );
					else
						request.DeleteAtTime( args.Nodes[i], args.Times );
				}
			}
			let response = co_await HistoryUpdateAwait{ move(request), _client, _sl };
			//A node's k-th value or time takes the server's operation result for it, or the node's status where the entry
			//carries none, refused whole, or wasn't sent.
			vector<uint> expected( count, _edit==DeleteAtTime ? args.Times.size() : 0 );
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
			if( _edit<Delete ){
				vector<uint> at( count );
				for( let& v : args.Values )
					add( v.Slot, at[v.Slot]++, v.Source );
			}
			else if( _edit==DeleteAtTime ){
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