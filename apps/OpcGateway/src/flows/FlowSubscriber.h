#pragma once
#include "../GatewaySocketSession.h"
#include "FlowFeed.h"

namespace Jde::Opc::Gateway{
	//One FlowSubscribe on the gateway's socket (docs/flows/spec.md *Live*):  its place in the flow's feed, and what the feed
	//sends it, each handed to the socket's strand.  The first ack is written by the FlowSubscribe itself, on that strand, so
	//what the feed sends from its join on is written behind it.  It holds the session weakly:  the session's map holds it,
	//and the feed while it is joined.  The session's side, FlowSubscribe to RemoveFlow, is defined with it in
	//FlowSubscriber.cpp.
	struct FlowSubscriber final : std::enable_shared_from_this<FlowSubscriber>{
		FlowSubscriber( wp<GatewaySocketSession> session, Jde::RequestId requestId, sp<FlowFeed> feed, bool hops )ι;
		α Join( sv name, SRCE )ε->FromServer::FlowSubscribeAck{ return _feed->Add( shared_from_this(), name, sl ); }//`name`:  the flow's.
		α End()ι->void;//a FlowUnsubscribe's, the close's or the feed's:  out of the feed, and nothing more is sent.
		α Ended()Ι->bool{ return _ended; }
		α Request()Ι->Jde::RequestId{ return _requestId; }
		α Hops()Ι->bool{ return _hops; }//it asked for the trace's records.
		α OnAck( const string& ack )ι->void;//a run in the place of the one it was on:  its FlowSubscribeAck, serialized once for all the feed's subscribers.
		α OnActivity( const string& activity )ι->void;//the feed's push, a FlowActivity serialized the same.
		α OnEnded( EHttpStatus status, string what )ι->void;//the feed ended it:  the flow stopped, purged, or the flows stopping.
	private:
		wp<GatewaySocketSession> _session;
		const Jde::RequestId _requestId;
		const sp<FlowFeed> _feed;
		const bool _hops;
		std::atomic<bool> _ended{};
	};
}