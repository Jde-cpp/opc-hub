#include <jde/app/client/appClient.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/process.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/access/Authorize.h>
#include <jde/access/client/accessClient.h>
#include <jde/web/client/socket/ClientQL.h>
#include <jde/app/client/usings.h>
#include <jde/app/client/AppClientSocketSession.h>
#include <jde/app/client/IAppClient.h>
#include <jde/app/client/clientSubscriptions.h>

#define let const auto

namespace Jde::App{
	using Web::Client::ClientHttpAwait;
	α reconnectWait()ι->Duration{ return Settings::FindDuration("/server/reconnectWait").value_or(5s); }
	α Client::IsSsl()ι->bool{ return Settings::FindBool("/server/isSsl").value_or( false ); }
	α Client::Host()ι->string{ return Settings::FindString("/server/host").value_or("localhost"); }
	α Client::Port()ι->PortType{ return Settings::FindNumber<PortType>("/server/port").value_or(1967); }
	α Client::InstanceName()ι->string{
		auto instanceName = Settings::FindString( "/instanceName" ).value_or( "" );
		return instanceName.empty() ? _debug ? "Debug" : "Release" : instanceName;
	}

	Ω reloadAccess( sp<Client::IAppClient> appClient )ι->VoidTask{
		try{
			co_await appClient->ReloadAccess();//on the new session's ClientQL - the one Configure was handed died with the old session.
			INFOT( ELogTags::App|ELogTags::Access, "Reloaded the access snapshot on the new session." );
		}
		catch( runtime_error& e ){
			//Not fatal, and not silent: authorization keeps answering from the snapshot it has, which is what it did before this ran
			//at all - but it is stale, and only a restart or the next reconnect will refresh it.
			WARNT( ELogTags::App|ELogTags::Access, "Could not reload the access snapshot on the new session: {}", e.what() );
		}
	}

	α Client::Connect( sp<IAppClient> appClient )ι->ConnectAwait::Task{
		try{
			if( Process::ShuttingDown() ){
				TRACET( ELogTags::App, "Not reconnecting - shutting down." );
				co_return;
			}
			co_await ConnectAwait{ appClient, true };
			if( Client::Subscriptions::Replay(appClient) && appClient->IsAccessConfigured() )
				reloadAccess( move(appClient) );//a replay means this is a reconnect, so the snapshot has a gap in it the deltas never filled.
		}
		catch( runtime_error& )
		{}
	}
}
namespace Jde::App::Client{
	struct LoginAwait final : TAwait<SessionPK>{
		using base = TAwait<SessionPK>;
		LoginAwait( const Crypto::CryptoSettings& cryptoSettings, SRCE )ε;
		α Suspend()ι->void{ Execute(); };
	private:
		α Execute()ι->Web::Client::ClientHttpAwait::Task;
		Web::Jwt _jwt;
	};

	Ω getJwt( const Crypto::CryptoSettings& cryptoSettings )ε->Web::Jwt{
		auto certificate = Crypto::ReadCertificate( cryptoSettings.Certificate.Path );//sole key material - the jwt derives the public key from it; the server's TrustStore chains it at enrollment.
		const Crypto::Certificate info{ certificate };//the cert is also the identity authority - claims mirror the server's enrollment derivation (name: UPN → email → CN, slug: CN) so they can't disagree with what enrollment records.
		auto name = info.Upn.size() ? info.Upn : info.Email.size() ? info.Email : info.CommonName;
		//what the certificate cannot say:  which program this is and where it runs.  Enrollment records it as users.description
		//(the cert's own fields have their own columns there); later logins skip the insert, so an admin's edit stays.
		auto description = Ƒ( "{} '{}' on {}", Process::AppName(), InstanceName(), Process::HostName() );
		return Web::Jwt{ {}, {0}, move(name), info.CommonName, 0, {}, TimePoint::min(), move(description), cryptoSettings.PrivateKey, move(certificate) };
	}
	LoginAwait::LoginAwait( const Crypto::CryptoSettings& cryptoSettings, SL sl )ε:
		base{sl},
		_jwt{ getJwt(cryptoSettings) }
	{}

