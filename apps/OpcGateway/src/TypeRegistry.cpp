#include "TypeRegistry.h"
#include "UAClient.h"
#include "LazySlot.h"
#include "async/BrowseNextAwait.h"
#include "async/NamespacesAwait.h"
#include "types/UAClientException.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	Ω freeChain( UA_DataTypeArray* a )ι->void{
		while( a ){
			auto next = a->next;
			for( uint i=0; i<a->typesSize; ++i )
				UA_DataType_clear( &a->types[i] );//the names, the members and the three ids - a string or guid id leaks when only the array goes.
			UA_free( a->types );
			UA_free( a );
			a = next;
		}
	}
	TypeRegistry::~TypeRegistry(){
		freeChain( _head.load() );
		ul _{ _mutex };
		for( auto a : _retired )
			freeChain( a );
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
	α TypeRegistry::Unexplained( const UA_NodeId& typeId )Ι->bool{
		rl _{ _mutex };
		return _unexplained.contains( NodeId{typeId} );
	}
	α TypeRegistry::NoteUnexplained( const UA_NodeId& typeId )ι->void{
		ul _{ _mutex };
		_unexplained.emplace( NodeId{typeId} );
	}
	α TypeRegistry::Missing( const UA_NodeId& encodingId )Ι->void{
		if( Process::ShuttingDown() )
			return;
		NodeId id{ encodingId };
		{
			rl _{ _mutex };
			if( _missing.contains(id) || std::chrono::steady_clock::now()<_retryAt )
				return;
		}
		auto client = UAClient::FindAny( *this );
		if( !client ){//not noted:  the next value naming it asks again, once a client is up to fetch through.
			TRACE( "'{}' named the encoding {} the type registry lacks - no client of it to fetch the server's structures with.", _slug, id.ToString() );
			return;
		}
		{
			ul _{ _mutex };
			if( !_missing.emplace(NodeId{id}).second )
				return;
		}
		DBG( "[{}]'{}' named the encoding {} the type registry lacks - fetching the server's structures.", hex(client->Handle()), _slug, id.ToString() );
		//Posted:  the note comes from a data-change callback or a serializer, never a place a service may start from.
		Post( [self=const_cast<TypeRegistry*>(this)->shared_from_this(), client=move(client)]()mutable ι->void{ self->Fetch( move(client), EFetch::Walk ); } );
	}
	α TypeRegistry::Start( sp<UAClient>&& client, AnyVoidAwait* waiter, EFetch kind )ι->void{
		bool launch{};
		{
			ul _{ _mutex };
			if( _fetching ){
				_again = move( client );
				_againKind = std::max( _againKind, kind );
				if( waiter )
					_nextWaiters.push_back( waiter );
			}
			else{
				if( waiter )
					_waiters.push_back( waiter );
				_fetching = launch = true;
			}
		}
		if( launch )
			Run( shared_from_this(), move(client), kind );
	}
	α TypeRegistry::Join( AnyVoidAwait* waiter )ι->void{
		{
			ul _{ _mutex };
			if( _fetching ){
				_waiters.push_back( waiter );
				return;
			}
		}
		waiter->Resume();//ended between await_ready and here.
	}
	α TypeRegistry::Finish( up<Exception> error )ι->void{
		vector<AnyVoidAwait*> waiters;
		sp<UAClient> again;
		EFetch kind;
		{
			ul _{ _mutex };
			_fetching = false;
			++_fetches;
			if( error ){//the ids its walk was for may ask again, after a pause:  a server failing every fetch is not asked at each value.
				_missing.clear();
				_retryAt = std::chrono::steady_clock::now()+RetryAfter;
			}
			waiters = move( _waiters );
			_waiters = move( _nextWaiters ); _nextWaiters.clear();//the next fetch's:  Start below, or one asked before it.
			again = move( _again ); _again = nullptr;
			kind = exchange( _againKind, EFetch::Check );
		}
		for( auto* waiter : waiters ){//each resume the last use of its waiter:  it may run the awaiter to completion inline.
			if( error && dynamic_cast<FetchAwait*>(waiter) )
				waiter->ResumeExp( move(*LazySlot<void>::Copy(*error)) );
			else
				waiter->Resume();
		}
		if( again )
			Start( move(again), nullptr, kind );
	}
	α TypeRegistry::Append( DataTypeArrayPtr&& types )ι->uint{
		if( !types || !types->typesSize )
			return 0;
		//No type here is in the chain:  the walk asked only for what it lacked, and no other fetch runs to add one meanwhile (_fetching).
		auto node = types.release();//the deleter freed a `cleanup` array;  from here the chain's, freed by ~TypeRegistry alone.
		node->cleanup = false;
		ul _{ _mutex };
		node->next = _head.load( std::memory_order_relaxed );//writers serialize on _mutex;  a reader takes the node whole or not at all.
		_head.store( node, std::memory_order_release );
		return node->typesSize;
	}
	α TypeRegistry::BrowseLevel( const vector<NodeId>& level, sp<UAClient> client, uint& batch, function<void(const UA_ReferenceDescription&)> visit )ι->AnyVoidTask{
		let references = [&]( const Browse::Response& response, std::span<const NodeId> asked )->void{
			for( uint i=0; i<asked.size() && i<response.resultsSize; ++i ){
				if( let sc = response.results[i].statusCode; UA_StatusCode_isBad(sc) ){
					DBG( "[{}]'{}' type registry:  browsing the subtypes of {} answered {}.", hex(client->Handle()), _slug, asked[i].ToString(), UAException::Message(sc) );
					continue;
				}
				response.VisitWhile( i, [&]( const UA_ReferenceDescription& ref ){ visit( ref ); return true; } );
			}
		};
		for( uint start=0; start<level.size(); ){
			let count = std::min<uint>( batch, level.size()-start );
			vector<NodeId> ids; ids.reserve( count );
			for( uint i=start; i<start+count; ++i )
				ids.push_back( NodeId{level[i]} );
			optional<Browse::Response> response; bool tooMany{};
			try{
				Browse::FoldersAwait browse{ Browse::Request::Subtypes(move(ids)), client };//an lvalue:  Any() stores an rvalue by value, and FoldersAwait is noncopyable.
				response = co_await Any( browse );
			}
			catch( const UAException& e ){
				if( e.Code()!=UA_STATUSCODE_BADTOOMANYOPERATIONS || count==1 )
					throw;
				tooMany = true;
			}
			if( tooMany ){//the server's MaxNodesPerBrowse is below the request:  fewer, and again, for the rest of the walk.
				batch = std::max<uint>( 1, count/2 );
				DBG( "[{}]'{}' type registry:  a browse of {} types was refused as too many - {} at a time.", hex(client->Handle()), _slug, count, batch );
				continue;
			}
			references( *response, std::span{level}.subspan(start, count) );
			//What the server held back past its MaxReferencesPerNode, followed (reviews-issue/421-type-registry.md #3):  a BrowseNext's
			//results[j] answers the j-th point of the answer before it, which `held` names the nodes of.
			vector<NodeId> held;
			for( uint i=0; i<count && i<response->resultsSize; ++i ){
				if( response->results[i].continuationPoint.length )
					held.push_back( NodeId{level[start+i]} );
			}
			for( uint rounds=0; held.size(); ++rounds ){
				THROW_IF( rounds>MaxNodes, "'{}' type registry:  the server never ended its continuation points.", _slug );
				optional<Browse::Response> more; up<Exception> error;
				try{
					BrowseNextAwait next{ *response, client };
					more = co_await Any( next );
				}
				catch( runtime_error& e ){
					error = Exception::From( move(e) );
				}
				if( error ){//let go what the server still holds, then fail the walk:  a level read in part would leave its types opaque.
					try{
						BrowseNextAwait release{ *response, client, true };
						co_await Any( release );
					}
					catch( const runtime_error& e ){
						DBG( "[{}]'{}' type registry:  could not release the continuation points - {}", hex(client->Handle()), _slug, e.what() );
					}
					error->Throw();
				}
				references( *more, held );
				vector<NodeId> still;
				for( uint j=0; j<held.size() && j<more->resultsSize; ++j ){
					if( more->results[j].continuationPoint.length )
						still.push_back( NodeId{held[j]} );
				}
				held = move( still );
				response = move( more );
			}
			start += count;
		}
	}
	α TypeRegistry::Run( sp<TypeRegistry> /*self:  the frame's copy holds this through the awaits*/, sp<UAClient> client, EFetch kind )ι->VoidTask{
		up<Exception> error;
		try{
			optional<Namespaces> namespaces;
			try{
				namespaces = co_await Any( NamespacesAwait{client} );
			}
			catch( const runtime_error& e ){//a server that won't serve its NamespaceArray:  the chain is kept as it is.
				DBG( "[{}]'{}' type registry:  could not read the namespace array - {}", hex(client->Handle()), _slug, e.what() );
			}
			bool moved{}, walk;
			{
				ul _{ _mutex };
				if( namespaces ){
					moved = _namespaces.size() && ( namespaces->Uris.size()<_namespaces.size() || !std::equal(_namespaces.begin(), _namespaces.end(), namespaces->Uris.begin()) );
					if( moved ){//set aside, not freed:  a value decoded against it may still be on its way.
						if( auto head = _head.exchange(nullptr, std::memory_order_acq_rel); head )
							_retired.push_back( head );
						_missing.clear();
						_unexplained.clear();
						_walked = false;
					}
					_namespaces = move( namespaces->Uris );
				}
				walk = kind==EFetch::Walk || !_walked;
			}
			if( moved ){
				INFO( "[{}]'{}' type registry:  the server's namespace array moved - its structures are fetched again.", hex(client->Handle()), _slug );
				UAClient::RechainTypes( *this );//none until the walk lands:  a body left opaque, not decoded by another index's type.
			}
			if( walk ){
				up<Exception> walkError;
				try{
					vector<NodeId> unknown;//the subtree's DataTypes the chain lacks, in browse order.
					flat_set<NodeId> seen;
					vector<NodeId> level; level.emplace_back( UA_NS0ID_STRUCTURE );
					uint batch = std::numeric_limits<uint>::max();
					for( uint depth=0; level.size(); ++depth ){
						THROW_IF( depth>MaxDepth || seen.size()>MaxNodes, "'{}' DataType tree exceeds the traversal limits:  {} deep, {} nodes.", _slug, depth, seen.size() );
						vector<NodeId> next;
						co_await BrowseLevel( level, client, batch, [&]( const UA_ReferenceDescription& ref ){
							if( !UA_ExpandedNodeId_isLocal(&ref.nodeId) )
								return;
							NodeId id{ ref.nodeId.nodeId };
							if( !seen.emplace(NodeId{id}).second )
								return;
							next.push_back( NodeId{id} );//every subtype is walked:  a known type's subtypes may be unknown.
							if( !FindDataType(id, this) )
								unknown.push_back( move(id) );
						} );
						level = move( next );
					}
					uint added{};
					if( unknown.size() ){
						let asked = unknown.size();
						auto types = co_await DataTypesAwait{ move(unknown), client, Chain(), SRCE_CUR };
						added = Append( move(types) );
						INFO( "[{}]'{}' type registry:  {} of {} structure definitions added, {} held.", hex(client->Handle()), _slug, added, asked, Count() );
					}
					else
						DBG( "[{}]'{}' type registry:  the Structure subtree holds nothing the chain lacks ({} held).", hex(client->Handle()), _slug, Count() );
				}
				catch( runtime_error& e ){
					walkError = Exception::From( move(e) );
				}
				{
					ul _{ _mutex };
					++_walks;
					if( !walkError )
						_walked = true;
				}
				if( walkError )
					walkError->Throw();
				UAClient::RechainTypes( *this );//every client, added or not:  one built before this fetch holds the chain as it was then.
			}
		}
		catch( UAClientException& e ){
			WARN( "[{}]'{}' type registry:  the fetch failed - {}", hex(client->Handle()), _slug, e.what() );
			error = e.Move();
		}
		catch( runtime_error& e ){
			WARN( "[{}]'{}' type registry:  the fetch failed - {}", hex(client->Handle()), _slug, e.what() );
			error = Exception::From( move(e) );
		}
		Finish( move(error) );
	}
}