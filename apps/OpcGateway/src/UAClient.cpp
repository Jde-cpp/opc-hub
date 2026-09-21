#include "UAClient.h"

#include <open62541/plugin/securitypolicy_default.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/utils/collections.h>
#include <jde/app/client/IAppClient.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <open62541/types.h>
#include <stdexcept>
#include "GatewayAppClient.h"
#include <jde/opc/ServerTrust.h>
#include "async/DataChanges.h"
#include "jde/fwk/crypto/CryptoSettings.h"
#include "jde/fwk/settings.h"
#include "uatypes/Browse.h"
#include "uatypes/uaTypes.h"

#define let const auto

namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	flat_map<ServerCnnctnNK,flat_map<Credential,sp<UAClient>>> _clients; shared_mutex _clientsMutex;
	//Why the last connect attempt on a slug failed.  Keyed by slug, not by credential: the connection list is per slug and a
	//failure that is credential-specific still leaves the slug unusable for that caller.  Its own mutex - the status query reads it
	//without touching _clients, and StateCallback writes it while unlocked.
	flat_map<ServerCnnctnNK,string> _connectErrors; mutex _connectErrorMutex;

	//Subscriptions that outlived their client.  A connection failure destroys the UAClient (ProcessingLoop's deregister
	//path) and takes every websocket session's monitored nodes with it, so a later write silently builds a fresh client
	//and the pushes never come back - soak-findings #10, seen by restarting the OpcServer under a live subscription.
	//What the dying client was monitoring is parked here, keyed exactly as _clients is, and the reconnect puts it back.
	//Entries leave only three ways: a reconnect moves them to _rebuilds, every subscriber drops them (a closed tab), or the
	//process shuts down.  A rebuild whose own client dies part-way parks what it had not finished back here (resubscribe).
	using Listeners = flat_map<sp<IDataChange>,flat_set<NodeId>>;
	//One chain per key, and the entry *is* the chain:  parkLocked creates it and has the caller start the chain, and only that
	//chain (or StopReconnects) erases it.  So an entry that exists always has a chain running on it - a second failure meanwhile
	//merges into it rather than starting another - and nothing else may erase one, however empty it looks (see PurgePending).
	struct PendingSubscription final{
		Listeners Nodes;
		sp<DurationTimer> Wait;//the chain's backoff wait while it is in one - here, not only in its frame, so StopReconnects can cancel it.
		uint Rejections{};//reconnect attempts refused outright in a row - see isRejection; the chain gives up at MaxRejections.
		string LastFailure;//what the last attempt failed with; a repeat of it logs quietly.
	};
	flat_map<ServerCnnctnNK,flat_map<Credential,PendingSubscription>> _pending; mutex _pendingMutex;
	std::atomic<uint> _reconnectsWaiting;
	//Listeners a rebuild has taken - out of _pending (connectPending) or off a client (Resubscribe) - and not yet put back on a
	//client.  Held only in resubscribe's frame they were out of reach of both unsubscribes, so a session that closed or dropped a
	//node mid-rebuild had it re-created for a listener nothing would unsubscribe (subscription-disconnect #4).  Keyed by rebuild,
	//under _pendingMutex, and moved in and out of _pending under that same lock so a listener is never in neither place.
	struct Rebuild final{
		ServerCnnctnNK Slug;//UnsubscribePending's filter.
		Listeners Nodes;
	};
	flat_map<uint,Rebuild> _rebuilds; uint _rebuildId{};

	Ω pendingFind( const ServerCnnctnNK& slug, const Credential& credential )ι->PendingSubscription*{//_pendingMutex held
		auto creds = _pending.find( slug );
		if( creds==_pending.end() )
			return nullptr;
		auto p = creds->second.find( credential );
		return p==creds->second.end() ? nullptr : &p->second;
	}
	Ω pendingErase( const ServerCnnctnNK& slug, const Credential& credential )ι->void{//_pendingMutex held
		if( auto creds = _pending.find(slug); creds!=_pending.end() ){
			creds->second.erase( credential );
			if( creds->second.empty() )
				_pending.erase( creds );
		}
	}
	Ω rebuildStart( const ServerCnnctnNK& slug, Listeners&& listeners )ι->uint{//_pendingMutex held
		let id = ++_rebuildId;
		_rebuilds.try_emplace( id, Rebuild{slug, move(listeners)} );
		return id;
	}
	Ω rebuildHasNodes( uint rebuild )ι->bool{
		lg _{ _pendingMutex };
		auto r = _rebuilds.find( rebuild );
		return r!=_rebuilds.end() && r->second.Nodes.size();
	}
	Ω stashPending( const sp<UAClient>& client )ι->void;//defined beside the rebuild it feeds, far below; ConnectionLost (here) is what calls it.
	Ω parkLocked( const ServerCnnctnNK& slug, const Credential& credential, const Listeners& listeners )ι->bool;//into _pending - also defined below.
	Ω startReconnect( const ServerCnnctnNK& slug, const Credential& credential )ι->void;//the chain parkLocked asked for - also defined below.
	Ω nodeCount( const Listeners& listeners )ι->uint{
		uint y{};
		for( let& [_,nodes] : listeners )
			y += nodes.size();
		return y;
	}
	//The distinct nodes these listeners hold - what they cost as monitored items once they are back on a client, since two
	//listeners watching one node share its item.  nodeCount is the per-listener total the logs report instead; this is the one
	//that lines up with UAMonitoringNodes::Count(), so status can add the two.
	Ω itemCount( const Listeners& listeners )ι->uint{
		flat_set<NodeId> nodes;
		for( let& [_,listenerNodes] : listeners )
			nodes.insert( listenerNodes.begin(), listenerNodes.end() );
		return nodes.size();
	}

	α UAClient::ConnectErrors()ι->flat_map<ServerCnnctnNK,string>{
		lg _{ _connectErrorMutex };
		return _connectErrors;
	}
	α UAClient::SetConnectError( const ServerCnnctnNK& slug, string message )ι->void{
		lg _{ _connectErrorMutex };
		_connectErrors.insert_or_assign( slug, move(message) );
	}
	α UAClient::ClearConnectError( const ServerCnnctnNK& slug )ι->void{
		lg _{ _connectErrorMutex };
		_connectErrors.erase( slug );
	}
	//The two ways a client goes.  RemoveClient forgets it:  what it was monitoring ends with it, which is what teardown, the tests and an
	//idle client's shutdown mean.  ConnectionLost parks that first and reconnects it (soak-findings #10), and is only for a client whose
	//connection failed.  RemoveClient used to park as well, so a test suite's teardown revived whatever a failed test left monitored, a
	//second after gtest had deleted the fixture its pushes wrote into (subscription-disconnect #10).
	α UAClient::ConnectionLost( sp<UAClient>&& client )ι->bool{
		stashPending( client );//before anything else drops it: whatever this client was monitoring has to outlive it, or the subscriptions die here.  Clears Connected - there, not here:  see it.
		return Deregister( move(client) );
	}
	α UAClient::RemoveClient( sp<UAClient>&& client )ι->bool{
		client->Discarded = true;//before Connected:  a rebuild reads Connected first, and must then see why it went - see resubscribe.
		client->Connected = false;
		return Deregister( move(client) );
	}
	α UAClient::Deregister( sp<UAClient>&& client )ι->bool{
		client->StopProcessing();//cancels the ping timer & processing loop; otherwise _pingTimer stays pending on the io_context (and the ping coroutine keeps a UAClient ref), blocking shutdown.
		bool erased{};
		ul _{ _clientsMutex };
		if( auto serverCreds = _clients.find(client->Slug()); serverCreds!=_clients.end() ){
			if( auto cred = serverCreds->second.find(client->Credential); cred!=serverCreds->second.end() && cred->second==client ){
				DBG( "[{}]Removing client: '{}'.", hex(client->Handle()), client->Slug() );
				serverCreds->second.erase( cred );
				erased = true;
				if( serverCreds->second.empty() )
					_clients.erase( serverCreds );

			}
			else if( cred!=serverCreds->second.end() )
				DBG( "[{}] - already replaced by [{}] for '{}' - leaving it.", hex(client->Handle()), hex(cred->second->Handle()), client->Slug() );
		}
		if( !erased )
			DBG( "[{}] - could not find client='{}'.", hex(client->Handle()), client->Slug() );
		client = nullptr;
		return erased;
	}
	//A refused submission, from UACε.  It tested BadServerNotConnected alone, which open62541 returns only for a channel already
	//down when the call starts;  one that goes under the call comes back as connectStatus - BadConnectionClosed,
	//BadSecureChannelClosed, or whatever ERR the server closed with - and the client stayed `Connected` for a rebuild to misread
	//(reviews/m2-closing.md #2).  So:  the statuses that mean it, or the refusal being connectStatus itself - sendRequest's
	//`return client->connectStatus`, after which the processing loop's next run_iterate fails and deregisters the client anyway;
	//this only gets there before the caller's catch reads Connected.  A refusal that is neither - BadOutOfMemory, a
	//BadSubscriptionIdInvalid found locally, a request too large to encode - leaves the client alone, as before.
	//Only a client still thought connected:  one already lost has been parked, and one RemoveClient discarded must not be -
	//ConnectionLost would park what it was monitoring, and a late submission on it would bring that back (subscription-disconnect #10).
	α UAClient::RemoveIfDisconnected( StatusCode sc, const sp<UAClient>& client )ι->void{
		if( !client || !sc || !client->Connected )
			return;
		auto lost = IsConnectionLoss( sc );
		if( !lost ){
			StatusCode connectStatus{};
			UA_Client_getState( client->UAPointer(), nullptr, nullptr, &connectStatus );
			lost = connectStatus==sc;
		}
		if( lost )
			ConnectionLost( sp<UAClient>{client} );
	}
	α UAClient::LiveClients()ι->vector<sp<UAClient>>{
		vector<sp<UAClient>> y;
		sl _{ _clientsMutex };
		for( let& [_, creds] : _clients ){
			for( let& [__, client] : creds ){
				if( client->Connected )
					y.push_back( client );
			}
		}
		return y;
	}
	α UAClient::ConnectionCounts()ι->flat_map<ServerCnnctnNK,uint32>{
		flat_map<ServerCnnctnNK,uint32> y;
		sl _{ _clientsMutex };
		for( let& [slug, creds] : _clients )
			y[slug] = (uint32)creds.size();
		return y;
	}
	α UAClient::StatusCounts()ι->tuple<uint,uint,uint>{
		vector<sp<UAClient>> clients;
		{
			sl _{ _clientsMutex };
			for( let& [slug, creds] : _clients )
				for( let& [cred, client] : creds )
					clients.push_back( client );
		}
		uint monitored{};//count outside the lock - MonitoredNodes() lazily constructs and Count() takes the nodes mutex.
		for( let& client : clients )
			monitored += client->MonitoredNodes().Count();
		//Nothing parked is on a client, so `monitored` reads 0 for a server that is down while its subscriptions are very much
		//alive - report them separately rather than folding them in, so an outage still shows as live items going to 0.  Taken
		//after _clientsMutex has been released, never inside it - the two are never held together anywhere.
		uint parked{};
		{
			lg _{ _pendingMutex };
			for( let& [slug, creds] : _pending ){
				for( let& [cred, entry] : creds )
					parked += itemCount( entry.Nodes );
			}
			for( let& [id, rebuild] : _rebuilds )//out of _pending and not yet back on a client: counted here, so a rebuild does not make them vanish.
				parked += itemCount( rebuild.Nodes );
		}
		return { clients.size(), monitored, parked };
	}
	concurrent_flat_map<uint32_t, uint32_t> _handles;
	α createHandle( const ServerCnnctn& slug )ι->Jde::Handle{
		//Handle packs the server id into its top 32 bits, so fold the 64-bit hash rather than truncating it (xor high^low keeps more entropy). A collision only merges two servers' connection-index counters, which are purely for log correlation - benign.
		uint32_t serverHash = slug.Id ? slug.Id : []( size_t h )ι{ return (uint32_t)h ^ (uint32_t)(h>>32); }( std::hash<string>{}(slug.Url) );
		uint32_t connectionIndex{};
		auto increment = [&connectionIndex]( auto& last ){ connectionIndex = ++last.second; };
		_handles.try_emplace_and_visit( serverHash, 0, increment, increment );
		return ( (Jde::Handle)serverHash << 32 ) | connectionIndex;
	}

	concurrent_flat_set<sp<UAClient>> _awaitingActivation;

	UAClient::UAClient( str address, Gateway::Credential cred )ε:
		UAClient{ ServerCnnctn{address}, move(cred) }
	{}

	UAClient::UAClient( ServerCnnctn&& opcServer, Gateway::Credential cred )ε:
		Credential{ move(cred) },
		_opcServer{ move(opcServer) },
		_handle{ createHandle(_opcServer) },
		_logger{ _handle },
		_ptr{ Create() }{
		try{
			//Configuration() must run BEFORE setDefault: it installs the custom security policies (and asserts securityPoliciesSize==0 first). setDefault then only back-fills fields left unset — its own None-policy install is guarded by securityPoliciesSize==0, so it won't clobber ours (open62541 ua_config_default.c). Reversing the order would trip Configuration()'s assert and leak the default policy.
			let sc = UA_ClientConfig_setDefault( Configuration() ); THROW_IFX( sc, UAClientException(sc, Handle()) );
			INFO( "[{}]Creating UAClient slug: '{}' url: '{}' credential: '{}' )", hex(Handle()), Slug(), Url(), Credential.ToString() );
			LogClientEndpoints();
		}
		catch( ... ){
			UA_Client_delete( _ptr );
			_ptr = nullptr;
			throw;
		}
	}

	α UAClient::Shutdown( bool /*terminate*/, SL /*sl*/ )ι->VoidAwait::Task{
		//Before the first co_await:  this runs fire-and-forget from Process::Shutdown, so everything after that suspends runs late,
		//and a chain waking in the gap built a client after the snapshot below that nothing ever stopped (subscription-disconnect #3).
		StopReconnects();
		vector<sp<UAClient>> clients;
		{
			sl _1{ _clientsMutex };
			for( auto&& [_,creds] : _clients )
				for( auto&& [_,client] : creds )
					clients.push_back( client );
		}
		//Must not hold _clientsMutex across co_await: the awaitable resumes on another pool thread (UB to unlock a shared_mutex off-thread) and the resume path (RemoveClient, StateCallback insertion) needs the unique lock.
		for( auto& client : clients ){
			if( client->_monitoredNodes )
				co_await client->_monitoredNodes->Shutdown();
			client->StopProcessing();
			if( client.use_count()>2 )//_clients + this local copy are expected; more may just be pending strand closures (StopProcessing above) - log-only heuristic.
				WARN( "[{}]use_count={}", hex(client->Handle()), client.use_count() );
		}
		ul _{ _clientsMutex };
		_clients.clear();
	}

	//for soak
	α UAClient::CryptoSettings( const ServerCnnctnNK& slug, sv applicationUri )ι->Crypto::CryptoSettings{
		auto settings = Settings::FindDefaultObject( "/gateway/issuedCerts" );//a copy - the file name below is per-slug.
		if( applicationUri.size() ){
			//A test's seam.  The SAN uri is the gateway's OWN applicationUri - what Configuration() advertises, and what a server
			//holds the certificate against - so every connection takes the block's one subjectAltName.  Until security-matrix #8
			//each was stamped with its connection's certificateUri, the server's uri, and the gateway introduced itself as the
			//server it was calling.
			auto certificate = Json::FindDefaultObject( settings, "certificate" );
			certificate["subjectAltName"] = Ƒ( "URI:{}", Str::Replace(string{applicationUri}, " ", "%20") );
			settings["certificate"] = move( certificate );
		}
		return Crypto::CryptoSettings{ settings, slug };
	}

	//the file name keys on the slug but the SAN on the gateway's applicationUri (the config block's), so an existing file is not proof it is the
	//cert this config describes; a changed uri would otherwise be rejected as BadCertificateUriInvalid forever with
	//nothing naming the file to delete.  ReissueReason compares only the SAN entry types that round-trip byte-for-byte
	//through the der ctor (URI among them; otherName is excluded on both sides), so a lossy rendering cannot re-issue in a loop.
	α UAClient::EnsureCertificate( const ServerCnnctnNK& slug, sv uri, SL sl )ε->void{
		let& settings = CryptoSettings( slug, uri );
		//EnsureKeyCertificate's guard, which came across with neither of this path's rewrites:  certificate.managed:false is the
		//operator's pair, used as found - the expiry and SAN checks below re-issue only what this product issued, and a
		//certificate a CA vouched for must never be replaced by a self-signed one (web-certs3 (b)).  It was parsed on this block
		//and ignored, so a CA-issued channel certificate whose SAN was not /gateway/issuedCerts' was overwritten in place behind
		//one INFO line (reviews/m2-closing.md #7).  Both files are the operator's to supply, and the certificate's name carries
		//the slug, so a missing one is named rather than left to fail as a file that could not be read.
		if( !settings.Certificate.Managed ){
			THROW_IFSL( !fs::exists(settings.PrivateKey.Path), "/gateway/issuedCerts/certificate/managed is false and the private key '{}' does not exist - supply the pair for connection '{}', or set managed:true to have one issued.", settings.PrivateKey.Path.string(), slug );
			THROW_IFSL( !fs::exists(settings.Certificate.Path), "/gateway/issuedCerts/certificate/managed is false and connection '{}' has no certificate at '{}' - supply the pair, or set managed:true to have one issued.", slug, settings.Certificate.Path.string() );
			return;
		}
		//the same predicate EnsureKeyCertificate uses for the web certificates - missing, expired or expiring, or the SAN (here the
		//gateway's own applicationUri, which a server holds against what we advertise) drifted - which is also how a certificate
		//issued before security-matrix #8, its SAN the server's uri, replaces itself - so the two paths cannot diverge again:  this
		//one compared the SAN uri alone and let an issued certificate run until the peer rejected it as expired (web-certs3 #17).
		let reason = Crypto::ReissueReason( settings, sl );
		if( reason.empty() )
			return;
		INFO( "Re-issuing '{}': {}.", settings.Certificate.Path.string(), reason );
		settings.CreateDirectories();
		if( !fs::exists(settings.PrivateKey.Path) )
			Crypto::CreateKey( settings, sl );
		Crypto::IssueCertificate( settings, std::chrono::days{365}, sl );
	}
	α UAClient::Configuration()ε->UA_ClientConfig*{
		let serverUri = Str::Replace( _opcServer.CertificateUri, " ", "%20" );//the server's applicationUri:  the endpoint filter, and since security-matrix #8 nothing else.
		bool addSecurity = !serverUri.empty();
		let certAuth = Credential.Type()==ETokenType::Certificate && AppClient()->SslSettings.has_value();//Create() already threw for a certificate credential with no ssl settings.
		//One certificate, two jobs.  With a certificateUri it is the channel's identity, under whichever secured policy the server
		//shares (securedPolicies, below).  With or without one, it is what the *auth* policies - the ones that encrypt the user
		//token - are built from:  open62541 builds an auth
		//policy only from a local certificate, though the token itself is encrypted to the server's, which every endpoint
		//description carries.  So a connection with no certificateUri stays on SecurityPolicy None - its data in the clear - and
		//still presents its credential encrypted wherever the server's token policy asks for that, as the Jde OpcServer and
		//Kepware both do on their None endpoints (reviews/security-matrix.md #1, ruled 09-18; NoSecurityTests).  The certificate
		//is the per-connection issued one (its SAN the gateway's own applicationUri - /gateway/issuedCerts);  certificate
		//authentication uses the app client's own instead, since the X509 token and the auth policy that signs for it must be the
		//same certificate - which is also why that credential's transport and authentication certificates are equal.
		if( !certAuth )
			EnsureCertificate( Slug() );
		auto config = UA_Client_getConfig( _ptr );
		ServerTrust::Install( *config, "/gateway", Handle(), Url() );//before setDefault, which would otherwise install AcceptAll;  applies to every endpoint that carries a certificate, secured or not.
		//The secured policies the gateway carries - for the channel with a certificateUri, for the user token always.  open62541
		//takes the endpoint with the highest securityLevel among the policies it finds here, so a server that offers an Aes policy
		//gets it, and one that offers Basic256Sha256 alone - Kepware - gets that (reviews/security-matrix.md #4; SecurityPolicyTests).
		//The deprecated ones (Basic128Rsa15, Basic256) are not carried, and a server that offers nothing else is told so (StateCallback).
		//securityLevel is the *server's* claim, read off the unauthenticated discovery channel, so it only ever ranks endpoints
		//that already meet the floor pinned below (config->securityMode) - it never chooses the mode (reviews/m2-closing.md #1).
		using PolicyCtor = UA_StatusCode(*)( UA_SecurityPolicy*, const UA_ByteString, const UA_ByteString, const UA_Logger* );
		const array<PolicyCtor,3> securedPolicies{ &UA_SecurityPolicy_Basic256Sha256, &UA_SecurityPolicy_Aes128Sha256RsaOaep, &UA_SecurityPolicy_Aes256Sha256RsaPss };//_securedPolicyUris, below, names the same three.
		const uint size = addSecurity ? 1+securedPolicies.size() : 1; ASSERT( !config->securityPoliciesSize );
		uint initialized = 0;//policies actually constructed; on an exception before ownership transfers to config, the deleter clears these — UA_free alone would leak each policy's internals (policyUri, contexts, ...).
		auto clearPolicies = [&initialized]( UA_SecurityPolicy* p )ι{ for(uint i=0; i<initialized; ++i) p[i].clear(&p[i]); UA_free(p); };
		up<UA_SecurityPolicy, decltype( clearPolicies )> securityPolicies{ (UA_SecurityPolicy*)UA_malloc(sizeof(UA_SecurityPolicy)*size), clearPolicies };
		auto sc = UA_SecurityPolicy_None( &securityPolicies.get()[0], UA_BYTESTRING_NULL, &_logger ); THROW_IFX( sc, UAClientException(sc, Handle()) );
		++initialized;
		let& settings = certAuth ? *AppClient()->SslSettings : CryptoSettings(); //requires authentication[AppClient] & transport[OpcServer] security be equal.
		auto certificate = ToUAByteString( Crypto::ReadCertificate(settings.Certificate.Path) );
		auto privateKey = ToUAByteString( Crypto::ReadPrivateKey(settings.PrivateKey) );
		//Two uris, two jobs (reviews/security-matrix.md #8; ApplicationUriTests).  config->applicationUri only FILTERS the server's
		//endpoints (matchEndpoint) - it is the server's, the connection's certificateUri.  clientDescription's is who this client
		//says it is, and a server holds it against the SAN of the certificate the client presents - against each other, never
		//against its own uri - so it is the gateway's:  the uri in that certificate's SAN, read from the file itself.  The issued
		//certificate takes it from /gateway/issuedCerts, the app client's own from its web ssl block, an operator's own pair
		//(managed:false) from wherever it was made.  Until #8 both were the certificateUri, and the gateway introduced itself
		//as the server it was calling.
		let ownUri = Crypto::Certificate{ Crypto::ReadCertificate(settings.Certificate.Path) }.SanUri();
		if( ownUri.empty() && addSecurity )
			WARN( "[{}]'{}' carries no URI in its subjectAltName, so this gateway has no applicationUri of its own to advertise - advertising '{}', the server's.", hex(Handle()), settings.Certificate.Path.string(), serverUri );
		if( let advertised = ownUri.empty() ? serverUri : ownUri; advertised.size() ){//in place of open62541's "unconfigured" placeholder.
			UA_String_clear( &config->clientDescription.applicationUri );
			config->clientDescription.applicationUri = UA_STRING_ALLOC( advertised.c_str() );
		}
		if( addSecurity ){
			UA_String_clear( &config->applicationUri );//clear any existing value before overwriting so the default isn't leaked.
			config->applicationUri = UA_STRING_ALLOC( serverUri.c_str() );
			//The floor:  a certificateUri means Sign & Encrypt, and this is the only mode filter open62541 has (matchEndpoint:
			//configuredSM > 0).  Unset, the None policy the discovery channel needs also matches a None *endpoint*, and whoever
			//answers GetEndpoints - the server, or anything on-path - takes the channel to None/None by claiming securityLevel 255
			//on it, with the real server's public certificate passing ServerTrust (reviews/m2-closing.md #1; SecurityPolicyTests).
			//The discovery channel is untouched - it opens on None before any endpoint is matched (initSecurityPolicy).
			config->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
			INFO( "[{}]Offering Basic256Sha256, Aes128_Sha256_RsaOaep and Aes256_Sha256_RsaPss, Sign & Encrypt only, with certificate '{}'", hex(Handle()), settings.Certificate.Path.string() );
			for( let ctor : securedPolicies ){
				sc = ctor( &securityPolicies.get()[initialized], *certificate, *privateKey, &_logger ); THROW_IFX( sc, UAClientException(sc, Handle()) );
				++initialized;
			}
		}
		else{
			//No endpoint filter - there is no uri to hold the server's against.
			INFO( "[{}]Using SecurityPolicy None - '{}' has no certificateUri; user tokens are encrypted, under the policy the server names, with certificate '{}'", hex(Handle()), Slug(), settings.Certificate.Path.string() );
		}
		auto grown = ( UA_SecurityPolicy* )UA_realloc( config->authSecurityPolicies, sizeof(UA_SecurityPolicy) *(config->authSecurityPoliciesSize + securedPolicies.size()) );
		THROW_IFX( !grown, UAClientException(UA_STATUSCODE_BADOUTOFMEMORY, Handle()) );//realloc failure leaves the original block valid; don't overwrite the pointer with null (would leak it and null-deref below).
		config->authSecurityPolicies = grown;
		for( let ctor : securedPolicies ){
			sc = ctor( &config->authSecurityPolicies[config->authSecurityPoliciesSize], *certificate.get(), *privateKey.get(), config->logging ); THROW_IFX( sc, UAClientException(sc, Handle()) );
			config->authSecurityPoliciesSize++;//one at a time:  what is constructed is the config's to clear, whatever throws next.
		}
		//A secret in the clear - a password or an issued token under a token policy of None, on any channel that is not
		//Sign & Encrypt - is refused by open62541 itself unless this is set (matchUserTokenPolicy), and the gateway sets it only
		//on the operator's say-so:  /gateway/allowPlaintextPassword, default false (reviews/security-matrix.md #6, ruled 09-18;
		//PlaintextPasswordTests).  The server has to agree as well - open62541's own servers take such a token only under
		//their allowNonePolicyPassword, and then only a password.
		config->allowNonePolicyPassword = Settings::FindBool( "/gateway/allowPlaintextPassword" ).value_or( false );
		let secret = Credential.Type()==ETokenType::Username || Credential.Type()==ETokenType::IssuedToken;
		if( config->allowNonePolicyPassword && secret )
			WARN( "[{}]/gateway/allowPlaintextPassword is on - '{}' sends its {} credential unencrypted if '{}' offers it no encrypting token policy.", hex(Handle()), Slug(), TokenTypeName(Credential.Type()), Url() );
		config->securityPolicies = securityPolicies.release();
		config->securityPoliciesSize = size;
		config->secureChannelLifeTime = 60 * 60 * 1000;

		return config;
	}
	α UAClient::AddSessionAwait( VoidAwait::Handle h )ι->void{
		{
			lg _{ _sessionAwaitableMutex };
			_sessionAwaitables.emplace_back( move(h) );
		}
		Process( ConnectRequestId, "Connect" );
	}
	α UAClient::TriggerSessionAwaitables()ι->void{
		vector<VoidAwait::Handle> handles;
		{
			lg _{ _sessionAwaitableMutex };
			for_each( _sessionAwaitables, [&handles](auto&& h){handles.emplace_back(h);} );
			_sessionAwaitables.clear();
		}
		for_each( handles, [](auto&& h){h.resume();} );
	}

	α UAClient::PresentedCertificate()Ι->optional<Crypto::CryptoSettings>{
		if( Credential.Type()==ETokenType::Certificate && AppClient()->SslSettings )
			return AppClient()->SslSettings;
		return _opcServer.CertificateUri.empty() ? optional<Crypto::CryptoSettings>{} : optional<Crypto::CryptoSettings>{ CryptoSettings() };
	}
	//The statuses a server turns a client certificate down with.  The first is the one that matters:  the Jde OpcServer and
	//Kepware both answer an untrusted certificate with BadSecurityChecksFailed at the OPN, whatever their own logs call it.
	Ω refusesCertificate( StatusCode sc )ι->bool{
		switch( sc ){
		case UA_STATUSCODE_BADSECURITYCHECKSFAILED:
		case UA_STATUSCODE_BADCERTIFICATEUNTRUSTED://ours are answered above it - ServerTrust::Rejection - so what is left is the server's.
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
	α UAClient::ApplicationUri()Ι->string{
		return ToString( UA_Client_getConfig(_ptr)->applicationUri );
	}
	α UAClient::AdvertisedUri()Ι->string{
		return ToString( UA_Client_getConfig(_ptr)->clientDescription.applicationUri );
	}
	α UAClient::LogClientEndpoints()ι->void{
		vector<string> policyUris;
		auto config = UA_Client_getConfig( _ptr );
		for( let& sp : Iterable<UA_SecurityPolicy>(config->securityPolicies, config->securityPoliciesSize) )
			policyUris.emplace_back( ToString(sp.policyUri) );
		//both uris: config->applicationUri filters the *server's* endpoints - the connection's certificateUri, and a wrong one
		//rejects every endpoint - and clientDescription's is what we advertise:  the gateway's own, from the SAN of the certificate it presents (Configuration()).
		INFO( "[{}]Client Security Policies: {}, applicationUri filter: '{}', advertised applicationUri: '{}'", hex(Handle()), Str::Join(policyUris), ToString(config->applicationUri), ToString(config->clientDescription.applicationUri) );
	}
	//The secured policies Configuration() carries - for the channel with a certificateUri, for the user token always.
	constexpr array<sv,3> _securedPolicyUris{ "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256", "http://opcfoundation.org/UA/SecurityPolicy#Aes128_Sha256_RsaOaep", "http://opcfoundation.org/UA/SecurityPolicy#Aes256_Sha256_RsaPss" };
	constexpr sv _securedPolicyNames{ "Basic256Sha256, Aes128_Sha256_RsaOaep and Aes256_Sha256_RsaPss" };
	Ω carried( str policyUri )ι->bool{ return find( _securedPolicyUris, policyUri )!=_securedPolicyUris.end(); }
	Ω policyName( str uri )ι->string{ let i = uri.rfind( '#' ); return i==string::npos ? uri : uri.substr( i+1 ); }//"…/SecurityPolicy#Basic256Sha256" -> "Basic256Sha256": the fragment is the part that says anything.
	//What GetEndpoints on `url` answers - logged one line per endpoint, its policy and mode and every token policy with the policy
	//that encrypts the token (`Username@Basic256Sha256`; `@None` is a token sent in the clear) - and summarized for the caller.
	α UAClient::LogServerEndpoints( str url, Jde::Handle h )ι->EndpointSummary{
		EndpointSummary endpoints;
		Logger logger{ h };
		UA_ClientConfig config{};
		config.logging = &logger;
		if( let sc = UA_ClientConfig_setDefault(&config); sc ){
			WARN( "[{}]Could not configure an endpoint client for url='{}': '({}){}'", hex(h), url, hex(sc), UAException::Message(sc) );
			UA_ClientConfig_clear( &config );
			return endpoints;
		}
		//The discovery channel, preset:  with config.endpoint empty, UA_Client_getEndpoints' connect fetches the endpoints and then
		//SELECTS one for its own channel (ua_client_connect.c endpointUnconfigured), and a None-only client finds none at a url whose
		//endpoints are all secured - Kepware publishes its None endpoint under the hostname url alone - so the very listing that
		//would explain the failure fails the same way.  A configured endpoint skips the selection (connectIterate: "an exact
		//endpoint was configured"), and GetEndpoints is a discovery service every server answers on a None channel.
		config.endpoint.endpointUrl = UA_STRING_ALLOC( url.c_str() );
		config.endpoint.securityMode = UA_MESSAGESECURITYMODE_NONE;
		UA_String_copy( &UA_SECURITY_POLICY_NONE_URI, &config.endpoint.securityPolicyUri );
		UA_Client *client = UA_Client_newWithConfig( &config );//takes a copy - from here the client owns the config, and UA_Client_delete clears it.
		if( !client ){
			WARN( "[{}]Could not create an endpoint client for url='{}'", hex(h), url );
			UA_ClientConfig_clear( &config );
			return endpoints;
		}
		UA_EndpointDescription* endpointArray{}; uint endpointArraySize{};
		if( let sc = UA_Client_getEndpoints(client, url.c_str(), &endpointArraySize, &endpointArray); sc ){
			WARN( "[{}]Could not get endpoints for url='{}': '({}){}'", hex(h), url, hex(sc), UAException::Message(sc) );
		}
		else{
			for( auto&& ep : Iterable<UA_EndpointDescription>(endpointArray, endpointArraySize) ){
				endpoints.NoneEndpoint = endpoints.NoneEndpoint || ep.securityMode==UA_MESSAGESECURITYMODE_NONE;
				constexpr array<sv,4> securityModeNames = { "Invalid", "None", "Sign", "SignAndEncrypt" };
				let securityMode = FromEnum( securityModeNames, ep.securityMode );
				let policyUri = ToString( ep.securityPolicyUri );
				vector<string> tokenPolicies;
				for( let& utp : Iterable<UA_UserTokenPolicy>(ep.userIdentityTokens, ep.userIdentityTokensSize) ){
					let type = ToTokenType( utp.tokenType );
					let tokenPolicyUri = utp.securityPolicyUri.length ? ToString( utp.securityPolicyUri ) : policyUri;//unset means the channel's own (open62541 matchUserTokenPolicy).
					tokenPolicies.emplace_back( Ƒ("{}@{}", TokenTypeName(type), policyName(tokenPolicyUri)) );
					endpoints.Policies.push_back( EndpointSummary::TokenPolicy{ep.securityMode, policyUri, type, tokenPolicyUri} );
				}
				let applicationUri = ToString( ep.server.applicationUri );
				INFO( "[{}]ServerEndpoint {}/{}=[{}], applicationUri: '{}'", hex(h), policyName(policyUri), securityMode, Str::Join(tokenPolicies), applicationUri );
				if( endpoints.ServerUri.empty() )
					endpoints.ServerUri = applicationUri;//every endpoint of one server carries the same uri; keep the first non-empty.
			}
			UA_Array_delete( endpointArray, endpointArraySize, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION] );
		}
		UA_Client_delete( client );
		return endpoints;
	}

	α UAClient::StateCallback( UA_Client *ua, UA_SecureChannelState channelState, UA_SessionState sessionState, StatusCode connectStatus )ι->void{
		constexpr std::array<sv,6> sessionStates = { "Closed", "CreateRequested", "Created", "ActivateRequested", "Activated", "Closing" };
		DBG( "[{}]channelState: '{}', sessionState: '{}', connectStatus: '({}){}'", hex((uint)ua), UAException::Message(channelState), FromEnum(sessionStates, sessionState), hex(connectStatus), UAException::Message(connectStatus) );
		if( auto client = sessionState == UA_SESSIONSTATE_ACTIVATED ? UAClient::TryFind(ua) : sp<UAClient>{}; client ){
			//a found client is a re-activation: no RequestDrain here - open62541 re-reads the namespace array only on
			//a fresh client (haveNamespaces), so flagging would leave the drain waiting out its full limit for a read
			//that never fires.
			client->TriggerSessionAwaitables();
			client->ClearRequest( ConnectRequestId );
			if( client->RecordSession() )
				client->Resubscribe();//a different session than the one we were monitoring on: its subscriptions are gone, so rebuild them or nothing pushes again.
		}

		if( sessionState == UA_SESSIONSTATE_ACTIVATED || connectStatus ){
			_awaitingActivation.erase_if( [ua, sessionState,connectStatus](sp<UAClient> client){
				if( client->UAPointer()!=ua )return false;

				string detail;//the reason handed to the waiting requests, not just the log - empty falls back to "Connection Failed".
				if( auto rejection = connectStatus ? ServerTrust::Rejection(*UA_Client_getConfig(ua)) : string{}; rejection.size() ){
					detail = move( rejection );//we rejected the server's certificate - already logged by the verifier.  Checked first: the status is BadCertificateUntrusted, the same code the server answers when it rejects ours.
				}
				else if( connectStatus == UA_STATUSCODE_BADIDENTITYTOKENREJECTED ){
					let endpoints = LogServerEndpoints( client->ConnectUrl(), client->Handle() );//the url that reached the server - a name's first address may not (ReachableUrl).
					client->LogClientEndpoints();
					//open62541 reports "No suitable endpoint found" as BadIdentityTokenRejected, so its usual causes read as a credential
					//problem.  A token type the server never offers (install-issues #24: anonymous, to a server that takes certificates
					//and issued tokens).  A credential the server takes only in the clear, which open62541 will not send unless
					///gateway/allowPlaintextPassword says so (security-matrix #6).  A connection with no certificateUri - SecurityPolicy
					//None, its tokens still encrypted (Configuration) - at a url with no unsecured endpoint (Kepware publishes none under
					//127.0.0.1), or whose unsecured endpoint takes the token only under a policy the gateway does not carry.  A server
					//that offers the credential only under policies the gateway does not carry at all - a deprecated one, an ECC one
					//(security-matrix #4; SecurityPolicyTests).  And with a certificateUri, our configured applicationUri filtering out every endpoint (matchEndpoint):  name
					//both uris - the fix is the slug's certificateUri, which is what Configuration() puts in the filter.  NoSecurityTests,
					//PlaintextPasswordTests, ExternalServerTests; reviews/security-matrix.md.
					let type = client->Credential.Type();
					let noUri = client->_opcServer.CertificateUri.empty();
					let allowPlain = UA_Client_getConfig( ua )->allowNonePolicyPassword;
					let secretType = type==ETokenType::Username || type==ETokenType::IssuedToken;
					let noneUri = ToString( UA_SECURITY_POLICY_NONE_URI );
					//What this client could have presented:  on an endpoint it can reach (None without a certificateUri;  with one, a
					//Sign & Encrypt endpoint under a secured policy it carries), a token under a secured policy it carries - or under None where that puts no secret on the
					//wire unencrypted:  an anonymous token, a Sign & Encrypt channel, or the operator's allowPlaintextPassword.
					bool offered{}, presentable{}, refusedPlain{}, securedOffer{}, belowFloor{};
					flat_set<string> foreign;//policies the server asks for and the gateway does not carry - Basic256, an ECC one.
					for( let& p : endpoints.Policies ){
						if( p.Type!=type )
							continue;
						offered = true;
						if( !noUri && p.Mode!=UA_MESSAGESECURITYMODE_SIGNANDENCRYPT ){//under the floor a certificateUri pins (Configuration) - out of reach, whatever securityLevel it claims (m2-closing #1).
							belowFloor = true;
							continue;
						}
						let inClear = secretType && p.Policy==noneUri && p.Mode!=UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
						let encryptable = type==ETokenType::Anonymous || carried( p.Policy ) || ( p.Policy==noneUri && type!=ETokenType::Certificate );
						if( !encryptable && p.Policy!=noneUri )
							foreign.emplace( policyName(p.Policy) );
						if( carried(p.ChannelPolicy) && noUri ){//out of reach without a certificateUri - but the way out, if it takes the credential encrypted.
							securedOffer = securedOffer || ( encryptable && !inClear );
							continue;
						}
						if( !carried(p.ChannelPolicy) && p.ChannelPolicy!=noneUri ){//a channel policy the gateway does not carry.
							foreign.emplace( policyName(p.ChannelPolicy) );
							continue;
						}
						if( encryptable && (!inClear || allowPlain) )
							presentable = true;
						else if( inClear )
							refusedPlain = true;
					}
					let foreignNames = Str::Join( vector<string>{foreign.begin(), foreign.end()} );
					if( endpoints.ServerUri.empty() || presentable ){}//the endpoints could not be read, or something fits: the filter, below, or a genuine rejection.
					else if( !offered )
						detail = Ƒ( "'{}' does not offer {} authentication (offered: [{}])", client->Url(), TokenTypeName(type), TokenTypeName(endpoints.Tokens()) );
					else if( refusedPlain )
						detail = Ƒ( "'{}' takes {} authentication only under a token policy of None on a channel that is not encrypted, which would put the credential on the wire in the clear - refused.  {}/gateway/allowPlaintextPassword=true sends it as it is", client->Url(), TokenTypeName(type), securedOffer ? Ƒ("Set the connection's certificateUri to the server's applicationUri '{}' to sign in encrypted, or ", endpoints.ServerUri) : string{"Setting "} );
					else if( noUri && !endpoints.NoneEndpoint )
						detail = Ƒ( "'{}' has no certificateUri, so the gateway connects with SecurityPolicy None, and '{}' has no unsecured endpoint - set the connection's certificateUri to the server's applicationUri '{}' to connect secured", client->Slug(), client->Url(), endpoints.ServerUri );
					else if( noUri )
						detail = Ƒ( "'{}' has no certificateUri, so the gateway connects with SecurityPolicy None, and the unsecured endpoint of '{}' takes {} authentication only under a security policy the gateway does not carry ([{}]; it carries {}) - try the connection with its certificateUri set to the server's applicationUri '{}'", client->Slug(), client->Url(), TokenTypeName(type), foreignNames, _securedPolicyNames, endpoints.ServerUri );
					else if( foreign.size() )
						detail = Ƒ( "'{}' offers {} authentication only under security policies the gateway does not carry ([{}]) - it carries {}", client->Url(), TokenTypeName(type), foreignNames, _securedPolicyNames );
					else if( belowFloor )
						detail = Ƒ( "'{}' has a certificateUri, which means Sign & Encrypt, and '{}' offers {} authentication only on endpoints that are not (None or Sign) - refused rather than downgraded.  Enable a Sign & Encrypt endpoint on the server, or clear the connection's certificateUri to connect unsecured", client->Slug(), client->Url(), TokenTypeName(type) );
					if( let clientUri = client->ApplicationUri(); detail.empty() && clientUri.size() && !endpoints.ServerUri.empty() && clientUri!=endpoints.ServerUri )
						detail = Ƒ( "the connection's certificateUri '{}' is not the applicationUri of the server at '{}', which is '{}' - every endpoint is filtered out; correct the certificateUri", clientUri, client->Url(), endpoints.ServerUri );
					if( detail.size() )
						ERR( "[{}]{}", hex(client->Handle()), detail );
				}
				else if( auto presented = refusesCertificate(connectStatus) ? client->PresentedCertificate() : optional<Crypto::CryptoSettings>{}; presented ){
					//The other direction:  the server turned OUR certificate down.  It says so with a status and nothing a client can show
					//- Kepware's "An error occurred verifying security." rides the ERR message into the log - so where our verifier, above,
					//names the server and the fix, this read as a bare BadSecurityChecksFailed, and only certificate authentication named a
					//file at all (reviews/security-matrix.md #12; RefusedCertificateTests, CertTests.Authenticate_Bad).  The status can
					//have other causes, hence "usually";  the certificate is the one to rule out first, and the file is what an operator
					//needs either way.  Path only in the detail - the subject/issuer/SAN dump belongs in the log.
					detail = Ƒ( "'{}' refused the secure channel - usually a server that does not trust this gateway's certificate yet.  It presented '{}':  trust that file in the server - a Jde OpcServer takes it from any of its /access/trustedCertDirs, another server from its own trust list, where it normally waits among the rejected certificates - and connect again", client->Url(), presented->Certificate.Path.string() );
					ERR( "[{}]{}", hex(client->Handle()), detail );
					try{//what the file holds - subject, SAN, expiry - for the log;  the settings object knows only where it is.
						Crypto::Certificate{ Crypto::ReadCertificate(presented->Certificate.Path) }.Log( Ƒ("[{}]Presented certificate '{}'", hex(client->Handle()), presented->Certificate.Path.string()) );
					}
					catch( const std::exception& ){}
				}

				client->ClearRequest( ConnectRequestId );//previous clear didn't have client
				if( sessionState == UA_SESSIONSTATE_ACTIVATED ){
					client->RecordSession();//the baseline the re-activation branch above compares against; nothing to rebuild on a first activation.
					ClearConnectError( client->Slug() );//a reachable slug - whatever the previous attempt failed on no longer applies.
					client->_asyncRequest.RequestDrain();//open62541 fires its namespace-array read right after this callback returns; ProcessingLoop must keep pumping until the reply is serviced (OnServiceBegin).
					{
						ul _{ _clientsMutex };
						client->Connected = true;
						auto& opcCreds = _clients.try_emplace( client->Slug() ).first->second;
						let inserted = opcCreds.try_emplace( client->Credential, client ).second;
						ASSERT( inserted ); // not sure why we would already have a record.
					}
					Post( [client]()ι->void {
						ConnectAwait::Resume( move(client) );
					});
				}
				else{
					//Remembered for serverConnections{connectionStatus}: the waiting requests get the reason once, the list has no other
					//way to learn the slug is broken.  Named by status where there is no richer detail - the caller's exception
					//carries the code separately, but a stored "Connection Failed" would say nothing about what went wrong.
					SetConnectError( client->Slug(), detail.size() ? detail : string{UAException::Message(connectStatus)} );
					string message{ detail.size() ? move(detail) : string{"Connection Failed"} };
					client->StopProcessing();// Break the UAClient<->_asyncRequest._client self-reference; on the failure path the client never enters _clients, so Shutdown would never Stop() it and the UAClient (and its UA_Client) would leak.
					Post(
						[client,connectStatus,message=move(message)]()ι->void {
							ConnectAwait::Resume(
								client->Slug(),
								client->Credential,
								UAClientException{connectStatus, client->Handle(), message}
							);
						}
					);
				}

				return true;
			});
		}
	}
	//Fires inside run_iterate (client mutex held, on the strand) as each service response is processed.  SERVICE_BEGIN
	//arrives while a request of ours is still tracked in _requests (its completion callback, which untracks it, runs
	//just after), so a request-id we never sent is open62541's own traffic - the post-activation namespace-array read
	//the processing loop's drain waits on.
	α UAClient::ServiceNotificationCallback( UA_Client* ua, UA_ApplicationNotificationType type, const UA_KeyValueMap payload )ι->void{
		if( type!=UA_APPLICATIONNOTIFICATIONTYPE_SERVICE_BEGIN )
			return;
		let requestId = (const UA_UInt32*)UA_KeyValueMap_getScalar( &payload, UA_QUALIFIEDNAME(0, (char*)"request-id"), &UA_TYPES[UA_TYPES_UINT32] );
		if( auto client = requestId ? TryFind(ua) : sp<UAClient>{}; client )
			client->_asyncRequest.OnServiceBegin( *requestId );
	}

	α inactivityCallback( UA_Client* /*client*/ )->void{
		BREAK;
	}
	α subscriptionInactivityCallback( UA_Client *client, SubscriptionId subscriptionId, void* /*subContext*/ ){
		DBG( "[{}.{}]subscriptionInactivityCallback", hex((uint)client), hex(subscriptionId) );
	}
	α UAClient::Create()ε->UA_Client*{
		//Every throwing step stays ABOVE the first allocation.  Create() is called from UAClient's member-initializer
		//list, so a throw out of it escapes before the object exists: ~UAClient never runs, and nothing else knows
		//about the event loop, the connection managers or the UA_Client.  Reading the credential material first means
		//the only failure that can realistically happen (missing/unreadable cert) leaves nothing to clean up.
		ByteStringPtr certificate{ nullptr, UA_ByteString_delete };
		ByteStringPtr privateKey{ nullptr, UA_ByteString_delete };
		if( Credential.Type()==ETokenType::Certificate ){
			//never blind-deref: Configuration() guards the same optional, and cert auth without ssl settings is a
			//configuration error, not a crash.
			let& ssl = AppClient()->SslSettings; THROW_IF( !ssl, "[{}]Certificate authentication configured but the app client has no ssl settings.", hex(Handle()) );
			certificate = ToUAByteString( Crypto::ReadCertificate(ssl->Certificate.Path) );
			privateKey = ToUAByteString( Crypto::ReadPrivateKey(ssl->PrivateKey) );
		}
		_config.logging = &_logger;
		_config.eventLoop = UA_EventLoop_new_POSIX( _config.logging );
		UA_ConnectionManager *tcpCM = UA_ConnectionManager_new_POSIX_TCP( "tcp connection manager"_uv );
		_config.eventLoop->registerEventSource( _config.eventLoop, (UA_EventSource*)tcpCM );
		_config.timeout = 10000; /*ms*/
		_config.stateCallback = StateCallback;
		_config.serviceNotificationCallback = ServiceNotificationCallback;//SERVICE_BEGIN ends the post-activation drain - see AsyncRequest::OnServiceBegin.
		_config.inactivityCallback = inactivityCallback;
		_config.subscriptionInactivityCallback = subscriptionInactivityCallback;
		if( Credential.Type()==ETokenType::Username ){
			UA_ClientConfig_setAuthenticationUsername( &_config, Credential.LoginName().c_str(), Credential.Password().c_str() );
			INFO( "[{}]Using username/password authentication: '{}'", hex(Handle()), Credential.LoginName() );
		}else if( Credential.Type()==ETokenType::Certificate ){
			INFO( "[{}]Using certificate authentication: '{}'", hex(Handle()), AppClient()->SslSettings->Certificate.ToString() );
			UA_ClientConfig_setAuthenticationCert( &_config, *certificate, *privateKey );//read above, before anything was allocated.
		}else if( Credential.Type()==ETokenType::IssuedToken ){
			ASSERT( Credential.Token().size() );
			INFO( "[{}]Using issued token authentication ({} bytes).", hex(Handle()), Credential.Token().size() );
			UA_IssuedIdentityToken* identityToken = UA_IssuedIdentityToken_new();
			identityToken->policyId = AllocUAString( "open62541-anonymous-policy"sv );
			UA_ByteString_allocBuffer( &identityToken->tokenData, Credential.Token().size() );
			identityToken->tokenData.length = Credential.Token().size();
			memcpy( identityToken->tokenData.data, Credential.Token().data(), Credential.Token().size() );
			UA_ExtensionObject_setValue( &_config.userIdentityToken, identityToken, &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
		}
		else
			WARN( "[{}]Using anonymous authentication.", hex(Handle()) );

		//	UA_ClientConfig_setAuthenticationCert( &_config, Credential.Certificate().c_str(), Credential.PrivateKey().c_str() );
		UA_ConnectionManager *udpCM = UA_ConnectionManager_new_POSIX_UDP( "udp connection manager"_uv );
		_config.eventLoop->registerEventSource( _config.eventLoop, (UA_EventSource*)udpCM );
		auto ua = UA_Client_newWithConfig( &_config );
		UA_Client_getConfig( ua )->eventLoop->logger = _config.logging;

		return ua;
	}
	namespace{
		using boost::asio::ip::tcp;
		//The first of `endpoints`, in their order, that takes a tcp connection within `timeout` - refused, unreachable and silent all
		//count as no.  All are tried at once, so a name costs its slowest dead address and not their sum, and it returns as soon as
		//the order is settled:  at once when the first answers.
		Ω firstAccepting( const vector<tcp::endpoint>& endpoints, steady_clock::duration timeout )ι->optional<uint>{
			try{
				boost::asio::io_context ctx;
				vector<tcp::socket> sockets;
				sockets.reserve( endpoints.size() );
				vector<optional<bool>> connected( endpoints.size() );
				auto settled = [&connected]()->optional<uint> {
					for( uint i=0; i<connected.size() && connected[i]; ++i ){
						if( *connected[i] )
							return i;
					}
					return nullopt;
				};
				for( uint i=0; i<endpoints.size(); ++i ){
					sockets.emplace_back( ctx );
					sockets.back().async_connect( endpoints[i], [&,i]( const boost::system::error_code& ec ){
						connected[i] = !ec;
						if( settled() )
							ctx.stop();
					});
				}
				ctx.run_for( timeout );
				let first = settled();
				for( auto& socket : sockets ){
					boost::system::error_code ignored;
					socket.close( ignored );
				}
				ctx.restart();
				ctx.run();//the cancelled connects complete here, while what their handlers write to is still in scope.
				return first;
			}
			catch( const runtime_error& ){
				return nullopt;
			}
		}
	}
	α UAClient::ReachableUrl( str url, Jde::Handle h )ι->string{
		try{
			constexpr sv scheme{ "opc.tcp://" };
			if( !url.starts_with(scheme) )
				return url;
			UA_String host{}, path{}; UA_UInt16 port{ 4840 };
			const UA_String uaUrl{ url.size(), (UA_Byte*)url.data() };//views into `url` - nothing is allocated.
			if( UA_parseEndpointUrl(&uaUrl, &host, &port, &path) || !host.length )
				return url;
			const string name{ (const char*)host.data, host.length };
			boost::system::error_code ec;
			if( name.front()=='[' || (boost::asio::ip::make_address(name, ec), !ec) )//an address already - there is no second one to fall back to.
				return url;
			boost::asio::io_context ctx;
			vector<tcp::endpoint> endpoints;
			for( let& entry : tcp::resolver{ctx}.resolve(name, std::to_string(port), ec) )
				endpoints.push_back( entry.endpoint() );
			if( ec || endpoints.size()<2 )
				return url;
			let first = firstAccepting( endpoints, 3s );//windows takes about two seconds to refuse a link-local or loopback IPv6 address.
			if( !first || *first==0 )
				return url;//the address open62541 will pick answers - the name stays - or none does, which open62541 reports as it always has.
			let address = endpoints[*first].address();
			auto reachable = Ƒ( "{}{}:{}", scheme, address.is_v6() ? Ƒ("[{}]", address.to_string()) : address.to_string(), port );
			if( path.length )
				reachable += Ƒ( "/{}", sv{(const char*)path.data, path.length} );
			INFO( "[{}]'{}' resolves to {} addresses and the first, {}, takes no connection, where open62541 would stop - connecting to '{}' instead.", hex(h), name, endpoints.size(), endpoints.front().address().to_string(), reachable );
			return reachable;
		}
		catch( const std::exception& ){
			return url;
		}
	}
	α UAClient::Connect()ε->void{
		//Pre-concurrency: nothing drives this client's run_iterate until Process below starts the loop, so the direct
		//UA_Client_connectAsync/SetClient calls here are single-threaded. Process must stay the LAST statement - after
		//it, every UA_Client_* call must go through the strand (PostUA).
		_connectUrl = ReachableUrl( Url(), Handle() );//security-matrix #10 - see the header.  A probe of the first address, and only for a name with several.
		DBG( "[{}]Connecting to '{}', using '{}'", hex(Handle()), _connectUrl, Credential.ToString() );
		let sc = UA_Client_connectAsync( UAPointer(), _connectUrl.c_str() ); THROW_IFX( sc, UAException(sc) );
		auto p = shared_from_this();
		ASSERT( !_awaitingActivation.contains(p) );
		_awaitingActivation.emplace( shared_from_this() );
		_asyncRequest.SetClient( p );
		Process( ConnectRequestId, "Connect" );
	}

	α UAClient::PostUA( function<void()> f )ι->void{
		//All UA_Client_* calls must run on this client's strand (open62541 clients are not thread-safe): run_iterate,
		//async submissions, and sync services all serialize here. dispatch runs f inline when the caller is already on
		//the strand (e.g. completion callbacks inside run_iterate) and posts otherwise. `self` keeps the client - and
		//with it _asyncRequest and the raw UA_Client - alive until f runs.
		boost::asio::dispatch( _asyncRequest.Strand(), [self=shared_from_this(), f=move(f)]{f();} );
	}

	α UAClient::PostStrand( function<void()> f )ι->void{
		//Always post (never dispatch inline): the handler runs after the current strand op returns. Used to break
		//re-entrancy - e.g. resuming a caller from inside run_iterate must not unblock/destroy the awaitable while the
		//strand handler that drove run_iterate is still touching it. `self` keeps the client (and its strand) alive until f runs.
		boost::asio::post( _asyncRequest.Strand(), [self=shared_from_this(), f=move(f)]{f();} );
	}

	α UAClient::Process( RequestId requestId, sv what )ι->void{
		if( _asyncRequest.IsStopped() )
			return;
		PostUA( [this, requestId, what=string{what}]{_asyncRequest.Process(requestId, what);} );
	}

	α UAClient::ClearRequest( RequestId requestId )ι->void{
		PostUA( [this, requestId]{_asyncRequest.Clear(requestId);} );
	}

	α UAClient::StopProcessing()ι->void{
		PostUA( [this]{_asyncRequest.Stop();} );
	}

	α UAClient::ShutdownIdle( sp<UAClient> client )ι->VoidAwait::Task{
		//TTL expiry (called from the processing loop): tear down only this client - _lastRequest is per-client, so one
		//client idling out must not disconnect the others (Shutdown() stops everything).
		if( client->_monitoredNodes )//not moved out: data-change callbacks can still arrive until RemoveClient below.
			co_await client->_monitoredNodes->Shutdown();
		RemoveClient( move(client) );
	}

	α UAClient::ProcessDataSubscriptions()ι->void{
		Process( SubscriptionRequestId, "DataSubscriptions" );
	}

	//One coroutine for the whole rebuild: AnyAwait is what lets a single frame co_await both the subscribe (a VoidAwait) and
	//the monitored-item creates (a TAwait<SubscriptionAck>) - the pairing rule would otherwise force a hand-off chain.
	//
	//The listeners wait in _rebuilds, not in this frame, and each leaves only once its create has answered:  in the frame they
	//were out of reach of both unsubscribes (subscription-disconnect #4).  Nodes unsubscribed while their create was out are taken
	//off the client again when it answers.
	//
	//The client can die under it - a server still starting drops the channel again - and then nothing else knows what this
	//rebuild had not yet put back:  the dying client's stashPending finds only the creates that completed.  So every node ends one
	//of four ways - restored, refused by a live server (a retry would be refused again), unsubscribed meanwhile, or parked for the
	//reconnect chain because the client died (subscription-disconnect #2).  Connected separates refused from parked - and it used
	//to be taken on trust, as something ConnectionLost had cleared before anything could fail.  It had not:  the failure is
	//usually the first the gateway hears of the loss, and a step that failed with the connection's status on a client still
	//`Connected` was read as the server's refusal - nothing parked, no chain, the live views gone for good, and by the time the
	//processing loop deregistered the client there was nothing left on it to park (reviews/m2-closing.md #2).  So the rebuild
	//reads the failure itself - declareLost - and Connected is true to it before anything is decided.
	//A node restored just before the client dies is not parked here:  its create completed,
	//so the dying client's stashPending takes it with the rest.  A client RemoveClient discarded parks nothing:  removing it means
	//forgetting what it monitored, and a rebuild must not bring that back (subscription-disconnect #10).
	//
	//ConnectionLost, not a park alone:  a parked entry's chain asks GetClient, which hands back whatever is registered, so a dead
	//client left in _clients would be given the same rebuild a second later.  Twice is harmless - the processing loop's own call,
	//when run_iterate fails behind this one, finds nothing on the client and no entry to erase.
	Ω declareLost( const sp<UAClient>& client, StatusCode sc )ι->void{
		if( UAClient::IsConnectionLoss(sc) && client->Connected && !client->Discarded )
			UAClient::ConnectionLost( sp<UAClient>{client} );
	}
	Ω resubscribe( sp<UAClient> client, uint rebuild )ι->VoidTask{
		let handle = client->Handle();
		let slug = client->Slug();
		optional<string> subscribeError;
		try{
			if( client->Connected && rebuildHasNodes(rebuild) )//every listener may have gone already, and an empty subscription would linger.
				co_await Any( SubscribeAwait{client} );
			while( client->Connected ){
				sp<IDataChange> dataChange; flat_set<NodeId> nodes;
				{//read, not taken:  the listener stays in _rebuilds, where both unsubscribes look, until its create has answered.
					lg _{ _pendingMutex };
					auto r = _rebuilds.find( rebuild );
					if( r==_rebuilds.end() || r->second.Nodes.empty() )
						break;
					dataChange = r->second.Nodes.begin()->first;
					nodes = r->second.Nodes.begin()->second;//the ack's results come back in this set's order.
				}
				let ack = co_await Any( DataChangeAwait{nodes, dataChange, client} );
				flat_set<NodeId> restored;
				if( (uint)ack.results_size()==nodes.size() ){//fewer:  GetResult found no request - the dying client's stashPending, or this listener's close, took it.
					auto result = ack.results().begin();
					for( let& node : nodes ){
						let sc = (StatusCode)(result++)->status_code();
						if( !sc )
							restored.emplace( node );
						declareLost( client, sc );//a create refused, or cancelled in flight, by the connection going - not the server's word on the node.  Before the lock:  it parks under it.
					}
				}
				flat_set<NodeId> dropped;//restored for a listener that unsubscribed them while the create was out.
				uint wantedCount{}, parked{}, parkedListeners{};
				bool died{}, start{};
				{
					lg _{ _pendingMutex };
					auto& remaining = _rebuilds[rebuild].Nodes;//only this frame erases its entry.
					flat_set<NodeId> wanted;
					if( auto p = remaining.find(dataChange); p!=remaining.end() ){
						wanted = move( p->second );
						remaining.erase( p );
					}
					wantedCount = wanted.size();
					for( let& node : restored ){
						if( !wanted.contains(node) )
							dropped.emplace( node );
					}
					died = !client->Connected;
					if( died && !client->Discarded ){//into _pending under the lock they leave _rebuilds by:  this listener's unrestored nodes, and every listener not yet attempted.
						for( let& node : wanted ){
							if( !restored.contains(node) )
								remaining[dataChange].emplace( node );
						}
						parked = nodeCount( remaining );
						parkedListeners = remaining.size();
						start = parkLocked( slug, client->Credential, remaining );
						remaining.clear();
					}
				}
				if( dropped.size() ){//off the client again - and out of _pending, should the client's death have parked them first.
					client->MonitoredNodes().Unsubscribe( flat_set<NodeId>{dropped}, dataChange );
					UAClient::UnsubscribePending( slug, dataChange, dropped );
				}
				let kept = restored.size()-dropped.size();
				let failed = wantedCount-kept;
				let gone = nodes.size()-wantedCount;
				//One call, not an if/else:  the log macros expand to a bare `if`, so an `else` after one does not parse.
				LOG( failed ? ELogLevel::Warning : ELogLevel::Information, _tags, "[{}]Re-subscribed {} of {} node(s) for '{}'{}{}.", hex(handle), kept, nodes.size(), dataChange->to_string(),
					failed ? !died ? " - the server refused the rest" : client->Discarded ? " - the client was removed, the rest are dropped" : " - the client died, the rest go back to the reconnect" : "",
					gone ? Ƒ(" - {} unsubscribed during the rebuild", gone) : string{} );
				if( parked )
					WARN( "[{}]Connection to '{}' lost while restoring its subscriptions - {} node(s) for {} listener(s) go back to the reconnect.", hex(handle), slug, parked, parkedListeners );
				if( start )
					startReconnect( slug, client->Credential );
			}
		}
		catch( runtime_error& e ){
			subscribeError = e.what();
			if( let p = dynamic_cast<const Exception*>(&e); p && p->HasCode() )
				declareLost( client, (StatusCode)p->Code() );//the subscribe went the same way - park below, rather than "a live client refused it".
		}
		Listeners left;//never attempted:  the subscribe failed, or the client died before their turn.
		bool start{};
		{
			lg _{ _pendingMutex };
			if( auto r = _rebuilds.find(rebuild); r!=_rebuilds.end() ){
				left = move( r->second.Nodes );
				_rebuilds.erase( r );
				if( !client->Connected && !client->Discarded )
					start = parkLocked( slug, client->Credential, left );
			}
		}
		if( left.empty() )
			co_return;
		//A live client refused the subscription:  nothing retries, and loud because the symptom is silence - writes keep working and
		//the live view never comes back.  Two ifs, not an if/else:  the log macros expand to a bare `if`.
		if( client->Connected )
			ERR( "[{}]Could not re-create the subscription; {} listener(s)' data changes stay dead:  {}", hex(handle), left.size(), subscribeError.value_or("") );
		if( !client->Connected && client->Discarded )
			DBG( "[{}]Client removed while restoring its subscriptions - {} node(s) for {} listener(s) are dropped with it.", hex(handle), nodeCount(left), left.size() );
		if( !client->Connected && !client->Discarded )
			WARN( "[{}]Connection to '{}' lost while restoring its subscriptions - {} node(s) for {} listener(s) go back to the reconnect.", hex(handle), slug, nodeCount(left), left.size() );
		if( start )
			startReconnect( slug, client->Credential );
	}

	//A session the server drops takes every subscription with it, and open62541 does not rebuild them - it only re-creates the
	//session, so writes recover and pushes never do (soak-findings #8: 12 min of successful writes with no data change, nothing
	//logged after DeleteSubscriptionCallback).  Called on re-activation, this puts back what this client was monitoring.
	//The reconnect half of #10.  Nothing else will do it: a client is built lazily by whatever request needs one, so a
	//connection that only subscribes has no traffic of its own to revive it and would stay dead until someone wrote to it.
	//It ends when the subscription is back, when every subscriber has gone, or at shutdown.
	//
	//Two coroutines, not one loop:  this is the house hand-off chain, because the two awaitables have different task types.
	//An earlier version bridged ConnectAwait through Any() so one frame could do both, and that is not safe here - take the
	//awaitable as your own task type, the way UAClient::Retry does.
	constexpr auto MaxReconnectDelay{ 15s };//capped, not given up on:  an industrial server can be down for a maintenance window and the live view is expected back with it.
	constexpr uint MaxRejections{ 3 };//in a row, ~7s apart at most:  one could be a server still loading its users; three is the credential or the certificate.

	//A connect failure a retry would only repeat:  the server refusing this credential or this client's certificate, the gateway
	//refusing the server's, or the connection row gone.  Anything else - refused, closed, timed out - is the server being away,
	//which is what the chain waits out.  The chain used to retry these too, forever:  a rotated password cost the server a real
	//login every 15s for as long as the subscriber's websocket lived (subscription-disconnect #13).
	Ω isRejection( const std::exception& e )ι->bool{
		let p = dynamic_cast<const Exception*>( &e );
		if( !p )
			return false;
		if( p->HttpStatus()==EHttpStatus::NotFound )//ConnectAwait::Create:  no ServerCnnctn row for the slug.
			return true;
		if( !p->HasCode() )
			return false;
		switch( (StatusCode)p->Code() ){
		case UA_STATUSCODE_BADIDENTITYTOKENREJECTED://also open62541's "no suitable endpoint" - a configuration mismatch, just as lasting.
		case UA_STATUSCODE_BADIDENTITYTOKENINVALID:
		case UA_STATUSCODE_BADUSERACCESSDENIED:
		case UA_STATUSCODE_BADUSERSIGNATUREINVALID:
		case UA_STATUSCODE_BADCERTIFICATEUNTRUSTED://either side's:  ServerTrust reports the gateway refusing the server with this code too.
		case UA_STATUSCODE_BADCERTIFICATEINVALID:
		case UA_STATUSCODE_BADCERTIFICATEREVOKED:
		case UA_STATUSCODE_BADCERTIFICATEURIINVALID:
		case UA_STATUSCODE_BADSECURITYPOLICYREJECTED:
			return true;
		default:
			return false;
		}
	}
	Ω connectPending( ServerCnnctnNK slug, Credential credential, uint attempt, steady_clock::duration delay )ι->ConnectAwait::Task;

	//ShuttingDown(), never Finalizing():  Finalizing is set after every shutdown function has run, so a chain gated on it could
	//still reconnect while UAClient::Shutdown was tearing the clients down (subscription-disconnect #3).
	Ω waitThenConnect( ServerCnnctnNK slug, Credential credential, uint attempt, steady_clock::duration delay )ι->DurationTimer::Task{
		sp<DurationTimer> timer;
		{//registered under the same lock StopReconnects cancels under:  it either finds this wait or has already cleared the entry.
			lg _{ _pendingMutex };
			auto p = Process::ShuttingDown() ? nullptr : pendingFind( slug, credential );
			if( !p )
				co_return;
			p->Wait = timer = ms<DurationTimer>( delay );
		}
		++_reconnectsWaiting;
		(void)co_await *timer;//a cancel resumes early with an error - not the signal; the checks below are.
		--_reconnectsWaiting;
		if( Process::ShuttingDown() )
			co_return;
		{//every subscriber gone (tabs closed while the server was down) - nothing left to restore.
			lg _{ _pendingMutex };
			auto p = pendingFind( slug, credential );
			if( p && p->Wait==timer )
				p->Wait = nullptr;
			if( !p || p->Nodes.empty() ){
				if( p )
					pendingErase( slug, credential );
				co_return;
			}
		}
		connectPending( move(slug), move(credential), attempt, delay );
	}

	Ω connectPending( ServerCnnctnNK slug, Credential credential, uint attempt, steady_clock::duration delay )ι->ConnectAwait::Task{
		try{
			auto client = co_await UAClient::GetClient( string{slug}, credential );
			if( Process::ShuttingDown() ){
				//Connected while the process was stopping.  UAClient::Shutdown's client snapshot can predate this client, and one left
				//running keeps the executor alive with its loop and ping until the shutdown watchdog exits the process hard.
				UAClient::RemoveClient( move(client) );
				co_return;
			}
			uint rebuild{}, listenerCount{};
			{//straight from _pending into _rebuilds under the one lock, so an unsubscribe finds them in one or the other (#4).
				lg _{ _pendingMutex };
				Listeners listeners;
				if( auto p = pendingFind(slug, credential); p )
					listeners = move( p->Nodes );
				pendingErase( slug, credential );//the chain ends here:  the rebuild owns them now, and parks back whatever this client dies before restoring (#2).
				listenerCount = listeners.size();
				if( listenerCount )
					rebuild = rebuildStart( slug, move(listeners) );
			}
			if( !rebuild )
				co_return;
			INFO( "[{}]Reconnected to '{}' after {} attempt(s) - restoring {} listener(s)' monitored nodes.", hex(client->Handle()), slug, attempt, listenerCount );
			resubscribe( move(client), rebuild );//the same rebuild the session-loss path uses.
		}
		catch( runtime_error& e ){
			//A server away for a day is ~5,800 attempts at the 15s cap, so only the first failure and each change of it warn - a repeat
			//logs at debug, and the warning in force always names why.  A rejection a retry would only repeat ends the chain after
			//MaxRejections in a row, dropping what it was restoring (#13).
			let rejected = isRejection( e );
			let p = dynamic_cast<const Exception*>( &e );
			let failure = p && p->HasCode() ? Ƒ( "{:x}", p->Code() ) : string{ e.what() };//the code, where there is one:  the message carries per-attempt detail.
			let next = std::min( delay*2, steady_clock::duration{MaxReconnectDelay} );
			bool found{}, changed{};
			uint rejections{}, listeners{};
			{
				lg _{ _pendingMutex };
				if( auto entry = pendingFind(slug, credential); entry ){//gone:  every subscriber left, or shutdown - nothing to restore.
					found = true;
					changed = entry->LastFailure!=failure;
					entry->LastFailure = failure;
					rejections = entry->Rejections = rejected ? entry->Rejections+1 : 0;
					if( rejections>=MaxRejections ){
						listeners = entry->Nodes.size();
						pendingErase( slug, credential );
					}
				}
			}
			let giveUp = rejections>=MaxRejections;
			if( giveUp )
				ERR( "Giving up reconnecting to '{}' after {} rejections in a row - {} listener(s)' monitored nodes are dropped until they subscribe again:  {}", slug, rejections, listeners, e.what() );
			if( found && !giveUp )
				LOG( changed ? ELogLevel::Warning : ELogLevel::Debug, _tags, "Could not reconnect to '{}' to restore its subscriptions (attempt {}{}) - retrying in {}:  {}", slug, attempt, rejected ? Ƒ(", rejection {} of {}", rejections, MaxRejections) : string{}, Chrono::ToString(next), e.what() );
			if( found && !giveUp && !Process::ShuttingDown() )
				waitThenConnect( move(slug), move(credential), attempt+1, next );
		}
	}

	Ω startReconnect( const ServerCnnctnNK& slug, const Credential& credential )ι->void{ waitThenConnect( slug, credential, 1, 1s ); }

	//Both ways into _pending - a dying client's stashPending and a rebuild its client died under - so they share one chain per key.
	//True when the caller has to start that chain (startReconnect), which it does once it has released the lock.  Always under the
	//lock the listeners were taken under, so an unsubscribe finds them in one place or the other (#4, #14).
	Ω parkLocked( const ServerCnnctnNK& slug, const Credential& credential, const Listeners& listeners )ι->bool{//_pendingMutex held
		if( listeners.empty() || Process::ShuttingDown() )//ShuttingDown under the lock StopReconnects clears under:  an entry parked after that clear would outlive Shutdown.
			return false;
		auto& creds = _pending[slug];
		let start = !creds.contains( credential );//no entry yet is no chain yet - the entry's existence is the flag, so it has to be read before the insert below creates one.
		auto& entry = creds[credential];
		for( let& [dataChange, nodes] : listeners )//merge: a second failure before the first reconnect finished must not drop the earlier listeners.
			entry.Nodes[dataChange].insert( nodes.begin(), nodes.end() );
		return start;
	}
	//Called as the client dies.  Deliberate teardowns park nothing: ShutdownIdle and Shutdown await MonitoredNodes::Shutdown
	//first, which deletes the items and leaves nothing to take.
	//
	//Connected goes false here, under the lock and after the park - not in ConnectionLost ahead of it, where it used to.  The
	//by-node unsubscribe (GatewaySocketSession::Unsubscribe) looks on the live clients - LiveClients(), which is Connected - and
	//then in _pending, and between that store and this park a session's nodes were in neither:  the whole web-thread operation
	//fits in the gap, the node was reported a failure, then parked, and the reconnect restored it for a listener that would
	//never ask again - pushed to a view the user had closed until the socket went, its ClientCalls never empty, so
	//DeleteMonitoring could not retire it either (reviews/m2-closing.md #5).  Now a client stays live until what it monitored is
	//parked:  a walk that still sees it takes the node off before the take, or finds it gone and waits on this lock for the
	//park.  After the park, not before it, for the rebuild's sake:  the take fails its in-flight create, and it reads Connected
	//under this lock to tell that apart from a refusal (resubscribe).
	Ω stashPending( const sp<UAClient>& client )ι->void{
		auto monitoredNodes = Process::ShuttingDown() ? nullptr : client->TryMonitoredNodes();//nothing reconnects on the way out, and the items stay put for Shutdown's MonitoredNodes::Shutdown to delete.
		uint nodes{}, listenerCount{};
		bool start{};
		{//take and park under the one lock, as Resubscribe takes into _rebuilds:  between the two the listeners were in neither place, so
			//UAClient::Unsubscribe's purge could miss a session closing at that instant, and the park then revived it (subscription-disconnect #14).
			lg _{ _pendingMutex };
			if( monitoredNodes ){
				let listeners = monitoredNodes->TakeForResubscribe();
				nodes = nodeCount( listeners );
				listenerCount = listeners.size();
				start = parkLocked( client->Slug(), client->Credential, listeners );
			}
			client->Connected = false;
		}
		if( !listenerCount )
			return;
		WARN( "[{}]Connection to '{}' lost with {} monitored node(s) for {} listener(s) - reconnecting to restore them.", hex(client->Handle()), client->Slug(), nodes, listenerCount );
		if( start )
			startReconnect( client->Slug(), client->Credential );
	}

	α UAClient::StopReconnects()ι->uint{
		uint cancelled{};
		lg _{ _pendingMutex };
		for( auto&& [_, creds] : _pending ){//auto&&: flat_map's iterator hands back a proxy pair.
			for( auto&& [__, entry] : creds ){
				if( entry.Wait ){//a Cancel before the wait starts is remembered - DurationTimer::Start re-issues it.
					entry.Wait->Cancel();
					++cancelled;
				}
			}
		}
		_pending.clear();
		return cancelled;
	}
	α UAClient::ReconnectsWaiting()ι->uint{ return _reconnectsWaiting; }

	α UAClient::PurgePending( const sp<IDataChange>& dataChange )ι->void{
		lg _{ _pendingMutex };
		for( auto&& [_, rebuild] : _rebuilds )//and what a rebuild is still putting back (#4) - the rebuild erases its own entry.
			rebuild.Nodes.erase( dataChange );
		//Nodes only:  an entry always has a chain running on it, and that chain erases it on its next tick once nothing is left
		//to restore (waitThenConnect).  Erasing one here would let a later failure start a second chain for the same key.
		for( auto&& [_, creds] : _pending ){
			for( auto&& [__, entry] : creds )
				entry.Nodes.erase( dataChange );
		}
	}

	α UAClient::UnsubscribePending( const ServerCnnctnNK& slug, const sp<IDataChange>& dataChange, const flat_set<NodeId>& nodes )ι->flat_set<NodeId>{
		flat_set<NodeId> dropped;
		auto drop = [&]( Listeners& listeners )ι{
			if( auto p = listeners.find(dataChange); p!=listeners.end() ){
				for( let& node : nodes ){
					if( p->second.erase(node) )
						dropped.emplace( node );
				}
				if( p->second.empty() )
					listeners.erase( p );
			}
		};
		lg _{ _pendingMutex };
		if( auto creds = _pending.find(slug); creds!=_pending.end() ){
			for( auto&& [_,entry] : creds->second )//auto&&: flat_map's iterator hands back a proxy pair.
				drop( entry.Nodes );
		}
		for( auto&& [_,rebuild] : _rebuilds ){//and what a rebuild is still putting back (#4).
			if( rebuild.Slug==slug )
				drop( rebuild.Nodes );
		}
		return dropped;
	}

	//The session's authentication token is unique per session, so it is what separates the two things ACTIVATED reports: a
	//*new* session (the server dropped ours - its subscriptions went with it, since a subscription belongs to its session)
	//from the *same* session re-activated on a new secure channel, where the server still holds everything and a rebuild
	//would duplicate the monitored items and orphan the originals.  True means "different session than the last one seen".
	α UAClient::ReadSessionToken()Ι->string{
		string token;
		UA_NodeId id{};
		UA_ByteString nonce{};
		if( !UA_Client_getSessionAuthenticationToken(_ptr, &id, &nonce) ){
			NodeId owned{ move(id) };//both come back copied - the wrapper clears the id, and the nonce is cleared below.
			UA_ByteString_clear( &nonce );
			token = owned.ToString();
		}
		return token;
	}
	α UAClient::RecordSession()ι->bool{
		auto token = ReadSessionToken();
		lg _{ _sessionTokenMutex };
		let changed = token.size() && _sessionToken.size() && token!=_sessionToken;//an unreadable or first-seen token is never a "change": there is nothing to rebuild before the first activation, and a rebuild on a guess would duplicate live items.
		_sessionToken = move( token );
		return changed;
	}

	α UAClient::Resubscribe()ι->void{
		//First, and whether or not anything is rebuilt:  the cached id belongs to the session that went away - on a server restart it is
		//stale rather than cleared (no deleteSubscriptionCallback fires), and SubscribeAwait::await_ready would skip the create and build
		//the next subscribe's items on a subscription no server has.  It used to be cleared only when there were listeners to rebuild,
		//leaving the stale id to a DeleteMonitoring pass that happened to be pending - which no longer clears what it did not empty (#6).
		SetCreatedSubscriptionResponse( nullptr );
		auto monitoredNodes = TryMonitoredNodes();//never MonitoredNodes(): a client that never subscribed has nothing to rebuild.
		if( !monitoredNodes )
			return;
		uint rebuild{}, nodes{}, listenerCount{};
		{//held across the take:  the listeners go straight off the client into _rebuilds, so an unsubscribe finds them in one or the other (#4).
			lg _{ _pendingMutex };
			auto listeners = monitoredNodes->TakeForResubscribe();
			if( listeners.empty() )
				return;
			nodes = nodeCount( listeners );
			listenerCount = listeners.size();
			rebuild = rebuildStart( Slug(), move(listeners) );
		}
		INFO( "[{}]Re-creating {} monitored node(s) for {} listener(s).", hex(Handle()), nodes, listenerCount );
		//PostStrand, not PostUA:  this runs inside StateCallback, i.e. inside run_iterate on the strand, and PostUA's dispatch
		//would submit re-entrantly on that stack.
		PostStrand( [client=shared_from_this(), rebuild]{ resubscribe( client, rebuild ); } );
	}

	α UAClient::StopProcessDataSubscriptions()ι->void{
		ClearRequest( SubscriptionRequestId );
	}

	α UAClient::ClearRequest( UA_Client* ua, RequestId requestId )ι->void{
		if( auto p = TryFind(ua); p )
			p->ClearRequest( requestId );
	}

	α UAClient::RetryVoid( function<void(sp<UAClient>&&) > f, UAException&& e, sp<UAClient>&& client )ι->ConnectAwait::Task{
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
				f( move(client) );
			}
			catch( UAException& retryEx ){
				retryEx.PrependWhat( "Retry failed" );
				retryEx.SetLevel( ELogLevel::Warning );
			}
		}
	}

	α UAClient::Unsubscribe( const sp<IDataChange>& dataChange )ι->void{
		PurgePending( dataChange );//a session that closed while its server was down must not be revived by the reconnect.
		vector<sp<UAClient>> clients;
		{
			sl _{ _clientsMutex };
			for( let& [_, credClients] : _clients ){
				for( let& [_, client] : credClients )
					clients.push_back( client );
			}
		}
		for( let& client : clients ){//outside the lock, as StatusCounts does:  Unsubscribe takes the nodes mutex.
			if( auto p = client->TryMonitoredNodes(); p )//a client that never subscribed has nothing to drop - no reason to build it one here.
				p->Unsubscribe( dataChange );
		}
		//And again after the walk.  A client that died between the purge above and the walk parked this session's nodes after the purge
		//had run, and its reconnect revived a session that had closed - one nothing would ever unsubscribe (subscription-disconnect #14).
		//stashPending takes and parks under the purge's lock, so whatever it took before the walk reached that client is parked by now,
		//and whatever it takes after, the walk had already removed.
		PurgePending( dataChange );
	}

	α UAClient::BrowsePathsToNodeIds( sv path, bool parents )Ε->flat_map<string,ExpectedNodeId>{
		ASSERT( _asyncRequest.Strand().running_in_this_thread() );//sync UA service - must be serialized with run_iterate (route callers through PostUA/UAStrandAwait).
		let segments = Str::Split( path, '/' );
		auto args = Reserve<UABrowsePath>( parents ? segments.size()-1 : 1 );
		vector<string> paths;
		for( uint i=0; i<(parents ? segments.size() : 1); ++i ){
			std::span<const sv> nodePath{ segments.begin(), segments.end()-i };
			paths.emplace_back( Str::Join(nodePath, "/") );
			args.emplace_back( UABrowsePath{nodePath, _opcServer.DefaultBrowseNs} );
		}
		return BrowsePathsToNodeIdResponse{ UA_Client_Service_translateBrowsePathsToNodeIds(_ptr, {{}, args.size(), args.data()}), paths, Handle() };
	}
	α UAClient::Find( UA_Client* ua, SL srce )ε->sp<UAClient>{
		sp<UAClient> y = TryFind( ua, srce );
		if( !y )
	 		throw Exception{ srce, ELogLevel::Debug, "[{}]Could not find client.", hex((uint)ua) };
		return y;
	}

	α UAClient::TryFind( UA_Client* ua, SL srce )ι->sp<UAClient>{
		if( Process::ShuttingDown() ){
			LOGSL( ELogLevel::Warning, srce, _tags, "Application is shutting down." );
		}else{
			sl _{ _clientsMutex };
			for( auto&& [_, credClients] : _clients ){
				for( auto&& [_, client] : credClients ){
					if( client->_ptr == ua )
						return client;
				}
			}
		}
		return {};
	}

	α UAClient::Find( str opcNK, const Gateway::Credential& cred )ι->sp<UAClient>{
		sl _{ _clientsMutex };
		sp<UAClient> y;
		if( auto creds = _clients.find(opcNK); creds!=_clients.end() ){
			if( auto client = creds->second.find(cred); client!=creds->second.end() )
				y = client->second;
		}
		return y;
	}

	UAClient::~UAClient() {
		UA_Client_delete( _ptr );
		INFO( "[{}]~UAClient( '{}', '{}' )", hex(Handle()), Slug(), Url() );
	}
}