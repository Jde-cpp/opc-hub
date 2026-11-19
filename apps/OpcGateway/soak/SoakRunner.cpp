#include "SoakRunner.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <numeric>
#include <set>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/http/ClientHttpResException.h>
#include "../tests/utils/GatewayClientSocket.h"
#include "../src/UAClient.h"
#include "../src/types/ServerCnnctn.h"
#include <jde/opc/proto/opc.Common.h>

#define let const auto

namespace Jde::Opc::Gateway::Soak{
	constexpr ELogTags _tags{ ELogTags::Test };
	using Tests::GatewayClientSocket;
	using Web::Client::ClientSocketAwait;
	using Web::Client::ClientHttpAwait;
	using Web::Client::ClientHttpRes;

	//A bare presence flag - no '=value' - falling back to a settings bool.  Settings::FindArgString cannot serve: it takes a
	//value, and Process::FindArg on a bare flag yields an empty string that it discards as unset.
	Ω argFlag( sv arg, sv path )ι->bool{
		if( Process::FindArg(string{arg}) )
			return true;
		return Settings::FindBool( path ).value_or( false );
	}

	α ActiveServers()ε->vector<ServerLeg>{
		vector<ServerLeg> legs;
		if( let servers = Json::FindArray(Settings::Value(), "/soak/servers"); servers ){
			for( let& s : *servers ){
				let& o = s.as_object();
				let flag = Json::FindString( o, "flag" );
				if( flag && !Process::FindArg(*flag) )
					continue;
				auto& leg = legs.emplace_back();
				leg.Slug = Json::AsString( o, "slug" );
				leg.Name = Json::FindString( o, "name" ).value_or( leg.Slug );
				leg.Description = Json::FindString( o, "description" ).value_or( "" );
				leg.CertificateUri = Json::FindString( o, "certificateUri" ).value_or( "" );
				leg.Url = Json::FindString( o, "url" ).value_or( "" );
				leg.User = Json::FindString( o, "user" ).value_or( "" );
				leg.Password = Json::FindString( o, "password" ).value_or( "" );
				if( flag ){//-externalUrl= etc. beat the flagged entry's settings, mirroring Settings::FindArgString.
					let arg = [&flag]( sv suffix ){ return Process::FindArg( Ƒ("{}{}", *flag, suffix) ); };
					if( let v = arg("Url"); v && v->size() )
						leg.Url = *v;
					if( let v = arg("Uri"); v && v->size() )
						leg.CertificateUri = *v;
					if( let v = arg("User"); v && v->size() )
						leg.User = *v;
					if( let v = arg("Pwd"); v && v->size() )
						leg.Password = *v;
				}
				if( let nodes = Json::FindArray(o, "nodes"); nodes ){
					for( let& n : *nodes )
						leg.Nodes.push_back( NodeId{n} );
				}
			}
		}
		THROW_IF( legs.empty(), "/soak/servers has no active leg - Opc.Soak.jsonnet lists them." );
		for( let& leg : legs )
			THROW_IF( leg.Nodes.empty(), "/soak/servers '{}' lists no nodes.", leg.Slug );
		return legs;
	}

	α CertPath( sv slug )ι->string{
		return UAClient::CryptoSettings( ServerCnnctnNK{slug} ).Certificate.Path.string();
	}

	Ω percentile( const vector<uint32>& latencies, double p )ι->uint{
		if( latencies.empty() )
			return 0;
		auto v = latencies;
		let i = std::min( v.size()-1, (uint)(p*v.size()) );
		std::nth_element( v.begin(), v.begin()+i, v.end() );
		return v[i];
	}

	//The gateway group's listener (spec *Subscriptions*):  the snapshot's bounds, then every record the group pushes as a
	//write lands - counted on the socket's read thread, so atomics.  A non-empty Error is the gateway ending it (its group
	//removed, or the historian stopping), which fails the verdict.
	struct SoakHistListener final : Tests::IHistListener{
		α OnSnapshot( const FromServer::HistSnapshot& page )ι->void override{ Snapshot += (uint)page.values_size(); }
		α OnRecords( const FromServer::HistRecords& records )ι->void override{ Records += (uint)records.values_size(); }
		α OnError( string what )ι->void override{ std::lock_guard _{ _mutex }; _error = move(what); }
		α Error()ι->string{ std::lock_guard _{ _mutex }; return _error; }
		std::atomic<uint> Snapshot{}, Records{};
	private:
		std::mutex _mutex;
		string _error;
	};

	//-flow's FlowSubscribe:  each node's counts over the run and its last error.  An ack holds a run's totals so far and each
	//push the counts since the push before (flows spec *Live*), so they add up from the ack's.  OnAck is a later run's - a
	//redeploy's or a restart's - whose totals start again, so the received/pushes ratio is not judged after one.  A non-empty
	//Ended is the gateway ending the subscription.
	struct SoakFlowListener final : Tests::IFlowListener{
		struct Totals{ uint64_t Received{}, Drops{}, Errors{}; };
		α OnActivity( const FromServer::FlowActivity& activity )ι->void override{
			std::lock_guard _{ _mutex };
			++Pushes;
			for( let& n : activity.nodes() ){
				if( n.has_counts() )
					Add( _totals[n.node()], n.counts() );
				if( n.error().size() )
					_lastError = Ƒ( "{}: {}", n.node(), n.error() );
			}
		}
		α OnAck( const FromServer::FlowSubscribeAck& ack )ι->void override{ std::lock_guard _{ _mutex }; ++Reruns; SeedLocked( ack ); }
		α OnError( string what )ι->void override{ std::lock_guard _{ _mutex }; _ended = move(what); }
		α Seed( const FromServer::FlowSubscribeAck& ack )ι->void{ std::lock_guard _{ _mutex }; SeedLocked( ack ); }
		α Counts( sv node )ι->Totals{ std::lock_guard _{ _mutex }; auto p = _totals.find( string{node} ); return p==_totals.end() ? Totals{} : p->second; }
		α LastError()ι->string{ std::lock_guard _{ _mutex }; return _lastError; }
		α Ended()ι->string{ std::lock_guard _{ _mutex }; return _ended; }
		std::atomic<uint> Pushes{}, Reruns{};
	private:
		Ω Add( Totals& t, const FromServer::FlowCounts& c )ι->void{ t.Received += c.received(); t.Drops += c.drops(); t.Errors += c.errors(); }
		α SeedLocked( const FromServer::FlowSubscribeAck& ack )ι->void{
			_totals.clear();
			for( let& n : ack.nodes() )
				Add( _totals[n.node()], n.counts() );
		}
		std::mutex _mutex;
		flat_map<string,Totals> _totals;
		string _lastError, _ended;
	};

	//-oee's OeeSubscribe:  every status pushed, the shift occurrences they named - by start, so each occurrence once - and
	//how often the gateway's calendar client was missing the machine's calendar.
	struct SoakOeeListener final : Tests::IOeeListener{
		α OnStatus( const FromServer::OeeStatus& status )ι->void override{
			std::lock_guard _{ _mutex };
			++Statuses;
			if( status.has_shift() )
				_shifts.insert( status.shift().start().seconds() );
			if( status.calendar_missing() )
				++CalendarMissing;
		}
		α OnError( string what )ι->void override{ std::lock_guard _{ _mutex }; _ended = move(what); }
		α Shifts()ι->uint{ std::lock_guard _{ _mutex }; return _shifts.size(); }
		α Ended()ι->string{ std::lock_guard _{ _mutex }; return _ended; }
		std::atomic<uint> Statuses{}, CalendarMissing{};
	private:
		std::mutex _mutex;
		std::set<int64_t> _shifts;
		string _ended;
	};

	//The gateway's own AppServer user, read from its status:  the identity its historian collector presents to an OPC
	//server (a Certificate token, the gateway's app-client cert), which an enforcing OpcServer must grant before a group
	//fills.  None when the gateway can't be reached or reports no user.  A short socket of its own on the grant client's
	//session - the gateway is up and logged in by the time soak.sh runs -grant (it waited on its HTTP port first).
	Ω gatewayHistorianUser( const sp<App::Client::IAppClient>& client )ι->optional<uint64_t>{
		try{
			let host = Settings::FindArgString( "-gatewayHost", "/soak/gatewayHost" ).value_or( "localhost" );
			let port = (PortType)Settings::FindArgNumber( "-gatewayPort", "/soak/gatewayPort" ).value_or( 1968 );
			let status = GatewayClientSocket::Open( host, port, client->SessionId() )->QuerySync( "status{ historianUser }" );
			let user = status.is_object() ? Json::FindNumber<uint64_t>( status.as_object(), "historianUser" ) : nullopt;
			return user && *user ? user : nullopt;
		}
		catch( const std::exception& e ){
			WARN( "Could not read the gateway's collector user: {}", e.what() );
			return nullopt;
		}
	}

	struct SoakRunner final : Tests::IListener, std::enable_shared_from_this<SoakRunner>{
		SoakRunner( sp<App::Client::IAppClient> client )ε;
		α Run()ε->int;
		α OnData( string opcId, NodeId nodeId, const Proto::Value& value )ι->void override;
	private:
		α Connect()ε->void;//main session's socket, shared by every leg without a User.
		α Login( ServerLeg& leg )ε->void;//POST /login for the leg's user; the leg gets its own socket on the returned session.
		α EnsureServerConnections()ε->void;
		α Subscribe( ServerLeg& leg )ε->void;
		α TrySubscribe( ServerLeg& leg, sv when )ι->bool;//Subscribe, logging a failure instead of throwing it; the leg stays unsubscribed for WriteCycle to retry.
		α WriteCycle( ServerLeg& leg )ε->void;//failures are counted and survived, a failed reconnect included - it is retried on the next transport failure.
		α Write( ServerLeg& leg, const NodeId& node, uint value )ε->bool;//one write with its retries; false = counted as a WriteFailure (and reconnected if it was the third *transport* failure in a row - an error the gateway answered with never reconnects).
		α SampleStatus()ι->void;
		α Reconnect( ServerLeg& leg )ε->void;
		α FindLeg( sv slug )ι->ServerLeg*;
		α Totals()Ι->Counters;
		α AllLatencies()Ι->vector<uint32>;
		α WriteSummary( sv verdict )ι->void;
		α SetupHistorian()ε->void;//a gateway group on the primary leg, over the node the soak writes, and a HistSubscribe to it.
		α ReadBackHistorian()ι->void;//reads the group's stored values over the run and ends the subscription.
		α RunPassthrough()ι->void;//-passthrough:  one aggregate read of the OpcServer's pump history and one history edit, timed.
		α PrimaryLeg()ι->ServerLeg*;//the first leg on the main socket with nodes - the OpcServer's, which the historian, the flow and OEE use.
		α ConnectionId( sv slug )ε->uint;
		α NsuNode( sv slug, const NodeId& node )ε->jobject;//the node spelled by namespace URI, as a group holds its members.
		α NodeValue( sv slug, const NodeId& node )ε->jvalue;
		α SetupFlow()ε->void;//-flow:  a group over its target, the flow deployed and subscribed.
		α FinishFlow()ι->bool;//its criteria, into _flowSummary.
		α SetupTightening()ε->void;//-openProtocol:  the emulator's tightening station as a connection, linked and subscribed.
		α OnTightening( uint results )ι->void;
		α FinishTightening()ι->bool;
		α SetupOee()ε->void;//-oee:  a calendar, a group over pump2's signals and a machine on them, live and subscribed.
		α FinishOee()ι->bool;

