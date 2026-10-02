#include <chrono>
#include <jde/web/server/Sessions.h>

#include <jde/web/server/HttpRequest.h>
#include <jde/fwk/str.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/web/server/auth/JwtLoginAwait.h>
#include <jde/app/IApp.h>
#include "ServerImpl.h"
#include "jde/fwk/exceptions/Exception.h"
#include "jde/fwk/usings.h"

#define let const auto
namespace Jde::Web::Server{
	constexpr ELogTags _tags{ ELogTags::Sessions };
	steady_clock::duration _restExpirationDuration{};
	α Sessions::RestSessionTimeout()ι->steady_clock::duration{
		if( _restExpirationDuration==steady_clock::duration::zero() )
			_restExpirationDuration = Settings::FindDuration( "/http/timeout" ).value_or( 30min );
		return _restExpirationDuration;
	}
	steady_clock::duration _sockExpirationDuration{};
	Ω sockExpirationDuration(){
		if( _sockExpirationDuration==steady_clock::duration::zero() )
			_sockExpirationDuration = Settings::FindDuration( "/http/socketTimeout" ).value_or( 24h );
		return _sockExpirationDuration;
	}
	steady_clock::time_point _lastTrim{ steady_clock::now() };

	concurrent_flat_map<SessionPK,sp<SessionInfo>> _sessions;
	Ω upsert( sp<SessionInfo>& info )ι->void{
		if( _sessions.emplace_or_visit(info->SessionId, info, [hasSocket=info->HasSocket](auto& existing){
			existing.second->HasSocket|=hasSocket;
			existing.second->Expiration=existing.second->NewExpiration();
		}) ){
			TRACE( "Session added: id: {:x}, userPK: {}, endpoint: '{}', expiration: '{}'", info->SessionId, info->UserPK.Value, info->UserEndpoint, ToIsoString<seconds>(info->Expiration) );
		}
	}

	α GetNewSessionId()ι->SessionPK{
		auto sessionId{ Crypto::Random<uint32_t>() };
		while( _sessions.contains(sessionId) )
			sessionId = Crypto::Random<uint32_t>();
		return sessionId;
	}

namespace	Sessions{
	α Internal::CreateSession( UserPK userPK, str endpoint, bool isSocket, bool add )ι->sp<SessionInfo>{
		auto y = sp<SessionInfo>( new SessionInfo{GetNewSessionId(), endpoint, isSocket} );
		y->UserPK = userPK;
		if( add )
			_sessions.emplace( y->SessionId, y );
		return y;
	}
}
	α Sessions::Add( UserPK userPK, string&& endpoint, bool isSocket )ι->sp<SessionInfo>{
		auto newSession = Internal::CreateSession( userPK, move(endpoint), isSocket, false );
		upsert( newSession );
		return newSession;
	}

	α Sessions::Remove( SessionPK sessionId )ι->bool{
		let y = _sessions.erase( sessionId );
		//#5: erasing the entry is not revocation on its own.  Every socket query & subscription executes under the
		//IWebsocketSession's own sp<SessionInfo>, so an open connection kept running as the removed user until its own timeout
		//(/http/socketTimeout, a day) - logout & purgeSession were not effective against it.
		if( let sockets = Web::Server::Internal::CloseSocketSessions(sessionId); sockets )
			DBG( "[{:x}]Session removed - closing {} socket session(s) bound to it.", sessionId, sockets );
		return y;
	}

	α Sessions::Find( SessionPK sessionId )ι->sp<SessionInfo>{
		sp<SessionInfo> y;
		_sessions.cvisit( sessionId, [&y](auto& kv){y = kv.second;} );
		return y;
	}

	α Sessions::Extend( SessionPK sessionId )ι->sp<SessionInfo>{
		sp<SessionInfo> y;
		_sessions.visit( sessionId, [&y]( auto& kv ){
			y = kv.second;
			if( y->Expiration>steady_clock::now() )
				y->Expiration = y->NewExpiration();
		} );
		return y;
	}

