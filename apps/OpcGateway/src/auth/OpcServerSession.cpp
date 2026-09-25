#include "OpcServerSession.h"
#include <jde/web/server/Sessions.h>
#define let const auto

namespace Jde::Opc::Gateway{
	flat_map<SessionPK,flat_map<ServerCnnctnNK,Credential>> _sessions; shared_mutex _sessionsMutex;
	//Nothing but an explicit /logout ever removed an entry, so this was a high-water mark:  every web session that had touched
	//a slug since startup, counted by opcSessions long after the session itself was gone.  (opcConnections never drifted the
	//same way - its _clients drain on the idle ttl.)  Web::Server's store is the authority and trims itself on expiry, so an id
	//it no longer knows - or knows only as expired, which UpdateExpiration will not revive - has no session behind it.  Called
	//under the caller's lock at the two growth points and on the read; the map is tiny, so an O(n) sweep is cheaper than a timer.
	Ω isLive( SessionPK sessionId )ι->bool{
		let session = Web::Server::Sessions::Find( sessionId );
		return session && session->Expiration>steady_clock::now();
	}
	//A session someone has signed in to.  A login page's request always has a live session - the web server gives every
	//request without an Authorization header a fresh one (Sessions::UpsertAwait) - but an anonymous one, no user behind it.
	Ω isSignedIn( SessionPK sessionId )ι->bool{
		let session = Web::Server::Sessions::Find( sessionId );
		return session && session->Expiration>steady_clock::now() && session->UserPK;
	}
	Ω pruneDeadSessions( ul& )ι->void{
		for( auto p = _sessions.begin(); p!=_sessions.end(); ){
			if( isLive(p->first) )
				++p;
			else{
				TRACET( ELogTags::Sessions, "Session {} gone - dropping its {} opc credential(s).", hex(p->first), p->second.size() );
				p = _sessions.erase( p );
			}
		}
	}

	α Credential::operator==( const Credential& other )Ι->bool{
		bool equal{ Type() == other.Type() };
		if( equal ){
			switch( Type() ){
				using enum ETokenType;
				case None: case Anonymous: equal = true; break;
				case IssuedToken: equal = get<Gateway::Token>( _value ) == get<Gateway::Token>( other._value ); break;
				case Username: equal = get<User>( _value ) == get<User>( other._value ); break;
				case Certificate: equal = get<Crypto::PublicKey>( _value ) == get<Crypto::PublicKey>( other._value ); break;
			}
		}
		return equal;
	}
	α Credential::operator<( const Credential& other )Ι->bool{
		optional<bool> less{ Type() == other.Type() ? optional<bool>{} : Type() < other.Type() };
		if( !less ){
			switch( Type() ){
				using enum ETokenType;
				case IssuedToken: less = get<Gateway::Token>( _value ) < get<Gateway::Token>( other._value ); break;
				case Username: less = get<User>( _value ) < get<User>( other._value ); break;
				case Certificate: less = get<Crypto::PublicKey>( _value ) < get<Crypto::PublicKey>( other._value ); break;
				default: less = false; break; //case None: case Anonymous: less = false; break;
			}
		}
		return *less;
	}

	α Credential::Type()Ι->ETokenType{
		ETokenType type{};
		switch( _value.index() ){
			using enum ETokenType;
			case 0: type = Anonymous; break;
			case 1: type = IssuedToken; break;
			case 2: type = Username; break;
			case 3: type = Certificate; break;
		}
		return type;
	}
	α Credential::LoginName()Ι->str{ ASSERT(Type()==ETokenType::Username); return get<User>(_value).LoginName; }
	α Credential::Password()Ι->str{ ASSERT(Type()==ETokenType::Username); return get<User>(_value).Password; }
	α Credential::ToString()Ι->string{
		if( !_display.empty() )
			return _display;
		switch( Type() ){
			using enum ETokenType;
			case None: case Anonymous: _display = "anonymous"; break;
			case Username: _display = Ƒ( "user: {}", LoginName() ); break;
			case IssuedToken: _display = Ƒ( "token: {:x}", (uint32)std::hash<string>{}(get<Gateway::Token>(_value)) ); break;
			case Certificate: _display = Ƒ( "cert: {:x}", get<Crypto::PublicKey>(_value).Hash32() ); break;
		}
		return _display;
	}
}
namespace Jde::Opc{
	α Gateway::AddSession( SessionPK sessionId, ServerCnnctnNK opcNK, Credential credential )ι->void{
		ul l{ _sessionsMutex };
		pruneDeadSessions( l ); //the map only grows here and in AuthCache - sweeping both bounds it without a timer, for a gateway nobody is watching the Connections list of.
		auto& sessionConnections = _sessions[sessionId];
		sessionConnections[opcNK] = move( credential );
	}

