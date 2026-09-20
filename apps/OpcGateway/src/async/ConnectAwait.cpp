#include "ConnectAwait.h"
#include "../UAClient.h"
#include "../GatewayAppClient.h"
#include <jde/access/Authorize.h>
#include "jde/fwk/exceptions/Exception.h"
#include <stdexcept>

#define let const auto

namespace Jde::Opc::Gateway{
	flat_map<ServerCnnctnNK,flat_map<Credential,vector<ConnectAwait::Handle>>> _requests; mutex _requestMutex;
	α SessionCredential( SessionPK sessionId, UserPK user, str opc )ι->optional<Credential>{
		optional<Credential> cred;
		if( sessionId ){
			cred = GetCredential( sessionId, opc );
			if( !cred && user ){ //if user/pwd would have cred, otherwise use jwt
				cred = Credential{ Ƒ("{:x}", sessionId) };
				cred->SetUserPK( user );//opcSessions joins user{} on this; a stored credential already carries it.
			}
		}
		return cred;
	}
	ConnectAwait::ConnectAwait( ServerCnnctnNK opc, SessionPK sessionId, UserPK user, SL sl )ι:
		base{sl},
		_opcSlug{ move(opc) },
		_cred{ SessionCredential(sessionId, user, _opcSlug).value_or(Credential{}) },
		_sessionId{ sessionId }
	{}
	α ConnectAwait::await_resume()ε->sp<UAClient>{
		auto client = Promise() ? base::await_resume() : _result;
		//A jwt-backed web session's connect never goes through PasswordAwait, so record it here or opcSessions never sees it. None = anonymous/no user - not a session worth counting.
		if( client && _sessionId && _cred.Type()!=ETokenType::None && !GetCredential(_sessionId, _opcSlug) )
			AddSession( _sessionId, _opcSlug, _cred );
		return client;
	}
	ConnectAwait::ConnectAwait( ServerCnnctnNK opc, const Web::Server::SessionInfo& session, SL sl )ι:
		ConnectAwait{ move(opc), session.SessionId, session.UserPK, sl }
	{}

	α ConnectAwait::EraseRequests( str opcNK, Credential cred, lg& )ι->vector<ConnectAwait::Handle>{
		vector<ConnectAwait::Handle> handles;
		if( auto p = _requests.find(opcNK); p != _requests.end() ){
			if( auto q = p->second.find(cred); q != p->second.end() ){
				handles = move( q->second );
				p->second.erase( q );
				if( p->second.empty() )
					_requests.erase( p );
			}
		}
		return handles;
	}

