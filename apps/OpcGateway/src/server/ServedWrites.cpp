#include "ServedWrites.h"
#include <jde/fwk/process/execution.h>
#include <jde/access/Authorize.h>
#include "../async/ConnectAwait.h"
#include "../async/WriteAwait.h"
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "ServedValues.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };
	Ω audit( UserPK user, const NodeId& node, const ServerCnnctnNK& slug )ι->string{
		let acl = AppClient()->Acl();//null for a client that never authorizes, as HistHost::Writer allows.
		return Ƒ( "Write-through by '{}' to {} on '{}'", acl ? acl->UserName(user) : std::to_string(user.Value), node.ToString(), slug );
	}
	Ω answered( UA_StatusCode result, sv detail )ι->string{ return Ƒ( "{}{}", result ? UAException::Message(result) : string{"Good"}, detail.empty() ? string{} : Ƒ(" - {}", detail) ); }

	α ServedWrites::Write( const UA_NodeId& node, UserPK user, const UA_NumericRange* range, const UA_DataValue& value )ι->UA_StatusCode{
		if( range )
			return UA_STATUSCODE_BADWRITENOTSUPPORTED;
		Value written{ value };
		let serving = _values->Served( node, written.hasValue ? &written.value : nullptr );
		if( !serving )
			return UA_STATUSCODE_BADNODEIDUNKNOWN;
		if( serving->Unmapped )
			return UA_STATUSCODE_BADNODEIDINVALID;
		bool first{};
		{
			ul _{ _mutex };
			if( _stopped )
				return UA_STATUSCODE_BADSHUTDOWN;
			auto& queue = _nodes[NodeId{node}];
			first = queue.empty();
			_keys.insert_or_assign( &value, &queue.emplace_back(Queued{&value, move(written), user, serving->Group, serving->Index, serving->Slug}) );
		}
		if( first )
			Post( [self=shared_from_this(), id=NodeId{node}]{ self->Forward( id ); } );
		return UA_STATUSCODE_GOODCOMPLETESASYNCHRONOUSLY;
	}
	α ServedWrites::Start( UA_Server& server )ι->void{
		ul _{ _serverMutex };
		_server = &server;
	}
	α ServedWrites::Stop()ι->void{
		{
			ul _{ _serverMutex };
			_server = nullptr;
		}
		{
			ul _{ _mutex };
			_stopped = true;
		}
		SetReady( nullptr, "the endpoint stopped" );
	}
	α ServedWrites::Cancelled( const void* value )ι->void{
		ul _{ _mutex };
		if( auto p = _keys.find(static_cast<const UA_DataValue*>(value)); p!=_keys.end() ){
			p->second->Key = nullptr;
			_keys.erase( p );
		}
	}
	α ServedWrites::Pop( std::deque<Queued>& queue )ι->void{
		if( let key = queue.front().Key )
			_keys.erase( key );
		queue.pop_front();
	}
	//The front of the node's queue to its source:  the collector's client, as the collector itself gets it, and the node by the index
	//the source gave it.  A write cancelled before it's sent, here or during the connect, is dropped:  its client has its answer.
	//Nothing here runs on the server's thread, so a wait for the source holds no service lock.
	α ServedWrites::Forward( NodeId node )ι->VoidTask{
		auto self = shared_from_this();
		auto head = Drop( node, true );
		if( !head )
			co_return;
		UA_StatusCode result{ UA_STATUSCODE_GOOD };
		string detail;
		try{
			auto host = HistHost::Instance().Find( head->Group );
			if( !host ){
				result = UA_STATUSCODE_BADNODEIDUNKNOWN;
				detail = "its group isn't live";
			}
			else if( let id = host->ServerId(head->Index); !id ){//before the connect, which it doesn't need.
				result = UA_STATUSCODE_BADNOCOMMUNICATION;
				detail = "the source hasn't given the node's id";
			}
			else{
				ConnectAwait connect{ string{head->Slug}, HistHost::Instance().Collector(head->Slug) };//an lvalue:  ConnectAwait is neither copied nor moved.
				auto client = co_await Any( connect );
				if( !Live(node) ){//cancelled during the connect.
					if( Drop(node) )
						Post( [self, node]{ self->Forward( node ); } );
					co_return;
				}
				WriteAwait write{ *id, move(head->Written), client };
				co_await Any( write );
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
	//Queued for the server's thread, where open62541 cancels and frees operations too:  there no cancel comes between the key's read and
	//the set, and an operation the key names is the write's, not one made at a freed one's address.  Once stopped, never set:  the
	//client has had open62541's Bad_Shutdown.
	α ServedWrites::Answer( NodeId node, UA_StatusCode result, string detail )ι->void{
		bool stopped{}, schedule{};
		{
			ul _{ _mutex };
			stopped = _stopped;
			if( !stopped ){
				schedule = _ready.empty();
				_ready.push_back( Ready{node, result, move(detail)} );
			}
		}
		if( stopped )
			Set( nullptr, node, result, detail, "the endpoint stopped" );
		else if( schedule )
			Schedule();
	}
	//Under _serverMutex, so the server is there for the call;  without one, Stop has the answers.  UA_Server_addTimedCallback takes the
	//service lock, which nothing holding _serverMutex is under.
	α ServedWrites::Schedule()ι->void{
		UA_StatusCode sc{};
		{
			ul _{ _serverMutex };
			if( !_server )
				return;
			sc = UA_Server_addTimedCallback( _server, Answered, this, UA_DateTime_nowMonotonic(), nullptr );
			if( !sc ){
				auto el = UA_Server_getConfig( _server )->eventLoop;
				el->cancel( el );//its wait, up to 500 ms between iterations.
			}
		}
		if( sc ){
			ERR( "Write-through answers couldn't be scheduled on the endpoint's thread:  {}.", UAException::Message(sc) );
			SetReady( nullptr, Ƒ("the endpoint's thread couldn't take it ({})", UAException::Message(sc)) );
		}
	}
	//`self` outlives the server:  GatewayServer holds it past its server, which frees a timer it never ran.
	α ServedWrites::Answered( UA_Server* server, void* self )ι->void{ static_cast<ServedWrites*>( self )->SetReady( server, {} ); }
	α ServedWrites::SetReady( UA_Server* ua, sv unset )ι->void{
		vector<Ready> ready;
		{
			ul _{ _mutex };
			ready = std::exchange( _ready, {} );
		}
		for( let& r : ready )
			Set( ua, r.Node, r.Result, r.Detail, unset );
	}
	α ServedWrites::Set( UA_Server* ua, const NodeId& node, UA_StatusCode result, sv detail, sv unset )ι->void{
		const UA_DataValue* key{}; UserPK user{}; ServerCnnctnNK slug;
		bool next{};
		{
			ul _{ _mutex };
			auto p = _nodes.find( node );
			if( p==_nodes.end() || p->second.empty() )
				return;
			auto& queue = p->second;
			key = queue.front().Key; user = queue.front().User; slug = move( queue.front().Slug );
			Pop( queue );
			next = queue.size();
			if( !next )
				_nodes.erase( p );
		}
		let who = audit( user, node, slug );
		let answer = answered( result, detail );
		if( !ua ){
			INFO( "{} answered after {}, never set:  {}.", who, unset, answer );
		}
		else if( let sc = key ? UA_Server_setAsyncWriteResult( ua, key, result ) : UA_STATUSCODE_BADNOTFOUND; sc==UA_STATUSCODE_BADNOTFOUND ){
			INFO( "{} answered after open62541 cancelled it - a timeout, a client's Cancel, the queue's limit or the shutdown:  {}.", who, answer );//its cancel names no reason.
		}
		else if( sc ){
			WARN( "{} couldn't be answered ({}):  {}.", who, UAException::Message(sc), answer );
		}
		else{
			INFO( "{}:  {}.", who, answer );
		}
		if( next )
			Post( [self=shared_from_this(), node]{ self->Forward( node ); } );
	}
	//Off the node's queue's front, each write open62541 cancelled before it was sent:  open62541 answered its client.  Once stopped,
	//every write not yet sent.  An empty queue is erased, so the node's next write forwards at once.  The front left is returned, with
	//`take` its value moved out to forward, in the one lock;  without, its value stays.
	α ServedWrites::Drop( const NodeId& node, bool take )ι->optional<Queued>{
		vector<std::pair<UserPK,ServerCnnctnNK>> dropped;
		optional<Queued> front;
		{
			ul _{ _mutex };
			auto p = _nodes.find( node );
			if( p==_nodes.end() )
				return nullopt;
			auto& queue = p->second;
			for( ; queue.size() && (_stopped || !queue.front().Key); Pop(queue) )
				dropped.emplace_back( queue.front().User, queue.front().Slug );
			if( queue.empty() )
				_nodes.erase( p );
			else{
				auto& f = queue.front();
				front = Queued{ f.Key, take ? move(f.Written) : Value{UA_STATUSCODE_GOOD}, f.User, f.Group, f.Index, f.Slug };
			}
		}
		for( let& [user, slug] : dropped )
			INFO( "{} dropped:  cancelled before it was sent.", audit(user, node, slug) );
		return front;
	}
	α ServedWrites::Live( const NodeId& node )Ι->bool{
		rl _{ _mutex };
		auto p = _nodes.find( node );
		return !_stopped && p!=_nodes.end() && p->second.size() && p->second.front().Key;
	}
}