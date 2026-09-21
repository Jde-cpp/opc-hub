#pragma once
#include <jde/opc/uatypes/Logger.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include "jde/fwk/crypto/CryptoSettings.h"
#include "jde/fwk/settings.h"
#include "uatypes/ExpectedNodeId.h"
#include "async/AsyncRequest.h"
#include "async/ConnectAwait.h"
#include "async/DataChanges.h"
#include "async/ReadValueAwait.h"
#include "auth/OpcServerSession.h"
#include "types/ServerCnnctn.h"
#include "types/MonitoringNodes.h"
#include "uatypes/Browse.h"
#include "NodeIndex.h"
#include "EnumTypeCache.h"

namespace Jde::Opc{ 	struct Value; }
namespace Jde::Opc::Gateway{
	namespace Browse{ struct Request; }
	namespace Read{ α OnResponse(UA_Client *client, void *userdata, RequestId requestId, StatusCode status, UA_DataValue *value)ι->void; }
	namespace Write{ α OnResponse(UA_Client *ua, void *userdata, RequestId requestId, UA_WriteResponse *response)ι->void; }
	namespace Attributes{ α OnResponse(UA_Client* ua, void* userdata, RequestId requestId, StatusCode status, UA_NodeId* dataType)ι->void; }

	struct CreateMonitoredItemsRequest;

	struct UAClient final : std::enable_shared_from_this<UAClient>{
		UAClient( ServerCnnctn&& opcServer, Credential cred )ε;
		UAClient( str address, Credential cred )ε;
		~UAClient();

		operator UA_Client* ()ι{ return _ptr; }
		Ω Shutdown( bool terminate=false, SRCE )ι->VoidAwait::Task;
		Ω ShutdownIdle( sp<UAClient> client )ι->VoidAwait::Task;//TTL expiry - tears down only this client (per-client _lastRequest).
		Ω GetClient( string id, Credential cred, SRCE )ι{ return ConnectAwait{move(id), move(cred), sl}; }
		Ω Find( str id, const Gateway::Credential& cred )ι->sp<UAClient>;
		//{clients, monitored items, parked items} for the status query.  Parked are the ones a lost connection is holding for its
		//reconnect (_pending/_rebuilds):  they are on no client, so the monitored count alone reads 0 for a server that is down.
		Ω StatusCounts()ι->tuple<uint,uint,uint>;
		Ω ConnectionCounts()ι->flat_map<ServerCnnctnNK,uint32>;
		//The reason the last connect attempt failed, per slug - cleared when one succeeds.  A slug with no live client and no
		//entry here was never attempted (or was drained when idle), which is what separates `Idle` from `Error` in serverConnections{connectionStatus}.
		Ω ConnectErrors()ι->flat_map<ServerCnnctnNK,string>;
		Ω Find( UA_Client* ua, SRCE )ε->sp<UAClient>;
		Ω TryFind( UA_Client* ua, SRCE )ι->sp<UAClient>;
		Ω RemoveClient( sp<UAClient>&& client )ι->bool;//forget the client:  it stops, and what it was monitoring ends with it.
		Ω ConnectionLost( sp<UAClient>&& client )ι->bool;//RemoveClient for a client whose connection failed:  what it was monitoring is parked and reconnected first.
		//The statuses that say the connection went, not that the server turned the request down - the one definition Retry, RetryVoid,
		//RemoveIfDisconnected and the rebuild read (reviews/m2-closing.md #2).  BadServerNotConnected:  the submit found the channel
		//already down.  BadConnectionClosed:  it went under the submit - sendRequest hands back connectStatus, or the send failed.
		//BadSecureChannelClosed:  the same, and what open62541 answers every call in flight with when the channel closes.
		Ω IsConnectionLoss( StatusCode sc )ι->bool{ return sc==UA_STATUSCODE_BADSERVERNOTCONNECTED || sc==UA_STATUSCODE_BADCONNECTIONCLOSED || sc==UA_STATUSCODE_BADSECURECHANNELCLOSED; }
		Ω RemoveIfDisconnected( StatusCode sc, const sp<UAClient>& client )ι->void;//a submission was refused with sc (UACε):  ConnectionLost when that means the connection went.  Strand-only.
		Ω LiveClients()ι->vector<sp<UAClient>>;//snapshot of the live clients - the `search` fan-out, which must never connect.
		Ω Unsubscribe( const sp<IDataChange>& dataChange )ι->void;//drop dataChange from every client's monitored items - a websocket session's OnClose, which is the only thing that ends its subscriptions.
		//A connection failure destroys the client but not what it was monitoring: that is parked and reconnected (soak-findings
		//#10), which puts those nodes in a second place an unsubscribe has to reach, or a closed session comes back to life.
		Ω PurgePending( const sp<IDataChange>& dataChange )ι->void;//everything parked for this listener - the OnClose path.
		Ω UnsubscribePending( const ServerCnnctnNK& slug, const sp<IDataChange>& dataChange, const flat_set<NodeId>& nodes )ι->flat_set<NodeId>;//just these nodes; returns the ones that were parked, which are a successful unsubscribe.
		//Ends every reconnect chain now:  cancels each one's backoff wait and forgets what they were restoring.  A waiting chain is
		//live asio work, so it held the executor - and the process's stop - for the rest of its delay, and one that woke mid-shutdown
		//built a client nothing stopped (subscription-disconnect #3).  Shutdown's first step; returns how many waits it cancelled.
		Ω StopReconnects()ι->uint;
		Ω ReconnectsWaiting()ι->uint;//chains in their backoff wait right now - for tests and diagnostics.

