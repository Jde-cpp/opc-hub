#pragma once
#include "opcHelpers.h"
#include "Owned.h"

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc{
	//The identifier accessors, written once over the vendor struct:  NodeId and ExNodeId's nodeId both forward here.
	Ξ IsNumeric( const UA_NodeId& x )ι->bool{ return x.identifierType==UA_NODEIDTYPE_NUMERIC; }
	Ξ IsString( const UA_NodeId& x )ι->bool{ return x.identifierType==UA_NODEIDTYPE_STRING; }
	Ξ IsGuid( const UA_NodeId& x )ι->bool{ return x.identifierType==UA_NODEIDTYPE_GUID; }
	Ξ IsBytes( const UA_NodeId& x )ι->bool{ return x.identifierType==UA_NODEIDTYPE_BYTESTRING; }
	Ξ Numeric( const UA_NodeId& x )ι->optional<UA_UInt32>{ return IsNumeric(x) ? x.identifier.numeric : optional<UA_UInt32>{}; }
	Ξ String( const UA_NodeId& x )ι->optional<string>{ return IsString(x) ? ToString(x.identifier.string) : optional<string>{}; }
	Ξ Guid( const UA_NodeId& x )ι->optional<uuid>{ return IsGuid(x) ? ToGuid(x.identifier.guid) : optional<uuid>{}; }
	Ξ Bytes( const UA_NodeId& x )ι->optional<UA_ByteString>{ return IsBytes(x) ? x.identifier.byteString : optional<UA_ByteString>{}; }

	struct NodeId final : Owned<UA_NodeId, UA_TYPES_NODEID>{
		NodeId()ι=default;
		NodeId( const UA_NodeId& x )ι:Owned{ x }{}
		NodeId( UA_NodeId&& x )ι:Owned{ move(x) }{}
		NodeId( UA_UInt16 namespaceIndex, UA_UInt32 numeric )ι:NodeId{UA_NodeId{namespaceIndex, UA_NODEIDTYPE_NUMERIC, {numeric}}}{}
		NodeId( UA_UInt32 numeric )ι:NodeId{0, numeric}{}
		NodeId( const QL::TableQL& q )ε;
		explicit NodeId( const jvalue& j )ε;
		Ω ParseQL( const QL::TableQL& q )ε->vector<NodeId>;
		Ω ObjectsFolder()ι->NodeId{ return NodeId{0, UA_NS0ID_OBJECTSFOLDER}; }

		α operator<( const NodeId& x )Ι->bool{ return UA_NodeId_order( this, &x )==UA_ORDER_LESS; }
		template<class H> friend α AbslHashValue( H h, const NodeId& n )ι->H{ return HashNodeId( std::move(h), n ); }

		Ω FromJson( const jvalue& v, UA_UInt16 ns=0 )ε->UA_NodeId;
		Ω FromJson( const jobject& j, UA_UInt16 ns=0 )ε->UA_NodeId;

		α IsNumeric()Ι{ return Opc::IsNumeric( *this ); }
		α IsString()Ι{ return Opc::IsString( *this ); }
		α IsGuid()Ι{ return Opc::IsGuid( *this ); }
		α IsBytes()Ι{ return Opc::IsBytes( *this ); }
		α Numeric()Ι->optional<UA_UInt32>{ return Opc::Numeric( *this ); }
		α String()Ι->optional<string>{ return Opc::String( *this ); }
		α Guid()Ι->optional<uuid>{ return Opc::Guid( *this ); }
		α Bytes()Ι->optional<UA_ByteString>{ return Opc::Bytes( *this ); }

		α Add( jobject& j )Ι->void;
		α ToJson()Ι->jobject;
		α ToString()Ι->string;
		Ω ToString( const vector<NodeId>& nodeIds )ι->string;
		Ω DecodeJson( const string& j )ε->NodeId;
	};
	static_assert( !std::is_polymorphic_v<NodeId> && sizeof(NodeId)==sizeof(UA_NodeId),
		"NodeId must stay layout-compatible with UA_NodeId - see reviews/opc-refactor.md B2" );
	α ToJson( const UA_NodeId& nodeId )ι->jobject;
	α AddNodeId( jobject& j, const UA_NodeId& nodeId, bool omitDefaultNs )ι->jobject&;
	Ξ operator==( const NodeId& x, const NodeId& y )ι->bool{ return UA_NodeId_equal( &x, &y ); }
	//Transparent, so a container keyed by NodeId finds a UA_NodeId without copying it.
	struct NodeHash final{
		using is_transparent = void;
		α operator()( const UA_NodeId& node )Ι->size_t{ return UA_NodeId_hash( &node ); }
	};
	struct NodeEqual final{
		using is_transparent = void;
		α operator()( const UA_NodeId& a, const UA_NodeId& b )Ι->bool{ return UA_NodeId_equal( &a, &b ); }
	};
}