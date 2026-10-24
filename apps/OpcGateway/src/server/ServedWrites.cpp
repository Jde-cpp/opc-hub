#include "ServedWrites.h"
#include <jde/fwk/process/execution.h>
#include <jde/access/Authorize.h>
#include <jde/opc/UAException.h>
#include "../async/ConnectAwait.h"
#include "../async/WriteAwait.h"
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "ServedValues.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };

	ServedWrites::ServedWrites( sp<ServedValues> values, Poster post )ι:
		_values{ move(values) },
		_post{ std::move(post) }
	{}

	α ServedWrites::Write( const UA_NodeId& node, UserPK user, const UA_NumericRange* range, const UA_DataValue& value )ι->UA_StatusCode{
		if( range )
			return UA_STATUSCODE_BADWRITENOTSUPPORTED;
		let serving = _values->Served( node );
		if( !serving )
			return UA_STATUSCODE_BADNODEIDUNKNOWN;
		Value written{ value };
		if( written.hasValue && _values->Unmap(node, written.value) )
			return UA_STATUSCODE_BADNODEIDINVALID;
		bool first{};
		{
			ul _{ _mutex };
			auto& queue = _nodes[NodeId{node}];
			first = queue.empty();
			queue.push_back( Queued{&value, move(written), user, serving->Group, serving->Index, serving->Slug} );
		}
		if( first )
			Post( [self=shared_from_this(), id=NodeId{node}]{ self->Forward( id ); } );
		return UA_STATUSCODE_GOODCOMPLETESASYNCHRONOUSLY;
	}
	α ServedWrites::Cancelled( const void* value )ι->void{
		ul _{ _mutex };
		for( auto& [_, queue] : _nodes ){
			for( auto& q : queue ){
				if( q.Key==value )
					q.Key = nullptr;
			}
		}
	}
	//The front of the node's queue to its source:  the collector's client, as the collector itself gets it, and the node by the index
	//the source gave it.  Nothing here runs on the server's thread, so a wait for the source holds no service lock.
	α ServedWrites::Forward( NodeId node )ι->VoidTask{
		auto self = shared_from_this();
		Value written{ UA_STATUSCODE_GOOD }; HistGroupPK group{}; HistGroupNodePK index{}; ServerCnnctnNK slug;
		{
			rl _{ _mutex };
			auto p = _nodes.find( node );
			if( p==_nodes.end() || p->second.empty() )
				co_return;
			let& head = p->second.front();
			written = head.Written;
			group = head.Group;
			index = head.Index;
			slug = head.Slug;
		}
		UA_StatusCode result{ UA_STATUSCODE_GOOD };
		string detail;
		try{
			auto host = HistHost::Instance().Find( group );
			if( !host ){
				result = UA_STATUSCODE_BADNODEIDUNKNOWN;
				detail = "its group isn't live";
			}
			else{
				ConnectAwait connect{ string{slug}, HistHost::Instance().Collector(slug) };//an lvalue:  ConnectAwait is neither copied nor moved.
				auto client = co_await Any( connect );
				if( let id = host->ServerId(index); !id ){
					result = UA_STATUSCODE_BADNOCOMMUNICATION;
					detail = "the source hasn't given the node's id";
				}
				else{
					WriteAwait write{ *id, move(written), client };
					co_await Any( write );
				}
			}
		}
		catch( const UAException& e ){
			result = (UA_StatusCode)e.Code();
		}
		catch( const runtime_error& e ){
			result = UA_STATUSCODE_BADINTERNALERROR;
			detail = e.what();
		}
		Answer( move(node), result, move(detail) );
	}
	//On the endpoint's thread:  the operation's result set, unless open62541 cancelled it meanwhile - heard here, or found by the set,
	//Bad_NotFound - then the audit line, the queue's front gone and the node's next write forwarded.  A cancel between the key's read
	//and the set is open62541's own window:  the cancel runs under the service lock, which the set takes, so none closes it from here.
	α ServedWrites::Answer( NodeId node, UA_StatusCode result, string detail )ι->void{
		_post( [self=shared_from_this(), node=move(node), result, detail=move(detail)]( UA_Server& ua ){
			const UA_DataValue* key{}; UserPK user{}; ServerCnnctnNK slug;
			{
				rl _{ self->_mutex };
				auto p = self->_nodes.find( node );
				if( p==self->_nodes.end() || p->second.empty() )
					return;
				let& head = p->second.front();
				key = head.Key; user = head.User; slug = head.Slug;
			}
			let sc = key ? UA_Server_setAsyncWriteResult( &ua, key, result ) : UA_STATUSCODE_BADNOTFOUND;
			let who = Ƒ( "Write-through by '{}' to {} on '{}'", AppClient()->Acl()->UserName(user), node.ToString(), slug );
			let answer = Ƒ( "{}{}", result ? UAException::Message(result) : string{"Good"}, detail.empty() ? string{} : Ƒ(" - {}", detail) );
			if( sc==UA_STATUSCODE_BADNOTFOUND ){
				INFO( "{} answered late, the client told Bad_Timeout:  {}.", who, answer );
			}
			else if( sc ){
				WARN( "{} couldn't be answered ({}):  {}.", who, UAException::Message(sc), answer );
			}
			else{
				INFO( "{}:  {}.", who, answer );
			}
			bool next{};
			{
				ul _{ self->_mutex };
				auto p = self->_nodes.find( node );
				if( p==self->_nodes.end() || p->second.empty() )
					return;
				p->second.pop_front();
				next = !p->second.empty();
				if( !next )
					self->_nodes.erase( p );
			}
			if( next )
				Post( [self, node]{ self->Forward( node ); } );
		} );
	}
}