#include "ServerMock.h"
#include <jde/web/client/ClientSsl.h>
#include "jde/fwk.h"
#include <jde/app/IApp.h>
#include <jde/web/server/Server.h>
#include <jde/web/server/Web.FromServer.h>
#include <jde/fwk/process/execution.h>

namespace Jde::Web{
	optional<std::jthread> _webThread;

	struct TestAppClient : App::IApp{
		α IsLocal()Ι->bool override{ return true; }
		α GraphQL( string&&, UserPK, bool, SL )ι->up<TAwait<jvalue>>{ return {}; }
		α Login( Web::Jwt&&, SL )ε->Web::Client::ClientSocketAwait<Web::FromServer::SessionInfo> override{ throw "noImpl"; }
		α ClientQuery( QL::RequestQL&&, UserPK, SL )ε->up<TAwait<jvalue>> override{ ASSERT(false); return {}; }
		α SessionInfoAwait( SessionPK, SL )ι->up<TAwait<Web::FromServer::SessionInfo>> override{ return {}; }
		α PublicKey()Ι->const Crypto::PublicKey& override{ return _publicKey; }

		α QueryArray( string&&, jobject, bool, SL=SRCE_CUR )ε->up<TAwait<jarray>> override{ return {}; }
		α QueryObject( string&&, jobject, bool, SL=SRCE_CUR )ε->up<TAwait<jobject>> override{ return {}; }
		α QueryValue( string&&, jobject, bool, SL=SRCE_CUR )ε->up<TAwait<jvalue>> override{ return {}; }
	private:
		Crypto::PublicKey _publicKey;
	};
	sp<App::IApp> _appClient = ms<TestAppClient>();
	α Mock::AppClient()ι->sp<App::IApp>{ return _appClient; }

	//Answers off the caller's stack:  Resume() runs the awaiting coroutine to completion and destroys this awaitable along with the
	//frame that owns it, so it can't be called from inside Suspend() - post it and let Suspend() return first.
	struct SessionInfoStubAwait final : TAwait<Web::FromServer::SessionInfo>{
		SessionInfoStubAwait( Web::FromServer::SessionInfo&& info, SL sl )ι:TAwait<Web::FromServer::SessionInfo>{sl}, _info{move(info)}{}
		α Suspend()ι->void override{ Post( [this]{ Resume( move(_info) ); } ); }
	private:
		Web::FromServer::SessionInfo _info;
	};
	struct ForeignAppClientImpl final : TestAppClient{
		ForeignAppClientImpl( string userEndpoint, UserPK userPK )ι:_userEndpoint{move(userEndpoint)}, _userPK{userPK}{}
		α IsLocal()Ι->bool override{ return false; }
		α SessionInfoAwait( SessionPK sessionId, SL sl )ι->up<TAwait<Web::FromServer::SessionInfo>> override{
			Server::SessionInfo info{ sessionId, steady_clock::now()+1h, _userPK, _userEndpoint, true };
			return mu<SessionInfoStubAwait>( Server::ToProto(info), sl );
		}
	private:
		string _userEndpoint; UserPK _userPK;
	};
	α Mock::ForeignAppClient( string userEndpoint, UserPK userPK )ι->sp<App::IApp>{ return ms<ForeignAppClientImpl>( move(userEndpoint), userPK ); }

	//#10: resumes with an exception carrying a chosen http status - the gateway's only way to tell a negative answer from a
	//transport failure.  Posted for the same reason as SessionInfoStubAwait: ResumeExp destroys this awaitable with the frame.
	struct SessionInfoFailAwait final : TAwait<Web::FromServer::SessionInfo>{
		SessionInfoFailAwait( EHttpStatus status, SL sl )ι:TAwait<Web::FromServer::SessionInfo>{sl}, _status{status}{}
		α Suspend()ι->void override{ Post( [this]{ ResumeExp( Exception{"SessionInfoAwait stub failure", ExceptionArgs{_status}, _sl} ); } ); }
	private:
		EHttpStatus _status;
	};
	struct FailingAppClientImpl final : TestAppClient{
		FailingAppClientImpl( EHttpStatus status )ι:_status{status}{}
		α IsLocal()Ι->bool override{ return false; }
		α SessionInfoAwait( SessionPK, SL sl )ι->up<TAwait<Web::FromServer::SessionInfo>> override{ return mu<SessionInfoFailAwait>( _status, sl ); }
	private:
		EHttpStatus _status;
	};
	α Mock::FailingAppClient( EHttpStatus status )ι->sp<App::IApp>{ return ms<FailingAppClientImpl>( status ); }

	sp<Mock::RequestHandler> _requestHandler;
	α Mock::Start( jobject settings )ε->void{
		_requestHandler = ms<RequestHandler>( move(settings) );
		Server::Start( _requestHandler );//generates the self-signed cert if needed and self-anchors it (C1) - in-process clients trust the mock with no incantation here.
	}

	α Mock::Stop()ι->void{
		Server::Stop( _requestHandler );
	}
}