#pragma once
#include <open62541/client.h>
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
#include "AggregateFunctions.h"

namespace Jde::Opc{ 	struct Value; }
namespace Jde::Opc::Gateway{
	namespace Browse{ struct Request; }
	namespace Tests{ struct UAClientState; }

	struct CreateMonitoredItemsRequest;
	//What a test connect (UAClient::ConnectForTest) reports:  the client with its session activated, or why the connect failed.
	using ConnectCallback = absl::AnyInvocable<void(sp<UAClient>, up<UAClientException>)>;
	//The connection error for a server that refused the certificate this gateway presented.  `sanUri` is the certificate's
	//subjectAltName URI:  empty when it carries none, nullopt when the file could not be read.
	α RefusedCertificateDetail( StatusCode sc, sv url, const fs::path& certificate, const optional<string>& sanUri, sv advertisedUri )ι->string;

	//How a slug's live clients changed, for the endpoint's `_Connection` object (server/GatewayServer.h, spec *Address space*):
	//Activated, a client's session came up, a new client's or one open62541 re-activated in place;  Lost, one's session went with
	//its connection, the client reconnecting in place or deregistered to be rebuilt;  Removed, one was closed or idled out;  Failed,
	//a connect failed with `Error`.  `Down`, no client of the slug had its session up just after the change:  a Lost with it is an
	//outage, and the next Activated ends it.  Whether the slug is connected is UAClient::HasSession's, asked when the change is
	//applied:  a later change has overtaken the one in hand by then.  Delivered off the registry's lock, on the strand or io thread
	//that made the change, so a listener posts what it does with it and never calls back into a client.
	struct ConnectionChange final{
		enum class EKind : uint8{ Activated, Lost, Removed, Failed };
		ServerCnnctnNK Slug;
		EKind Kind;
		bool Down;
		string Error;
	};
	struct UAClient final : std::enable_shared_from_this<UAClient>{
		UAClient( ServerCnnctn&& opcServer, Credential cred )ε;
		UAClient( str address, Credential cred )ε;
		~UAClient();