		sp<App::Client::IAppClient> _client;
		sp<GatewayClientSocket> _socket;//the main session's socket - User legs hold their own in ServerLeg::Socket.
		vector<ServerLeg> _legs;
		string _host;
		PortType _port;
		Duration _duration, _writePeriod, _pushTimeout, _statusPeriod, _quietInterval, _quietPeriod, _retryDelay;
		Duration _startDelay;//a settle delay after the subscription ack, to see whether it avoids the first-write hang.
		uint _writeRetries, _missRetries;//extra attempts per write / extra rounds per missed push before they count as a WriteFailure / Miss.
		bool _readBack;//ask the mutation for { value } and check what it echoes - soak-findings #6, off by default.  See Write().
		bool _passthrough;//-passthrough (soak.sh --emulator):  each quiet window, an aggregate read of the OpcServer's own pump history over a long span and a small history edit, through the gateway - the day-page round trips and edit-lock holds #210/#211/#220 deferred to the soak.  Measured, never fails the run.
		fs::path _csvPath, _summaryPath;
		std::ofstream _csv;

		std::mutex _mutex; std::condition_variable _cv;

		uint _socketDrops{}, _statusFailures{}, _quietWindows{};
		bool _completed{};
		bool _mainReconnecting{};//ServerLeg::Reconnecting for the shared main-session socket - one drop for all of its legs.

		//The historian leg:  one gateway group on the primary (local, no-User) leg, collecting the same node the soak writes,
		//so every write is stored and read back at the end.  A 24h run crosses one midnight in timeZone, whose day-file archive
		//rewrite nothing else exercises on a wall clock.  Unset when setup failed - the run then skips the historian criteria
		//rather than fail on an immature add.  The subscription rides the main socket, so a drop (which already fails the run)
		//also ends it;  no hist re-subscribe on reconnect.
		ServerLeg* _histLeg{};
		optional<uint32> _histGroup;
		uint _histConnectionId{};
		TimePoint _histFrom{};
		uint _histStored{};
		sp<SoakHistListener> _histListener;
		vector<jvalue> _histReadNodes;//the group's nodes spelled by namespace URI (nsu), as the subscription and the read-back name them.

		//The pass-through measurement (-passthrough):  pump nodes on the primary connection, the OpcServer's own history,
		//read as an aggregate and edited through the gateway.  _pumpResolved stays true after the namespace lookup, whether
		//or not the pumps namespace was found; a miss disables it for the run.  Counts and worst-case holds go to the summary.
		bool _pumpResolved{};
		optional<NodeId> _pumpRead, _pumpEdit;
		uint _passReads{}, _passReadValues{}, _passEdits{}, _passEditValues{};
		uint64_t _passReadMsMax{}, _passEditMsMax{};
		string _passError;
		flat_map<string,flat_map<uint16,string>> _namespaces;//each connection's URIs by index, read once.

		//Opt-in legs for what the November release added, each set up before the writes start and judged at the end, its
		//counts in the summary under its name.  A leg asked for and not set up fails the run:  unlike the historian, it was asked for.
		//-flow:  `opc in` on the node the soak writes, a Function node doubling it, `opc out` to a second node, at trace level
		//counts - so the Function's QuickJS runs a message a write for the run and the trace's day files roll at UTC midnight.
		bool _flow;
		uint32 _flowId{};
		string _flowSlug;
		optional<NodeId> _flowSource, _flowTarget;
		TimePoint _flowFrom{};
		sp<SoakFlowListener> _flowListener;
		RequestId _flowSubscription{};
		string _flowError;
		jobject _flowSummary;
		//-openProtocol:  a connection of kind open-protocol to the emulator's tightening station, its results counted by the
		//driver's own `_Op.Results` (read-only, so no write cycle) and the link read back at the end.
		bool _openProtocol;
		string _tightSlug, _tightUrl;
		uint _tightStart{}, _tightLast{}, _tightPushes{};
		optional<steady_clock::time_point> _tightFirstPush, _tightLastPush;
		steady_clock::duration _tightMaxGap{};
		string _tightError;
		jobject _tightSummary;
		//-oee:  a machine on pump2's State, Total, Reject and Reason under a UTC calendar, three 8 h shifts for a run of 8 h or
		//more - one of whose boundaries is UTC midnight - else two short ones a couple of minutes in.
		bool _oee;
		uint32 _oeeMachine{};
		TimePoint _oeeFrom{};
		bool _oeeLongShifts{};
		sp<SoakOeeListener> _oeeListener;
		RequestId _oeeSubscription{};
		string _oeeError;
		jobject _oeeSummary;
	};

