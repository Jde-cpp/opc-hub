#include "SoakRunner.h"
#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <numeric>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/http/ClientHttpResException.h>
#include "../tests/utils/GatewayClientSocket.h"
#include "../src/UAClient.h"
#include "../src/types/proto/opc.FromServer.h"

#define let const auto

namespace Jde::Opc::Gateway::Soak{
	constexpr ELogTags _tags{ ELogTags::Test };
	using Tests::GatewayClientSocket;
	using Web::Client::ClientSocketAwait;
	using Web::Client::ClientHttpAwait;
	using Web::Client::ClientHttpRes;

	//-<arg>=<value> beats the settings file so soak.sh can vary a run without another jsonnet.
	Ω argDuration( sv arg, sv path, Duration dflt )ε->Duration{
		if( auto a = Process::FindArg(string{arg}); a && a->size() )
			return Chrono::ToDuration( string{*a} );
		return Settings::FindDuration( path ).value_or( dflt );
	}
	Ω argString( sv arg, sv path, sv dflt )ι->string{
		if( auto a = Process::FindArg(string{arg}); a && a->size() )
			return *a;
		return Settings::FindString( path ).value_or( string{dflt} );
	}
	Ω argNumber( sv arg, sv path, uint dflt )ε->uint{
		if( auto a = Process::FindArg(string{arg}); a && a->size() ){
			let n = Str::TryTo<uint>( string{*a} );
			THROW_IF( !n, "{} '{}' is not a number.", arg, *a );
			return *n;
		}
		return Settings::FindNumber<uint>( path ).value_or( dflt );
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
				if( flag ){//-externalUrl= etc. beat the flagged entry's settings, mirroring argString.
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
		if( legs.empty() ){
			auto& leg = legs.emplace_back();
			leg.Slug = "OpcSoak"; leg.Name = "Soak test server"; leg.Description = "Soak test connection";
			leg.CertificateUri = "urn:open62541.server.application"; leg.Url = "opc.tcp://127.0.0.1:4840";
		}
		if( legs.front().Nodes.empty() )
			legs.front().Nodes.push_back( NodeId{4, 6017} );
		return legs;
	}

	//Where -createCert wrote the leg's client certificate - the file an external server has to trust.  Asks UAClient
	//rather than re-deriving the layout; the SAN doesn't affect the path, so the uri is not needed here.
	Ω CertPath( sv slug )ι->string{
		return UAClient::CryptoSettings( ServerCnnctnNK{slug} ).Certificate.Path.string();
	}

	Ω percentile( const vector<uint32>& latencies, double p )ι->uint{
		if( latencies.empty() )
			return 0;
		auto v = latencies;
		let i = std::min( v.size()-1, (size_t)(p*v.size()) );
		std::nth_element( v.begin(), v.begin()+i, v.end() );
		return v[i];
	}

	struct SoakRunner final : Tests::IListener, std::enable_shared_from_this<SoakRunner>{
		SoakRunner( sp<App::Client::IAppClient> client )ε;
		α Run()ε->int;
		α OnData( string opcId, NodeId nodeId, const vector<FromServer::Value>& values )ι->void override;
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
		α Total( uint ServerLeg::* counter )Ι->uint;
		α AllLatencies()Ι->vector<uint32>;
		α WriteSummary( sv verdict )ι->void;

		sp<App::Client::IAppClient> _client;
		sp<GatewayClientSocket> _socket;//the main session's socket - User legs hold their own in ServerLeg::Socket.
		vector<sp<GatewayClientSocket>> _retiredSockets;//dropped sockets stay alive - Connect() registered them with Process::AddShutdown.
		vector<ServerLeg> _legs;
		string _host;
		PortType _port;
		Duration _duration, _writePeriod, _pushTimeout, _statusPeriod, _quietInterval, _quietPeriod, _retryDelay;
		uint _writeRetries, _missRetries;//extra attempts per write / extra rounds per missed push before they count as a WriteFailure / Miss.
		fs::path _csvPath, _summaryPath;
		std::ofstream _csv;

		std::mutex _mutex; std::condition_variable _cv;

		uint _socketDrops{}, _statusFailures{}, _quietWindows{};
		bool _completed{};
		bool _mainReconnecting{};//ServerLeg::Reconnecting for the shared main-session socket - one drop for all of its legs.
	};

	SoakRunner::SoakRunner( sp<App::Client::IAppClient> client )ε:
		_client{ move(client) },
		_legs{ ActiveServers() },
		_host{ argString("-gatewayHost", "/soak/gatewayHost", "localhost") },
		_port{ Settings::FindNumber<PortType>("/soak/gatewayPort").value_or(1968) },
		_duration{ argDuration("-duration", "/soak/duration", std::chrono::hours{24}) },
		_writePeriod{ argDuration("-writePeriod", "/soak/writePeriod", 1s) },
		_pushTimeout{ argDuration("-pushTimeout", "/soak/pushTimeout", 5s) },
		_statusPeriod{ argDuration("-statusPeriod", "/soak/statusPeriod", 1min) },
		_quietInterval{ argDuration("-quietInterval", "/soak/quietInterval", std::chrono::hours{6}) },
		_quietPeriod{ argDuration("-quietPeriod", "/soak/quietPeriod", 10min) },
		_retryDelay{ argDuration("-retryDelay", "/soak/retryDelay", 1s) },
		_writeRetries{ argNumber("-writeRetries", "/soak/writeRetries", 2) },
		_missRetries{ argNumber("-missRetries", "/soak/missRetries", 2) },
		_csvPath{ argString("-csv", "/soak/csv", "soak.csv") },
		_summaryPath{ argString("-summary", "/soak/summary", "summary.json") }
	{
		for( auto& leg : _legs )
			leg.LatenciesMs.reserve( 1'000 );
	}

	α GrantWriteRights( const sp<App::Client::IAppClient>& client )ι->void{
		try{
			let schema = argString( "-opcSchema", "/soak/opcSchema", _debug ? "opc.debug" : "opc.release" );
			constexpr uint allAccess{ 0x7F };//the UA access-level byte: read|write|historyRead|historyWrite|semanticChange|statusWrite|timestampWrite.
			client->QuerySync<jvalue>(
				"createAcl( identity:{id:$userId}, permissionRight:{ allowed:$allowed, denied:0, resource:{schemaName:$schemaName, slug:\"nodeIds\"}} )",
				{{"userId", client->UserPK().Value}, {"allowed", allAccess}, {"schemaName", schema}} );
			INFO( "Granted OPC node access for user {} on '{}'.", client->UserPK().Value, schema );
		}
		catch( const std::exception& e ){
			INFO( "createAcl failed (already granted on a previous run?): {}", e.what() );
		}
	}

	//Built aside and swapped in only once it is up, so a failed attempt throws before it touches anything.  It used to replace _socket
	//first:  a failure left _socket a never-handshaken socket - the next status query on it blocked for the full request timeout - and
	//the legs on the retired one (subscription-disconnect #8).  A failed attempt's socket is simply released:  its destructor takes
	//back the shutdown registration its Connect made, and a long outage would otherwise pile one up per attempt.
	α SoakRunner::Connect()ε->void{
		optional<ssl::context> ctx;
		auto socket = ms<GatewayClientSocket>( Executor(), ctx );
		BlockVoidAwait( socket->RunSession(_host, _port) );
		BlockAwait<ClientSocketAwait<uint32>,uint>( socket->Connect(_client->SessionId()) );
		if( _socket )
			_retiredSockets.push_back( move(_socket) );
		_socket = move( socket );
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
		let sessionId = Str::TryTo<SessionPK>( authorization, nullptr, 16 );
		THROW_IF( !sessionId || !*sessionId, "[{}]/login for user '{}' returned no session id in the Authorization header ('{}').", leg.Slug, leg.User, authorization );
		optional<ssl::context> ctx;
		auto socket = ms<GatewayClientSocket>( Executor(), ctx );//built aside and swapped in only once it is up, as in Connect (#8).
		BlockVoidAwait( socket->RunSession(_host, _port) );
		BlockAwait<ClientSocketAwait<uint32>,uint>( socket->Connect(*sessionId) );
		if( leg.Socket )
			_retiredSockets.push_back( move(leg.Socket) );
		leg.Socket = move( socket );
		leg.SessionId = *sessionId;
		leg.Subscribed = false;//a new socket carries no subscription.
		INFO( "[{}]Logged in as '{}' - session {:x}.", leg.Slug, leg.User, leg.SessionId );
	}

	α SoakRunner::EnsureServerConnections()ε->void{
		for( let& leg : _legs ){
			let existing = _socket->QuerySync( Ƒ("serverConnection( slug: \"{}\" ){{ id url }}", leg.Slug) );
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
			THROW_IF( ack.results(i).status_code(), "Subscription for node '{}' failed: {:x}.", leg.Nodes[i].ToString(), ack.results(i).status_code() );
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

	α SoakRunner::OnData( string opcId, NodeId nodeId, const vector<FromServer::Value>& values )ι->void{
		if( values.empty() )
			return;
		try{
			let v = FromServer::ToValue( values.back() ).AsNumber<uint>();
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
			++leg.WriteFailures;
			WARN( "[{}]updateVariable for {} skipped - the leg has no socket.", leg.Slug, node.ToString() );
			return false;
		}
		for( uint attempt{}; ; ++attempt ){
			try{
				//no {value} result-request: the subscription push is the round-trip assertion, and the mutation's read-back
				//never resumes when UA responses land in the same run_iterate (split-process localhost; see soak findings).
				string q{ "updateVariable( opc: $opc, id: $id, value: $value )" };
				leg.Socket->QuerySync( move(q), jobject{ {"opc",leg.Slug}, {"id",node.ToJson()}, {"value",value} } );
				leg.ConsecutiveFailures = 0;
				return true;
			}
			catch( const std::exception& e ){
				if( attempt<_writeRetries && !Process::ShuttingDown() ){
					++leg.WriteRetries;
					WARN( "[{}]updateVariable failed for {} (attempt {} of {}) - retrying in {}: {}", leg.Slug, node.ToString(), attempt+1, _writeRetries+1, Chrono::ToString(_retryDelay), e.what() );
					std::this_thread::sleep_for( _retryDelay );
					continue;
				}
				++leg.WriteFailures;
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
				++leg.Writes;
			std::unique_lock lock{ _mutex };
			if( _cv.wait_for(lock, _pushTimeout, [&]{ auto p = leg.Latest.find(node); return p!=leg.Latest.end() && p->second==value; }) ){
				++leg.Pushes;
				leg.LatenciesMs.push_back( (uint32)duration_cast<milliseconds>(steady_clock::now()-start).count() );
				return;
			}
			lock.unlock();
			if( attempt<_missRetries && !Process::ShuttingDown() ){
				++leg.MissRetries;
				WARN( "[{}]No data-change push for {} value {} within {} (attempt {} of {}) - re-sending in {}.", leg.Slug, node.ToString(), value, Chrono::ToString(_pushTimeout), attempt+1, _missRetries+1, Chrono::ToString(_retryDelay) );
				std::this_thread::sleep_for( _retryDelay );
				continue;
			}
			++leg.Misses;
			WARN( "[{}]No data-change push for {} value {} within {} after {} attempt(s).", leg.Slug, node.ToString(), value, Chrono::ToString(_pushTimeout), attempt+1 );
			return;
		}
	}

	α SoakRunner::Total( uint ServerLeg::* counter )Ι->uint{
		return std::accumulate( _legs.begin(), _legs.end(), uint{}, [counter](uint sum, let& l){ return sum+l.*counter; } );
	}

	α SoakRunner::AllLatencies()Ι->vector<uint32>{
		vector<uint32> all;
		for( let& l : _legs )
			all.insert( all.end(), l.LatenciesMs.begin(), l.LatenciesMs.end() );
		return all;
	}

	α SoakRunner::SampleStatus()ι->void{
		try{
			string q{ "status{ memory startTime uptimeSeconds clients monitoredItems pendingItems }" };
			let j = _socket->QuerySync( move(q) );
			let& o = j.as_object();
			let num = [&o]( sv name )->int64_t { auto p = o.find(name); return p==o.end() ? -1 : p->value().to_number<int64_t>(); };
			let all = AllLatencies();
			_csv << ToIsoString( Clock::now() )
				<< ',' << num("memory") << ',' << num("uptimeSeconds") << ',' << num("clients") << ',' << num("monitoredItems") << ',' << num("pendingItems")
				<< ',' << Total(&ServerLeg::Writes) << ',' << Total(&ServerLeg::Pushes) << ',' << Total(&ServerLeg::Misses) << ',' << Total(&ServerLeg::WriteFailures) << ',' << Total(&ServerLeg::WriteRetries) << ',' << Total(&ServerLeg::MissRetries) << ',' << _socketDrops << ',' << _statusFailures
				<< ',' << percentile(all, .5) << ',' << percentile(all, .99);
			for( let& l : _legs )
				_csv << ',' << l.Writes << ',' << l.Pushes << ',' << l.Misses << ',' << l.WriteFailures << ',' << l.WriteRetries << ',' << l.MissRetries << ',' << percentile(l.LatenciesMs, .5) << ',' << percentile(l.LatenciesMs, .99);
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
				servers[l.Slug] = jobject{
					{"writes", l.Writes}, {"pushes", l.Pushes}, {"misses", l.Misses}, {"writeFailures", l.WriteFailures}, {"writeRetries", l.WriteRetries}, {"missRetries", l.MissRetries},
					{"p50Ms", percentile(l.LatenciesMs, .5)}, {"p99Ms", percentile(l.LatenciesMs, .99)},
					{"maxMs", l.LatenciesMs.empty() ? 0 : *std::ranges::max_element(l.LatenciesMs)}
				};
			}
			jobject y{
				{"verdict", verdict}, {"completed", _completed}, {"writes", Total(&ServerLeg::Writes)}, {"pushes", Total(&ServerLeg::Pushes)}, {"misses", Total(&ServerLeg::Misses)},
				{"writeFailures", Total(&ServerLeg::WriteFailures)}, {"writeRetries", Total(&ServerLeg::WriteRetries)}, {"missRetries", Total(&ServerLeg::MissRetries)}, {"socketDrops", _socketDrops}, {"statusFailures", _statusFailures},
				{"quietWindows", _quietWindows}, {"p50Ms", percentile(all, .5)}, {"p99Ms", percentile(all, .99)},
				{"maxMs", all.empty() ? 0 : *std::ranges::max_element(all)},
				{"servers", servers}
			};
			std::ofstream f{ _summaryPath };
			f << serialize( y );
			INFO( "Summary written to '{}'.", _summaryPath.string() );
		}
		catch( const std::exception& e ){
			WARN( "Could not write summary: {}", e.what() );
		}
	}

	α SoakRunner::Run()ε->int{
		_csv.open( _csvPath, std::ios::app );
		THROW_IF( !_csv.is_open(), "Could not open csv '{}'.", _csvPath.string() );
		if( _csv.tellp()==0 ){
			_csv << "time,memory,uptimeSeconds,clients,monitoredItems,pendingItems,writes,pushes,misses,writeFailures,writeRetries,missRetries,socketDrops,statusFailures,p50Ms,p99Ms";
			for( let& l : _legs )
				_csv << Ƒ( ",writes_{0},pushes_{0},misses_{0},writeFailures_{0},writeRetries_{0},missRetries_{0},p50Ms_{0},p99Ms_{0}", l.Slug );
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
		if( let d = argDuration("-startDelay", "/soak/startDelay", 0s); d>Duration::zero() )
			std::this_thread::sleep_for( d );//see if a settle delay after the subscription ack avoids the first-write hang.
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
		SampleStatus();
		let pass = _completed && !_socketDrops && !_statusFailures && std::ranges::all_of( _legs, [](let& l){ return !l.Misses && !l.WriteFailures; } );
		WriteSummary( pass ? "PASS" : "FAIL" );
		INFO( "Soak {}: completed={}, writes={}, pushes={}, misses={}, writeFailures={}, writeRetries={}, missRetries={}, socketDrops={}, statusFailures={}, p50={}ms, p99={}ms.",
			pass ? "PASS" : "FAIL", _completed, Total(&ServerLeg::Writes), Total(&ServerLeg::Pushes), Total(&ServerLeg::Misses), Total(&ServerLeg::WriteFailures), Total(&ServerLeg::WriteRetries), Total(&ServerLeg::MissRetries), _socketDrops, _statusFailures, percentile(AllLatencies(), .5), percentile(AllLatencies(), .99) );
		for( let& l : _legs )
			INFO( "  [{}]writes={}, pushes={}, misses={}, writeFailures={}, writeRetries={}, missRetries={}, p50={}ms, p99={}ms.", l.Slug, l.Writes, l.Pushes, l.Misses, l.WriteFailures, l.WriteRetries, l.MissRetries, percentile(l.LatenciesMs, .5), percentile(l.LatenciesMs, .99) );
		return pass ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	α Run( sp<App::Client::IAppClient> client )ε->int{
		auto runner = ms<SoakRunner>( move(client) );
		return runner->Run();
	}
}