		operator UA_Client* ()ι{ return _ptr; }
		Ω Shutdown( bool terminate=false, SRCE )ι->VoidAwait::Task;
		Ω SetConnectionListener( function<void(const ConnectionChange&)> listener )ι->void;//one at a time;  null clears it.
		Ω ShutdownIdle( sp<UAClient> client )ι->VoidAwait::Task;//TTL expiry - tears down only this client (per-client _lastRequest).
		Ω GetClient( string id, Credential cred, SRCE )ι{ return ConnectAwait{move(id), move(cred), sl}; }
		Ω Find( str id, const Gateway::Credential& cred )ι->sp<UAClient>;
		//{clients, monitored items, parked items} for the status query.  Parked are the ones a lost connection is holding for its
		//reconnect (_pending/_rebuilds):  they are on no client, so the monitored count alone reads 0 for a server that is down.
		Ω StatusCounts()ι->tuple<uint,uint,uint>;
		Ω ConnectionCounts()ι->flat_map<ServerCnnctnNK,uint32>;
		Ω HasSession( const ServerCnnctnNK& slug )ι->bool;//a registered client of the slug has its session activated.
		//The reason the last connect attempt failed, per slug - cleared when one succeeds.  A slug with no live client and no
		//entry here was never attempted (or was drained when idle), which is what separates `Idle` from `Error` in serverConnections{connectionStatus}.
		Ω ConnectErrors()ι->flat_map<ServerCnnctnNK,string>;
		Ω LastFailures()ι->flat_map<ServerCnnctnNK,string>;//per slug, "<status> - <detail>" of its last failed connect, a later success or not.
		Ω Find( UA_Client* ua, SRCE )ε->sp<UAClient>;
		Ω TryFind( UA_Client* ua, SRCE )ι->sp<UAClient>;
		Ω RemoveClient( sp<UAClient>&& client )ι->bool;//forget the client:  it stops, and what it was monitoring ends with it.
		Ω ConnectionLost( sp<UAClient>&& client )ι->bool;//RemoveClient for a client whose connection failed:  what it was monitoring is parked and reconnected first.
		//The statuses that say the connection went, not that the server turned the request down - the one definition RemoveIfDisconnected
		//and the rebuild read (reviews/m2-closing.md #2).  BadServerNotConnected:  the submit found the channel
		//already down.  BadConnectionClosed:  it went under the submit - sendRequest hands back connectStatus, or the send failed.
		//BadSecureChannelClosed:  the same, and what open62541 answers every call in flight with when the channel closes.
		Ω IsConnectionLoss( StatusCode sc )ι->bool{ return sc==UA_STATUSCODE_BADSERVERNOTCONNECTED || sc==UA_STATUSCODE_BADCONNECTIONCLOSED || sc==UA_STATUSCODE_BADSECURECHANNELCLOSED; }
		//The statuses that say the session went while the channel may stand:  the server timed it out or closed it.  The keep-alive's
		//lapse, and with IsConnectionLoss the end of a search crawl.
		Ω IsSessionLoss( StatusCode sc )ι->bool{ return sc==UA_STATUSCODE_BADSESSIONIDINVALID || sc==UA_STATUSCODE_BADSESSIONCLOSED; }
		//The statuses a server turns a client certificate down with - the connect's hint, and every one a rejection that ends the
		//reconnect chain.  The first is the one that matters:  the Jde OpcServer and Kepware answer an untrusted certificate with
		//BadSecurityChecksFailed at the OPN, whatever their own logs call it.
		Ω RefusesCertificate( StatusCode sc )ι->bool{
			switch( sc ){
			case UA_STATUSCODE_BADSECURITYCHECKSFAILED:
			case UA_STATUSCODE_BADCERTIFICATEUNTRUSTED:
			case UA_STATUSCODE_BADCERTIFICATEINVALID:
			case UA_STATUSCODE_BADCERTIFICATEURIINVALID:
			case UA_STATUSCODE_BADCERTIFICATETIMEINVALID:
			case UA_STATUSCODE_BADCERTIFICATEUSENOTALLOWED:
			case UA_STATUSCODE_BADCERTIFICATEREVOKED:
				return true;
			default:
				return false;
			}
		}
		Ω RemoveIfDisconnected( StatusCode sc, const sp<UAClient>& client )ι->void;//a submission was refused with sc (UACε):  ConnectionLost when that means the connection went.  Strand-only.
		Ω LiveClients()ι->vector<sp<UAClient>>;//snapshot of the live clients - the `search` fan-out, which must never connect.
		Ω ConnectionEdited( const DB::Key& connection, bool removed )ι->uint;//every client built from that server_connections row leaves the registry - parked and reconnected on the row as it now is, or, removed, closed.  Returns how many.  OpcQLHook.
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
		//Connect once to report, not to serve:  testConnection's.  The client connects as ConnectAwait's would - the same certificate
		//and credential, keyed on its slug - but is registered nowhere:  not in the client registry, so no query, search or status
		//ever finds it;  not in ConnectErrors, so a failed test is not the slug's connectionStatus;  and no ConnectAwait waiter is
		//drained with it, which matters when the saved row of the same slug is connecting at the same time.  The result goes to
		//`onResult`, off the strand - BadTimeout past the request timeout, BadShutdown when Shutdown comes first - and the caller
		//discards the client (StopProcessing) once it has read what it wanted.
		α ConnectForTest( ConnectCallback onResult )ε->void;

		α SubscriptionId()Ι->SubscriptionId{ auto p = CreatedSubscriptionResponse(); return p ? p->subscriptionId : 0; }
		//Responses are written on the strand but read by await_ready/await_resume on arbitrary threads - guard the shared_ptrs themselves.
		α CreatedSubscriptionResponse()Ι->sp<UA_CreateSubscriptionResponse>{ lg _{_responseMutex}; return _createdSubscriptionResponse; }
		α SetCreatedSubscriptionResponse( sp<UA_CreateSubscriptionResponse> p )ι->void{ lg _{_responseMutex}; _createdSubscriptionResponse = move(p); }

