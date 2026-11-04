#include "MethodArguments.h"
#include <jde/ql/types/TableQL.h>
#include <jde/opc/uatypes/UAString.h>
#include "UAClient.h"
#include "async/ReadAwait.h"
#include "async/ReadValueAwait.h"
#include "async/ValueTypesAwait.h"
#include "uatypes/Browse.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α Argument::ToJson( const QL::TableQL& q )Ι->jobject{
		jobject y;
		if( q.FindColumn("name") )
			y["name"] = Opc::ToString( name );
		if( q.FindColumn("dataType") )
			y["dataType"] = Opc::ToJson( dataType );
		if( q.FindColumn("valueRank") )
			y["valueRank"] = valueRank;
		if( q.FindColumn("arrayDimensions") ){
			jarray dimensions;
			for( size_t i=0; i<arrayDimensionsSize; ++i )
				dimensions.push_back( arrayDimensions[i] );
			y["arrayDimensions"] = move( dimensions );
		}
		if( q.FindColumn("description") )
			y["description"] = Opc::ToString( description.text );
		return y;
	}
	α Argument::BuiltIn()Ι->const UA_DataType*{ return ValueTypes::BuiltIn( &dataType ); }

	α MethodArguments::ToJson( const QL::TableQL& q )Ι->jobject{
		jobject y;
		if( q.FindColumn("name") )
			y["name"] = Name;
		let rows = []( const vector<Argument>& arguments, const QL::TableQL& ql ){
			jarray y; y.reserve( arguments.size() );
			for( let& a : arguments )
				y.push_back( a.ToJson(ql) );
			return y;
		};
		if( auto inputsQL = q.FindTable("inputArguments"); inputsQL )
			y["inputArguments"] = rows( Inputs, *inputsQL );
		if( auto outputsQL = q.FindTable("outputArguments"); outputsQL )
			y["outputArguments"] = rows( Outputs, *outputsQL );
		return y;
	}

	α MethodArgumentCache::GetAwait::await_ready()ι->bool{
		if( auto p = _cache.Find(_id); p ){
			_result = move( p );
			return true;
		}
		return false;
	}
	α MethodArgumentCache::GetAwait::Suspend()ι->void{
		_cache.StartLocked( move(_client), move(_id), this );
	}

	α MethodArgumentCache::Find( const NodeId& id )Ι->Ptr{
		rl _{ _mutex };
		auto p = _slots.find( id );
		return p!=_slots.end() && p->second.State()==Slot::EState::Ready ? p->second.Value() : Ptr{};
	}

	α MethodArgumentCache::StartLocked( sp<UAClient>&& client, NodeId&& id, GetAwait* waiter )ι->void{
		bool launch{};
		{
			ul _{ _mutex };
			launch = _slots.try_emplace( NodeId{id} ).first->second.Start( waiter );//a new slot:  own the fetch.
		}
		if( launch )
			Fetch( move(client), move(id) );//fire & forget:  the task keeps the client (and so this cache) alive until Finish.
	}

	α MethodArgumentCache::Finish( const NodeId& id, Ptr arguments, up<Exception> error, bool retryable )ι->void{
		Slot::Resumes resumes;
		{
			ul _{ _mutex };
			auto slot = _slots.find( id );
			ASSERT( slot!=_slots.end() );
			resumes = slot->second.Finish( move(arguments), move(error), retryable );
			if( slot->second.State()==Slot::EState::Empty )
				_slots.erase( slot );//a transport/session failure or an unknown id - the next request retries from scratch, and a mistyped id does not poison the connection.
		}
		resumes.Run();
	}

	Ω browseIs( const UA_ReferenceDescription& ref, sv name )ι->bool{ return ref.browseName.namespaceIndex==0 && ToSV(ref.browseName.name)==name; }
	//The property's value as the Argument array it is - open62541 unwraps the ExtensionObjects since the type is in UA_TYPES - or
	//none for a method without the property.
	Ω arguments( const flat_map<NodeId,Value>& values, const optional<NodeId>& property, sv propertyName, const NodeId& method, const sp<UAClient>& client )ε->vector<Argument>{
		vector<Argument> y;
		if( !property )
			return y;
		auto v = values.find( *property ); THROW_IF( v==values.end(), "No value returned for the {} property of '{}'.", propertyName, method.ToString() );
		if( !v->second.hasValue )
			throw UAClientException{ (StatusCode)v->second.status, client->Handle(), Ƒ("read {} of '{}'", propertyName, method.ToString()) };
		let& variant = v->second.value;
		THROW_IF( variant.type!=&UA_TYPES[UA_TYPES_ARGUMENT], "{} of '{}' is a '{}', not an Argument array.", propertyName, method.ToString(), variant.type ? variant.type->typeName : "null" );
		let count = UA_Variant_isScalar( &variant ) ? 1 : variant.arrayLength;
		y.reserve( count );
		for( size_t i=0; i<count; ++i )
			y.emplace_back( ((const UA_Argument*)variant.data)[i] );
		return y;
	}

	//The node's class and name, then its HasProperty references for the two argument properties, then their values:  three round
	//trips, once per method per connection.  One coroutine with Any-wrapped awaits rather than a hand-off chain.  The awaitables
	//are lvalues:  Any() stores an rvalue by value and FoldersAwait is noncopyable.
	α MethodArgumentCache::Fetch( sp<UAClient> client, NodeId id )ι->VoidTask{
		sp<MethodArguments> arguments; up<Exception> error; bool retryable{};
		try{
			ReadAwait read{ ReadRequest{id, {UA_ATTRIBUTEID_NODECLASS, UA_ATTRIBUTEID_DISPLAYNAME}}, client };
			auto attributes = co_await Any( read );
			THROW_IF( attributes.resultsSize!=2, "Reading '{}' returned {} results for 2 attributes.", id.ToString(), attributes.resultsSize );
			const UA_DataValue &nodeClass = attributes.results[0], &displayName = attributes.results[1];
			if( !nodeClass.hasValue )//BadNodeIdUnknown, a dead session:  the caller's problem, not the cache's.
				throw UAClientException{ (StatusCode)nodeClass.status, client->Handle(), Ƒ("read nodeClass of '{}'", id.ToString()) };
			THROW_IF( *(UA_NodeClass*)nodeClass.value.data!=UA_NODECLASS_METHOD, "'{}' is not a Method node (class {}).", id.ToString(), (uint)*(UA_NodeClass*)nodeClass.value.data );
			arguments = ms<MethodArguments>();
			arguments->Id = id;
			if( displayName.hasValue && displayName.value.type==&UA_TYPES[UA_TYPES_LOCALIZEDTEXT] )
				arguments->Name = Opc::ToString( ((UA_LocalizedText*)displayName.value.data)->text );
			Browse::FoldersAwait browse{ Browse::Request::Properties(NodeId{id}), client };
			auto refs = co_await Any( browse );
			optional<NodeId> inputs, outputs;
			if( refs.resultsSize )
				refs.VisitWhile( 0, [&]( const UA_ReferenceDescription& ref ){
					if( browseIs(ref, "InputArguments") )
						inputs = NodeId{ ref.nodeId.nodeId };
					else if( browseIs(ref, "OutputArguments") )
						outputs = NodeId{ ref.nodeId.nodeId };
					return true;
				} );
			flat_set<NodeId> properties;
			for( let& p : {inputs, outputs} ){
				if( p )
					properties.insert( *p );
			}
			if( properties.size() ){
				ReadValueAwait readProperties{ move(properties), client };
				auto values = co_await Any( readProperties );
				arguments->Inputs = Gateway::arguments( values, inputs, "InputArguments", id, client );
				arguments->Outputs = Gateway::arguments( values, outputs, "OutputArguments", id, client );
			}
			INFO( "[{}]Method '{}' ({}) - {} inputs, {} outputs.", hex(client->Handle()), arguments->Name, id.ToString(), arguments->Inputs.size(), arguments->Outputs.size() );
		}
		catch( UAClientException& e ){//session/transport, or an id the server does not have:  do not pin the failure on the cache.
			retryable = true;
			error = e.Move();
		}
		catch( runtime_error& e ){
			error = Exception::From( move(e) );
		}
		Finish( id, move(arguments), move(error), retryable );
	}
}