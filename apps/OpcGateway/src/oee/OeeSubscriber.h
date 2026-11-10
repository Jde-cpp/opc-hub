#pragma once
#include "../GatewaySocketSession.h"
#include "OeeFeed.h"

namespace Jde::Opc::Gateway{
	//One OeeSubscribe on the gateway's socket (docs/oee/spec.md *Live*):  its place in the machine's feed, and what the feed
	//pushes, the latest held until the ack is written and sent after it.  The feed pushes from whichever thread built the
	//status, so OnStatus only hands it to the socket's strand.  It holds the session weakly:  the session's map holds it, and
	//the feed while it is joined.  The session's side, OeeSubscribe to RemoveOee, is defined with it in OeeSubscriber.cpp.
	struct OeeSubscriber final : std::enable_shared_from_this<OeeSubscriber>{
		OeeSubscriber( wp<GatewaySocketSession> session, Jde::RequestId requestId, sp<OeeFeed> feed )ι;
		α Join( SRCE )ι->OeeFeed::AddAwait{ return _feed->Add( shared_from_this(), sl ); }
		α End()ι->void;//an OeeUnsubscribe's, the close's or the feed's:  out of the feed, and nothing more is sent.
		α Ended()Ι->bool{ return _ended; }
		α Request()Ι->Jde::RequestId{ return _requestId; }
		α Live()ι->void;//the ack is written:  what was held goes, and each push from here on goes as it comes.
		α OnStatus( const FromServer::OeeStatus& status )ι->void;//the feed's.
		α OnEnded( EHttpStatus status, string what )ι->void;//the feed ended it:  the machine deleted or purged, or OEE stopping.
	private:
		α Send( const FromServer::OeeStatus& status )ι->void;
		wp<GatewaySocketSession> _session;
		const Jde::RequestId _requestId;
		const sp<OeeFeed> _feed;
		std::atomic<bool> _ended{};
		std::mutex _mutex;
		bool _live{};
		optional<FromServer::OeeStatus> _held;
	};
}