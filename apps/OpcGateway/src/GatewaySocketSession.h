#pragma once
#include <jde/web/server/Sessions.h>
#include <jde/web/client/usings.h>
#include "ql/GatewayQL.h"
#include "types/MonitoringNodes.h"
#include <jde/web/server/IWebsocketSession.h>

namespace Jde::Proto{ class Query; }
namespace Jde::Opc{ struct NodeId; }
namespace Jde::Opc::Hist{ enum class ESubscriptionEnd : uint8; namespace Proto{ class HistoryRecord; } }
namespace Jde::Opc::Gateway{
	struct HistSubscriber; struct OeeSubscriber;
	using namespace Jde::Web::Server;
	using namespace Jde::Web::Client;
	struct GatewaySocketSession final: TWebsocketSession<FromServer::Transmission,FromClient::Transmission>, IDataChange{
		using base = TWebsocketSession<FromServer::Transmission,FromClient::Transmission>;
		GatewaySocketSession( sp<IRestStream> stream, beast::flat_buffer&& buffer, TRequestType&& request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι;
		α OnRead( FromClient::Transmission&& transmission )ι->void override;
		α SendDataChange( const ServerCnnctnNK& opcNK, const NodeId& node, const Value& value, const ITypes* types )ι->void override;
		α SendEvent( const ServerCnnctnNK& opcNK, const NodeId& node, const vector<const UA_Variant*>& fields, const ITypes* types )ι->void override;//the EventValues push (spec *Events*).
		//What the last Subscribe asked for the node on the connection, the defaults when it named none (spec *Watch list*), and a
		//negative sampling interval, which asks the publishing interval, as that interval in ms, so the merge can order it.
		α ItemParameters( const ServerCnnctnNK& opcNK, const NodeId& node, Duration publishingInterval )Ι->MonitoringParameters override;
		α ItemModified( const ServerCnnctnNK& opcNK, const NodeId& node, const MonitoredItemModifyResult& result, const optional<AskReverted>& reverted )ι->void override;//the MonitoredItemModified push, and a refused ask put back.
		α ConnectionRestored( const ServerCnnctnNK& opcNK, const flat_set<NodeId>& nodes )ι->void override;//the EventsRestored push, for those of the nodes this session holds event items on.
		α to_string()Ι->string override{ return Ƒ( "{:x}", Id() ); }
		α UserPK()Ι->Jde::UserPK override{ return Session() ? Session()->UserPK : Jde::UserPK{}; }
		α WriteException( runtime_error&& e, Jde::RequestId requestId, SRCE )ι->void override;
		//A historian subscription's page or push, written on the strand unless it has ended by then:  HistUnsubscribe ends it
		//there before writing its completion, so nothing of it follows that.
		α WriteHist( sp<HistSubscriber> subscriber, FromServer::Transmission&& message )ι->void;
		//A push's records, the same, converted there (HistSubscriber::Records):  decoding a stored structure's body is no work for the
		//thread that stored it.
		α WriteHist( sp<HistSubscriber> subscriber, vector<Hist::Proto::HistoryRecord>&& records )ι->void;
		//The group ended a historian subscription, removed or stopping:  on the strand, out of the map and the client told
		//under its id, unless a HistUnsubscribe or the close ended it first.
		α HistEnded( sp<HistSubscriber> subscriber, Hist::ESubscriptionEnd why )ι->void;
		//A live OEE subscription's push, written on the strand unless it has ended by then, as a historian subscription's is.
		α WriteOee( sp<OeeSubscriber> subscriber, string&& message )ι->void;//a Transmission's bytes.
		//The machine's feed ended a live OEE subscription, the machine deleted or purged or OEE stopping:  on the strand, out of
		//the map and the client told under its id, unless an OeeUnsubscribe or the close ended it first.
		α OeeEnded( sp<OeeSubscriber> subscriber, EHttpStatus status, string what )ι->void;
	private:
		α CreateSubscription( sp<UAClient> client, flat_map<NodeId,MonitoringParameters> asked, flat_map<NodeId,optional<MonitoringParameters>> prior, RequestId requestId )ι->VoidAwait::Task;
		//The asks a Subscribe kept for nodes its create refused, put back to what each had before, or gone with nothing before -
		//unless a later Subscribe has asked the node since (418-watch-items #9).
		α Unstore( const ServerCnnctnNK& slug, const flat_set<NodeId>& refused, const flat_map<NodeId,MonitoringParameters>& asked, const flat_map<NodeId,optional<MonitoringParameters>>& prior )ι->void;
		α LocalQL()Ι->sp<QL::IQL> override{ return QLPtr(); }
		α OnClose()ι->void override;
		α ProcessTransmission( FromClient::Transmission&& transmission )ι->void;
		α SendQueryClient( QL::TableQL&&, Jde::UserPK, Jde::RequestId )ε->void override{ throw Exception{ "NoImpl" }; }
		α SetSessionId( str sessionId, RequestId requestId )->Sessions::UpsertAwait::Task;
		α SharedFromThis()ι->sp<GatewaySocketSession>{ return std::dynamic_pointer_cast<GatewaySocketSession>(shared_from_this()); }

		α GraphQL( Jde::Proto::Query&& q, uint requestId )ι->TAwait<jvalue>::Task;
		//A Subscribe's, or a SubscribeEvents' (spec *Events*), whose one node asks an event item - MonitoringParameters::IsEvent - and gets
		//no first read, an object having no Value.  By value:  a coroutine keeps a reference parameter as one, and the transmission it
		//names is gone once the connect suspends.
		α Subscribe( ServerCnnctnNK opcId, flat_map<NodeId,MonitoringParameters> asked, uint32 requestId )ι->TAwait<sp<UAClient>>::Task;
		α Unsubscribe( ServerCnnctnNK&& opcId, flat_set<NodeId> nodes, uint32 requestId )ι->void;//an Unsubscribe's nodes, or an UnsubscribeEvents' one.
		//A historian subscription (spec *Subscriptions*), defined in hist/HistSubscriber.cpp:  the group's Read gate, the ack
		//with each node's status, the snapshot's pages, then every record as it is stored, until HistUnsubscribe or the close.
		α HistSubscribe( FromClient::HistSubscribe&& request, Jde::RequestId requestId )ι->void;
		//Registers the subscription and takes its snapshot on the executor, writes the ack and the snapshot's pages, `limit`
		//values a page, then lets the pushes through.
		α HistSubscribing( sp<HistSubscriber> subscriber, FromServer::HistSubscribeAck ack, uint limit )ι->VoidTask;
		α HistUnsubscribe( Jde::RequestId subscription, Jde::RequestId requestId )ι->void;
		α RemoveHist( Jde::RequestId subscription, const HistSubscriber* only=nullptr )ι->sp<HistSubscriber>;//out of the map, ended:  if `only`, just when it is the one there.
		//A live OEE subscription (oee spec *Live*), defined in oee/OeeSubscriber.cpp:  the reads' gate, the ack with the machine's
		//status now, then an OeeStatus as the machine's feed pushes one, until OeeUnsubscribe, the close or the feed's end.
		α OeeSubscribe( FromClient::OeeSubscribe&& request, Jde::RequestId requestId )ι->void;
		α OeeSubscribing( sp<OeeSubscriber> subscriber )ι->VoidTask;//joins the feed, which reads the shift's rows for its first, writes the ack, then lets the pushes through.
		α OeeUnsubscribe( Jde::RequestId subscription, Jde::RequestId requestId )ι->void;
		α RemoveOee( Jde::RequestId subscription, const OeeSubscriber* only=nullptr )ι->sp<OeeSubscriber>;
		//Under _histMutex:  a live subscription of any kind holds the id, which names one across them all.  Every subscribe asks.
		α Holds( Jde::RequestId requestId )Ι->bool{ return _histSubscriptions.contains( requestId ) || _oeeSubscriptions.contains( requestId ); }

		α WriteSubscription( const jvalue& j, Jde::RequestId requestId )ι->void override;
		α WriteSubscription( uint32 /*appPK*/, uint32 /*appInstancePK*/, const Logging::Entry& /*e*/, const QL::Subscription& /*sub*/ )ι->void override{ ASSERT(false); }
		α WriteSubscriptionAck( flat_set<QL::SubscriptionId>&& subscriptionIds, Jde::RequestId requestId )ι->void override;
		α WriteComplete( Jde::RequestId requestId )ι->void override;
		α WriteException( string&& e, Jde::RequestId requestId, SL sl )ι->void override;
		α WriteException( Exception&& e )ι->void{ WriteException( move(e), 0 ); }

		α SendAck( uint32 id )ι->void override;

		mutable std::mutex _histMutex;//both maps', and _closed.
		flat_map<Jde::RequestId,sp<HistSubscriber>> _histSubscriptions;//by the HistSubscribe's request id.
		flat_map<Jde::RequestId,sp<OeeSubscriber>> _oeeSubscriptions;//by the OeeSubscribe's:  an id names one subscription across the two.
		bool _closed{};
		//Per connection and node, what each Subscribe asked (ItemParameters) - by the client's slug, set once the connect found the
		//client and before the create, the defaults for a node it named none for, and kept until the node is unsubscribed.  An
		//ask the server refused is put back:  the create's in Unstore, a modify's in ItemModified.
		mutable std::mutex _parametersMutex;
		flat_map<ServerCnnctnNK,flat_map<NodeId,MonitoringParameters>> _parameters;
	};
}