		Ω ClearRequest( UA_Client* ua, RequestId requestId )ι->void;
		α ClearRequest( RequestId requestId )ι->void;
		α MonitoredNodes()ι->UAMonitoringNodes&{ std::call_once(_monitoredNodesOnce, [this]{_monitoredNodes = mu<UAMonitoringNodes>(shared_from_this());}); return *_monitoredNodes; }//lazy but thread-safe: callers run on the loop thread (data-change callbacks) and pool threads (subscribe/unsubscribe) concurrently. Can't build eagerly in the ctor — shared_from_this() isn't valid until make_shared finishes wiring the weak ref.
		α TryMonitoredNodes()ι->UAMonitoringNodes*{ return _monitoredNodes.get(); }//never constructs: null means "no subscriptions", which for a query or an unsubscribe is simply nothing to do. Nothing clears the member once set - Shutdown used to move it out, which left this and MonitoredNodes() racing a still-pumping strand.
		α Process( RequestId requestId, sv what, bool keepAlive=false )ι->void;
		α StopProcessing()ι->void;//dispatches AsyncRequest::Stop onto the strand with a keep-alive.
		α PostUA( absl::AnyInvocable<void()> f )ι->void;//runs f on this client's strand - the only place UA_Client_* calls are allowed once the processing loop can run. Holds shared_from_this until f runs.
		α PostStrand( absl::AnyInvocable<void()> f )ι->void;//like PostUA but always defers (asio::post, not dispatch): use when a strand callback must not run re-entrantly inside the current strand handler. Holds shared_from_this until f runs.
		α Processing()ι->bool{ return _asyncRequest.IsRunning(); }
		α OnStrand()Ι->bool{ return _asyncRequest.Strand().running_in_this_thread(); }//inside run_iterate or a PostUA - where UA_Client_* calls, and the ordering a strand callback relies on, are safe.
		α ProcessDataSubscriptions()ι->void;
		α StopProcessDataSubscriptions()ι->void;
		α Resubscribe()ι->void;//unconditional rebuild: re-create the subscription and the monitored items a dropped session took with it.  RecordSession is what decides when this is needed.
		α RecordSession()ι->bool;//records the current session's authentication token; true when it names a different session than the last one seen.
		α ReadSessionToken()Ι->string;//what RecordSession would record, without recording it - empty when the token cannot be read.  Compare with SessionToken() to see whether the session changed, and leave the baseline alone.
		α SessionToken()Ι->string{ lg _{_sessionTokenMutex}; return _sessionToken; }//the token RecordSession last read - empty means it could not be read, which turns the rebuild off (see there).
		α RecordBreak()ι->void;//StateCallback, for any state short of an activated session:  the first since the last activation is when the connection broke.
		α TakeBreak()ι->TimePoint;//when the connection broke, for IDataChange::ConnectionLost - RecordBreak's time, else now - and clears it, so the next loss is a new break.

		//The url to hand open62541:  `url` itself, unless its host is a name whose first address takes no connection and a later one
		//does - then that address in the name's place.  open62541 connects to the first address a name resolves to and never tries
		//the next (eventloop_posix_tcp.c), so such a name fails BadConnectionRejected one address short of the server:  IPv6
		//link-local ahead of IPv4 against a server that listens on IPv4 alone - External on a dual-stack box - or `localhost`, ::1
		//first on windows, against a server bound to 127.0.0.1 (reviews/security-matrix.md #10; HostNameTests).  An address, a name
		//with one address, and a name whose first address answers all come back exactly as given, so nothing changes where a connect
		//already works;  the substitution is made only where open62541 would have failed.
		Ω ReachableUrl( str url, Jde::Handle h )ι->string;
		//The connection's issued certificate, <slug>.pem under /gateway/issuedCerts.  Its SAN uri is the GATEWAY's own applicationUri -
		//the block's certificate/subjectAltName, urn:$(HostName):Jde-Cpp:$(PRODUCT_NAME) as shipped - and not the connection's
		//certificateUri, which is the server's and only filters its endpoints (reviews/security-matrix.md #8; ApplicationUriTests).
		Ω EnsureCertificate( const ServerCnnctnNK& slug, SRCE )ε->void;//no-op if the cert exists. Callable before any client - the Jde OpcServer rescans trustedCertDirs on a failed verify (UATrust), so pre-start creation only matters for third-party servers that snapshot their trust list.
		Ω CryptoSettings( const ServerCnnctnNK& slug )ι->Crypto::CryptoSettings;//the issued certificate's:  /gateway/issuedCerts with the slug's file names.
		α Slug()Ι->const ServerCnnctnNK&{ return _opcServer.Slug; }
		α Name()Ι->str{ return _opcServer.Name; }
		α Index()ι->NodeIndex&{ return _nodeIndex; }//node names for `search`, crawled on first use;  dies with the client.
		α EnumTypes()ι->EnumTypeCache&{ return _enumTypes; }//enumeration definitions for `__type(opc,ns,i)`, read on first use;  dies with the client.
		α Aggregates()ι->AggregateFunctions&{ return _aggregates; }//the server's AggregateFunctions folder for an aggregate `history`, read on first use;  dies with the client.
		α Url()Ι->str{ return _opcServer.Url; }
		α ConnectUrl()Ι->str{ return _connectUrl.empty() ? _opcServer.Url : _connectUrl; }//what Connect() handed open62541 - Url(), or ReachableUrl's substitute for it.
		α ApplicationUri()Ι->string;//the endpoint filter open62541 matches against the server's ApplicationUri - not clientDescription's.
		α AdvertisedUri()Ι->string;//clientDescription's:  what this client calls itself - the uri SAN of the certificate it presents (Configuration(), security-matrix #8).
		α IsDefault()Ι->bool{ return _opcServer.IsDefault; }
		α DefaultBrowseNs()Ι->NsIndex{ return _opcServer.DefaultBrowseNs; }
		α Handle()Ι->Jde::Handle{ return _handle; }
		α UAPointer()Ι->UA_Client*{ return _ptr; }
		//TranslateBrowsePathsToNodeIds for `path` - a/b/c, each segment `ns~name` or a bare name in DefaultBrowseNs - from `start`,
		//the Objects folder unless a caller has a root (a template's, spec *Templates*), following HierarchicalReferences and their
		//subtypes, so a path through HasComponent or HasProperty resolves as one through Organizes does.  `parents` asks for every
		//prefix of the path as well.  Strand-only, as every sync service (UAStrandAwait).
		α BrowsePathsToNodeIds( sv path, bool parents, const NodeId& start=NodeId::ObjectsFolder() )Ε->flat_map<string,ExpectedNodeId>;
		α Credential()Ι->const Gateway::Credential&{ return _credential; }
		α IsConnected()Ι->bool{ return _connected; }
		α IsDiscarded()Ι->bool{ return _discarded; }//set by RemoveClient just before it clears connected:  a rebuild on this client drops what it has not restored instead of parking it.
	private:
		Ω StashPending( const sp<UAClient>& client )ι->void;//ConnectionLost's:  parks what the client monitored, then clears connected - defined beside the rebuild it feeds.
		Ω Deregister( sp<UAClient>&& client )ι->bool;//the stop-and-erase RemoveClient and ConnectionLost share.
		Ω StateCallback( UA_Client *ua, UA_SecureChannelState channelState, UA_SessionState sessionState, StatusCode connectStatus )ι->void;
		Ω SetConnectError( const ServerCnnctnNK& slug, string message )ι->void;
		Ω ClearConnectError( const ServerCnnctnNK& slug )ι->void;
		Ω ServiceNotificationCallback( UA_Client* ua, UA_ApplicationNotificationType type, const UA_KeyValueMap payload )ι->void;
		α Configuration()ε->void;
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
		//The test connect's deadline, the client's request timeout:  open62541 gives an async connect none - a dropped SYN waits out
		//the kernel's retries, and a server that closes after HEL is reconnected to forever.  Strand-only, as the three below.
		α TestDeadline( sp<UAClient> self )ι->VoidTask;
		α TakeTestResult()ι->ConnectCallback;//_onTestResult, once:  whichever of StateCallback, the deadline and Shutdown reports first.
		α ExpireTest( StatusCode sc )ι->void;//reports `sc` for a test connect still unanswered, and stops the client.