	α Sessions::Get()ι->vector<sp<SessionInfo>>{
		vector<sp<SessionInfo>> y;
		_sessions.cvisit_all( [&y](auto& kv){y.emplace_back(kv.second);} );
		return y;
	}
	α Sessions::Size()ι->uint{ return _sessions.size(); }

	SessionInfo::SessionInfo( SessionPK sessionPK, str userEndpoint, bool hasSocket )ι:
		SessionId{ sessionPK },
		UserEndpoint{ userEndpoint },
		HasSocket{ hasSocket },
		Expiration{ NewExpiration() },
		LastServerUpdate{ steady_clock::now() },
		IsInitialRequest{ true }
	{}

	SessionInfo::SessionInfo( SessionPK sessionPK, steady_clock::time_point expiration, Jde::UserPK userPK, str userEndpointAddress, bool hasSocket )ι:
		QL::IQLSession{ userPK },
		SessionId{ sessionPK },
		UserEndpoint{ userEndpointAddress },
		HasSocket{ hasSocket },
		Expiration{ expiration },
		LastServerUpdate{ steady_clock::now() }
	{}

	α SessionInfo::NewExpiration()Ι->steady_clock::time_point{
		return steady_clock::now()+( HasSocket ? sockExpirationDuration() : Sessions::RestSessionTimeout() );
	}

