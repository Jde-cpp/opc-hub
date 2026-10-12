#pragma once
#include <jde/historian/Group.h>
#include "../GatewaySocketSession.h"

namespace Jde::Opc::Gateway{
	struct HistGroupHost;
	//One HistSubscribe on the gateway's socket (spec *Subscriptions*):  the group's registration and snapshot, and what the
	//group pushes, queued until the snapshot's pages are written and sent at once after.  The group delivers on the thread that
	//stored the record, so OnRecords only converts and hands the message to the socket's strand.  The page reads and the end
	//take one lock, the queue another, so a push never waits on a page.  It holds neither the session nor the group's host:
	//the group holds it through its registration, and an owning pointer back would make a cycle that outlived them both.
	//The session's side, HistSubscribe to RemoveHist, is defined with it in HistSubscriber.cpp.
	struct HistSubscriber final : Hist::ISubscriber{
		HistSubscriber( wp<GatewaySocketSession> session, Jde::RequestId requestId, wp<HistGroupHost> host, flat_map<Hist::NodeIndex,uint32> slots, vector<Hist::NodeIndex> members, UA_DateTime from )ι;
		α HasMembers()Ι->bool{ return !_members.empty(); }//a HistSubscribe naming only nodes the group doesn't hold registers nothing.
		α Register( const sp<HistSubscriber>& self, SL sl )ε->void;//with the group, which takes the snapshot:  under its locks, so on the executor.
		α Next( uint limit, SL sl )ε->Hist::ReadResult;
		α End()ι->void;
		α Ended()Ι->bool{ return _ended; }
		α Request()Ι->Jde::RequestId{ return _requestId; }
		α Host( SL sl )Ε->sp<HistGroupHost>;
		//The node's place in the HistSubscribe's nodes:  none for one the subscription doesn't name, which the group never sends.
		α Slot( Hist::NodeIndex index )Ι->optional<uint32>;
		α Page( Hist::ReadResult&& result, bool more )Ι->FromServer::HistSnapshot;
		α Live()ι->void;//the snapshot is written:  what queued goes, and each push from here on goes as it comes.
		α OnRecords( vector<Hist::Proto::HistoryRecord>&& records )ι->void override;
	private:
		α Send( vector<Hist::Proto::HistoryRecord>&& records )ι->void;
		wp<GatewaySocketSession> _session;
		const Jde::RequestId _requestId;
		const wp<HistGroupHost> _host;
		const flat_map<Hist::NodeIndex,uint32> _slots;
		vector<Hist::NodeIndex> _members;//until Register.
		const UA_DateTime _from;
		std::mutex _pageMutex;
		up<Hist::Subscription> _subscription;
		std::atomic<bool> _ended{};
		std::mutex _queueMutex;
		bool _live{};
		vector<Hist::Proto::HistoryRecord> _queued;
	};
}