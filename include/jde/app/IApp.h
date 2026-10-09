#pragma once
#include <jde/fwk/chrono.h>
#include <jde/fwk/co/Await.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/web/Jwt.h>
#include <jde/app/usings.h>
#include <jde/app/log/ProtoLog.h>
#include <jde/web/client/proto/Web.FromServer.pb.h>
#include <jde/fwk/log/SpdLog.h>	//no longer reachable through <jde/fwk.h>

namespace Jde::QL{ struct RequestQL; }
namespace Jde::App{
	struct IApp{
		virtual ~IApp()=default;//msvc warning

		α InstancePK()Ι->ProgInstPK{ return _instancePK; }
		β IsLocal()Ι->bool{ return false; }
		α ConnectionPK()Ι->App::ConnectionPK{ return _connectionPK; }
		β PublicKey()Ι->const Crypto::PublicKey& = 0;
		α SetAppPKs( ProgInstPK instPK, App::ConnectionPK pk )ι->void{ _instancePK = instPK; _connectionPK = pk; }
		β SessionInfoAwait( SessionPK sessionPK, SRCE )ε->up<TAwait<Web::FromServer::SessionInfo>> = 0;
		α Verify( const Web::Jwt& jwt )Ε->void;
		β Login( Web::Jwt&& jwt, SRCE )ε->up<TAwait<Web::FromServer::SessionInfo>> = 0;//up<TAwait>, as SessionInfoAwait: the socket and the in-process implementations differ in type.
		β ClientQuery( QL::RequestQL&& q, UserPK executer, SRCE )ε->up<TAwait<jvalue>> =0; //AppServer->[Gateway]|[OpcServer]
		Ω Status()ι->jobject{
			return jobject{
				{"memory", Process::MemorySize()},
				{"startTime", ToIsoString<seconds>(Process::StartTime())},
				{"uptimeSeconds", duration_cast<std::chrono::seconds>(Clock::now()-Process::StartTime()).count()}
			};
		}

		α LoadLogSettings( SRCE )ι->void;//every log target's instanceTagLevel rows, applied to the loggers this process runs.
		template<class T=jobject> [[nodiscard]] α Query( string&& q, jobject variables, bool returnRaw=true, SRCE )ε->up<TAwait<T>>;
		template<class T=jobject> α QuerySync( string&& q, jobject variables, bool returnRaw=true, SRCE )ε->T;

	protected:
		β QueryArray( string&& q, jobject variables, bool returnRaw, SRCE )ε->up<TAwait<jarray>> = 0;
		β QueryObject( string&& q, jobject variables, bool returnRaw, SRCE )ε->up<TAwait<jobject>> = 0;
		β QueryValue( string&& q, jobject variables, bool returnRaw, SRCE )ε->up<TAwait<jvalue>> = 0;
	private:
		ProgInstPK _instancePK{};
		App::ConnectionPK _connectionPK{};
	};

	Ŧ IApp::Query( string&& q, jobject variables, bool returnRaw, SL sl )ε->up<TAwait<T>>{
		if constexpr ( std::is_same_v<T, jarray> )
			return QueryArray( move(q), move(variables), returnRaw, sl );
		else if constexpr ( std::is_same_v<T, jobject> )
			return QueryObject( move(q), move(variables), returnRaw, sl );
		else if constexpr ( std::is_same_v<T, jvalue> )
			return QueryValue( move(q), move(variables), returnRaw, sl );
		else
			static_assert( false, "Unsupported type for IApp::Query" );
	}
	Ŧ IApp::QuerySync( string&& q, jobject variables, bool returnRaw, SL sl )ε->T{
		up<TAwait<T>> await = Query<T>( move(q), move(variables), returnRaw, sl );
		return BlockAwait<TAwait<T>,T>( move(*await) );
	}
	Ξ IApp::Verify( const Web::Jwt& jwt )Ε->void{
		THROW_IF( PublicKey()!=jwt.PublicKey, "Signor not trusted" );
		Crypto::Verify( PublicKey(), jwt.HeaderBodyEncoded, jwt.Signature );
	}
}