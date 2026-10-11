#pragma once
#include "NodeId.h"
#include "Owned.h"

namespace Jde::Opc{
	struct ExNodeId final : Owned<UA_ExpandedNodeId, UA_TYPES_EXPANDEDNODEID>{
		ExNodeId()ι=default;
		ExNodeId( UA_NodeId&& x )ι:Owned{ UA_ExpandedNodeId{x, UA_STRING_NULL, 0} }{ UA_NodeId_init( &x ); }
		ExNodeId( const NodeId& x )ι;
		ExNodeId( const UA_ExpandedNodeId& x )ι:Owned{ x }{}
		ExNodeId( UA_ExpandedNodeId&& x )ι:Owned{ move(x) }{}
		explicit ExNodeId( const jvalue& j )ε;
		ExNodeId( UA_UInt32 numeric )ι:ExNodeId{UA_NodeId{0, UA_NODEIDTYPE_NUMERIC, {numeric}}}{}
		α operator<( const ExNodeId& x )Ι->bool{ return UA_ExpandedNodeId_order( this, &x )==UA_ORDER_LESS; }
		template<class H> friend α AbslHashValue( H h, const ExNodeId& n )ι->H{ return HashNodeId( H::combine(std::move(h), ToSV(n.namespaceUri), n.serverIndex), n.nodeId ); }

		α NsIndex()Ι->UA_UInt16{ return nodeId.namespaceIndex; }
		α Numeric()Ι->optional<UA_UInt32>{ return Opc::Numeric( nodeId ); }
		α String()Ι->optional<string>{ return Opc::String( nodeId ); }
		α Guid()Ι->optional<uuid>{ return Opc::Guid( nodeId ); }
		α Bytes()Ι->optional<UA_ByteString>{ return Opc::Bytes( nodeId ); }

		α IsNumeric()Ι{ return Opc::IsNumeric( nodeId ); }
		α IsString()Ι{ return Opc::IsString( nodeId ); }
		α IsGuid()Ι{ return Opc::IsGuid( nodeId ); }
		α IsBytes()Ι{ return Opc::IsBytes( nodeId ); }

		//Hands over the nodeId alone and leaves it null.  Owned::Move, which this hides, would hand over the whole struct.
		α Move()ι->UA_NodeId;
		α ToJson()Ι->jobject;
		α Add( jobject& j )Ι->void;
		α ToString()Ι->string;//the UA spelling, `nsu=…;s=…`, as NodeId::ToString.
	};
	static_assert( !std::is_polymorphic_v<ExNodeId> && sizeof(ExNodeId)==sizeof(UA_ExpandedNodeId),
		"ExNodeId must stay layout-compatible with UA_ExpandedNodeId - see reviews/opc-review3.md #2" );

	Ξ operator==( const ExNodeId& x, const ExNodeId& y )ι->bool{ return UA_ExpandedNodeId_equal( &x, &y ); }
	α ToJson( const UA_ExpandedNodeId& nodeId )ε->jobject;
}