	SoakRunner::SoakRunner( sp<App::Client::IAppClient> client )ε:
		_client{ move(client) },
		_legs{ ActiveServers() },
		_host{ Settings::FindArgString( "-gatewayHost", "/soak/gatewayHost" ).value_or( "localhost" ) },
		_port{ (PortType)Settings::FindArgNumber( "-gatewayPort", "/soak/gatewayPort" ).value_or( 1968 ) },
		_duration{ Settings::FindArgDuration( "-duration", "/soak/duration" ).value_or( std::chrono::hours{24} ) },
		_writePeriod{ Settings::FindArgDuration( "-writePeriod", "/soak/writePeriod" ).value_or( 1s ) },
		_pushTimeout{ Settings::FindArgDuration( "-pushTimeout", "/soak/pushTimeout" ).value_or( 5s ) },
		_statusPeriod{ Settings::FindArgDuration( "-statusPeriod", "/soak/statusPeriod" ).value_or( 1min ) },
		_quietInterval{ Settings::FindArgDuration( "-quietInterval", "/soak/quietInterval" ).value_or( std::chrono::hours{6} ) },
		_quietPeriod{ Settings::FindArgDuration( "-quietPeriod", "/soak/quietPeriod" ).value_or( 10min ) },
		_retryDelay{ Settings::FindArgDuration( "-retryDelay", "/soak/retryDelay" ).value_or( 1s ) },
		_startDelay{ Settings::FindArgDuration( "-startDelay", "/soak/startDelay" ).value_or( 0s ) },
		_writeRetries{ Settings::FindArgNumber( "-writeRetries", "/soak/writeRetries" ).value_or( 2 ) },
		_missRetries{ Settings::FindArgNumber( "-missRetries", "/soak/missRetries" ).value_or( 2 ) },
		_readBack{ argFlag("-readBack", "/soak/readBack") },
		_passthrough{ argFlag("-passthrough", "/soak/passthrough") },
		_csvPath{ Settings::FindArgString( "-csv", "/soak/csv" ).value_or( "soak.csv" ) },
		_summaryPath{ Settings::FindArgString( "-summary", "/soak/summary" ).value_or( "summary.json" ) },
		_flow{ argFlag("-flow", "/soak/flow") },
		_openProtocol{ argFlag("-openProtocol", "/soak/openProtocol") },
		_tightSlug{ Settings::FindString( "/soak/tighteningSlug" ).value_or( "SoakTightening" ) },
		_tightUrl{ Settings::FindArgString( "-tighteningUrl", "/soak/tighteningUrl" ).value_or( "open-protocol://127.0.0.1:4545" ) },
		_oee{ argFlag("-oee", "/soak/oee") }
	{
		for( auto& leg : _legs )
			leg.LatenciesMs.reserve( 1'000 );
	}

	//Grant first, then enforce:  createAcl's admin check passes anyone while the row is deleted (TestAdminResource), and the
	//restore's TestSchemaAdmin finds no active root to test.  The other way round the grant is refused - a user who
	//administers nothing on an enforced resource - which is why HistoryTests::Enforce grants before it restores too.  The
	//restore reaches a running OpcServer as an event and re-maps its node rights (OpcAuthorize::UpdateResourceDeleted);
	//a restart loads the same rows at startup.  Throws:  the db is fresh per run, so nothing is "already granted", and
	//a run on an OPEN server measures nothing of authorization - soak.sh fails early on either.
	α EnforceWriteRights( const sp<App::Client::IAppClient>& client )ε->void{
		let schema = Settings::FindArgString( "-opcSchema", "/soak/opcSchema" ).value_or( _debug ? "opc.debug" : "opc.release" );
		//ERights, not the UA access byte the old 0x7F was labelled as:  OpcAuthorize translates (ToAccess), and All carries
		//Administer, so this user can still grant on the row once it is enforced.
		client->GrantRights( client->UserPK(), Access::ERights::All, schema, "nodeIds" );
		INFO( "Granted user {} every right on '{}' nodeIds.", client->UserPK().Value, schema );
		client->QuerySync<jvalue>( "restoreResource( schemaName:$schemaName, slug:\"nodeIds\", criteria:null )", {{"schemaName", schema}} );
		INFO( "Enforced '{}' nodeIds - the OpcServer re-maps its node rights on the event.", schema );
		//The collector's grant (spec *Collector contract*).  The gateway historizes under its OWN OPC credential, a
		//Certificate token the OpcServer maps to the gateway's AppServer user - not the soak user granted above - so on an
		//enforcing server that user needs Read and Subscribe or a group never fills, a path no soak has run (the gateway
		//suite's embedded server is OPEN).  Best effort:  the soak's writes/pushes/RSS still stand without it, and a
		//collector the grant missed shows as a historian group that stored nothing, not as a run that never started.  The
		//soak user, holding All, administers the row and may grant it;  made after the enforce, as the restore above is.
		//-flow:  a flow writes as that same collector (flows spec *Rights*), so it takes Update too;  and an `opc out` writes only
		//a node of a group under an enforced gateway `histGroups`, which the soak user, who deploys, may update - granted and
		//then enforced, nodeIds' order.  Unlike the collector's grant these throw:  the run asked for the flow.
		let flow = argFlag( "-flow", "/soak/flow" );
		try{
			if( let collector = gatewayHistorianUser(client); collector && *collector!=(uint64_t)client->UserPK().Value ){
				using enum Access::ERights;
				let rights = flow ? Read | Subscribe | Update : Read | Subscribe;
				client->GrantRights( Jde::UserPK{(Jde::UserPK::Type)*collector}, rights, schema, "nodeIds" );
				INFO( "Granted the gateway's collector (user {}) {} on '{}' nodeIds.", *collector, flow ? "Read+Subscribe+Update" : "Read+Subscribe", schema );
			}
			else
				WARN( "The gateway reported no distinct collector user - a historian group may not fill under enforcement." );
		}
		catch( const std::exception& e ){
			WARN( "Could not grant the gateway's collector: {}", e.what() );
		}
		if( flow ){
			client->GrantRights( client->UserPK(), Access::ERights::All, "gateway", "histGroups" );
			client->QuerySync<jvalue>( "restoreResource( schemaName:\"gateway\", slug:\"histGroups\", criteria:null )", {} );
			INFO( "Granted user {} every right on gateway histGroups and enforced it, for the flow's `opc out`.", client->UserPK().Value );
		}
	}

	//Built aside and swapped in only once it is up, so a failed attempt throws before it touches anything.  It used to replace _socket
	//first:  a failure left _socket a never-handshaken socket - the next status query on it blocked for the full request timeout - and
	//the legs on the retired one (subscription-disconnect #8).  The replaced socket and a failed attempt's are simply released:  a
	//request in flight and the read loop each hold their own reference, and the destructor takes back the shutdown registration
	//its Connect made.
	α SoakRunner::Connect()ε->void{
		_socket = GatewayClientSocket::Open( _host, _port, _client->SessionId() );
		for( auto& leg : _legs ){
			if( leg.User.empty() ){
				leg.Socket = _socket;
				leg.SessionId = _client->SessionId();
				leg.Subscribed = false;//a new socket carries no subscription.
			}
		}
		INFO( "Connected to gateway {}:{}.", _host, _port );
	}

	α SoakRunner::Login( ServerLeg& leg )ε->void{
		let body = serialize( jobject{ {"opc",leg.Slug}, {"user",leg.User}, {"password",leg.Password} } );
		string authorization;
		try{
			//ClientHttpAwait::await_resume throws ClientHttpResException on any error status, so a THROW_IF on res.IsError()
			//here would be dead code. That exception now carries the status, reason & response body, but the body is whatever
			//the gateway chose to say - the catches below add the soak-side context (which leg, which user, what to fix).
			auto res = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{
				_host, "/login", body, _port, {.ContentType="application/json", .IsSsl=false} } );
			authorization = res[http::field::authorization];
		}
		catch( Web::Client::ClientHttpResException& e ){
			let status = (uint)e.Status();//read before the move - argument evaluation order is unspecified.
			throw Exception{ SRCE_CUR, {}, move(e),
				"[{}]/login failed for user '{}': http {}. The gateway refused the login or could not open a session on {} - the UA status code is in the gateway log. On a first run against an external server, trust '{}' in its certificate/configuration manager and confirm '{}' may write the configured nodes.",
				leg.Slug, leg.User, status, leg.Url, CertPath(leg.Slug), leg.User };
		}
		catch( runtime_error& e ){
			throw Exception{ SRCE_CUR, {}, move(e), "[{}]/login for user '{}' could not reach the gateway at {}:{}.", leg.Slug, leg.User, _host, _port };
		}
		let sessionId = Str::TryTo<SessionPK,16>( authorization );
		THROW_IF( !sessionId || !*sessionId, "[{}]/login for user '{}' returned no session id in the Authorization header ('{}').", leg.Slug, leg.User, authorization );
		leg.Socket = GatewayClientSocket::Open( _host, _port, *sessionId );//built aside and swapped in only once it is up, as in Connect (#8).
		leg.SessionId = *sessionId;
		leg.Subscribed = false;//a new socket carries no subscription.
		INFO( "[{}]Logged in as '{}' - session {}.", leg.Slug, leg.User, hex(leg.SessionId) );
	}

	α SoakRunner::EnsureServerConnections()ε->void{
		for( let& leg : _legs ){
			let existing = _socket->QuerySync( Ƒ("serverConnection( slug: \"{}\", deleted: null ){{ id url }}", leg.Slug) );
			if( existing.is_object() && existing.get_object().contains("id") ){
				INFO( "Server connection '{}' exists: {}.", leg.Slug, serialize(existing) );
				continue;
			}
			let created = _socket->QuerySync( Ƒ("mutation createServerConnection( slug:\"{}\", name:\"{}\", certificateUri:\"{}\", description:\"{}\", url:\"{}\", isDefault:false ){{id}}",
				leg.Slug, leg.Name, leg.CertificateUri, leg.Description, leg.Url) );
			INFO( "Created server connection '{}': {}.", leg.Slug, serialize(created) );
		}
	}

	α SoakRunner::Subscribe( ServerLeg& leg )ε->void{
		leg.Subscribed = false;
		THROW_IF( !leg.Socket, "[{}]no socket to subscribe on - a reconnect failed and will be retried.", leg.Slug );
		let ack = BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( leg.Socket->Subscribe(leg.Slug, leg.Nodes, shared_from_this()) );
		THROW_IF( (uint)ack.results_size()!=leg.Nodes.size(), "Subscription ack has {} results for {} nodes.", ack.results_size(), leg.Nodes.size() );
		for( int i=0; i<ack.results_size(); ++i )
			THROW_IF( ack.results(i).status_code(), "Subscription for node '{}' failed: {}.", leg.Nodes[i].ToString(), hex(ack.results(i).status_code()) );
		leg.Subscribed = true;
		INFO( "Subscribed to {} node(s) on '{}'.", leg.Nodes.size(), leg.Slug );
	}
	α SoakRunner::TrySubscribe( ServerLeg& leg, sv when )ι->bool{
		try{
			Subscribe( leg );
		}
		catch( const std::exception& e ){
			WARN( "[{}]Subscribe {} failed - the next write cycle retries it: {}", leg.Slug, when, e.what() );
		}
		return leg.Subscribed;
	}

	α SoakRunner::FindLeg( sv slug )ι->ServerLeg*{
		auto p = std::ranges::find_if( _legs, [slug](let& l){ return l.Slug==slug; } );
		return p==_legs.end() ? nullptr : &*p;
	}

	α SoakRunner::OnData( string opcId, NodeId nodeId, const Proto::Value& value )ι->void{
		if( value.of_case()==Proto::Value::OF_NOT_SET )
			return;
		try{
			let v = Opc::Value{ ProtoUtils::ToVariant(value) }.AsNumber<uint>();
			if( _openProtocol && opcId==_tightSlug ){//no leg:  the driver's `_Op.Results`, read-only.
				OnTightening( v );
				return;
			}
			{
				std::lock_guard _{ _mutex };
				if( auto leg = FindLeg(opcId); leg )
					leg->Latest[nodeId] = v;
			}
			TRACE( "OnData: {} {}={}.", opcId, nodeId.ToString(), v );
			_cv.notify_all();
		}
		catch( const std::exception& e ){
			WARN( "OnData: could not convert value for {}: {}", nodeId.ToString(), e.what() );
		}
	}

	α SoakRunner::Reconnect( ServerLeg& leg )ε->void{
		//One drop per outage, however many attempts it takes:  counting each attempt reported a 30s outage as 15 drops (#8).  The
		//shared main-session socket is one drop for all of its legs.  Counted at the first attempt, not on success, so a gateway
		//that never comes back still shows its drop.
		bool& reconnecting = leg.User.empty() ? _mainReconnecting : leg.Reconnecting;
		let first = !reconnecting;
		if( first ){
			reconnecting = true;
			++_socketDrops;
		}
		WARN( "[{}]Reconnecting to the gateway{} (drop #{}).", leg.Slug, first ? "" : " again", _socketDrops );
		//A socket that comes back does not bring its subscriptions:  every leg on it is unsubscribed until a subscribe succeeds.  That
		//subscribe can fail on a socket that works - the gateway's OPC server still coming back - and used to throw out of here,
		//leaving the leg connected, never re-subscribed, and missing every push for the rest of the run; on the shared socket the
		//legs after the failing one were not even tried (subscription-disconnect #7).  So each leg is tried on its own, a failure
		//is left to WriteCycle to retry, and only the connect or login - a socket that did not come back - throws.  Those throw
		//before they change anything (#8), so a failed attempt leaves the legs on the old socket and the counters where they were.
		if( leg.User.empty() ){//shared main-session socket: recreate it and resubscribe every leg on it.
			Connect();
			for( auto& l : _legs ){
				if( l.User.empty() )
					TrySubscribe( l, "after the reconnect" );
			}
		}
		else{//the old session may no longer authenticate after a drop - log in again.
			Login( leg );
			TrySubscribe( leg, "after the reconnect" );
		}
		reconnecting = false;
		leg.ConsecutiveFailures = 0;
	}

