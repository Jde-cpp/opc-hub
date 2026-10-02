#pragma once
#ifndef EXNODE_H
#define EXNODE_H
#include "opcHelpers.h"

namespace Jde::DB{ struct Row; struct Value; }
namespace Jde::Opc{
	struct NodeId;
	struct ExNodeId : UA_ExpandedNodeId{
		ExNodeId()ι:UA_ExpandedNodeId{UA_EXPANDEDNODEID_NULL}{}
		ExNodeId( UA_NodeId&& x )ι:UA_ExpandedNodeId{move(x), UA_EXPANDEDNODEID_NULL.namespaceUri, UA_EXPANDEDNODEID_NULL.serverIndex}{ UA_NodeId_init( &x ); }
		ExNodeId( const NodeId& x )ι;
		ExNodeId( const UA_ExpandedNodeId& x )ι:ExNodeId{}{ UA_ExpandedNodeId_copy( &x, this ); }
		ExNodeId( UA_ExpandedNodeId&& x )ι:UA_ExpandedNodeId{move(x)}{ UA_ExpandedNodeId_init(&x); }
		ExNodeId( const flat_map<string,string>& x )ε;//rest params
		explicit ExNodeId( const jvalue& j )ε;
		ExNodeId( UA_UInt32 numeric )ι:ExNodeId{UA_NodeId{0, UA_NODEIDTYPE_NUMERIC, {numeric}}}{}
		ExNodeId( const ExNodeId& x )ι;
		ExNodeId( ExNodeId&& x )ι;
		ExNodeId( DB::Row& r, uint8 nsIndex, bool extended=false )ε;
		α operator=( ExNodeId&& x )ι->ExNodeId&;
		~ExNodeId(){ Clear(); }
		α operator<( const ExNodeId& x )Ι->bool;
		template<class H> friend α AbslHashValue( H h, const ExNodeId& n )ι->H{ return HashNodeId( H::combine(std::move(h), ToSV(n.namespaceUri), n.serverIndex), n.nodeId ); }
		α operator=( const ExNodeId& x )ι->ExNodeId&;
		α InsertParams( bool extended )Ι->vector<DB::Value>;

		α NsIndex()Ι->UA_UInt16{ return nodeId.namespaceIndex; }
		α Numeric()Ι->optional<UA_UInt32>{ return nodeId.identifierType==UA_NODEIDTYPE_NUMERIC ? nodeId.identifier.numeric : optional<UA_UInt32>{}; }
		α String()Ι->optional<string>{ return nodeId.identifierType==UA_NODEIDTYPE_STRING ? ToString(nodeId.identifier.string) : optional<string>{}; }
		α Guid()Ι->optional<uuid>{ return nodeId.identifierType==UA_NODEIDTYPE_GUID ? ToGuid(nodeId.identifier.guid) : optional<uuid>{}; }
		α Bytes()Ι->optional<UA_ByteString>{ return nodeId.identifierType==UA_NODEIDTYPE_BYTESTRING ? nodeId.identifier.byteString : optional<UA_ByteString>{}; }

		α IsNumeric()Ι{ return nodeId.identifierType==UA_NODEIDTYPE_NUMERIC; }
		α IsString()Ι{ return nodeId.identifierType==UA_NODEIDTYPE_STRING; }
		α IsGuid()Ι{ return nodeId.identifierType==UA_NODEIDTYPE_GUID; }
		α IsBytes()Ι{ return nodeId.identifierType==UA_NODEIDTYPE_BYTESTRING; }

		α Clear()ι->void;
		α Copy()Ι->UA_NodeId;
		α Move()ι->UA_NodeId;
		α ToJson()Ι->jobject;
		α Add( jobject& j )Ι->void;
		α to_string()Ι->string;
	};
	static_assert( !std::is_polymorphic_v<ExNodeId> && sizeof(ExNodeId)==sizeof(UA_ExpandedNodeId),
		"ExNodeId must stay layout-compatible with UA_ExpandedNodeId - see reviews/opc-review3.md #2" );

	Ξ operator==( const ExNodeId& x, const ExNodeId& y )ι->bool{ return !(x<y) && !(y<x); }
	α ToJson( const UA_ExpandedNodeId& nodeId )ε->jobject;
}
#endif