		α SubscriptionId()Ι->SubscriptionId{ auto p = CreatedSubscriptionResponse(); return p ? p->subscriptionId : 0; }
		//Responses are written on the strand but read by await_ready/await_resume on arbitrary threads - guard the shared_ptrs themselves.
		α CreatedSubscriptionResponse()Ι->sp<UA_CreateSubscriptionResponse>{ lg _{_responseMutex}; return _createdSubscriptionResponse; }
		α SetCreatedSubscriptionResponse( sp<UA_CreateSubscriptionResponse> p )ι->void{ lg _{_responseMutex}; _createdSubscriptionResponse = move(p); }
		α MonitoringModeResponse()Ι->sp<UA_SetMonitoringModeResponse>{ lg _{_responseMutex}; return _monitoringModeResponse; }
		α SetMonitoringModeResponse( sp<UA_SetMonitoringModeResponse> p )ι->void{ lg _{_responseMutex}; _monitoringModeResponse = move(p); }

		Ω ClearRequest( UA_Client* ua, RequestId requestId )ι->void;
		Ṫ ClearRequestH( UA_Client* ua , RequestId requestId )ι->T;
		α ClearRequest( RequestId requestId )ι->void;
		Ŧ ClearRequestH( RequestId requestId )ι->T;//{ return ClearRequest<UARequest<T>>( requestId )->CoHandle; }
		α MonitoredNodes()ι->UAMonitoringNodes&{ std::call_once(_monitoredNodesOnce, [this]{_monitoredNodes = mu<UAMonitoringNodes>(shared_from_this());}); return *_monitoredNodes; }//lazy but thread-safe: callers run on the loop thread (data-change callbacks) and pool threads (subscribe/unsubscribe) concurrently. Can't build eagerly in the ctor — shared_from_this() isn't valid until make_shared finishes wiring the weak ref.
		α TryMonitoredNodes()ι->UAMonitoringNodes*{ return _monitoredNodes.get(); }//never constructs: null means "no subscriptions", which for a query or an unsubscribe is simply nothing to do. Nothing clears the member once set - Shutdown used to move it out, which left this and MonitoredNodes() racing a still-pumping strand.
		Ŧ Retry( function<void(sp<UAClient>&&, T)> f, UAException&& e, sp<UAClient> pClient, T h )ι->ConnectAwait::Task;
		α RetryVoid( function<void(sp<UAClient>&&) > f, UAException&& e, sp<UAClient>&& pClient )ι->ConnectAwait::Task;
		α Process( RequestId requestId, sv what )ι->void;
		α StopProcessing()ι->void;//dispatches AsyncRequest::Stop onto the strand with a keep-alive.
		α PostUA( function<void()> f )ι->void;//runs f on this client's strand - the only place UA_Client_* calls are allowed once the processing loop can run. Holds shared_from_this until f runs.
		α PostStrand( function<void()> f )ι->void;//like PostUA but always defers (asio::post, not dispatch): use when a strand callback must not run re-entrantly inside the current strand handler. Holds shared_from_this until f runs.
		α Processing()ι->bool{ return _asyncRequest.IsRunning(); }
		α ProcessDataSubscriptions()ι->void;
		α StopProcessDataSubscriptions()ι->void;
		α Resubscribe()ι->void;//unconditional rebuild: re-create the subscription and the monitored items a dropped session took with it.  RecordSession is what decides when this is needed.
		α RecordSession()ι->bool;//records the current session's authentication token; true when it names a different session than the last one seen.
		α ReadSessionToken()Ι->string;//what RecordSession would record, without recording it - empty when the token cannot be read.  Compare with SessionToken() to see whether the session changed, and leave the baseline alone.
		α SessionToken()Ι->string{ lg _{_sessionTokenMutex}; return _sessionToken; }//the token RecordSession last read - empty means it could not be read, which turns the rebuild off (see there).
		α AddSessionAwait( VoidAwait::Handle h )ι->void;
		α TriggerSessionAwaitables()ι->void;