	α SoakRunner::Write( ServerLeg& leg, const NodeId& node, uint value )ε->bool{
		//A failed write is re-sent with the same value up to _writeRetries times before it counts: the 2026-09-05 24h run lost its
		//verdict to a single 5s UA read timeout during a ~25s whole-machine pause.  Re-sending a write that actually landed is
		//harmless (same value), and the caller's push wait is satisfied either way.  Only exhausting the attempts is a WriteFailure;
		//WriteRetries counts the extra attempts so a PASS still shows how often it leaned on them.  ConsecutiveFailures stays a
		//per-cycle count, so the reconnect threshold is unchanged.
		if( !leg.Socket ){//not reachable since Connect/Login swap a socket in only once it is up (#8); outside the loop so it could not burn the retries.
			++leg.Count.WriteFailures;
			WARN( "[{}]updateVariable for {} skipped - the leg has no socket.", leg.Slug, node.ToString() );
			return false;
		}
		for( uint attempt{}; ; ++attempt ){
			try{
				//By default no {value} result-request: the subscription push is the round-trip assertion, and the
				//mutation's read-back was recorded as never resuming when UA responses land in the same run_iterate
				//(split-process localhost; soak-findings #6).  -readBack asks for the echo and checks it, which is how
				//#6 gets re-tested: its likely cause was #2, a noexcept QuerySync that terminated the process on any
				//exception response, and that was fixed 09-16.  A wrong or missing echo is an ordinary write failure -
				//it takes the retries and the counters with it - so a stale #6 shows up as a FAIL, never as a hang.
				string q{ _readBack
					? "updateVariable( opc: $opc, id: $id, value: $value ){ value }"
					: "updateVariable( opc: $opc, id: $id, value: $value )" };
				let result = leg.Socket->QuerySync( move(q), jobject{ {"opc",leg.Slug}, {"id",node.ToJson()}, {"value",value} } );
				if( _readBack ){
					let echoed = result.is_object() ? Json::FindNumber<uint>( result.as_object(), "value" ) : std::nullopt;
					THROW_IF( !echoed, "the write asked for {{ value }} and got back '{}'", serialize(result) );
					THROW_IF( *echoed!=value, "the write echoed back {}, not the {} it was given", *echoed, value );
				}
				leg.ConsecutiveFailures = 0;
				return true;
			}
			catch( const std::exception& e ){
				if( attempt<_writeRetries && !Process::ShuttingDown() ){
					++leg.Count.WriteRetries;
					WARN( "[{}]updateVariable failed for {} (attempt {} of {}) - retrying in {}: {}", leg.Slug, node.ToString(), attempt+1, _writeRetries+1, Chrono::ToString(_retryDelay), e.what() );
					std::this_thread::sleep_for( _retryDelay );
					continue;
				}
				++leg.Count.WriteFailures;
				WARN( "[{}]updateVariable failed for {} after {} attempt(s): {}", leg.Slug, node.ToString(), attempt+1, e.what() );
				//Only a dead socket is a reason to reconnect.  An error the gateway *answered* with came back over a working
				//connection - an OPC server that is down, a rejected write - and reconnecting cannot fix it: the reconnect's
				//re-subscribe asks the gateway for the same server and fails the same way.  That is what ended the client on a
				//25s OpcServer outage, with the gateway healthy throughout (soak-findings #12).  It still counts as a
				//WriteFailure; it just does not count toward a reconnect, and the socket that answered is evidently fine.  A dead
				//socket, a write on a closed one and a request timeout all arrive as a plain Exception (GatewayClientSocket::CloseTasks).
				if( dynamic_cast<const Tests::GatewayErrorResponse*>(&e) )
					leg.ConsecutiveFailures = 0;
				else if( ++leg.ConsecutiveFailures>=3 ){
					try{
						Reconnect( leg );
					}
					catch( const std::exception& re ){
						//Not fatal:  the gateway may still be restarting.  (A socket that came back while the server behind it has not
						//is no failure here - Reconnect leaves that leg's subscribe to WriteCycle.)  The drop is already counted, the
						//legs are still on the old socket, and leaving the counter at the threshold means the next transport failure
						//tries again - a run that never recovers still fails on its counters, and soak.sh fails a gateway that has
						//actually gone away.
						leg.ConsecutiveFailures = 2;
						WARN( "[{}]Reconnect to the gateway failed - retrying on the next failure: {}", leg.Slug, re.what() );
					}
				}
				return false;
			}
		}
	}

	α SoakRunner::WriteCycle( ServerLeg& leg )ε->void{
		//No subscription, no push to wait for:  every round below would time out into a Miss.  A failed retry is only logged - the
		//write still runs and counts, which during an outage is a WriteFailure, as it should be.
		if( !leg.Subscribed && leg.Socket )
			TrySubscribe( leg, "retry" );
		let& node = leg.Nodes[leg.WriteIndex++ % leg.Nodes.size()];
		let start = steady_clock::now();
		//A miss - the write was acked but no data-change push arrived within _pushTimeout - is retried the same way, with a FRESH
		//value each round: re-sending the last one would leave the node unchanged and give the server nothing to push, and a late
		//push for the earlier value is simply superseded.  Only exhausting the rounds is a Miss; MissRetries counts the extra ones.
		//Writes/Pushes/Misses stay per cycle (a retried round is not another Write), so a run without write failures still has
		//writes == pushes + misses.  Latency is measured from the first write, so a rescued cycle shows its full cost in maxMs.
		for( uint attempt{}; ; ++attempt ){
			let value = ++leg.Counter;
			if( !Write(leg, node, value) )
				return;//counted as a WriteFailure (and reconnected if it was the third in a row); the push criterion does not apply.
			if( attempt==0 )
				++leg.Count.Writes;
			std::unique_lock lock{ _mutex };
			if( _cv.wait_for(lock, _pushTimeout, [&]{ auto p = leg.Latest.find(node); return p!=leg.Latest.end() && p->second==value; }) ){
				++leg.Count.Pushes;
				leg.LatenciesMs.push_back( (uint32)duration_cast<milliseconds>(steady_clock::now()-start).count() );
				return;
			}
			lock.unlock();
			if( attempt<_missRetries && !Process::ShuttingDown() ){
				++leg.Count.MissRetries;
				WARN( "[{}]No data-change push for {} value {} within {} (attempt {} of {}) - re-sending in {}.", leg.Slug, node.ToString(), value, Chrono::ToString(_pushTimeout), attempt+1, _missRetries+1, Chrono::ToString(_retryDelay) );
				std::this_thread::sleep_for( _retryDelay );
				continue;
			}
			++leg.Count.Misses;
			WARN( "[{}]No data-change push for {} value {} within {} after {} attempt(s).", leg.Slug, node.ToString(), value, Chrono::ToString(_pushTimeout), attempt+1 );
			return;
		}
	}

	α Counters::operator+=( const Counters& x )ι->Counters&{
		for( let& [_,member] : Names )
			this->*member += x.*member;
		return *this;
	}
	α Counters::CsvHeader( sv suffix )ι->string{
		string y;
		for( let& [name,_] : Names )
			y += Ƒ( ",{}{}", name, suffix );
		return y;
	}
	α Counters::Csv()Ι->string{
		string y;
		for( let& [_,member] : Names )
			y += Ƒ( ",{}", this->*member );
		return y;
	}
	α Counters::AddTo( jobject& o )Ι->void{
		for( let& [name,member] : Names )
			o[name] = this->*member;
	}
	α Counters::Log()Ι->string{
		string y;
		for( let& [name,member] : Names )
			y += Ƒ( "{}{}={}", y.empty() ? "" : ", ", name, this->*member );
		return y;
	}
	α SoakRunner::Totals()Ι->Counters{
		Counters y;
		for( let& l : _legs )
			y += l.Count;
		return y;
	}

	α SoakRunner::AllLatencies()Ι->vector<uint32>{
		vector<uint32> all;
		for( let& l : _legs )
			all.insert( all.end(), l.LatenciesMs.begin(), l.LatenciesMs.end() );
		return all;
	}

	α SoakRunner::SampleStatus()ι->void{
		try{
			string q{ "status{ memory startTime uptimeSeconds clients monitoredItems pendingItems historianGroups historianBytes }" };
			let j = _socket->QuerySync( move(q) );
			let& o = j.as_object();
			let num = [&o]( sv name )->int64_t { auto p = o.find(name); return p==o.end() ? -1 : p->value().to_number<int64_t>(); };
			let all = AllLatencies();
			_csv << ToIsoString( Clock::now() )
				<< ',' << num("memory") << ',' << num("uptimeSeconds") << ',' << num("clients") << ',' << num("monitoredItems") << ',' << num("pendingItems") << ',' << num("historianGroups") << ',' << num("historianBytes")
				<< Totals().Csv() << ',' << _socketDrops << ',' << _statusFailures
				<< ',' << percentile(all, .5) << ',' << percentile(all, .99);
			for( let& l : _legs )
				_csv << l.Count.Csv() << ',' << percentile(l.LatenciesMs, .5) << ',' << percentile(l.LatenciesMs, .99);
			_csv << std::endl;
		}
		catch( const std::exception& e ){
			++_statusFailures;
			WARN( "Status query failed: {}", e.what() );
		}
	}

	α SoakRunner::WriteSummary( sv verdict )ι->void{
		try{
			let all = AllLatencies();
			jobject servers;
			for( let& l : _legs ){
				jobject leg;
				l.Count.AddTo( leg );
				leg["p50Ms"] = percentile( l.LatenciesMs, .5 );
				leg["p99Ms"] = percentile( l.LatenciesMs, .99 );
				leg["maxMs"] = l.LatenciesMs.empty() ? 0 : *std::ranges::max_element( l.LatenciesMs );
				servers[l.Slug] = move( leg );
			}
			jobject y{ {"verdict", verdict}, {"completed", _completed} };
			Totals().AddTo( y );
			y["socketDrops"] = _socketDrops;
			y["statusFailures"] = _statusFailures;
			y["quietWindows"] = _quietWindows;
			y["p50Ms"] = percentile( all, .5 );
			y["p99Ms"] = percentile( all, .99 );
			y["maxMs"] = all.empty() ? 0 : *std::ranges::max_element( all );
			y["servers"] = move( servers );
			if( _histGroup ){//what collection stored against what the live stream carried - the counts a human weighs on the first runs.
				y["historian"] = jobject{
					{"group", *_histGroup}, {"leg", _histLeg ? _histLeg->Slug : string{}},
					{"stored", _histStored}, {"snapshot", _histListener ? _histListener->Snapshot.load() : 0}, {"stream", _histListener ? _histListener->Records.load() : 0},
					{"legPushes", _histLeg ? _histLeg->Count.Pushes : 0}, {"ended", _histListener ? _histListener->Error() : string{}}
				};
			}
			if( _passthrough ){//the pass-through measurements (#210/#211/#220):  worst-case holds a human reads against the 100ms budget.
				y["passthrough"] = jobject{
					{"reads", _passReads}, {"readValues", _passReadValues}, {"readMsMax", _passReadMsMax},
					{"edits", _passEdits}, {"editValues", _passEditValues}, {"editMsMax", _passEditMsMax},
					{"error", _passError}
				};
			}
			if( _flow )
				y["flow"] = _flowSummary;
			if( _openProtocol )
				y["tightening"] = _tightSummary;
			if( _oee )
				y["oee"] = _oeeSummary;
			std::ofstream f{ _summaryPath };
			f << serialize( y );
			INFO( "Summary written to '{}'.", _summaryPath.string() );
		}
		catch( const std::exception& e ){
			WARN( "Could not write summary: {}", e.what() );
		}
	}