	Ω normalizedAddress( str s )ι->string{//::ffff:127.0.0.1 (an IPv4-mapped IPv6 address) and 127.0.0.1 name the same peer - strip the prefix so they compare equal.
		constexpr sv mapped{ "::ffff:" };
		return s.starts_with(mapped) && s.find('.')!=string::npos ? s.substr(mapped.size()) : s;
	}
	Ω sameEndpoint( str a, str b )ι->bool{//is this the address the session was minted for?
		let na = normalizedAddress( a ); let nb = normalizedAddress( b );
		if( na==nb )
			return true;
		boost::system::error_code eca, ecb;
		let aa = net::ip::make_address( na, eca ); let bb = net::ip::make_address( nb, ecb );
		return !eca && !ecb && aa.is_loopback() && bb.is_loopback();//127.0.0.1 vs ::1 - same host, so not a trust boundary; every other address requires an exact match.
	}
	α UpdateExpiration( SessionPK sessionId, str userEndpoint, bool socket )ε->sp<SessionInfo>{
		sp<SessionInfo> info;
		_sessions.visit( sessionId, [&info, sessionId, userEndpoint, socket](auto& kv){
			sp<SessionInfo> existing = kv.second;
			let& existingAddress = existing->UserEndpoint;
			if( !sameEndpoint(existingAddress, userEndpoint) ){//proof of possession - a session id is only a credential from the address it was minted for; otherwise a guessed/leaked id is a full takeover.  Return null so the caller reports the generic "Session not found", not an existence oracle for a bound-elsewhere id.
				WARNT( ELogTags::HttpServerRead, "[{:x}]Session endpoint '{}' does not match request endpoint '{}' - denying.", sessionId, existingAddress, userEndpoint );
				return;
			}
			auto& existingExpiration = existing->Expiration;
			if( existingExpiration>steady_clock::now() ){
				existing->HasSocket |= socket;//sticky - a socket connecting on a rest session promotes it to the socket timeout, later rest requests must not demote it.
				existingExpiration = existing->NewExpiration();
				info = existing;
			}
			else
				TRACET( ELogTags::HttpServerRead, "[{:x}]Session expired:  '{}'", sessionId, ToIsoString<seconds>(existingExpiration) );
		} );
		if( _lastTrim<steady_clock::now()-Sessions::RestSessionTimeout() ){
			_sessions.erase_if( [](auto& kv){return kv.second->Expiration<steady_clock::now();} );
			_lastTrim = steady_clock::now();
		}
		return info;
	}

namespace Sessions{
	α UpsertAwait::Suspend()ι->void{
		if( 	_authorization.starts_with("Bearer ") ){
			try{
				FromJwt( _authorization.substr(7) );
			}
			catch( runtime_error& e ){
				ResumeExp( move(e) );
			}
		}
		else if( _authorization.size() )
			FromSessionId();
		else
			CreateSession();
	}
	α UpsertAwait::FromJwt( str jwt )ι->TTask<UserPK>{
		try{
			auto userPK = co_await JwtLoginAwait{ Web::Jwt{jwt}, _endpoint, _appClient };
			CreateSession( userPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α UpsertAwait::CreateSession( UserPK userPK )ι->void{
		auto info = Sessions::Internal::CreateSession( userPK, _endpoint, _socket, false );
		upsert( info );
		Resume( move(info) );
	}
	α UpsertAwait::FromSessionId()ι->TTask<Web::FromServer::SessionInfo>{
		try{
			optional<SessionPK> sessionId = Str::TryTo<SessionPK,16>( _authorization );
			THROW_IF( !sessionId, "Invalid sessionId:  '{}'.", _authorization );
			auto info = UpdateExpiration( *sessionId, _endpoint, _socket );
			if( !info ){
				up<TAwait<Web::FromServer::SessionInfo>> await{ !_appClient || _appClient->IsLocal() ? nullptr : _appClient->SessionInfoAwait(*sessionId, _sl) };//3rd party, eg AppServer
				if( !await ){  //no 3rd party
					if( _throw )
						throw Exception( SRCE_CUR, ELogLevel::Debug, "[{}]Session not found.", Ƒ("{:x}", *sessionId) );
					else{
						_h.resume();
						co_return;
					}
				}
				bool denied{}; string remoteEndpoint;
				try{
					Web::FromServer::SessionInfo proto{ co_await *await };
					remoteEndpoint = proto.user_endpoint();
					//#4: the 3rd party answers a bare Sessions::Find with no endpoint check of its own, so the proof of possession
					//UpdateExpiration enforces locally has to be repeated on its answer.  Without it, overwriting UserEndpoint below
					//rebinds a sniffed/guessed id to whoever presented it, and every later request hits the rebound local entry.
					denied = !sameEndpoint( remoteEndpoint, _endpoint );
					if( !denied ){
						let expiration = Chrono::ToClock<steady_clock,Clock>( Protobuf::ToTimePoint(proto.expiration()) );
						info = ms<SessionInfo>( *sessionId, expiration, UserPK{proto.user_pk()}, remoteEndpoint, proto.has_socket() );
						info->UserEndpoint = _endpoint;//normalize - sameEndpoint accepts ::ffff: mapped & loopback variants, store what later requests compare against.
						info->HasSocket = _socket;
					}
				}
				catch( Exception& e ){
					//only a *negative answer* may become an anonymous user.  Every failure used to land here, so an AppServer
					//restart or its request-deadline close - hit while a logged-in user was on the gateway - cached that user as
					//anonymous under their own session id, and UpdateExpiration then kept refreshing the entry without ever asking
					//again: permission denied everywhere for RestSessionTimeout, and it outlived the outage.  The status is what
					//distinguishes them (ServerSocketSession::SessionInfo answers NotFound); it survives the wire on the base.
					if( e.HttpStatus()!=EHttpStatus::NotFound )
						throw;//"could not ask" - resume with the exception and cache nothing, so the next request retries.
					e.SetLevel(ELogLevel::Debug);
					info = ms<SessionInfo>( *sessionId, steady_clock::now()+RestSessionTimeout(), UserPK{0}, _endpoint, _socket );
				}
				if( denied ){//outside the catch - a denial must not fall through to the anonymous-user entry, which would cache an attacker-chosen id.  Same generic answer as the no-3rd-party branch, never an existence oracle.
					DBGT( ELogTags::HttpServerRead, "[{:x}]Session endpoint '{}' does not match request endpoint '{}' - denying.", *sessionId, remoteEndpoint, _endpoint );
					if( _throw )
						throw Exception( SRCE_CUR, ELogLevel::Debug, "[{}]Session not found.", Ƒ("{:x}", *sessionId) );
					_h.resume();
					co_return;
				}
				upsert( info );
			}
			Resume( move(info) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α UpsertAwait::await_resume()ε->sp<SessionInfo>{
		base::CheckException();
		return Promise()->Value() ? *Promise()->Value() : sp<SessionInfo>{};
	}
}}