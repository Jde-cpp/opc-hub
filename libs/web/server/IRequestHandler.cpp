#include <jde/web/server/IRequestHandler.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/access/IAcl.h>

namespace Jde::Web::Server{

	IRequestHandler::IRequestHandler( jobject settings, sp<App::IApp> appServer )ι:
		_appServer{move(appServer)},
		_cancelSignal{ ms<net::cancellation_signal>() },
		_ctx{ ssl::context{ssl::context::tlsv12} },
		_settings{settings}
	{}

	α IRequestHandler::WebsocketSession( sp<IRestStream>&&, beast::flat_buffer&&, TRequestType, tcp::endpoint, uint32 )ι->sp<IWebsocketSession>{
		return {};
	}
	α IRequestHandler::BlockTillStarted()ε->void{
		while( _started.load()==EStartState::None )
			_started.wait( EStartState::None );
		if( _started.load()==EStartState::Failed )//#11: the exit Server::Start's ε always implied.
			throw Exception{ SRCE_CUR, ExceptionArgs{ELogTags::Server|ELogTags::Http, 0, EHttpStatus::ServiceUnavailable}, "Web server could not start: {}", _startError };
	}
	α IRequestHandler::Start()ι->void{
		_started.store( EStartState::Started );
		_started.notify_all();
	}
	α IRequestHandler::FailStart( string&& why )ι->void{
		_startError = move( why );//before the store: the release/acquire pair on _started is what publishes it to BlockTillStarted.
		_started.store( EStartState::Failed );
		_started.notify_all();
	}
	α IRequestHandler::Stop( bool, SL )ι->void{
		if( _cancelSignal )
			_cancelSignal->emit( net::cancellation_type::all );
		_started.store( EStartState::None );
		_started.notify_all();
	}

	α IRequestHandler::UserName( UserPK userPK )ι->string{
		if( Schemas().size()==0 || Schemas().front()->Authorizer==nullptr )
			return std::to_string( userPK.Value );
		return Schemas().front()->Authorizer->UserName( userPK );
	}
}