	//A gateway group over the primary leg's node, created through the socket as the web does (HistGroupFixture), then a
	//HistSubscribe from now on:  the snapshot's bound, then a record per write.  The collector, under the gateway's own
	//credential, subscribes the node on its own client and stores every change (the group sets no thresholds).  Throws on
	//any step - Run catches it and leaves the historian out of the run rather than fail it.
	α SoakRunner::PrimaryLeg()ι->ServerLeg*{
		auto p = std::ranges::find_if( _legs, [](let& l){ return l.User.empty() && !l.Nodes.empty(); } );
		return p==_legs.end() ? nullptr : &*p;
	}
	α SoakRunner::ConnectionId( sv slug )ε->uint{
		let conn = _socket->QuerySync( Ƒ("serverConnection( slug: \"{}\", deleted: null ){{ id }}", slug) );
		THROW_IF( !conn.is_object() || !conn.as_object().contains("id"), "[{}]no connection id.", slug );
		return Json::AsNumber<uint>( conn.as_object(), "id" );
	}
	//Right after a group's create the collector hasn't read the server's NamespaceArray, so a member named by index answers
	//Bad_NodeIdUnknown:  the group holds each by URI (HistNodeText), and the soak spells them so.
	α SoakRunner::NsuNode( sv slug, const NodeId& node )ε->jobject{
		auto& uris = _namespaces[string{slug}];
		if( uris.empty() ){
			let rows = _socket->QuerySync( "namespaces( opc: $opc ){ index uri }", jobject{ {"opc", slug} } );
			if( rows.is_array() ){
				for( let& r : rows.as_array() ){
					if( let u = Json::FindSV(r.as_object(), "uri"); u && u->size() )
						uris.emplace( Json::AsNumber<uint16>(r.as_object(), "index"), string{*u} );
				}
			}
		}
		auto o = node.ToJson();
		let ns = Json::FindNumber<uint16>( o, "ns" );
		let uri = ns ? uris.find( *ns ) : uris.end();
		THROW_IF( !ns || uri==uris.end(), "[{}]can't resolve the namespace of '{}'.", slug, node.ToString() );
		o.erase( "ns" );
		o["nsu"] = uri->second;
		return o;
	}
	α SoakRunner::NodeValue( sv slug, const NodeId& node )ε->jvalue{
		let read = _socket->QuerySync( "node( opc: $opc, id: $id ){ value }", jobject{ {"opc", slug}, {"id", node.ToJson()} } );
		THROW_IF( !read.is_object() || !read.as_object().contains("value"), "[{}]no value for '{}': {}", slug, node.ToString(), serialize(read) );
		return read.as_object().at( "value" );
	}

	α SoakRunner::SetupHistorian()ε->void{
		_histLeg = PrimaryLeg();//it shares the main socket, which the subscription rides.
		THROW_IF( !_histLeg, "No local leg with nodes to historize - skipping the historian." );
		_histConnectionId = ConnectionId( _histLeg->Slug );
		let created = _socket->QuerySync( "mutation createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }",
			jobject{ {"name", "Soak"}, {"slug", "soak"}, {"connection", _histConnectionId} } );
		_histGroup = Json::AsNumber<uint32>( created.as_object(), "id" );
		for( let& node : _histLeg->Nodes )
			_socket->QuerySync( "mutation createHistGroupNode( histGroupId: $g, node: $node ){ id }", jobject{ {"g", *_histGroup}, {"node", node.ToJson()} } );
		//Subscribe and read by namespace URI, not the server index (NsuNode).
		_histReadNodes.clear();
		vector<ExNodeId> nodes; nodes.reserve( _histLeg->Nodes.size() );
		for( let& n : _histLeg->Nodes ){
			let o = NsuNode( _histLeg->Slug, n );
			nodes.emplace_back( jvalue{o} );
			_histReadNodes.push_back( jvalue{o} );
		}
		_histListener = ms<SoakHistListener>();
		_histFrom = Clock::now();
		let ack = BlockAwait<ClientSocketAwait<vector<StatusCode>>,vector<StatusCode>>( _socket->HistSubscribe(*_histGroup, nodes, _histFrom, _histListener) );
		THROW_IF( ack.size()!=_histLeg->Nodes.size(), "HistSubscribe ack has {} statuses for {} nodes.", ack.size(), _histLeg->Nodes.size() );
		for( uint i=0; i<ack.size(); ++i )
			THROW_IF( ack[i], "[{}]HistSubscribe for node '{}' failed: {}.", _histLeg->Slug, _histLeg->Nodes[i].ToString(), ack[i] );//StatusCode has a name formatter; {:x} would be rejected.
		INFO( "Historian group {} on '{}' subscribed over {} node(s) from {}.", *_histGroup, _histLeg->Slug, nodes.size(), ToIsoString(_histFrom) );
	}