		//The url to hand open62541:  `url` itself, unless its host is a name whose first address takes no connection and a later one
		//does - then that address in the name's place.  open62541 connects to the first address a name resolves to and never tries
		//the next (eventloop_posix_tcp.c), so such a name fails BadConnectionRejected one address short of the server:  IPv6
		//link-local ahead of IPv4 against a server that listens on IPv4 alone - Kepware on a dual-stack box - or `localhost`, ::1
		//first on windows, against a server bound to 127.0.0.1 (reviews/security-matrix.md #10; HostNameTests).  An address, a name
		//with one address, and a name whose first address answers all come back exactly as given, so nothing changes where a connect
		//already works;  the substitution is made only where open62541 would have failed.
		Ω ReachableUrl( str url, Jde::Handle h )ι->string;
		//The connection's issued certificate, <slug>.pem under /gateway/issuedCerts.  Its SAN uri is the GATEWAY's own applicationUri -
		//the block's certificate/subjectAltName, urn:$(HostName):Jde-Cpp:$(PRODUCT_NAME) as shipped - and not the connection's
		//certificateUri, which is the server's and only filters its endpoints (reviews/security-matrix.md #8; ApplicationUriTests).
		//`applicationUri` puts another uri in the SAN's place:  the re-issue tests' seam - nothing in production passes it.
		Ω EnsureCertificate( const ServerCnnctnNK& slug, sv applicationUri={}, SRCE )ε->void;//no-op if the cert exists. Callable before any client - the Jde OpcServer rescans trustedCertDirs on a failed verify (UATrust), so pre-start creation only matters for third-party servers that snapshot their trust list.
		Ω CryptoSettings( const ServerCnnctnNK& slug, sv applicationUri={} )ι->Crypto::CryptoSettings; //for soak
		α Slug()Ι->const ServerCnnctnNK&{ return _opcServer.Slug; }
		α Name()Ι->str{ return _opcServer.Name; }
		α Index()ι->NodeIndex&{ return _nodeIndex; }//node names for `search`, crawled on first use;  dies with the client.
		α EnumTypes()ι->EnumTypeCache&{ return _enumTypes; }//enumeration definitions for `__type(opc,ns,i)`, read on first use;  dies with the client.
		α Url()Ι->str{ return _opcServer.Url; }
		α ConnectUrl()Ι->str{ return _connectUrl.empty() ? _opcServer.Url : _connectUrl; }//what Connect() handed open62541 - Url(), or ReachableUrl's substitute for it.
		α ApplicationUri()Ι->string;//the endpoint filter open62541 matches against the server's ApplicationUri - not clientDescription's.
		α AdvertisedUri()Ι->string;//clientDescription's:  what this client calls itself - the uri SAN of the certificate it presents (Configuration(), security-matrix #8).
		α IsDefault()Ι->bool{ return _opcServer.IsDefault; }
		α DefaultBrowseNs()Ι->NsIndex{ return _opcServer.DefaultBrowseNs; }
		α Handle()Ι->Jde::Handle{ return _handle; }
		α UAPointer()Ι->UA_Client*{ return _ptr; }
		α BrowsePathsToNodeIds( sv path, bool parents )Ε->flat_map<string,ExpectedNodeId>;
		UA_ClientConfig _config{};//TODO move private.
		Gateway::Credential Credential;