	α LoginAwait::Execute()ι->ClientHttpAwait::Task{
		try{
			jobject j{ {"jwt", _jwt.Payload()} };
			TRACET( ELogTags::App, "Logging in {}:{}", Host(), Port() );
			auto res = co_await ClientHttpAwait{ Host(), "/login", {}, Port(), {.Authorization= Ƒ("Bearer {}", _jwt.Payload())} };
			auto sessionPK = Str::TryTo<SessionPK>( res[http::field::authorization], nullptr, 16 );
			THROW_IF( !sessionPK, "Invalid authorization: {}.", res[http::field::authorization] );
			Resume( move(*sessionPK) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	ConnectAwait::ConnectAwait( sp<IAppClient> appClient, bool retry, SL sl )ι:
		VoidAwait{sl},
		_appClient{ appClient },
		_retry{ retry }
	{}

	α ConnectAwait::Retry( const runtime_error& e )ι->DurationTimer::Task{
		//Said, not silent: a product whose registry is not up yet - the OpcServer started beside a hub still on its first
		//start (install-issues #12) - retries here for as long as it takes, and its console or log should show why it waits.
		//The wait is what is left of reconnectWait since the attempt began, not reconnectWait on top of the attempt: a refused
		//connect costs ~2 s per address on Windows, and `localhost` is two addresses, so "retrying in 5s" used to run at 9
		//(the 09-15 rerun's retry table).  A floor of half a second keeps an attempt that outlasts the setting from spinning.
		let elapsed = steady_clock::now() - _attemptStart;
		let wait = std::max( duration_cast<Duration>(reconnectWait()-elapsed), duration_cast<Duration>(500ms) );
		WARNT( ELogTags::App, "Could not connect to the AppServer at {}:{} - retrying in {}s: {}", Host(), Port(), Ƒ("{:.1f}", duration<double>(wait).count()), e.what() );
		try{
			(void)co_await DurationTimer{ wait };
			THROW_IF( Process::ShuttingDown(), "Shutting down." );
			HttpLogin();
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α ConnectAwait::RunSocket( SessionPK sessionId )ι->TAwait<Proto::FromServer::ConnectionInfo>::Task{
		try{
			THROW_IF( Process::ShuttingDown(), "Shutting down." );
			TRACET( ELogTags::App, "[{}]Creating socket session", hex(sessionId) );
			auto info = co_await StartSocketAwait{ sessionId, _appClient->Acl(), _appClient, _sl };//null for a client that never authorizes (emulator, soak).
			if( _appClient->ResourceSchema.size() && !info.auth_result() )//the AppServer's TestSchemaAdmin gate on the auth_resource we sent
				WARNT( ELogTags::Access, "AppServer declined to delegate '{}' admin checks to this instance - grant its user Administer on the schema's root resources and reconnect;  until then the AppServer applies its flat rule.", _appClient->ResourceSchema );
			_appClient->SetAppPKs( info.instance_pk(), info.connection_pk() );
			Post( _h );  //in OnRead, will block subsequent reads
		}
		catch( runtime_error& e ){
			if( _retry && !Process::ShuttingDown() )
				Retry( e );
			else
				ResumeExp( move(e) );
		}
	}
	α ConnectAwait::HttpLogin()ι->LoginAwait::Task{
		_attemptStart = steady_clock::now();
		try{
			let sessionId = co_await LoginAwait{ *_appClient->SslSettings };//http call
			THROW_IF( Process::ShuttingDown(), "Shutting down." );
			RunSocket( sessionId );
		}
		catch( runtime_error& e ){
			if( _retry && !Process::ShuttingDown() )//a retry timer armed during teardown only delays the executor drain.
				Retry( e );
			else
				ResumeExp( move(e) );
		}
	}
}