	//The group's history over the run, paged to the end, the count the verdict checks against the live pushes - it reads
	//across the midnight boundary a 24h run crosses, so the archived day's file is read too.  Then the HistUnsubscribe.
	α SoakRunner::ReadBackHistorian()ι->void{
		if( !_histGroup || !_histLeg )
			return;
		try{
			jarray nodes;
			for( let& n : _histLeg->Nodes )
				nodes.push_back( n.ToJson() );
			let end = Clock::now();
			string continuation;
			uint stored{};
			for( uint page=0; !Process::ShuttingDown(); ++page ){
				THROW_IF( page>100'000, "Historian read-back is still paging after 100,000 pages." );
				let result = _socket->QuerySync(
					"history( group: $group, nodes: $nodes, start: $start, end: $end, limit: $limit, continuation: $continuation ){ continuation values{ status } }",
					jobject{ {"group", *_histGroup}, {"nodes", nodes}, {"start", UADateTime{_histFrom}.ToJson()}, {"end", UADateTime{end}.ToJson()}, {"limit", 10'000}, {"continuation", continuation.size() ? jvalue{continuation} : jvalue{}} } );
				let& o = result.as_object();
				stored += (uint)Json::FindDefaultArray( o, "values" ).size();
				let c = o.if_contains( "continuation" );
				continuation = c && c->is_string() ? string{c->get_string()} : string{};
				if( continuation.empty() )
					break;
			}
			_histStored = stored;
			INFO( "Historian group {} stored {} value(s) over the run; snapshot {}, stream {}.", *_histGroup, _histStored, _histListener->Snapshot.load(), _histListener->Records.load() );
		}
		catch( const std::exception& e ){
			WARN( "Historian read-back failed: {}", e.what() );
		}
		try{
			BlockAwait<ClientSocketAwait<bool>,bool>( _socket->HistUnsubscribe(_socket->LastHistSubscription()) );
		}
		catch( const std::exception& e ){
			WARN( "HistUnsubscribe failed: {}", e.what() );
		}
	}

	//Resolves the pump nodes once (urn:jde:pumps on the primary connection), then times two pass-throughs through the
	//gateway, under the soak user's session:  an aggregate read over a long span, one interval a day - so a day-file pass
	//and a round trip each (#211/#220) - and a small history edit, one flush and fsync an entry (#210).  Never throws:  the
	//emulator and these grants are newer than the rest, so a failure is logged and carried in the summary, not a FAIL.  It
	//reads one pump and edits another, so the synthetic edit never skews the read.
	α SoakRunner::RunPassthrough()ι->void{
		if( !_histLeg )
			return;
		try{
			if( !_pumpResolved ){
				_pumpResolved = true;
				let ns = _socket->QuerySync( "namespaces( opc: $opc ){ index uri }", jobject{ {"opc", _histLeg->Slug} } );
				optional<uint16_t> index;
				if( ns.is_array() ){
					for( let& row : ns.get_array() ){
						if( Json::FindSV(row.as_object(), "uri").value_or("")=="urn:jde:pumps" )
							index = Json::AsNumber<uint16>( row.as_object(), "index" );
					}
				}
				if( !index ){
					WARN( "Pass-through disabled: the OpcServer has no 'urn:jde:pumps' namespace - is the emulator running?" );
					return;
				}
				_pumpRead = NodeId{ *index, 6022 };//pump2.motorRpm, the sine signal - read as an aggregate.
				_pumpEdit = NodeId{ *index, 6042 };//pump4.motorRpm - edited, apart from the node read.
			}
			if( !_pumpRead )
				return;//resolution failed earlier this run.
			let days = Settings::FindArgNumber( "-passthroughDays", "/soak/passthroughDays" ).value_or( 1000 );//#220's realistic worst load: a day interval a page.
			let end = Clock::now();
			let start = end-std::chrono::hours{ 24*(int64_t)days };
			let readStart = steady_clock::now();
			string continuation; uint values{}, pages{};
			for( ; !Process::ShuttingDown(); ++pages ){
				if( pages>16*(days+16) ){ WARN( "Pass-through read still paging after {} pages.", pages ); break; }//a day a page at least, and a busy day several, as the OpcServer's scan budget stops it (#504).
				let page = _socket->QuerySync(
					"history( opc: $opc, nodes: $nodes, start: $start, end: $end, interval: $interval, aggregate: $aggregate, continuation: $continuation ){ continuation values{ status } }",
					jobject{ {"opc", _histLeg->Slug}, {"nodes", _pumpRead->ToJson()}, {"start", UADateTime{start}.ToJson()}, {"end", UADateTime{end}.ToJson()}, {"interval", 86'400'000.0}, {"aggregate", "Average"}, {"continuation", continuation.size() ? jvalue{continuation} : jvalue{}} } );
				let& o = page.as_object();
				values += (uint)Json::FindDefaultArray( o, "values" ).size();
				let c = o.if_contains( "continuation" );
				continuation = c && c->is_string() ? string{c->get_string()} : string{};
				if( continuation.empty() )
					break;
			}
			let readMs = (uint64_t)duration_cast<milliseconds>( steady_clock::now()-readStart ).count();
			++_passReads; _passReadValues += values; _passReadMsMax = std::max( _passReadMsMax, readMs );
			INFO( "Pass-through aggregate read: {} day-interval page(s) over {} day(s), {} value(s), {} ms.", pages, days, values, readMs );
			constexpr uint entries{ 100 };
			jarray vals;
			for( uint i=0; i<entries; ++i )
				vals.push_back( jobject{ {"node", _pumpEdit->ToJson()}, {"source", UADateTime{end-std::chrono::seconds{(int)i+1}}.ToJson()}, {"value", 1000.0+i} } );
			let editStart = steady_clock::now();
			let edited = _socket->QuerySync( "upsertHistory( opc: $opc, values: $values ){ values{ status } nodes{ status } }", jobject{ {"opc", _histLeg->Slug}, {"values", move(vals)} } );
			let editMs = (uint64_t)duration_cast<milliseconds>( steady_clock::now()-editStart ).count();
			++_passEdits; _passEditValues += (uint)Json::FindDefaultArray( edited.as_object(), "values" ).size(); _passEditMsMax = std::max( _passEditMsMax, editMs );
			INFO( "Pass-through history edit: {} entr(ies) upserted, {} ms.", entries, editMs );
		}
		catch( const std::exception& e ){
			_passError = e.what();
			WARN( "Pass-through measurement failed: {}", e.what() );
		}
	}

	Ω failuresJson( const vector<string>& failures )ι->jarray{
		jarray y;
		for( let& f : failures )
			y.emplace_back( f );
		return y;
	}

	//The target joins a group of its own:  an `opc out` writes only a grouped node, under an enforced gateway `histGroups`
	//(-grant -flow enforces it beside nodeIds), and the soak group's counts are the historian criteria's.  The flow writes as
	//the gateway's own OPC user, which -grant -flow gave Update.
	α SoakRunner::SetupFlow()ε->void{
		let leg = PrimaryLeg();
		THROW_IF( !leg, "No local leg with nodes for the flow's source." );
		_flowSlug = leg->Slug;
		_flowSource = leg->Nodes.front();
		let configured = Settings::FindObject( "/soak/flowNode" );
		_flowTarget = configured ? NodeId{ jvalue(*configured) } : NodeId{ (UA_UInt16)Json::AsNumber<uint16>(_flowSource->ToJson(), "ns"), 6003 };//Lamp2.Intensity, 6017's sibling.
		let source = NsuNode( _flowSlug, *_flowSource );
		let target = NsuNode( _flowSlug, *_flowTarget );
		let group = Json::AsNumber<uint32>( _socket->QuerySync("mutation createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }",
			jobject{ {"name", "Soak flow"}, {"slug", "soak-flow"}, {"connection", ConnectionId(_flowSlug)} }).as_object(), "id" );
		_socket->QuerySync( "mutation createHistGroupNode( histGroupId: $g, node: $node ){ id }", jobject{ {"g", group}, {"node", serialize(target)} } );
		_flowId = Json::AsNumber<uint32>( _socket->QuerySync("mutation createFlow( name:$name, slug:$slug, enabled:true, trace:\"counts\" ){ id }",
			jobject{ {"name", "Soak flow"}, {"slug", "soak-flow"} }).as_object(), "id" );
		let graph = Ƒ( R"({{"nodes":[
{{"id":"in","type":"opc in","props":{{"connection":"{0}","node":{1}}},"wires":[["double"]]}},
{{"id":"double","type":"function","props":{{"code":"msg.payload = msg.payload*2;\nreturn msg;"}},"wires":[["out"]]}},
{{"id":"out","type":"opc out","props":{{"connection":"{0}","node":{2}}},"wires":[]}}]}})", _flowSlug, serialize(source), serialize(target) );
		jobject deployed;
		for( uint attempt=1; ; ++attempt ){//a refusal may be the gateway's access snapshot not yet holding -grant's histGroups.
			try{
				deployed = _socket->QuerySync( "mutation deployFlow( flow:$flow, graph:$graph, comment:$comment ){ flow version running fault{ node message } }",
					jobject{ {"flow", _flowId}, {"graph", graph}, {"comment", "soak"} } ).as_object();
				break;
			}
			catch( const Tests::GatewayErrorResponse& e ){
				if( attempt>=6 )
					throw;
				WARN( "deployFlow refused (attempt {} of 6) - retrying in 5s: {}", attempt, e.what() );
				std::this_thread::sleep_for( 5s );
			}
		}
		THROW_IF( !Json::FindBool(deployed, "running").value_or(false), "Flow {} deployed but is not running: {}", _flowId, serialize(deployed) );
		_flowListener = ms<SoakFlowListener>();
		_flowFrom = Clock::now();
		let ack = BlockAwait<ClientSocketAwait<FromServer::FlowSubscribeAck>,FromServer::FlowSubscribeAck>( _socket->FlowSubscribe(_flowId, false, _flowListener) );
		_flowSubscription = _socket->LastFlowSubscription();
		_flowListener->Seed( ack );
		INFO( "Flow {} running on '{}':  {} doubled into {}.", _flowId, _flowSlug, _flowSource->ToString(), _flowTarget->ToString() );
	}

	//Errors and drops none, the Function node fed at least half as often as the soak's own pushes - a flow whose `opc in`
	//stopped hearing the node mid-run falls under it - and the target holding twice the source once the last write is through.
	//flowCounts, the trace's per-minute records, is reported beside the live figures, a minute or two behind them.
	α SoakRunner::FinishFlow()ι->bool{
		vector<string> failures;
		if( _flowError.size() )
			failures.push_back( Ƒ("setup: {}", _flowError) );
		else{
			try{
				BlockAwait<ClientSocketAwait<bool>,bool>( _socket->FlowUnsubscribe(_flowSubscription) );
			}
			catch( const std::exception& e ){
				WARN( "FlowUnsubscribe failed: {}", e.what() );
			}
			let in = _flowListener->Counts( "in" ), function = _flowListener->Counts( "double" ), out = _flowListener->Counts( "out" );
			let pushes = PrimaryLeg() ? PrimaryLeg()->Count.Pushes : 0;
			let errors = in.Errors+function.Errors+out.Errors;
			let drops = in.Drops+function.Drops+out.Drops;
			_flowSummary = jobject{ {"flow", _flowId}, {"activityPushes", _flowListener->Pushes.load()}, {"reruns", _flowListener->Reruns.load()},
				{"functionReceived", function.Received}, {"written", out.Received}, {"errors", errors}, {"drops", drops}, {"legPushes", pushes} };
			if( errors )
				failures.push_back( Ƒ("{} error(s), the last {}", errors, _flowListener->LastError()) );
			if( drops )
				failures.push_back( Ƒ("{} message(s) dropped", drops) );
			if( !function.Received )
				failures.push_back( "the Function node received nothing" );
			else if( !_flowListener->Reruns && function.Received*2<pushes )
				failures.push_back( Ƒ("the Function node received {} message(s) for {} push(es) of its source", function.Received, pushes) );
			if( let ended = _flowListener->Ended(); ended.size() )
				failures.push_back( Ƒ("the gateway ended the subscription:  {}", ended) );
			try{
				std::this_thread::sleep_for( 2s );//the last write's message through the Function and `opc out`.
				let source = NodeValue( _flowSlug, *_flowSource ).to_number<double>();
				let target = NodeValue( _flowSlug, *_flowTarget ).to_number<double>();
				_flowSummary["source"] = source;
				_flowSummary["target"] = target;
				if( target!=2*source )
					failures.push_back( Ƒ("the target holds {} for a source of {}", target, source) );
			}
			catch( const std::exception& e ){
				failures.push_back( Ƒ("reading the source and target failed:  {}", e.what()) );
			}
			try{
				let counts = _socket->QuerySync( "flowCounts( flow:$flow, start:$start, end:$end, by:\"PT1H\" ){ continuation counts{ node received errors } }",
					jobject{ {"flow", _flowId}, {"start", ToIsoString(_flowFrom)}, {"end", ToIsoString(Clock::now())} } );
				jobject received;
				for( let& c : Json::FindDefaultArray(counts.as_object(), "counts") ){
					let& o = c.as_object();
					auto& n = received[Json::AsSV( o, "node" )];
					n = (n.is_number() ? n.to_number<uint64_t>() : 0)+Json::FindNumber<uint64_t>( o, "received" ).value_or( 0 );
				}
				_flowSummary["traceReceived"] = move( received );
			}
			catch( const std::exception& e ){
				WARN( "flowCounts failed: {}", e.what() );
			}
		}
		_flowSummary["failures"] = failuresJson( failures );
		return failures.empty();
	}

	Ω opNode( sv name )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(ServerCnnctn::DriverBrowseNs, string{name}.c_str()) }; }
	//A 64-bit integer comes over QL as the protobufjs Long the javascript clients read, {high, low, unsigned}
	//(OpcHelperTests.SixtyFourBitIntegersUseTheLongForm), and the narrower ones as numbers.
	Ω asCount( const jvalue& v )ε->uint{
		if( !v.is_object() )
			return v.to_number<uint>();
		let& o = v.as_object();
		return (uint)( (Json::AsNumber<uint64_t>(o, "high")<<32) | Json::AsNumber<uint64_t>(o, "low") );
	}

	//The row alone starts the driver and its loopback server (DriverHost::RowChanged);  the subscription waits for its link.
	//`_Op.Results` counts the station's results as the driver writes them, so a push a result, the station's one every 2 s.
	α SoakRunner::SetupTightening()ε->void{
		let existing = _socket->QuerySync( Ƒ("serverConnection( slug: \"{}\", deleted: null ){{ id }}", _tightSlug) );
		if( !existing.is_object() || !existing.as_object().contains("id") ){
			_socket->QuerySync( "mutation createServerConnection( slug:$slug, name:$name, description:$description, url:$url, isDefault:false, kind:\"open-protocol\" ){ id }",
				jobject{ {"slug", _tightSlug}, {"name", "Soak tightening station"}, {"description", "The PLC emulator's Open Protocol station"}, {"url", _tightUrl} } );
		}
		let deadline = steady_clock::now()+90s;
		for( ;; ){
			try{
				if( let v = NodeValue(_tightSlug, opNode("_Op.Connected")); v.is_bool() && v.as_bool() )
					break;
			}
			catch( const std::exception& e ){//the driver's server comes up beside the row.
				TRACE( "[{}]_Op.Connected not read yet:  {}", _tightSlug, e.what() );
			}
			THROW_IF( steady_clock::now()>deadline, "[{}]the driver never linked to {} in 90s.", _tightSlug, _tightUrl );
			std::this_thread::sleep_for( 2s );
		}
		_tightStart = asCount( NodeValue(_tightSlug, opNode("_Op.Results")) );
		let ack = BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( _socket->Subscribe(_tightSlug, {opNode("_Op.Results")}, shared_from_this()) );
		THROW_IF( ack.results_size()!=1 || ack.results(0).status_code(), "[{}]the _Op.Results subscription failed.", _tightSlug );
		INFO( "[{}]Linked to {} - {} result(s) so far.", _tightSlug, _tightUrl, _tightStart );
	}
	α SoakRunner::OnTightening( uint results )ι->void{
		let now = steady_clock::now();
		std::lock_guard _{ _mutex };
		if( _tightLastPush )
			_tightMaxGap = std::max( _tightMaxGap, now-*_tightLastPush );
		else
			_tightFirstPush = now;
		_tightLastPush = now;
		_tightLast = results;
		++_tightPushes;
	}
	//The link up at the end, no result lost, the counters adding up, results at a quarter of the station's rate at least, and
	//none of the run without one for two minutes.  The reconnects are reported:  a backfilled one loses nothing.
	α SoakRunner::FinishTightening()ι->bool{
		vector<string> failures;
		if( _tightError.size() )
			failures.push_back( Ƒ("setup: {}", _tightError) );
		else{
			try{
				BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( _socket->Unsubscribe(_tightSlug, {opNode("_Op.Results")}) );
			}
			catch( const std::exception& e ){
				WARN( "[{}]Unsubscribe failed: {}", _tightSlug, e.what() );
			}
			try{
				let read = [this]( sv name ){ return NodeValue( _tightSlug, opNode(name) ); };
				let connected = read( "_Op.Connected" );
				let results = asCount( read("_Op.Results") );
				let lost = asCount( read("_Op.LostResults") );
				let reconnects = asCount( read("_Op.Reconnects") );
				let total = asCount( read("Counters.Total") ), ok = asCount( read("Counters.Ok") ), nok = asCount( read("Counters.Nok") );
				std::lock_guard _{ _mutex };
				let seconds = _tightFirstPush && _tightLastPush ? (uint)duration_cast<std::chrono::seconds>( *_tightLastPush-*_tightFirstPush ).count() : 0;
				let gained = results-_tightStart;
				let maxGap = (uint)duration_cast<std::chrono::seconds>( _tightMaxGap ).count();
				_tightSummary = jobject{ {"connection", _tightSlug}, {"connected", connected}, {"results", gained}, {"lastPushed", _tightLast}, {"pushes", _tightPushes}, {"seconds", seconds},
					{"maxGapSeconds", maxGap}, {"lost", lost}, {"reconnects", reconnects}, {"total", total}, {"ok", ok}, {"nok", nok} };
				if( !connected.is_bool() || !connected.as_bool() )
					failures.push_back( "the driver was not linked at the end" );
				if( lost )
					failures.push_back( Ƒ("{} result(s) lost", lost) );
				if( total!=ok+nok )
					failures.push_back( Ƒ("Counters.Total {} is not Ok {} + Nok {}", total, ok, nok) );
				if( !_tightPushes )
					failures.push_back( "no result was pushed" );
				else if( seconds>=60 && gained*4<seconds )
					failures.push_back( Ƒ("{} result(s) in {}s, under a quarter of the station's one every 2s", gained, seconds) );
				if( maxGap>120 )
					failures.push_back( Ƒ("{}s without a result", maxGap) );
			}
			catch( const std::exception& e ){
				failures.push_back( Ƒ("the read-back failed:  {}", e.what()) );
			}
		}
		_tightSummary["failures"] = failuresJson( failures );
		return failures.empty();
	}

	//The calendar on the AppServer, in UTC.  Three 8 h shifts every day for a run of 8 h or more, so a day's run crosses them
	//and one boundary is UTC midnight;  else two back to back from two minutes on, each a quarter of the run and 3 minutes at
	//least, so a 10-minute smoke crosses both.  pump2 (Pumps.h):  its toggle and its 10s-a-minute stop under code 1, small
	//stops under a 2-minute smallStop, its count a unit every 2 s, which idealCycle takes.
	α SoakRunner::SetupOee()ε->void{
		let leg = PrimaryLeg();
		THROW_IF( !leg, "No local leg for OEE's group." );
		let now = Clock::now();
		_oeeLongShifts = _duration>=std::chrono::hours{8};
		let calendar = Json::AsNumber<uint32>( _client->QuerySync<jvalue>(R"(createCalendar( name:"Soak OEE", slug:"soak-oee", timeZone:"UTC", dayStart:0 ){ id })", {}).as_object(), "id" );
		let patterns = _client->QuerySync<jvalue>( Ƒ("calendarPatterns( calendarId:{}, deleted:null ){{ id }}", calendar), {} );
		THROW_IF( !patterns.is_array() || patterns.as_array().empty(), "Calendar {} has no pattern:  {}", calendar, serialize(patterns) );
		let pattern = Json::AsNumber<uint32>( patterns.as_array().front().as_object(), "id" );
		vector<std::pair<uint,uint>> shifts;//the start, minutes into the UTC day, and the length
		if( _oeeLongShifts )
			shifts = { {0, 480}, {480, 480}, {960, 480} };
		else{
			let length = (uint)std::max<int64_t>( 3, std::chrono::duration_cast<std::chrono::minutes>(_duration).count()/4 );
			let minute = (uint)std::chrono::duration_cast<std::chrono::minutes>( now-std::chrono::floor<std::chrono::days>(now) ).count();
			shifts = { {(minute+2)%1440, length}, {(minute+2+length)%1440, length} };
		}
		for( uint i=0; i<shifts.size(); ++i )
			_client->QuerySync<jvalue>( Ƒ(R"(createCalendarShift( calendarPatternId:{}, name:"{}", days:127, start:{}, minutes:{} ){{ id }})", pattern, (char)('A'+i), shifts[i].first, shifts[i].second), {} );
		let group = Json::AsNumber<uint32>( _socket->QuerySync("mutation createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }",
			jobject{ {"name", "Soak OEE pump2"}, {"slug", "soak-oee"}, {"connection", ConnectionId(leg->Slug)} }).as_object(), "id" );
		constexpr std::array<std::pair<sv,uint32>,4> signals{{ {"State", 6021}, {"Total", 6023}, {"Reject", 6024}, {"Reason", 6025} }};
		flat_map<sv,uint32> members;
		for( let& [role,id] : signals ){
			members[role] = Json::AsNumber<uint32>( _socket->QuerySync("mutation createHistGroupNode( histGroupId: $g, node: $node ){ id }",
				jobject{ {"g", group}, {"node", serialize(jobject{ {"nsu", "urn:jde:pumps"}, {"i", id} })} }).as_object(), "id" );
		}
		_oeeMachine = Json::AsNumber<uint32>( _socket->QuerySync("mutation createOeeMachine( name:$name, slug:$slug, histGroupId:$group, calendarId:$calendar, idealCycle:$ideal, countUnit:$unit, smallStop:$small ){ id }",
			jobject{ {"name", "Soak pump2"}, {"slug", "soak-pump2"}, {"group", group}, {"calendar", calendar}, {"ideal", 2000.0}, {"unit", "units"}, {"small", 120'000} }).as_object(), "id" );
		for( let& [role,_] : signals )//State first, as a machine needs it or a count.
			_socket->QuerySync( "mutation createOeeMachineSignal( oeeMachineId:$machine, role:$role, histGroupNodeId:$node ){ id }", jobject{ {"machine", _oeeMachine}, {"role", role}, {"node", members[role]} } );
		let deadline = steady_clock::now()+90s;
		for( ;; ){
			let status = _socket->QuerySync( "oeeMachineStatus( machine:$id ){ calendar engine why }", jobject{ {"id", _oeeMachine} } );
			let& o = status.as_object();
			if( Json::FindSV(o, "engine").value_or("")=="live" && Json::FindSV(o, "calendar").value_or("")=="found" )
				break;
			THROW_IF( steady_clock::now()>deadline, "Machine {} never ran live on its calendar:  {}", _oeeMachine, serialize(status) );
			std::this_thread::sleep_for( 2s );
		}
		_oeeListener = ms<SoakOeeListener>();
		_oeeFrom = Clock::now();
		let ack = BlockAwait<ClientSocketAwait<FromServer::OeeStatus>,FromServer::OeeStatus>( _socket->OeeSubscribe(_oeeMachine, _oeeListener) );
		_oeeSubscription = _socket->LastOeeSubscription();
		_oeeListener->OnStatus( ack );
		INFO( "OEE machine {} live on pump2, {} shift(s) a day in calendar {}.", _oeeMachine, shifts.size(), calendar );
	}
	//The engine live on its calendar at the end, every shift occurrence the run spans heard live and read back with planned
	//time.  The span is trimmed a minute at each end, so an occurrence that began or ended within a minute of them is not owed.
	//The figures themselves are reported, not bounded:  a gap's counts land in the minute it ends (docs/demo/plan.md).
	α SoakRunner::FinishOee()ι->bool{
		vector<string> failures;
		if( _oeeError.size() )
			failures.push_back( Ƒ("setup: {}", _oeeError) );
		else{
			try{
				BlockAwait<ClientSocketAwait<bool>,bool>( _socket->OeeUnsubscribe(_oeeSubscription) );
			}
			catch( const std::exception& e ){
				WARN( "OeeUnsubscribe failed: {}", e.what() );
			}
			let end = Clock::now();
			uint owed{ 2 };
			if( _oeeLongShifts ){
				let minute = []( TimePoint t ){ return std::chrono::duration_cast<std::chrono::minutes>( t.time_since_epoch() ).count(); };
				let from = minute( _oeeFrom )+1, to = minute( end )-1;
				owed = to>from ? (uint)( to/480-from/480+1 ) : 1;
			}
			try{
				let status = _socket->QuerySync( "oeeMachineStatus( machine:$id ){ calendar engine why processed }", jobject{ {"id", _oeeMachine} } );
				let figures = _socket->QuerySync( "oee( machine:$id, start:$start, end:$end, by:\"shift\" ){ start shift planned availability performance quality oee }",
					jobject{ {"id", _oeeMachine}, {"start", ToIsoString(_oeeFrom)}, {"end", ToIsoString(end)} } );
				uint planned{};
				if( figures.is_array() ){
					for( let& b : figures.as_array() ){
						if( Json::FindNumber<int64_t>(b.as_object(), "planned").value_or(0)>0 )
							++planned;
					}
				}
				let& o = status.as_object();
				_oeeSummary = jobject{ {"machine", _oeeMachine}, {"statuses", _oeeListener->Statuses.load()}, {"shiftsHeard", _oeeListener->Shifts()},
					{"shiftsRead", planned}, {"shiftsOwed", owed}, {"calendarMissing", _oeeListener->CalendarMissing.load()}, {"status", status}, {"shifts", figures} };
				if( Json::FindSV(o, "engine").value_or("")!="live" )
					failures.push_back( Ƒ("the engine is not live:  {}", serialize(status)) );
				if( Json::FindSV(o, "calendar").value_or("")!="found" )
					failures.push_back( Ƒ("the calendar is not found:  {}", serialize(status)) );
				if( !_oeeListener->Statuses )
					failures.push_back( "no status was pushed" );
				if( _oeeListener->Shifts()<owed )
					failures.push_back( Ƒ("{} shift occurrence(s) heard live of the {} the run spans", _oeeListener->Shifts(), owed) );
				if( planned<owed )
					failures.push_back( Ƒ("{} shift occurrence(s) read back with planned time of the {} the run spans", planned, owed) );
				if( let ended = _oeeListener->Ended(); ended.size() )
					failures.push_back( Ƒ("the gateway ended the subscription:  {}", ended) );
			}
			catch( const std::exception& e ){
				failures.push_back( Ƒ("the read-back failed:  {}", e.what()) );
			}
		}
		_oeeSummary["failures"] = failuresJson( failures );
		return failures.empty();
	}

	α SoakRunner::Run()ε->int{
		_csv.open( _csvPath, std::ios::app );
		THROW_IF( !_csv.is_open(), "Could not open csv '{}'.", _csvPath.string() );
		if( _csv.tellp()==0 ){
			_csv << "time,memory,uptimeSeconds,clients,monitoredItems,pendingItems,historianGroups,historianBytes" << Counters::CsvHeader() << ",socketDrops,statusFailures,p50Ms,p99Ms";
			for( let& l : _legs )
				_csv << Counters::CsvHeader( Ƒ("_{}", l.Slug) ) << Ƒ( ",p50Ms_{0},p99Ms_{0}", l.Slug );
			_csv << std::endl;
		}
		Connect();
		EnsureServerConnections();
		for( auto& leg : _legs ){//after EnsureServerConnections - login requires the connection's access Provider row.
			if( leg.User.size() )
				Login( leg );
		}
		for( auto& leg : _legs ){
			try{
				Subscribe( leg );
			}
			catch( const std::exception& ){
				//At startup a failure is fatal and worth a hint.  Connection-level failures (untrusted cert, bad credential) already
				//surfaced in Login; reaching here on an external leg points at the nodes themselves.
				if( leg.User.size() )
					WARN( "[{}]Subscribe failed - confirm the configured nodes exist and '{}' may read them; if the server rejected the client certificate, trust '{}' in its configuration manager.", leg.Slug, leg.User, CertPath(leg.Slug) );
				throw;
			}
		}
		if( _startDelay>Duration::zero() )
			std::this_thread::sleep_for( _startDelay );
		try{
			SetupHistorian();
		}
		catch( const std::exception& e ){
			_histGroup = nullopt;//the run proceeds without the historian criteria rather than fail on them.
			WARN( "Historian group setup failed - this run will not exercise collection: {}", e.what() );
		}
		//OEE first:  a short run's shifts start two minutes after its setup, before the others' waits on links.  A main socket
		//that drops ends these subscriptions with it, and already fails the run.
		let setup = [&]( bool on, sv name, auto&& f, string& error ){
			if( !on )
				return;
			try{
				f();
			}
			catch( const std::exception& e ){
				error = e.what();
				WARN( "{} setup failed - the run will FAIL:  {}", name, error );
			}
		};
		setup( _oee, "OEE", [this]{ SetupOee(); }, _oeeError );
		setup( _flow, "Flow", [this]{ SetupFlow(); }, _flowError );
		setup( _openProtocol, "Tightening", [this]{ SetupTightening(); }, _tightError );
		let start = Clock::now();
		let deadline = start+_duration;
		auto nextStatus = start;
		auto nextQuiet = start+_quietInterval;
		INFO( "Soak started: duration={}, writePeriod={}, servers={}, deadline={}.", Chrono::ToString(_duration), Chrono::ToString(_writePeriod), _legs.size(), ToIsoString(deadline) );
		while( Clock::now()<deadline && !Process::ShuttingDown() ){
			let cycleStart = Clock::now();
			if( cycleStart>=nextStatus ){
				SampleStatus();
				nextStatus = cycleStart+_statusPeriod;
			}
			if( cycleStart>=nextQuiet ){
				++_quietWindows;
				let quietEnd = std::min( cycleStart+_quietPeriod, deadline );
				INFO( "Quiet window #{} until {} - subscriptions stay open, no writes.", _quietWindows, ToIsoString(quietEnd) );
				if( _passthrough )//the window is writeless, so a long aggregate read's service-lock hold stalls nothing of the soak's own.
					RunPassthrough();
				while( Clock::now()<quietEnd && !Process::ShuttingDown() ){
					if( Clock::now()>=nextStatus ){
						SampleStatus();
						nextStatus = Clock::now()+_statusPeriod;
					}
					std::this_thread::sleep_for( 1s );
				}
				nextQuiet = Clock::now()+_quietInterval;
				INFO( "Quiet window #{} over - resuming writes.", _quietWindows );
				continue;
			}
			for( auto& leg : _legs )
				WriteCycle( leg );
			std::this_thread::sleep_until( cycleStart+_writePeriod );
		}
		_completed = Clock::now()>=deadline;
		for( auto& leg : _legs ){
			//A null socket here is an access violation, which no catch below sees:  the run ended with no final status sample and no
			//summary.json.  A user leg whose re-login failed used to be left without one (subscription-disconnect #9); since
			//Connect/Login swap a socket in only once it is up (#8) none can be, and the guard keeps it that way for any later path.
			if( !leg.Socket ){
				WARN( "[{}]Unsubscribe skipped - the leg has no socket.", leg.Slug );
				continue;
			}
			try{
				BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( leg.Socket->Unsubscribe(leg.Slug, leg.Nodes) );
			}
			catch( const std::exception& e ){
				WARN( "[{}]Unsubscribe failed: {}", leg.Slug, e.what() );
			}
		}
		ReadBackHistorian();
		let flowOk = !_flow || FinishFlow();
		let tightOk = !_openProtocol || FinishTightening();
		let oeeOk = !_oee || FinishOee();
		SampleStatus();
		//The historian criteria, scored only when a group was set up (else the historian is simply left out of this run):  the
		//live stream delivered records and the gateway never ended the subscription.  The gate is the stream, not the end-of-run
		//read-back:  the stream is a direct count of pushed records, where the read-back adds QL paging a parse bug could skew,
		//and a stream with zero records is the real defect (the collector never subscribed or stored).  The stored count and the
		//exact stream/push relationship - bounds, the collector's own sampling and the snapshot make them differ a little - are
		//reported for a human to weigh, not asserted.
		let histErr = _histListener ? _histListener->Error() : string{};
		let histOk = !_histGroup || (_histListener && _histListener->Records.load()>0 && histErr.empty());
		let pass = _completed && !_socketDrops && !_statusFailures && histOk && flowOk && tightOk && oeeOk && std::ranges::all_of( _legs, [](let& l){ return !l.Count.Misses && !l.Count.WriteFailures; } );
		WriteSummary( pass ? "PASS" : "FAIL" );
		INFO( "Soak {}: completed={}, {}, socketDrops={}, statusFailures={}, p50={}ms, p99={}ms.",
			pass ? "PASS" : "FAIL", _completed, Totals().Log(), _socketDrops, _statusFailures, percentile(AllLatencies(), .5), percentile(AllLatencies(), .99) );
		if( _histGroup )
			INFO( "  [historian]group={}, stored={}, snapshot={}, stream={}{}.", *_histGroup, _histStored, _histListener->Snapshot.load(), _histListener->Records.load(), histErr.empty() ? string{} : Ƒ(", ended: {}", histErr) );
		for( let& l : _legs )
			INFO( "  [{}]{}, p50={}ms, p99={}ms.", l.Slug, l.Count.Log(), percentile(l.LatenciesMs, .5), percentile(l.LatenciesMs, .99) );
		if( _flow )
			INFO( "  [flow]{}", serialize(_flowSummary) );
		if( _openProtocol )
			INFO( "  [tightening]{}", serialize(_tightSummary) );
		if( _oee )
			INFO( "  [oee]{}", serialize(_oeeSummary) );
		return pass ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	α Run( sp<App::Client::IAppClient> client )ε->int{
		auto runner = ms<SoakRunner>( move(client) );
		return runner->Run();
	}
}