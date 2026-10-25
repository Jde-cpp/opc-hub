#include "ServedValues.h"
#include <jde/opc/UAException.h>

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };

	α ServedValues::Join( HistGroupPK group, HistGroupNodePK index )ι->void{
		ul _{ _mutex };
		_groups[group].Members.try_emplace( index );
	}
	α ServedValues::Leave( HistGroupPK group, HistGroupNodePK index )ι->void{
		ul _{ _mutex };
		if( auto g = _groups.find(group); g!=_groups.end() )
			g->second.Members.erase( index );
	}
	α ServedValues::Forget( HistGroupPK group )ι->void{
		ul _{ _mutex };
		_groups.erase( group );
	}
	α ServedValues::Find( HistGroupPK group, HistGroupNodePK index )Ι->const Member*{
		auto g = _groups.find( group );
		if( g==_groups.end() )
			return nullptr;
		auto p = g->second.Members.find( index );
		return p==g->second.Members.end() ? nullptr : &p->second;
	}
	α ServedValues::SetUris( HistGroupPK group, vector<string> uris )ι->void{
		auto shared = ms<const vector<string>>( move(uris) );
		ul _{ _mutex };
		_groups[group].Namespaces = move( shared );
	}
	α ServedValues::SeedUris( HistGroupPK group, vector<string> uris )ι->void{
		if( uris.empty() )
			return;
		auto shared = ms<const vector<string>>( move(uris) );
		ul _{ _mutex };
		if( auto& namespaces = _groups[group].Namespaces; !namespaces )
			namespaces = move( shared );
	}
	α ServedValues::Index( const ServerCnnctnNK& slug, string remote, UA_UInt16 index )ι->void{
		ul _{ _mutex };
		_indexes[slug].insert_or_assign( move(remote), index );
	}
	α ServedValues::Add( NodeId node, HistGroupPK group, HistGroupNodePK index, ServerCnnctnNK slug )ι->bool{
		ul _{ _mutex };
		return _nodes.try_emplace( move(node), Served{group, index, move(slug)} ).second;
	}
	α ServedValues::Remove( const NodeId& node )ι->void{
		ul _{ _mutex };
		_nodes.erase( node );
	}
	α ServedValues::Store( HistGroupPK group, HistGroupNodePK index, const UA_DataValue& value )ι->void{
		ul _{ _mutex };
		auto g = _groups.find( group );
		if( g==_groups.end() )
			return;
		if( auto m = g->second.Members.find(index); m!=g->second.Members.end() ){
			m->second.Last.emplace( value );
			m->second.Namespaces = g->second.Namespaces;
		}
	}
	//The status alone, the value and its timestamps as they were:  a value before any read says why there is none.
	α ServedValues::Lose( HistGroupPK group, std::span<const HistGroupNodePK> indexes )ι->void{
		ul _{ _mutex };
		auto g = _groups.find( group );
		if( g==_groups.end() )
			return;
		for( let index : indexes ){
			auto m = g->second.Members.find( index );
			if( m==g->second.Members.end() )
				continue;
			if( auto& last = m->second.Last; !last )
				last.emplace( UA_STATUSCODE_BADNOCOMMUNICATION );
			else{
				last->status = UA_STATUSCODE_BADNOCOMMUNICATION;
				last->hasStatus = true;
			}
		}
	}
	α ServedValues::Read( const UA_NodeId& node, const UA_NumericRange* range, UA_DataValue& value )ι->UA_StatusCode{
		uint unmapped{};
		ServerCnnctnNK slug;
		{
			rl _{ _mutex };
			auto p = _nodes.find( node );
			if( p==_nodes.end() )
				return UA_STATUSCODE_BADNODEIDUNKNOWN;
			let& served = p->second;
			let member = Find( served.Group, served.Index );
			if( !member || !member->Last )
				return UA_STATUSCODE_BADWAITINGFORINITIALDATA;
			let& last = *member->Last;
			//With no value the status is the return:  open62541 takes a Good return as holding one, a null for a nullable DataType and
			//Bad_WaitingForInitialData over the status for the rest (ua_services_attribute.c:600).  The timestamps are kept.
			if( !last.hasValue ){
				if( let sc = UA_DataValue_copy(&last, &value); sc )
					return sc;
				return value.status ? value.status : UA_STATUSCODE_BADWAITINGFORINITIALDATA;
			}
			if( let sc = range ? UA_DataValue_copyRange( &last, &value, *range ) : UA_DataValue_copy( &last, &value ); sc )
				return sc;
			let uris = member->Namespaces.get();
			let known = _indexes.find( served.Slug );
			let indexes = known==_indexes.end() ? nullptr : &known->second;
			unmapped = Remap( value.value, [&]( UA_UInt16 ns )->optional<UA_UInt16>{
				if( !uris || !indexes || ns>=uris->size() )
					return nullopt;
				auto p = indexes->find( (*uris)[ns] );
				return p==indexes->end() ? nullopt : optional<UA_UInt16>{ p->second };
			} );
			if( !unmapped || served.Warned )
				return UA_STATUSCODE_GOOD;
			slug = served.Slug;
		}
		{
			ul _{ _mutex };
			if( auto p = _nodes.find(node); p==_nodes.end() || std::exchange(p->second.Warned, true) )
				return UA_STATUSCODE_GOOD;
		}
		WARN( "Node {}'s value names {} namespace index(es) the endpoint can't place from '{}''s NamespaceArray - served as the source gave them.", NodeId{node}.ToString(), unmapped, slug );
		return UA_STATUSCODE_GOOD;
	}

	using Map = absl::FunctionRef<optional<UA_UInt16>(UA_UInt16)>;
	//Each index to the endpoint's, counting those it can't place.
	Ω remapIndex( UA_UInt16& ns, Map map, uint& unmapped )ι->void{
		if( !ns )//0 is 0 everywhere.
			return;
		if( let y = map(ns); y )
			ns = *y;
		else
			++unmapped;
	}
	Ω remapData( void* data, const UA_DataType& type, size_t count, Map map, uint& unmapped )ι->void;
	//A structure's members as open62541 lays them out (ua_types.c's copyStructure and copyUnion):  each after its padding, an array,
	//optional or not, as its size and pointer, an optional scalar by pointer, null when absent;  a union's selected member alone, at
	//its padding from the start.
	Ω remapMembers( void* p, const UA_DataType& type, Map map, uint& unmapped )ι->void{
		auto ptr = reinterpret_cast<uintptr_t>( p );
		let array = [&]( const UA_DataTypeMember& m ){ remapData( *reinterpret_cast<void**>(ptr+sizeof(size_t)), *m.memberType, *reinterpret_cast<size_t*>(ptr), map, unmapped ); };
		if( type.typeKind==UA_DATATYPEKIND_UNION ){
			let selection = *reinterpret_cast<UA_UInt32*>( ptr );
			if( !selection || selection>type.membersSize )
				return;
			let& m = type.members[selection-1];
			ptr += m.padding;
			if( m.isArray )
				array( m );
			else
				remapData( reinterpret_cast<void*>(ptr), *m.memberType, 1, map, unmapped );
			return;
		}
		for( size_t i=0; i<type.membersSize; ++i ){
			let& m = type.members[i];
			ptr += m.padding;
			if( m.isArray ){
				array( m );
				ptr += sizeof(size_t) + sizeof(void*);
			}
			else if( m.isOptional ){
				remapData( *reinterpret_cast<void**>(ptr), *m.memberType, 1, map, unmapped );
				ptr += sizeof(void*);
			}
			else{
				remapData( reinterpret_cast<void*>(ptr), *m.memberType, 1, map, unmapped );
				ptr += m.memberType->memSize;
			}
		}
	}
	//`count` values of `type` at `data`, walked only for a kind that can hold an index.
	Ω remapData( void* data, const UA_DataType& type, size_t count, Map map, uint& unmapped )ι->void{
		switch( type.typeKind ){
		case UA_DATATYPEKIND_NODEID: case UA_DATATYPEKIND_EXPANDEDNODEID: case UA_DATATYPEKIND_QUALIFIEDNAME: case UA_DATATYPEKIND_EXTENSIONOBJECT:
		case UA_DATATYPEKIND_VARIANT: case UA_DATATYPEKIND_DATAVALUE: case UA_DATATYPEKIND_STRUCTURE: case UA_DATATYPEKIND_OPTSTRUCT: case UA_DATATYPEKIND_UNION:
			break;
		default:
			return;
		}
		if( !data || data==UA_EMPTY_ARRAY_SENTINEL )
			return;
		for( size_t i=0; i<count; ++i ){
			void* p = static_cast<UA_Byte*>( data ) + i*type.memSize;
			switch( type.typeKind ){
			case UA_DATATYPEKIND_NODEID:
				remapIndex( static_cast<UA_NodeId*>(p)->namespaceIndex, map, unmapped );
				break;
			case UA_DATATYPEKIND_EXPANDEDNODEID:{//with a URI the index is ignored (Part 6 §5.2.2.10), so it names no namespace to move.
				auto& n = *static_cast<UA_ExpandedNodeId*>( p );
				if( !n.namespaceUri.length )
					remapIndex( n.nodeId.namespaceIndex, map, unmapped );
				break;}
			case UA_DATATYPEKIND_QUALIFIEDNAME:
				remapIndex( static_cast<UA_QualifiedName*>(p)->namespaceIndex, map, unmapped );
				break;
			case UA_DATATYPEKIND_EXTENSIONOBJECT:{//encoded, its type id;  decoded, a type this build knows, namespace 0's, its members.
				auto& o = *static_cast<UA_ExtensionObject*>( p );
				if( o.encoding<=UA_EXTENSIONOBJECT_ENCODED_XML )
					remapIndex( o.content.encoded.typeId.namespaceIndex, map, unmapped );
				else if( o.content.decoded.type )
					remapData( o.content.decoded.data, *o.content.decoded.type, 1, map, unmapped );
				break;}
			case UA_DATATYPEKIND_VARIANT:
				unmapped += ServedValues::Remap( *static_cast<UA_Variant*>(p), map );
				break;
			case UA_DATATYPEKIND_DATAVALUE:
				unmapped += ServedValues::Remap( static_cast<UA_DataValue*>(p)->value, map );
				break;
			default://a structure, optional fields or a union.
				remapMembers( p, type, map, unmapped );
				break;
			}
		}
	}
	α ServedValues::Remap( UA_Variant& value, Map map )ι->uint{
		uint unmapped{};
		if( value.type )
			remapData( value.data, *value.type, UA_Variant_isScalar(&value) ? 1 : value.arrayLength, map, unmapped );
		return unmapped;
	}

	Ω read( UA_Server*, const UA_NodeId*, void*, const UA_NodeId* nodeId, void* nodeContext, UA_Boolean, const UA_NumericRange* range, UA_DataValue* value )ι->UA_StatusCode{
		if( !nodeContext || !nodeId || !value )
			return UA_STATUSCODE_BADINTERNALERROR;
		return static_cast<ServedValues*>( nodeContext )->Read( *nodeId, range, *value );
	}
	α ServedValues::Source()ι->UA_CallbackValueSource{ return UA_CallbackValueSource{ .read=read, .write=nullptr }; }
}