#include "HistEventsQLAwait.h"
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/Variant.h>
#include "HistQL.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	constexpr uint TimeField{ 4 };//Time's place among the base fields (EventFilter::Base).

	HistEventsQLAwait::Args::Args( const QL::Input& input, SL sl )ε{
		let refuse = [&]( string what ){ return Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "{}:  {}", Command, move(what) }; };
		let node = input.FindPtr<jvalue>( "node" );
		if( !node || node->is_null() )
			throw refuse( "names no node." );
		Node = node->is_string() ? NodeId::DecodeJson( string{node->get_string()} ) : NodeId{ *node };
		let time = [&]( sv name )->optional<UA_DateTime>{
			let p = input.FindPtr<jvalue>( name );
			return p && !p->is_null() ? optional<UA_DateTime>{ UADateTime{*p, sl}.UA() } : nullopt;
		};
		let start = time( "start" ), end = time( "end" );
		if( !start )
			throw refuse( "needs a start." );
		Start = *start;
		End = end.value_or( 0 );//0:  the server's now, as Part 11 reads an open end.
		if( let c = input.FindPtr<jstring>("continuation"); c && c->size() ){//the last page's last Time, to read on from.
			let tick = Str::TryTo<UA_DateTime>( *c );
			if( !tick )
				throw UAException{ UA_STATUSCODE_BADCONTINUATIONPOINTINVALID, "The continuation isn't one of the gateway's.", {ELogLevel::Debug}, sl };
			Start = *tick;
		}
		vector<string> select;
		if( let p = input.FindPtr<jvalue>("select"); p && !p->is_null() ){
			if( p->is_string() )
				select.emplace_back( p->get_string() );
			else if( p->is_array() ){
				for( let& j : p->get_array() ){
					if( !j.is_string() )
						throw refuse( Ƒ("select names a field that is no text - {}.", serialize(j)) );
					select.emplace_back( j.get_string() );
				}
			}
			else
				throw refuse( "select is a browse path, or a list of them." );
		}
		Filter = EventFilter::Of( select, sl );
		let readLimit = HistQL::ReadLimit();
		let limit = input.TryNumber<uint>( "limit" ).value_or( 0 );
		Limit = (uint32)std::min<uint>( limit && limit<readLimit ? limit : readLimit, std::numeric_limits<uint32>::max() );
	}

	α HistEventsQLAwait::Execute()ι->TAwait<HistoryReadResponse>::Task{
		jvalue y;
		std::exception_ptr failed;
		string point;//a continuation point the server holds, released before the answer.
		optional<NodeId> node;
		try{
			const Args args{ _query, _sl };
			node = args.Node;
			HistoryReadRequest request{ args.Start, args.End, args.Limit, args.Filter };
			request.Add( args.Node );
			auto response = co_await HistoryReadAwait{ move(request), _client, _sl };
			auto& result = response.Result( 0 );
			point.assign( (const char*)result.continuationPoint.data, result.continuationPoint.length );
			jobject answer;
			let status = result.statusCode;
			if( UA_StatusCode_isBad(status) )
				DBG( "[{}]historyEvents {} answered {}.", hex(_client->Handle()), args.Node.ToString(), UAException::Message(status) );
			if( _query.FindColumn("status") )
				answer["status"] = status;
			let events = Read( args, result );
			if( _query.FindTable("events") )
				answer["events"] = events;
			if( _query.FindColumn("continuation") )
				answer["continuation"] = point.size() && _lastTime ? jvalue{ std::to_string(*_lastTime) } : jvalue{};
			y = _query.TransformResult( move(answer) );
		}
		catch( runtime_error& ){
			failed = std::current_exception();
		}
		if( point.size() && node ){
			try{
				HistoryReadRequest release{ 0, 0, 0, EventFilter{}, true };
				release.Add( *node, point );
				co_await HistoryReadAwait{ move(release), _client, _sl };
			}
			catch( const std::exception& e ){//the server's to drop with the session, then.
				DBG( "[{}]Releasing the event continuation point failed:  {}", hex(_client->Handle()), e.what() );
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

	α HistEventsQLAwait::Read( const Args& args, const UA_HistoryReadResult& result )ι->jarray{
		jarray y;
		let& data = result.historyData;
		let decoded = data.encoding==UA_EXTENSIONOBJECT_DECODED || data.encoding==UA_EXTENSIONOBJECT_DECODED_NODELETE;
		if( !decoded || data.content.decoded.type!=&UA_TYPES[UA_TYPES_HISTORYEVENT] )
			return y;
		let& history = *static_cast<const UA_HistoryEvent*>( data.content.decoded.data );
		let types = _client->Types().get();
		let count = args.Filter.Select.size();
		for( let& event : std::span{history.events, history.eventsSize} ){
			jarray fields;
			for( uint i=0; i<count; ++i ){
				const UA_Variant* field = i<event.eventFieldsSize ? &event.eventFields[i] : nullptr;
				if( !field || UA_Variant_isEmpty(field) ){
					fields.emplace_back( nullptr );
					continue;
				}
				if( i==TimeField && UA_Variant_hasScalarType(field, &UA_TYPES[UA_TYPES_DATETIME]) )
					_lastTime = *static_cast<const UA_DateTime*>( field->data );
				try{
					fields.push_back( Variant{*field}.ToJson(types) );
				}
				catch( const std::exception& e ){//a field no json carries, a DiagnosticInfo say.
					DBG( "[{}]historyEvents {}:  field {} cannot be written - {}", hex(_client->Handle()), args.Node.ToString(), args.Filter.Select[i].Key, e.what() );
					fields.emplace_back( nullptr );
				}
			}
			y.push_back( jobject{ {"fields", move(fields)} } );
		}
		return y;
	}
}