		std::atomic<bool> Connected{};
		std::atomic<bool> Discarded{};//set by RemoveClient just before it clears Connected:  a rebuild on this client drops what it has not restored instead of parking it.
	private:
		Ω Deregister( sp<UAClient>&& client )ι->bool;//the stop-and-erase RemoveClient and ConnectionLost share.
		Ω StateCallback( UA_Client *ua, UA_SecureChannelState channelState, UA_SessionState sessionState, StatusCode connectStatus )ι->void;
		Ω SetConnectError( const ServerCnnctnNK& slug, string message )ι->void;
		Ω ClearConnectError( const ServerCnnctnNK& slug )ι->void;
		Ω ServiceNotificationCallback( UA_Client* ua, UA_ApplicationNotificationType type, const UA_KeyValueMap payload )ι->void;
		α Configuration()ε->UA_ClientConfig*;
		α Create()ε->UA_Client*;
		α Connect()ε->void;
		//What GetEndpoints on `url` answers, for the connect-failure diagnostics:  the server's ApplicationUri (empty when the
		//endpoints could not be read), whether it has an unsecured (None) endpoint at that url at all, and every token policy of
		//every endpoint - the endpoint's mode and channel policy, the token type, and the policy that encrypts the token:  its
		//own securityPolicyUri, or the channel's when that is unset (open62541 matchUserTokenPolicy).
		struct EndpointSummary final{
			struct TokenPolicy final{ UA_MessageSecurityMode Mode; string ChannelPolicy; ETokenType Type; string Policy; };
			string ServerUri;
			vector<TokenPolicy> Policies;
			bool NoneEndpoint{};
			α Tokens()Ι->ETokenType{ ETokenType y{}; for( const auto& p : Policies ) y |= p.Type; return y; }//every type offered, on any endpoint.
		};
		Ω LogServerEndpoints( str url, Jde::Handle h )ι->EndpointSummary;
		α LogClientEndpoints()ι->void;
		//The certificate this client shows a server:  the app client's own for certificate authentication, else the connection's
		//issued one, which goes on the channel when there is a certificateUri.  nullopt when it shows none.
		α PresentedCertificate()Ι->optional<Crypto::CryptoSettings>;

		α CryptoSettings()Ι->Crypto::CryptoSettings{ return CryptoSettings(Slug()); }

		ServerCnnctn _opcServer;
		string _connectUrl;//set in Connect(), before the processing loop starts; read after it.

		vector<VoidAwait::Handle> _sessionAwaitables; mutable mutex _sessionAwaitableMutex;

		//The authentication token of the session last seen activated - see RecordSession.  Written from StateCallback (the loop
		//thread) and read off it through SessionToken()/ReadSessionToken(), so the lock is what keeps a reader - a test, a status
		//query - from racing a re-activation rather than a precaution against a future one.
		string _sessionToken; mutable mutex _sessionTokenMutex;

		sp<UA_SetMonitoringModeResponse> _monitoringModeResponse;
		sp<UA_CreateSubscriptionResponse> _createdSubscriptionResponse;
		mutable mutex _responseMutex;

		AsyncRequest _asyncRequest;
		Jde::Handle _handle;
		Logger _logger; //after handle
		UA_Client* _ptr{};//needs to be after _logger, _config, Password.
		friend ConnectAwait;
		friend α Read::OnResponse( UA_Client *client, void *userdata, RequestId requestId, StatusCode status, UA_DataValue *value )ι->void;
		friend α Write::OnResponse( UA_Client *ua, void *userdata, RequestId requestId, UA_WriteResponse *response )ι->void;
		friend α Attributes::OnResponse( UA_Client* ua, void* userdata, RequestId requestId, StatusCode status, UA_NodeId* dataType )ι->void;

		NodeIndex _nodeIndex;
		EnumTypeCache _enumTypes;
		std::once_flag _monitoredNodesOnce;
		up<UAMonitoringNodes> _monitoredNodes;//destroy first
	};

#define let const auto
	Ŧ UAClient::Retry( function<void(sp<UAClient>&&, T)> f, UAException&& e, sp<UAClient> client, T h )ι->ConnectAwait::Task{
		//TODO limit retry attempts.
		let slug = client->Slug();
		let credential = client->Credential;
		let lost = IsConnectionLoss( (StatusCode)e.Code() );
		if( lost )//a failed connection keeps what the client monitored; any other failure removes it as before.
			ConnectionLost( move(client) );
		else
			RemoveClient( move(client) );
		if( lost ){
			try{
				client = co_await GetClient( move(slug), move(credential) );
				f( move(client), h );
			}
			catch( runtime_error& e ){
				h.promise().ResumeExp( move(e), h );
			}
		}
		else
			h.promise().ResumeExp( move(e), h );
	}
}
#undef let