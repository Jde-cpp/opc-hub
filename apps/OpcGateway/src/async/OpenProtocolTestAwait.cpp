#include "OpenProtocolTestAwait.h"
#include <jde/fwk/process/execution.h>

#define let const auto

namespace Jde::Opc::Gateway{
	using OpenProtocol::Session;

	α OpenProtocolTestAwait::Suspend()ι->void{
		Session::Settings settings;
		settings.Revisions = _row.Revisions;
		_session.emplace( boost::asio::make_strand(*Executor()), _row.Host, _row.Port, move(settings), Session::Events{
			[this]( Session::Connection&& connection ){ Finish( move(connection) ); },
			[this]( const OpenProtocol::Failure& why ){ Finish( std::unexpected{why} ); },//the session would try again:  the test is one attempt.
			{}
		} );
		_session->Start();
	}
	α OpenProtocolTestAwait::Finish( Outcome&& outcome )ι->void{
		if( _outcome )//a link lost after MID 0002, before the stop reached the strand.
			return;
		_outcome = move( outcome );
		_session->Stop( [this]{//the session calls nothing after this and holds nothing of ours, so the resume may destroy it.
			Post( [this]{
				auto outcome = move( *_outcome );
				if( outcome )
					Resume( move(*outcome) );
				else{
					using enum OpenProtocol::EFailure;
					let& why = outcome.error();
					ResumeExp( Exception{_sl, {why.Kind==Timeout ? EHttpStatus::GatewayTimeout : EHttpStatus::BadGateway}, "The controller at {}:{} did not start communication:  {}", _row.Host, _row.Port, why.What} );
				}
			} );
		} );
	}
}