	α Gateway::AuthCache( const Credential& cred, const ServerCnnctnNK& opcNK, SessionPK sessionId )ι->optional<bool>{
		optional<bool> authenticated;
		//A hit stores the credential under the caller's session and hands that session back (PasswordAwait::await_resume) - which
		//signs no one in:  a login page's session is anonymous, and a second sign-in of a cached user stayed anonymous, under
		//enforcement locked out until a restart emptied the cache (reviews/install-issues.md #47).  Only a signed-in session is
		//vouched for - a re-auth, or a second connection;  anything else takes the full path, which mints a session with the
		//user (PasswordAwait::AddSession), on the pooled client its credential finds.
		if( !isSignedIn(sessionId) )
			return authenticated;
		Jde::UserPK matchedUser; //by value: the reference into _sessions is dead once the insert below runs.
		ul l{ _sessionsMutex };
		for( let& [_,sessionConnections] : _sessions ){
			auto p = sessionConnections.find(opcNK);
			if( p==sessionConnections.end() )
				continue;
			auto& existingCred = p->second;
			if( existingCred.Type()!=cred.Type() )
				continue;
			if( existingCred.IsUser() ){
				if( existingCred.LoginName()==cred.LoginName() )
					authenticated = existingCred.Password()==cred.Password();
			}
			else
				authenticated = existingCred==cred;
			if( authenticated.has_value() ){
				matchedUser = existingCred.UserPK();
				break;
			}
		};
		if( authenticated && *authenticated ){
			auto stored = cred;
			stored.SetUserPK( matchedUser ); //the incoming credential never carries the user - the matched one does.
			_sessions[sessionId][opcNK] = move( stored );
		}
		pruneDeadSessions( l ); //after the match, not before:  whether a dead session's credential may still vouch for a new one is the AuthCache design (review #14), untouched here.
		return authenticated;
	}

	α Gateway::Logout( SessionPK sessionId )ι->void{
		ul _{ _sessionsMutex };
		let erased = _sessions.erase( sessionId );
		if( erased ){
			TRACET( ELogTags::App, "Session {} erased.", hex(sessionId) );
		}else
			TRACET( ELogTags::App, "Session {} not found.", hex(sessionId) );
	}
	α Gateway::GetCredential( SessionPK sessionId, str opcId )ι->optional<Credential>{
		optional<Credential> cred;
		sl _{ _sessionsMutex };
		if( auto p = _sessions.find(sessionId); p!=_sessions.end() ){
			if( auto creds = p->second.find(opcId); creds!=p->second.end() ){
				cred = creds->second;
			}
		}
		return cred;
	}
	α Gateway::SessionCounts()ι->vector<SessionCount>{
		flat_map<tuple<ServerCnnctnNK,ETokenType,Jde::UserPK>,uint32> counts;
		{
			ul l{ _sessionsMutex }; //unique, not shared:  the sweep below erases.
			pruneDeadSessions( l );
			for( let& [_,sessionConnections] : _sessions ){ //at most one credential per opcNK per session - no per-session dedup needed.
				for( let& [opcNK,cred] : sessionConnections )
					++counts[ {opcNK, cred.Type(), cred.UserPK()} ];
			}
		}
		vector<SessionCount> y; y.reserve( counts.size() );
		for( let& [key,count] : counts )
			y.emplace_back( get<0>(key), get<1>(key), get<2>(key), count );
		return y;
	}
}
namespace Jde::Opc{
	α Gateway::ToTokenType( UA_UserTokenType ua )ι->ETokenType{
		switch( ua ){
			using enum ETokenType;
			case UA_USERTOKENTYPE_ANONYMOUS: return Anonymous;
			case UA_USERTOKENTYPE_USERNAME: return Username;
			case UA_USERTOKENTYPE_CERTIFICATE: return Certificate;
			case UA_USERTOKENTYPE_ISSUEDTOKEN: return IssuedToken;
			default: return None;
		}
	}
}