		UA_ClientConfig _config{};
		Gateway::Credential _credential;
		std::atomic<bool> _connected{};
		//Its session is activated:  set on each activation, cleared when a registered client's leaves it and open62541 reconnects it in
		//place (StateCallback).  HasSession's alone - _connected keeps the meaning the rebuild and the request paths read.
		std::atomic<bool> _sessionUp{};
		std::atomic<bool> _discarded{};
		ServerCnnctn _opcServer;
		string _connectUrl;//set in Connect(), before the processing loop starts; read after it.
		ConnectCallback _onTestResult;//set by ConnectForTest, before Connect;  strand-only after it - see TakeTestResult.
		TimerCancel _testDeadline;//strand-confined.

		//The authentication token of the session last seen activated - see RecordSession.  Written from StateCallback (the loop
		//thread) and read off it through SessionToken()/ReadSessionToken(), so the lock is what keeps a reader - a test, a status
		//query - from racing a re-activation rather than a precaution against a future one.
		string _sessionToken; mutable mutex _sessionTokenMutex;
		//RecordBreak's time, TimePoint{} for none.  The session-loss rebuild only starts once the new session is activated, after the
		//outage, and spec *Compression*'s late-report rule needs the break, not that.
		std::atomic<TimePoint> _brokeAt{};

		sp<UA_CreateSubscriptionResponse> _createdSubscriptionResponse;
		mutable mutex _responseMutex;

		AsyncRequest _asyncRequest;
		Jde::Handle _handle;
		Logger _logger; //after handle
		UA_Client* _ptr{};//needs to be after _logger, _config, Password.
		friend ConnectAwait;
		friend Tests::UAClientState;//QLTests' held-but-disconnected client.

		NodeIndex _nodeIndex;
		EnumTypeCache _enumTypes;
		AggregateFunctions _aggregates;
		std::once_flag _monitoredNodesOnce;
		up<UAMonitoringNodes> _monitoredNodes;//destroy first
	};
}