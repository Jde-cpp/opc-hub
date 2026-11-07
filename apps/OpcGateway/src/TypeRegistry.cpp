#include "TypeRegistry.h"
#include "UAClient.h"
#include "LazySlot.h"
#include "types/UAClientException.h"
#include "uatypes/Browse.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	TypeRegistry::~TypeRegistry(){
		for( auto a = _head.load(); a; ){
			auto next = a->next;
			for( uint i=0; i<a->typesSize; ++i )
				UA_DataType_clear( &a->types[i] );//the names, the members and the three ids - a string or guid id leaks when only the array goes.
			UA_free( a->types );
			UA_free( a );
			a = next;
		}
	}
	α TypeRegistry::Types()Ι->vector<const UA_DataType*>{
		vector<const UA_DataType*> y;
		for( auto a = Chain(); a; a = a->next ){
			for( uint i=0; i<a->typesSize; ++i )
				y.push_back( &a->types[i] );
		}
		return y;
	}
	α TypeRegistry::Count()Ι->uint{
		uint y{};
		for( auto a = Chain(); a; a = a->next )
			y += a->typesSize;
		return y;
	}
	α TypeRegistry::Missing( const UA_NodeId& encodingId )Ι->void{
		if( Process::ShuttingDown() )
			return;
		{
			ul _{ _mutex };
			if( !_missing.emplace(NodeId{encodingId}).second )
				return;
		}
		auto client = UAClient::FindAny( *this );
		if( !client ){
			DBG( "'{}' named the encoding {} the type registry lacks - no client of it to fetch the server's structures with.", _slug, NodeId{encodingId}.ToString() );
			return;
		}
		DBG( "[{}]'{}' named the encoding {} the type registry lacks - fetching the server's structures.", hex(client->Handle()), _slug, NodeId{encodingId}.ToString() );
		//Posted:  the note comes from a data-change callback or a serializer, never a place a service may start from.
		Post( [self=const_cast<TypeRegistry*>(this)->shared_from_this(), client=move(client)]()mutable ι->void{ self->Fetch( move(client) ); } );
	}
	α TypeRegistry::Start( sp<UAClient>&& client, AnyVoidAwait* waiter )ι->void{
		bool launch{};
		{
			ul _{ _mutex };
			if( waiter )
				_waiters.push_back( waiter );
			if( _fetching )
				_again = move( client );
			else{
				_fetching = true;
				launch = true;
			}
		}
		if( launch )
			Run( shared_from_this(), move(client) );
	}
	α TypeRegistry::Finish( up<Exception> error )ι->void{
		vector<AnyVoidAwait*> waiters;
		sp<UAClient> again;
		{
			ul _{ _mutex };
			_fetching = false;
			++_fetches;
			waiters = move( _waiters ); _waiters.clear();
			again = move( _again ); _again = nullptr;
		}
		for( auto* waiter : waiters ){//each resume the last use of its waiter:  it may run the awaiter to completion inline.
			if( error )
				waiter->ResumeExp( move(*LazySlot<void>::Copy(*error)) );
			else
				waiter->Resume();
		}
		if( again )
			Start( move(again), nullptr );
	}
	α TypeRegistry::Append( DataTypeArrayPtr&& types )ι->uint{
		if( !types || !types->typesSize )
			return 0;
		auto node = types.release();//the deleter freed a `cleanup` array;  from here the chain's, freed by ~TypeRegistry alone.
		node->cleanup = false;
		node->next = nullptr;
		ul _{ _mutex };
		uint kept{};
		for( uint i=0; i<node->typesSize; ++i ){//a type a fetch meanwhile added is cleared, the rest closed up:  one definition per id.
			if( FindDataType(node->types[i].typeId, this) ){
				DBG( "'{}' type registry:  '{}' was added by another fetch - dropped.", _slug, node->types[i].typeName );
				UA_DataType_clear( &node->types[i] );
			}
			else if( kept++!=i ){
				node->types[kept-1] = node->types[i];
				memset( &node->types[i], 0, sizeof(UA_DataType) );
			}
		}
		node->typesSize = kept;
		if( !kept ){
			UA_free( node->types );
			UA_free( node );
			return 0;
		}
		node->next = _head.load( std::memory_order_relaxed );//writers serialize on _mutex;  a reader takes the node whole or not at all.
		_head.store( node, std::memory_order_release );
		return kept;
	}
	α TypeRegistry::Run( sp<TypeRegistry> /*self:  the frame's copy holds this through the awaits*/, sp<UAClient> client )ι->VoidTask{
		up<Exception> error;
		try{
			vector<NodeId> unknown;//the subtree's DataTypes the chain lacks, in browse order.
			flat_set<NodeId> seen;
			vector<NodeId> level; level.emplace_back( UA_NS0ID_STRUCTURE );
			for( uint depth=0; level.size(); ++depth ){
				THROW_IF( depth>MaxDepth || seen.size()>MaxNodes, "'{}' DataType tree exceeds the traversal limits:  {} deep, {} nodes.", _slug, depth, seen.size() );
				vector<NodeId> ids; ids.reserve( level.size() );
				for( let& id : level )
					ids.push_back( NodeId{id} );
				Browse::FoldersAwait browse{ Browse::Request::Subtypes(move(ids)), client };//an lvalue:  Any() stores an rvalue by value, and FoldersAwait is noncopyable.
				let response = co_await Any( browse );
				vector<NodeId> next;
				for( uint i=0; i<level.size() && i<response.resultsSize; ++i ){
					if( let sc = response.results[i].statusCode; sc ){
						DBG( "[{}]'{}' type registry:  browsing the subtypes of {} answered {}.", hex(client->Handle()), _slug, level[i].ToString(), UAException::Message(sc) );
						continue;
					}
					response.VisitWhile( i, [&]( const UA_ReferenceDescription& ref ){
						if( !UA_ExpandedNodeId_isLocal(&ref.nodeId) )
							return true;
						NodeId id{ ref.nodeId.nodeId };
						if( !seen.emplace(NodeId{id}).second )
							return true;
						next.push_back( NodeId{id} );//every subtype is walked:  a known type's subtypes may be unknown.
						if( !FindDataType(id, this) )
							unknown.push_back( move(id) );
						return true;
					} );
				}
				level = move( next );
			}
			uint added{};
			if( unknown.size() ){
				let asked = unknown.size();
				auto types = co_await Any( DataTypesAwait{move(unknown), client, Chain(), SRCE_CUR} );
				added = Append( move(types) );
				INFO( "[{}]'{}' type registry:  {} of {} structure definitions added, {} held.", hex(client->Handle()), _slug, added, asked, Count() );
			}
			else
				DBG( "[{}]'{}' type registry:  the Structure subtree holds nothing the chain lacks ({} held).", hex(client->Handle()), _slug, Count() );
			UAClient::RechainTypes( *this );//every client, added or not:  one built before this fetch holds the chain as it was then.
		}
		catch( UAClientException& e ){
			DBG( "[{}]'{}' type registry:  the fetch failed - {}", hex(client->Handle()), _slug, e.what() );
			error = e.Move();
		}
		catch( runtime_error& e ){
			WARN( "[{}]'{}' type registry:  the fetch failed - {}", hex(client->Handle()), _slug, e.what() );
			error = Exception::From( move(e) );
		}
		Finish( move(error) );
	}
}