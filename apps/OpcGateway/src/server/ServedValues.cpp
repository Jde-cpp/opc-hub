#include "ServedValues.h"
#include <jde/opc/UAException.h>

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };

	α ServedValues::Add( NodeId node, HistGroupPK group, HistGroupNodePK index, ServerCnnctnNK slug, optional<Value> last )ι->bool{
		ul _{ _mutex };
		let [p, added] = _nodes.try_emplace( node, Served{group, index, move(slug), move(last)} );
		if( added )
			_members.insert_or_assign( std::pair{group, index}, move(node) );
		return added;
	}
	α ServedValues::Remove( const NodeId& node )ι->void{
		ul _{ _mutex };
		if( auto p = _nodes.find(node); p!=_nodes.end() ){
			_members.erase( std::pair{p->second.Group, p->second.Index} );
			_nodes.erase( p );
		}
	}
	α ServedValues::Store( HistGroupPK group, HistGroupNodePK index, const UA_DataValue& value )ι->void{
		ul _{ _mutex };
		if( auto m = _members.find(std::pair{group, index}); m!=_members.end() ){
			if( auto p = _nodes.find(m->second); p!=_nodes.end() )
				p->second.Last.emplace( value );
		}
	}
	//The status alone, the value and its timestamps as they were:  a value before any read says why there is none.
	α ServedValues::Lose( HistGroupPK group, std::span<const HistGroupNodePK> indexes )ι->void{
		ul _{ _mutex };
		for( let index : indexes ){
			auto m = _members.find( std::pair{group, index} );
			auto p = m==_members.end() ? _nodes.end() : _nodes.find( m->second );
			if( p==_nodes.end() )
				continue;
			auto& last = p->second.Last;
			if( !last )
				last.emplace( UA_STATUSCODE_BADNOCOMMUNICATION );
			else{
				last->status = UA_STATUSCODE_BADNOCOMMUNICATION;
				last->hasStatus = true;
			}
		}
	}
	α ServedValues::SetNamespaces( ServerCnnctnNK slug, vector<UA_UInt16> table )ι->void{
		ul _{ _mutex };
		_tables.insert_or_assign( move(slug), move(table) );
	}
	α ServedValues::Table( const ServerCnnctnNK& slug )Ι->vector<UA_UInt16>{
		rl _{ _mutex };
		auto p = _tables.find( slug );
		return p==_tables.end() ? vector<UA_UInt16>{} : p->second;
	}
	α ServedValues::Read( const NodeId& node, const UA_NumericRange* range, UA_DataValue& value )ι->UA_StatusCode{
		ul _{ _mutex };
		auto p = _nodes.find( node );
		if( p==_nodes.end() )
			return UA_STATUSCODE_BADNODEIDUNKNOWN;
		auto& served = p->second;
		if( !served.Last ){
			value.hasStatus = true;
			value.status = UA_STATUSCODE_BADWAITINGFORINITIALDATA;
			return UA_STATUSCODE_GOOD;
		}
		if( let sc = range ? UA_DataValue_copyRange( &*served.Last, &value, *range ) : UA_DataValue_copy( &*served.Last, &value ); sc )
			return sc;
		auto t = _tables.find( served.Slug );
		let unmapped = Remap( value.value, t==_tables.end() ? std::span<const UA_UInt16>{} : std::span<const UA_UInt16>{t->second} );
		if( unmapped && !std::exchange(served.Warned, true) )
			WARN( "Node {}'s value names {} namespace index(es) '{}'s table doesn't hold - served as the source gave them.", node.ToString(), unmapped, served.Slug );
		return UA_STATUSCODE_GOOD;
	}

	//Each index to the table's, counting those past its end.
	Ω remapIndex( UA_UInt16& ns, std::span<const UA_UInt16> table, uint& unmapped )ι->void{
		if( ns<table.size() )
			ns = table[ns];
		else if( ns )//0 maps to 0 in every table, read or not.
			++unmapped;
	}
	Ŧ each( UA_Variant& value, auto&& f )ι->void{
		let count = UA_Variant_isScalar( &value ) ? 1 : value.arrayLength;
		for( size_t i=0; i<count; ++i )
			f( static_cast<T*>(value.data)[i] );
	}
	α ServedValues::Remap( UA_Variant& value, std::span<const UA_UInt16> table )ι->uint{
		uint unmapped{};
		if( !value.type || !value.data )
			return unmapped;
		switch( value.type->typeKind ){
		case UA_DATATYPEKIND_NODEID:
			each<UA_NodeId>( value, [&]( UA_NodeId& n ){ remapIndex( n.namespaceIndex, table, unmapped ); } );
			break;
		case UA_DATATYPEKIND_EXPANDEDNODEID://with a URI the index is ignored (Part 6 §5.2.2.10), so it names no namespace to move.
			each<UA_ExpandedNodeId>( value, [&]( UA_ExpandedNodeId& n ){ if( !n.namespaceUri.length ) remapIndex( n.nodeId.namespaceIndex, table, unmapped ); } );
			break;
		case UA_DATATYPEKIND_QUALIFIEDNAME:
			each<UA_QualifiedName>( value, [&]( UA_QualifiedName& n ){ remapIndex( n.namespaceIndex, table, unmapped ); } );
			break;
		case UA_DATATYPEKIND_EXTENSIONOBJECT://decoded, its type is one this build knows, namespace 0's:  nothing to move.
			each<UA_ExtensionObject>( value, [&]( UA_ExtensionObject& o ){ if( o.encoding<=UA_EXTENSIONOBJECT_ENCODED_XML ) remapIndex( o.content.encoded.typeId.namespaceIndex, table, unmapped ); } );
			break;
		case UA_DATATYPEKIND_VARIANT:
			each<UA_Variant>( value, [&]( UA_Variant& v ){ unmapped += Remap( v, table ); } );
			break;
		case UA_DATATYPEKIND_DATAVALUE:
			each<UA_DataValue>( value, [&]( UA_DataValue& v ){ unmapped += Remap( v.value, table ); } );
			break;
		default:
			break;
		}
		return unmapped;
	}

	Ω read( UA_Server*, const UA_NodeId*, void*, const UA_NodeId* nodeId, void* nodeContext, UA_Boolean, const UA_NumericRange* range, UA_DataValue* value )ι->UA_StatusCode{
		if( !nodeContext || !nodeId || !value )
			return UA_STATUSCODE_BADINTERNALERROR;
		return static_cast<ServedValues*>( nodeContext )->Read( NodeId{*nodeId}, range, *value );
	}
	α ServedValues::Source()ι->UA_CallbackValueSource{ return UA_CallbackValueSource{ .read=read, .write=nullptr }; }
}