	//"" names the default connection (ServerCnnctnAwait: is_default) - the login page's username with no DOMAIN\
	//(reviews/install-issues.md #1).  Resolved before anything is keyed:  _requests, UAClient::Find and the activation's
	//Resume all go by the slug the client carries, so a waiter registered under "" was never woken - the session activated
	//on the server and the login timed out - and a second login would have built a second client for the same slug.
	α ConnectAwait::ResolveDefault()ι->TAwait<vector<ServerCnnctn>>::Task{
		try{
			auto servers = co_await ServerCnnctnAwait{ string{} };
			THROW_IFX( servers.empty(), Exception( _sl, {EHttpStatus::NotFound}, "No default OPC server connection.") );
			_opcSlug = servers.front().Slug;
			Start();
		}
		catch( Exception& e ){
			base::ResumeExp( move(e) );
		}
	}
	α ConnectAwait::Suspend()ι->void{
		//install-issues #24: a web session with no user and no stored credential resolves to an empty Credential, whose Type()
		//is Anonymous.  Opening a client on it browses the OPC server anonymously - which the bundled OpcServer does not offer
		//(certificate/token only), so a signed-out page's restored tab drew `No suitable endpoint found`/`BadIdentityTokenRejected`
		//twice per load and left the connection reading Error.  Anonymous access is allowed while the connection resource is
		//unenforced (open access is the point of leaving it so), and refused 401 once an admin enforces gateway/serverConnections,
		//where an anonymous session holds no grant - before a client is opened.  Delegated to Authorize::Test, which passes an
		//unenforced resource (FindActiveResourcePK null) and throws Unauthorized for an unknown user on an enforced one.
		//
		//install-issues #39:  `&& _cred.Type()==ETokenType::Anonymous` used to stand here too, and it left the Enforced switch on
		//gateway/serverConnections gating the *table* - listing and editing connections - but not opening a session on one.  Any
		//authenticated user reached any connection by slug.  Measured on a live hub with the resource enforced and a user holding
		//no grant anywhere:  `serverConnections{}` refused with "does not have 'Read' access", while a node read, a full browse and
		//an `updateVariable` write on that same connection all went through.  Every web session consults the authorizer now, and
		//being authenticated is no longer a way past a resource the admin chose to enforce.
		//
		//Still only a web session:  the ctor that carries an explicit Credential sets no _sessionId - the soak harness, the tests,
		//anything internal - and has no user to test.  Unenforced, which is how this ships and how every walk ran, Test passes for
		//everyone and nothing changes.
		if( _sessionId ){
			if( auto acl = AppClient()->Acl(); acl ){
				try{
					acl->Test( "gateway", "serverConnections", Access::ERights::Read, _cred.UserPK(), _sl );
				}
				catch( Exception& e ){ base::ResumeExp( move(e) ); return; }
			}
		}
		if( _opcSlug.empty() )
			ResolveDefault();
		else
			Start();
	}
	α ConnectAwait::Start()ι->void{
		if( auto client = UAClient::Find(_opcSlug, _cred); client ){
			TRACET( ((ELogTags)EOpcLogTags::Opc) | ELogTags::Access, "[{}]Found client for cred: {}", hex(client->Handle()), _cred.ToString() );
			base::Resume( move(client) );
			return;
		}
		sp<UAClient> client;
		bool create = false;
		{
			lg l{ _requestMutex };
			//Re-check under _requestMutex: a client can activate and drain _requests between the unlocked Find above and here. StateCallback inserts into _clients *before* Posting the drain (which needs this mutex), so a locked Find cannot miss a client whose drain already ran. Without this, a stale miss registers as the new first handle → duplicate Create() → the second client fails StateCallback's ASSERT(inserted) and is silently dropped.
			if( client = UAClient::Find(_opcSlug, _cred); !client ){
				auto opcHandles = _requests.try_emplace( _opcSlug ).first;
				auto credHandles = opcHandles->second.try_emplace( _cred ).first;
				credHandles->second.push_back( _h );
				create = credHandles->second.size()==1;
			}
		}
		if( client )
			base::Resume( move(client) );
		else if( create )
			Create();
	}
	α ConnectAwait::Create()ι->TAwait<vector<ServerCnnctn>>::Task{
		try{
			auto servers = co_await ServerCnnctnAwait{ _opcSlug };
			THROW_IFX( servers.empty(), Exception( _sl, {EHttpStatus::NotFound}, "Could not find connection:  '{}'", _opcSlug) );
			auto client = ms<UAClient>( move(servers.front()), _cred );
			client->Connect();
		}
		catch( Exception& e ){
			vector<ConnectAwait::Handle> handles;
			{
				lg l{ _requestMutex };
				handles = EraseRequests( _opcSlug, _cred, l );
			}
			//Copy per waiter, never Move:  Move() moves the payload *out of* e, so the first "clone" hollowed it and every
			//waiter after it - the last, which takes e itself, included - got the format string with its _args gone
			//("Could not find connection:  '{}'").  Only the last may consume e.  The UAClientException cast keeps the UA status and UserMessage a connect failure carries;
			//any other subclass copies as its Exception base, which a polymorphic copy would need a Copy() virtual
			//mirroring Move() across the hierarchy to avoid.
			auto copy = [&e]()->up<Exception>{
				if( let p = dynamic_cast<const UAClientException*>(&e); p )
					return mu<UAClientException>( *p );
				return mu<Exception>( e );
			};
			for( uint i=0; i<handles.size(); ++i ){
				auto& h = handles[i];
				if( i==handles.size()-1 )
					h.promise().ResumeExp( move(e), h );
				else
					h.promise().ResumeExp( move(*copy()), h );
			}
		}
	}
	α ConnectAwait::Resume( str opcNK, Credential cred, function<void(ConnectAwait::Handle)> resume )ι->void{
		vector<ConnectAwait::Handle> handles;
		{
			lg l{ _requestMutex };
			handles = EraseRequests( opcNK, cred, l );
		}
		for( auto h : handles )
			resume( h );
	}

	α ConnectAwait::Resume( sp<UAClient> client )ι->void{
		Resume( client->Slug(), client->Credential, [client](ConnectAwait::Handle h){ h.promise().Resume(sp<UAClient>(client), h); } );
	}
	α ConnectAwait::Resume( str opcNK, Credential cred, const UAClientException&& e )ι->void{
		Resume( opcNK, cred, [e2=move(e)](ConnectAwait::Handle h)mutable{ h.promise().ResumeExp(UAClientException{e2}, h); } );
	}
}