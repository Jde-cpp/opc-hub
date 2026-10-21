#include "ServerMock.h"
#include <jde/web/client/ClientSsl.h>
#include "jde/fwk.h"
#include <jde/app/IApp.h>
#include <jde/web/server/Server.h>
#include <jde/web/server/Web.FromServer.h>
#include <jde/fwk/process/execution.h>
#include <jde/ql/IQL.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/ql/types/TableQL.h>

#define let const auto

namespace Jde::Web{
	optional<std::jthread> _webThread;

	struct TestAppClient : App::IApp{
		α IsLocal()Ι->bool override{ return true; }
		α GraphQL( string&&, UserPK, bool, SL )ι->up<TAwait<jvalue>>{ return {}; }
		α Login( Web::Jwt&&, SL )ε->up<TAwait<Web::FromServer::SessionInfo>> override{ throw Exception{"noImpl"}; }
		α ClientQuery( QL::RequestQL&&, UserPK, SL )ε->up<TAwait<jvalue>> override{ ASSERT(false); return {}; }
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

	//#248: /graphql end to end with no database.  `echo` is a system table, so the parser resolves no view for it, and the query
	//`echo` and the mutation `createEcho` answer with what reached them - the parse, the variables and the response shape are
	//what is under test.  Anything else falls through to the select, which has no data source here.
	struct EchoQL final : QL::IQL{
		α Authorizer()ε->Access::Authorize& override{ throw Exception{"noImpl"}; }
		α AuthorizerPtr()ε->sp<Access::Authorize> override{ return {}; }
		α CustomQuery( QL::TableQL& ql, QL::Creds, SL sl )ι->up<TAwait<jvalue>> override{
			if( ql.JsonName!="echo" )
				return {};
			jarray columns;
			for( let& c : ql.Columns )
				columns.emplace_back( c.JsonName );
			return mu<CompletedAwait<jvalue>>( [y=jobject{ {"args", ql.ExtrapolateVariables()}, {"columns", move(columns)} }]()->jvalue{ return y; }, sl );
		}
		α CustomMutation( QL::MutationQL& m, QL::Creds, SL sl )ι->up<TAwait<jvalue>> override{
			if( m.JsonTableName!="echo" )
				return {};
			return mu<CompletedAwait<jvalue>>( [y=jobject{ {"command", m.CommandName}, {"args", m.ExtrapolateVariables()} }]()->jvalue{ return y; }, sl );
		}
		α LogQuery( QL::TableQL&&, QL::Creds, SL )ε->up<TAwait<jvalue>> override{ throw Exception{"noImpl"}; }
		α LogSettingsQuery( QL::TableQL&&, QL::Creds, SL )ε->up<TAwait<jvalue>> override{ throw Exception{"noImpl"}; }
		α StatusQuery( QL::TableQL&&, QL::Creds, SL )ε->jobject override{ throw Exception{"noImpl"}; }
		α Query( string, jobject, UserPK, bool, SL )ε->up<TAwait<jvalue>> override{ throw Exception{"noImpl"}; }
		α QueryObject( string, jobject, UserPK, bool, SL )ε->up<TAwait<jobject>> override{ throw Exception{"noImpl"}; }
		α QueryArray( string, jobject, UserPK, bool, SL )ε->up<TAwait<jarray>> override{ throw Exception{"noImpl"}; }
		α Upsert( string, jobject, UserPK )ε->jarray override{ throw Exception{"noImpl"}; }
		α Schemas()Ι->const vector<sp<DB::AppSchema>>& override{ return _schemas; }
		α Subscribe( string&&, jobject, sp<QL::IListener>, UserPK, SL )ε->up<TAwait<vector<QL::SubscriptionId>>> override{ throw Exception{"noImpl"}; }
	private:
		vector<sp<DB::AppSchema>> _schemas;
	};
	sp<QL::IQL> _ql = ms<EchoQL>();
	α Mock::RequestHandler::QLServer()ι->sp<QL::IQL>{ return _ql; }

	sp<Mock::RequestHandler> _requestHandler;
	α Mock::Start( jobject settings )ε->void{
		QL::SetSystemTables( {"echo"} );
		_requestHandler = ms<RequestHandler>( move(settings) );
		Server::Start( _requestHandler );//generates the self-signed cert if needed and self-anchors it (C1) - in-process clients trust the mock with no incantation here.
	}

	α Mock::Stop()ι->void{
		Server::Stop( _requestHandler );
	}
}