#include "BaseTypeCache.h"
#include "UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	//One inverse HasSubtype browse a step, up to a supertype of namespace 0 - usually one, an enumeration's Enumeration.
	α BaseTypeCache::Fetch( sp<UAClient> client, NodeId id )ι->VoidTask{
		sp<NodeId> base; up<Exception> error; bool retryable{};
		try{
			NodeId type{ id };
			for( uint step=0; !base; ++step ){
				THROW_IF( step==MaxSteps, "'{}' has no namespace-0 supertype within {} steps.", id.ToString(), MaxSteps );
				Browse::FoldersAwait browse{ Browse::Request::Supertype(NodeId{type}), client };//an lvalue:  Any() stores an rvalue by value, and FoldersAwait is noncopyable.
				let refs = co_await Any( browse );
				optional<NodeId> super;
				if( refs.resultsSize )
					refs.VisitWhile( 0, [&]( const UA_ReferenceDescription& ref ){ super = NodeId{ ref.nodeId.nodeId }; return false; } );
				THROW_IF( !super, "'{}' has no supertype{}.", type.ToString(), step ? Ƒ(", reached from '{}'", id.ToString()) : string{} );
				if( super->namespaceIndex )
					type = move( *super );
				else
					base = ms<NodeId>( move(*super) );
			}
			DBG( "[{}]DataType '{}' derives from '{}'.", hex(client->Handle()), id.ToString(), base->ToString() );
		}
		catch( UAClientException& e ){//session/transport:  do not pin the failure on the cache.
			retryable = true;
			error = e.Move();
		}
		catch( runtime_error& e ){
			error = Exception::From( move(e) );
		}
		Finish( id, move(base), move(error), retryable );
	}
}