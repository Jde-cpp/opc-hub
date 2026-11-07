#include "MethodArguments.h"
#include <jde/fwk/co/AllAwait.h>
#include <jde/ql/types/TableQL.h>
#include <jde/opc/uatypes/UAString.h>
#include "UAClient.h"
#include "async/ValueTypesAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α Argument::ToJson( const QL::TableQL& q )Ι->jobject{
		jobject y;
		if( q.FindColumn("name") )
			y["name"] = Opc::ToString( name );
		if( q.FindColumn("dataType") )
			y["dataType"] = Opc::ToJson( dataType );
		if( q.FindColumn("baseDataType") )
			y["baseDataType"] = UA_NodeId_isNull( &Base ) ? jvalue{} : Opc::ToJson( Base );
		if( q.FindColumn("valueRank") )
			y["valueRank"] = valueRank;
		if( q.FindColumn("arrayDimensions") ){
			jarray dimensions;
			for( uint i=0; i<arrayDimensionsSize; ++i )
				dimensions.push_back( arrayDimensions[i] );
			y["arrayDimensions"] = move( dimensions );
		}
		if( q.FindColumn("description") )
			y["description"] = Opc::ToString( description.text );
		return y;
	}
	α Argument::BuiltIn()Ι->const UA_DataType*{
		if( Base.namespaceIndex==0 && Base.identifierType==UA_NODEIDTYPE_NUMERIC ){
			if( Base.identifier.numeric==UA_NS0ID_ENUMERATION )//abstract, so not in UA_TYPES:  an enumeration of the server's own, encoded as Int32
				return &UA_TYPES[UA_TYPES_INT32];
			if( Base.identifier.numeric==UA_NS0ID_UNION )//a structure, as Structure (i=22) is ExtensionObject's id
				return &UA_TYPES[UA_TYPES_EXTENSIONOBJECT];
		}
		return ValueTypes::BuiltIn( &Base );
	}

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

	//The property's value, results[at] of the read, as the Argument array it is - open62541 unwraps the ExtensionObjects since the
	//type is in UA_TYPES - or none for a method without the property.
	Ω arguments( const ReadResponse& values, optional<uint> at, sv propertyName, const NodeId& method, const sp<UAClient>& client )ε->vector<Argument>{
		vector<Argument> y;
		if( !at )
			return y;
		THROW_IF( *at>=values.resultsSize, "No value returned for the {} property of '{}'.", propertyName, method.ToString() );
		let& value = values.results[*at];
		if( !value.hasValue )
			throw UAClientException{ (StatusCode)value.status, client->Handle(), Ƒ("read {} of '{}'", propertyName, method.ToString()) };
		let& variant = value.value;
		THROW_IF( variant.type!=&UA_TYPES[UA_TYPES_ARGUMENT], "{} of '{}' is a '{}', not an Argument array.", propertyName, method.ToString(), variant.type ? variant.type->typeName : "null" );
		let count = UA_Variant_isScalar( &variant ) ? 1 : variant.arrayLength;
		y.reserve( count );
		for( uint i=0; i<count; ++i )
			y.emplace_back( ((const UA_Argument*)variant.data)[i] );
		return y;
	}

	//The node's class and name with its HasProperty references, at once, then both argument properties' values in one read:  two
	//round trips, once per method per connection.  The awaitables are lvalues:  All() and Any() store an rvalue by value, and
	//FoldersAwait is noncopyable.
	α MethodArgumentCache::Fetch( sp<UAClient> client, NodeId id )ι->VoidTask{
		sp<MethodArguments> arguments; up<Exception> error; bool retryable{};
		try{
			ReadAwait read{ ReadRequest{id, {UA_ATTRIBUTEID_NODECLASS, UA_ATTRIBUTEID_DISPLAYNAME}}, client };
			Browse::FoldersAwait browse{ Browse::Request::Properties(NodeId{id}), client };
			auto [attributes, refs] = co_await All( read, browse );
			THROW_IF( attributes.resultsSize!=2, "Reading '{}' returned {} results for 2 attributes.", id.ToString(), attributes.resultsSize );
			const UA_DataValue &nodeClass = attributes.results[0], &displayName = attributes.results[1];
			if( !nodeClass.hasValue )//BadNodeIdUnknown, a dead session:  the caller's problem, not the cache's.
				throw UAClientException{ (StatusCode)nodeClass.status, client->Handle(), Ƒ("read nodeClass of '{}'", id.ToString()) };
			THROW_IF( *(UA_NodeClass*)nodeClass.value.data!=UA_NODECLASS_METHOD, "'{}' is not a Method node (class {}).", id.ToString(), (uint)*(UA_NodeClass*)nodeClass.value.data );
			arguments = ms<MethodArguments>();
			arguments->Id = id;
			if( displayName.hasValue && displayName.value.type==&UA_TYPES[UA_TYPES_LOCALIZEDTEXT] )
				arguments->Name = Opc::ToString( ((UA_LocalizedText*)displayName.value.data)->text );
			vector<NodeId> properties;
			optional<uint> inputs, outputs;//each property's index in `properties`
			if( refs.resultsSize )
				refs.VisitWhile( 0, [&]( const UA_ReferenceDescription& ref ){
					if( let input = Browse::IsNs0Name(ref, "InputArguments"); input || Browse::IsNs0Name(ref, "OutputArguments") ){
						(input ? inputs : outputs) = properties.size();
						properties.emplace_back( ref.nodeId.nodeId );
					}
					return true;
				} );
			if( properties.size() ){
				ReadAwait readProperties{ ReadRequest{properties, UA_ATTRIBUTEID_VALUE}, client };
				let values = co_await Any( readProperties );
				arguments->Inputs = Gateway::arguments( values, inputs, "InputArguments", id, client );
				arguments->Outputs = Gateway::arguments( values, outputs, "OutputArguments", id, client );
			}
			for( auto* list : {&arguments->Inputs, &arguments->Outputs} ){//each type of the server's own to its base, one walk per type per connection (BaseTypeCache)
				for( auto& a : *list ){
					if( !a.dataType.namespaceIndex ){
						a.Base = NodeId{ a.dataType };
						continue;
					}
					try{
						if( let base = co_await client->BaseTypes().Get( client, NodeId{a.dataType} ); base )
							a.Base = *base;
					}
					catch( const UAException& e ){//a lost session, as the cache's copy has it:  the entry fetches again next time, rather than keep an untyped argument.
						throw UAClientException{ (StatusCode)e.Code(), client->Handle(), Ƒ("base type of {}'s '{}'", id.ToString(), Opc::ToString(a.name)) };
					}
					catch( const Exception& e ){
						DBG( "[{}]{}'s '{}' ({}):  {} - the json implies its type.", hex(client->Handle()), id.ToString(), Opc::ToString(a.name), NodeId{a.dataType}.ToString(), e.what() );
					}
				}
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