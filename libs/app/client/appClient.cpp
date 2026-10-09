#include <jde/app/client/appClient.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/web/client/ClientSsl.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/process.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/access/Authorize.h>
#include <jde/access/client/accessClient.h>
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/socket/ClientQL.h>
#include <jde/app/client/usings.h>
#include <jde/app/client/AppClientSocketSession.h>
#include <jde/app/client/IAppClient.h>
#include <jde/app/client/clientSubscriptions.h>

#define let const auto

namespace Jde::App{
	using Web::Client::ClientHttpAwait;
	Ω reconnectWait()ι->Duration{ return Settings::FindDuration("/server/reconnectWait").value_or(5s); }
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
	α ServerSettings::IsSsl()ι->bool{ return Settings::FindBool("/server/isSsl").value_or( false ); }
	α ServerSettings::Host()ι->string{ return Settings::FindString("/server/host").value_or("localhost"); }
	α ServerSettings::Port()ι->PortType{ return Settings::FindNumber<PortType>("/server/port").value_or(1967); }

	Ω getJwt( const Crypto::CryptoSettings& cryptoSettings )ε->Web::Jwt{
		auto certificate = Crypto::ReadCertificate( cryptoSettings.Certificate.Path );//sole key material - the jwt derives the public key from it; the server's TrustStore chains it at enrollment.
		const Crypto::Certificate info{ certificate };//the cert is also the identity authority - claims mirror the server's enrollment derivation (name: UPN → email → CN, slug: CN) so they can't disagree with what enrollment records.
		auto name = info.Upn.size() ? info.Upn : info.Email.size() ? info.Email : info.CommonName;
		//what the certificate cannot say:  which program this is and where it runs.  Enrollment records it as users.description
		//(the cert's own fields have their own columns there); later logins skip the insert, so an admin's edit stays.
		auto description = Ƒ( "{} '{}' on {}", Process::AppName(), InstanceName(), Process::HostName() );
		return Web::Jwt{ {}, {0}, move(name), info.CommonName, 0, {}, TimePoint::min(), move(description), cryptoSettings.PrivateKey, move(certificate) };
	}
	Ω closeSession( sp<AppClientSocketSession> session, SL sl )ι->VoidTask{
		try{
			co_await session->Close( false, sl );
		}
		catch( Exception& ){
		}
	}

	ConnectAwait::ConnectAwait( sp<IAppClient> appClient, bool retry, SL sl )ι:
		VoidAwait{sl},
		_appClient{ appClient },
		_retry{ retry }
	{}

	α ConnectAwait::Execute()ι->VoidAwait::Task{
		for(;;){
			let attemptStart = steady_clock::now();//the retry wait is measured from here, so the cadence is the setting's whatever a refused connect costs.
			sp<AppClientSocketSession> session;
			Duration wait{};//set by the catch:  co_await is not allowed inside a handler, so the timer runs after it.
			try{
				THROW_IF( Process::ShuttingDown(), "Shutting down." );
				let jwt = getJwt( *_appClient->SslSettings );
				TRACET( ELogTags::App, "Logging in {}:{}", ServerSettings::Host(), ServerSettings::Port() );
				ClientHttpAwait login{ ServerSettings::Host(), "/login", {}, ServerSettings::Port(), {.Authorization= Ƒ("Bearer {}", jwt.Payload())} };
				let res = co_await Any( login );
				let sessionId = Str::TryTo<SessionPK,16>( res[http::field::authorization] );
				THROW_IF( !sessionId, "Invalid authorization: {}.", res[http::field::authorization] );
				THROW_IF( Process::ShuttingDown(), "Shutting down." );//a socket opened on a stopping executor may never complete.

				TRACET( ELogTags::App, "[{}]Creating socket session", hex(*sessionId) );
				session = ms<AppClientSocketSession>( Executor(), ServerSettings::IsSsl() ? Web::Client::Ssl::MakeContext() : optional<ssl::context>{}, _appClient->Acl(), _appClient );//Acl() is null for a client that never authorizes (emulator, soak).
				auto run = session->RunSession( ServerSettings::Host(), ServerSettings::Port() );
				co_await Any( run );
				auto connect = session->Connect( *sessionId );//handshake
				auto info = co_await Any( connect );
				session->SetInfo( move(*info.mutable_session_info()) );
				_appClient->SetSession( move(session) );
				_appClient->ServerPublicKey = {
					{ info.certificate_modulus().begin(), info.certificate_modulus().end() },
					{ info.certificate_exponent().begin(), info.certificate_exponent().end() }
				};
				if( _appClient->ResourceSchema.size() && !info.auth_result() )//the AppServer's TestSchemaAdmin gate on the auth_resource we sent
					WARNT( ELogTags::Access, "AppServer declined to delegate '{}' admin checks to this instance - grant its user Administer on the schema's root resources and reconnect;  until then the AppServer applies its flat rule.", _appClient->ResourceSchema );
				_appClient->SetAppPKs( info.instance_pk(), info.connection_pk() );
				Post( _h );//posted, not resumed:  this runs inside the socket's OnRead, and resuming inline would block subsequent reads.
				co_return;
			}
			catch( runtime_error& e ){
				if( session )
					closeSession( session, _sl );
				if( !_retry || Process::ShuttingDown() ){//a retry timer armed during teardown only delays the executor drain.
					ResumeExp( move(e) );
					co_return;
				}
				//Said, not silent: a product whose registry is not up yet - the OpcServer started beside a hub still on its first
				//start (install-issues #12) - retries here for as long as it takes, and its console or log should show why it waits.
				//The wait is what is left of reconnectWait since the attempt began, not reconnectWait on top of the attempt: a refused
				//connect costs ~2 s per address on Windows, and `localhost` is two addresses, so "retrying in 5s" used to run at 9
				//(the 09-15 rerun's retry table).  A floor of half a second keeps an attempt that outlasts the setting from spinning.
				wait = std::max( duration_cast<Duration>(reconnectWait()-(steady_clock::now()-attemptStart)), duration_cast<Duration>(500ms) );
				WARNT( ELogTags::App, "Could not connect to the AppServer at {}:{} - retrying in {}s: {}", ServerSettings::Host(), ServerSettings::Port(), Ƒ("{:.1f}", duration<double>(wait).count()), e.what() );
			}
			DurationTimer timer{ wait };
			(void)co_await Any( timer );
		}
	}
}