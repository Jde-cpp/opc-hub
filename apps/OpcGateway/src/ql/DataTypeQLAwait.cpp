#include "DataTypeQLAwait.h"
#include "../async/ConnectAwait.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α toJson( const UA_DataType& dt, const QL::TableQL& q )ι->jobject{
		jobject j;
		if( q.FindColumn("typeId") )
			j["typeId"] = NodeId{ dt.typeId }.ToJson();
		if( q.FindColumn("binaryEncodingId") )
			j["binaryEncodingId"] = NodeId{ dt.binaryEncodingId }.ToJson();
		if( q.FindColumn("xmlEncodingId") )
			j["xmlEncodingId"] = NodeId{ dt.xmlEncodingId }.ToJson();
		if( q.FindColumn("memSize") )
			j["memSize"] = dt.memSize;
		if( q.FindColumn("typeKind") )
			j["typeKind"] = dt.typeKind;
		if( q.FindColumn("pointerFree") )
			j["pointerFree"] = dt.pointerFree;
		if( q.FindColumn("overlayable") )
			j["overlayable"] = dt.overlayable;
		if( auto memberQL = q.FindTable("members"); memberQL ){
			jarray jMembers;
			for( uint m=0; m<dt.membersSize; ++m ){
				let member = &dt.members[m];
				jobject jMember;
				if( memberQL->FindColumn("padding") )
					jMember["padding"] = member->padding;
				if( memberQL->FindColumn("isArray") )
					jMember["isArray"] = ( bool )member->isArray;
				if( memberQL->FindColumn("isOptional") )
					jMember["isOptional"] = ( bool )member->isOptional;
				if( memberQL->FindColumn("type") )
					jMember["type"] = toJson( *member->memberType, *memberQL );
				jMembers.push_back( jMember );
			}
			j["members"] = jMembers;
		}
		return j;
	}
	//getRemoteDataTypes allocates the array with cleanup=true but does not add it to the client config, so the caller owns it. open62541's UA_cleanupDataTypeWithCustom is not exported; mirror it here.
	//UA_DataType_clear is, and it also frees the typeId and encoding ids - a string or guid id leaked when this freed only the names and members.
	Ω freeRemoteDataTypes( UA_DataTypeArray* a )ι->void{
		while( a ){
			auto next = a->next;
			if( a->cleanup ){
				for( size_t i=0; i<a->typesSize; ++i )
					UA_DataType_clear( &a->types[i] );
				UA_free( a->types );
				UA_free( a );
			}
			a = next;
		}
	}
	α DataTypeQLAwait::Suspend()ι->void{
		_client->PostUA( [this]{//getRemoteDataTypes is a sync UA service - must run on the client's strand.
			try{
				jarray y;
				let nodeIds = NodeId::ParseQL( _ql );
				THROW_IF( nodeIds.empty(), "No nodeIds specified." );
				vector<UA_NodeId> rawIds; rawIds.reserve( nodeIds.size() );
				for( let& id : nodeIds )
					rawIds.emplace_back( static_cast<const UA_NodeId&>(id) );
				UA_DataTypeArray *customTypes;
				if( auto sc = UA_Client_getRemoteDataTypes(*_client, rawIds.size(), rawIds.data(), &customTypes); sc )
					throw UAClientException{ sc, _client->Handle(), Ƒ("Could not get data types: {}", NodeId::ToString(nodeIds)), _sl };
				for( uint i=0; i<customTypes->typesSize; ++i )
					y.push_back( toJson(customTypes->types[i], _ql) );
				freeRemoteDataTypes( customTypes );
				Resume( _ql.TransformResult(move(y)) );
			}
			catch( runtime_error& e ){
				ResumeExp( move(e) );
			}
		});
	}
}