#include "Emulator.h"
#include <jde/fwk/chrono.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/fwk/crypto/issue.h>
#include <jde/access/usings.h>//Access::ERights - the vocabulary -grant's acl is written in.
#include "Devices.h"
#include "EmulatorClient.h"
#include "PlcServer.h"
#include "Signals.h"

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ ELogTags::App };

	//the device's UA pair: /emulator/ssl puts it under the PlcEmulator product dir with the applicationUri as its SAN - the
	//client's channel certificate on the OpcServer, and the PLC server's own where /emulator/plc/ssl names none.
	Ω opcCertificate()ι->Crypto::CryptoSettings{ return Crypto::CryptoSettings{ Settings::FindDefaultObject("/emulator/ssl") }; }

	//pubsub: process values publish over Part 14 and anything the contract does not list is written over the session;
	//write: everything over the session - the PLC server is not started.  Both subscribe the commands on the OpcServer.
	//server: the PLC server alone, on the library's endpoint - every tag is written to its own nodes, no session on the
	//OpcServer and no writer, and the commands arrive as clients' writes to those nodes (docs/servers/spec.md *Emulator*).
	enum class ETransport : uint8{ PubSub, Write, Server };
	Ω toString( ETransport t )ι->sv{ return t==ETransport::PubSub ? "pubsub" : t==ETransport::Write ? "write" : "server"; }
	//Tag, Device and ParseDevices live in Devices.h - a unit the tests build without a session or a PLC server (T2).

	struct Emulator final : noncopyable{
		Emulator( sp<App::Client::IAppClient> app )ε;
		α Run()ε->int;
	private:
		α Connect()ε->void;//session, browse-path resolution, command subscriptions.
		α Reconnect()ε->void;
		α Tick( bool sessionUp )ι->void;//one pass of the device clock; driven by BOTH loops - Run()'s and Reconnect()'s wait.
		α Cycle( Duration dt, bool sessionUp )ι->void;
		α OnCommand( const PlcServer::Command& command )ι->void;//a client's write to a command tag on the PLC's own server (-transport=server).
		α LogStatus( bool onlyIfChanged=false )ι->void;//onlyIfChanged: the final line at exit, skipped when the timed one already said it (#12).
		Ω OnDataChange( UA_Client*, UA_UInt32, void*, UA_UInt32, void* monContext, UA_DataValue* value )ι->void;

		sp<App::Client::IAppClient> _app;
		ETransport _transport;
		string _url, _applicationUri, _serverApplicationUri, _nsUri;//the device's own uri (advertised + the cert SAN) and the endpoint filter - two jobs, two knobs (#15); _nsUri = the contract's namespace, the default for every device path (#16).
		Duration _period, _statusPeriod, _reconnectMin, _reconnectMax, _reconnectDelay;
		optional<Duration> _duration;
		up<PlcServer> _plc;
		up<EmulatorClient> _client;
		vector<Device> _devices;
		vector<Tag*> _commandTags;//the command tags Watch keys, by index (-transport=server).
		//The device clock.  Members rather than Run() locals because Reconnect()'s wait loop advances them too (#7).
		steady_clock::time_point _lastCycle{}, _nextCycle{}, _nextStatus{};
		UA_UInt32 _subscription{};//the command subscription this session holds, kept so a clean shutdown can delete it (#10).
		uint _cycles{}, _published{}, _writes{}, _writeFailures{}, _consecutiveFailures{}, _externalChanges{}, _reconnects{};
		bool _connected{};//a session has activated at least once.  Until then a failed attempt is the initial connect still failing, not a lost session (#17).
		uint _attempts{};//connect attempts before the first activation - the initial phase's own count; _reconnects counts only losses after it.
		SessionPK _session{};//the AppServer session the OPC session was activated with - its identity (#1).
		string _lastStatus;//what LogStatus last wrote - the final line compares against it (#12).
	};

	Emulator::Emulator( sp<App::Client::IAppClient> app )ε:
		_app{ move(app) },
		_transport{ ETransport::PubSub },
		_url{ Settings::FindArgString( "-url", "/emulator/url" ).value_or( "opc.tcp://127.0.0.1:4840" ) },
		_applicationUri{ Settings::FindString("/emulator/applicationUri").value_or("urn:jde:plc-emulator") },
		_serverApplicationUri{ Settings::FindString("/emulator/serverApplicationUri").value_or("urn:open62541.server.application") },
		_period{ Settings::FindArgDuration( "-period", "/emulator/period" ).value_or( 1s ) },
		_statusPeriod{ Settings::FindArgDuration( "-statusPeriod", "/emulator/statusPeriod" ).value_or( 1min ) },
		_reconnectMin{ Settings::FindArgDuration( "-reconnectMin", "/emulator/reconnectMin" ).value_or( 1s ) },
		_reconnectMax{ Settings::FindArgDuration( "-reconnectMax", "/emulator/reconnectMax" ).value_or( 30s ) },
		_reconnectDelay{ _reconnectMin }{
		let transport = Settings::FindArgString( "-transport", "/emulator/transport" ).value_or( "pubsub" );
		THROW_IF( transport!="pubsub" && transport!="write" && transport!="server", "transport '{}' must be pubsub, write or server.", transport );
		_transport = transport=="write" ? ETransport::Write : transport=="server" ? ETransport::Server : ETransport::PubSub;
		if( let d = Settings::FindArgDuration( "-duration", "/emulator/duration" ).value_or( Duration::zero() ); d>Duration::zero() )
			_duration = d;
		optional<PubSub::Config> contract;
		if( _transport==ETransport::PubSub )
			contract.emplace( Settings::AsObject("/emulator/pubsub") );
		//The device paths' default namespace under every transport, the nodeset's:  /emulator/namespace, else the contract's - a
		//block the write and server transports read for nothing else, and may leave out.
		_nsUri = Settings::FindString( "/emulator/namespace" ).value_or( contract ? contract->Namespace : Settings::FindString("/emulator/pubsub/dataSet/namespace").value_or("") );
		let opc = opcCertificate();
		//On every start, not only -createCert:  the SAN must equal the applicationUri we advertise, and an edited uri would
		//otherwise present the stale SAN forever, refused BadCertificateUriInvalid with nothing naming the file to delete
		//(the gateway's CertTests lesson).  EnsureKeyCertificate re-issues on SAN drift and expiry as well as absence.
		Crypto::EnsureKeyCertificate( opc );
		if( _transport!=ETransport::Write ){
			let nodeset = Settings::FindPath( "/emulator/plc/nodeset" ); THROW_IF( !nodeset, "/emulator/plc/nodeset is required - the nodeset the OpcServer loads." );
			//The server's sessions log in through the hub over this process's app client, as the OpcServer's do (UAAccess);  the
			//pubsub transport's writer samples the contract's nodes, the server transport has no contract.
			_plc = mu<PlcServer>( PlcServer::ReadSettings(opc), _app, *nodeset, move(contract) );
		}
		_devices = ParseDevices( Settings::FindDefaultArray("/emulator/devices"), _plc ? FindField{[this](sv name){ return _plc->FindField(name); }} : FindField{}, SRCE_CUR );
		for( auto& device : _devices ){//the vectors are final: ParseDevices returned them.
			for( auto& tag : device.Tags )
				tag.Self = this;
		}
		if( _transport==ETransport::Server ){
			//Every tag is a node of the PLC's own server, resolved once - the address space is this process's - and a generated one
			//written as its node's type.  A command tag is watched:  a client's write of it, the gateway's write-through of what the
			//web UI wrote, lands in OnCommand through Tick, where the OpcServer's data-change notification lands under the other
			//transports, and the node's value now is the command's start, as their first notification is.
			let ns = _nsUri.size() ? _plc->Namespace( _nsUri ) : NsIndex{};
			for( auto& device : _devices ){
				for( auto& tag : device.Tags ){
					tag.Node = _plc->Resolve( Ƒ("{}/{}", device.Path, tag.Spec.Name), ns );
					let command = tag.Spec.Mode==EMode::Command;
					if( command ){
						if( let p = std::ranges::find_if(_commandTags, [&](let t){ return t->Node==tag.Node; }); p!=_commandTags.end() )
							THROW( "[{}]{} and [{}]{} are both {} - a command node has one tag.", (*p)->Owner->Name, (*p)->Spec.Name, device.Name, tag.Spec.Name, tag.Node.ToString() );
						if( let value = _plc->Watch(tag.Node, _commandTags.size()); value )
							device.Command = *value;
						_commandTags.push_back( &tag );
					}
					else if( tag.Generator )
						tag.Type = &_plc->ValueType( tag.Node );
					DBG( "[{}]{} -> {} ({}{})", device.Name, tag.Spec.Name, tag.Node.ToString(), command ? "watched" : "written as ", command ? Ƒ(", {}", device.Command) : string{tag.Type ? tag.Type->typeName : "?"} );
				}
			}
		}
		if( _plc )
			_plc->Run();
		if( _transport!=ETransport::Server )
			_client = mu<EmulatorClient>( _url, _applicationUri, _serverApplicationUri, opc );
	}

	α Emulator::Connect()ε->void{
		let session = _app->SessionId();
		_client->Connect( Ƒ("{:x}", session) );
		_session = session;
		//Device paths and tag names resolve in the contract's namespace by default - the one the published fields live
		//in - so config writes `pump1`, not `pumps~pump1`.  That alias is the contract's own convention (PubSub.h:
		//dataSet.name stands for dataSet.namespace inside its field paths); borrowing it here tied every device path to
		//the dataset's NAME, and a renamed dataset failed each of them "Unknown namespace 'pumps'" with nothing saying
		//why (#16).  `<index>~name` still reaches another namespace explicitly.
		let ns = _nsUri.size() ? _client->Namespace( _nsUri ) : NsIndex{};
		_subscription = 0;//a fresh session: whatever the previous one held died with it.
		uint commands{};
		for( auto& device : _devices ){
			for( auto& tag : device.Tags ){
				let command = tag.Spec.Mode==EMode::Command;
				if( !command && tag.Field )
					continue;//published - the OpcServer's reader owns that node's writes.
				tag.Node = _client->Resolve( Ƒ("{}/{}", device.Path, tag.Spec.Name), ns, {} );
				tag.Seen = false;
				tag.StatusRefused = false;//a fresh session may come with a different acl - ask again.
				if( command ){
					if( !_subscription )
						_subscription = _client->CreateSubscription( _period );
					_client->Monitor( _subscription, tag.Node, _period, &tag, OnDataChange );
					++commands;
				}
				DBG( "[{}]{} -> {} ({})", device.Name, tag.Spec.Name, tag.Node.ToString(), command ? "subscribed" : "written" );
			}
		}
		_reconnectDelay = _reconnectMin;
		_connected = true;
		_attempts = 0;
		INFO( "Connected to '{}': {} command tag(s) subscribed.", _url, commands );
	}

	α Emulator::OnDataChange( UA_Client*, UA_UInt32, void*, UA_UInt32, void* monContext, UA_DataValue* value )ι->void{
		auto tag = (Tag*)monContext;
		if( !tag || !value || !value->hasValue || !UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_BOOLEAN]) )
			return;
		let command = *(UA_Boolean*)value->value.data;
		auto& device = *tag->Owner;
		if( !tag->Seen ){//the initial notification carries the current value.
			tag->Seen = true;
			device.Command = command;
			DBG( "[{}]{} = {} (initial)", device.Name, tag->Spec.Name, command );
		}
		else if( device.Command!=command ){
			device.Command = command;
			++tag->Self->_externalChanges;
			INFO( "[{}]{} <- {} (external)", device.Name, tag->Spec.Name, command );
		}
	}

	α Emulator::OnCommand( const PlcServer::Command& command )ι->void{
		if( command.Key>=_commandTags.size() )
			return;
		auto& tag = *_commandTags[command.Key];
		auto& device = *tag.Owner;
		if( device.Command==command.Value )
			return;
		device.Command = command.Value;
		++_externalChanges;
		INFO( "[{}]{} <- {} (external, user {})", device.Name, tag.Spec.Name, command.Value, command.User.Value );
	}

	//A real PLC's process values evolve whether or not anyone is connected, so every generator advances on every period
	//regardless of `sessionUp` - what the session gates is only the *write* over it.  Published tags (tag.Field) go to the
	//local PLC node, which the Part 14 writer samples on its own timer, so they keep flowing to the OpcServer through an
	//outage of the client session.  A session-written tag keeps evolving in memory and lands on the first cycle after
	//recovery, instead of the whole outage arriving as one `dt` (#7).  Under the server transport every tag goes to the
	//PLC's own node, and `sessionUp` gates nothing.
	α Emulator::Cycle( Duration dt, bool sessionUp )ι->void{
		for( auto& device : _devices ){
			for( auto& tag : device.Tags ){
				if( !tag.Generator )
					continue;
				//The generator is sampled even through a fault that holds the reading: the process runs on, the sensor is blind.
				tag.Status = tag.Quality.Apply( dt, tag.Generator->Next(dt, device.Command), tag.Value );
				if( tag.Field ){
					try{
						_plc->Write( *tag.Field, tag.Value, tag.Status );
						++_published;
					}
					catch( const std::exception& e ){
						WARN( "[{}]local write {} failed: {}", device.Name, tag.Spec.Name, e.what() );
					}
					continue;
				}
				if( _transport==ETransport::Server ){
					try{
						_plc->Write( tag.Node, *tag.Type, tag.Value, tag.Status );//the whole DataValue:  the status needs no StatusWrite on its own server.
						++_writes;
					}
					catch( const std::exception& e ){
						++_writeFailures;
						WARN( "[{}]local write {} failed: {}", device.Name, tag.Spec.Name, e.what() );
					}
					continue;
				}
				if( !sessionUp )
					continue;//nothing to write to; the value is current and goes out on the next cycle after recovery.
				UA_Variant v;
				UA_Boolean b = tag.Value!=0;
				if( tag.Spec.IsBool() )
					UA_Variant_setScalar( &v, &b, &UA_TYPES[UA_TYPES_BOOLEAN] );
				else
					UA_Variant_setScalar( &v, &tag.Value, &UA_TYPES[UA_TYPES_DOUBLE] );
				try{
					try{
						_client->Write( tag.Node, v, tag.StatusRefused ? UA_STATUSCODE_GOOD : tag.Status );
					}
					catch( const UAException& e ){
						//Over a session a non-Good status needs StatusWrite twice - on the node's AccessLevel (else
						//BadWriteNotSupported) and on the user's (else BadUserAccessDenied; open62541 copyAttributeIntoNode,
						//OPC 10000-3 8.57).  The pumps nodeset is AccessLevel 3 and -grant stops at Read|Update|Subscribe (#6), so
						//this transport carries the reading and PubSub carries its quality: say so once, then send the value alone.
						//BadUserAccessDenied is also a user with no Write at all, so the value alone must land before the status is called
						//refused; BadNotWritable, the node's own AccessLevel, refuses the value too.
						let refused = e.Code()==UA_STATUSCODE_BADWRITENOTSUPPORTED || e.Code()==UA_STATUSCODE_BADUSERACCESSDENIED;
						if( !refused || !tag.Status || tag.StatusRefused )
							throw;
						_client->Write( tag.Node, v );
						tag.StatusRefused = true;
						WARN( "[{}]{}: the server refused this reading's status ({}) over the session - {}.  Writing the value alone from here on; the pubsub transport carries the status.", device.Name, tag.Spec.Name, ToString(tag.Status), UAException::Message((StatusCode)e.Code()) );
					}
					++_writes;
					_consecutiveFailures = 0;
				}
				catch( const std::exception& e ){
					++_writeFailures;
					++_consecutiveFailures;
					WARN( "[{}]write {} failed: {}", device.Name, tag.Spec.Name, e.what() );
				}
			}
		}
		++_cycles;
	}

	//The device clock, driven by both loops.  `sessionUp` false only suppresses the session writes - see Cycle.
	α Emulator::Tick( bool sessionUp )ι->void{
		if( _plc ){//the commands clients wrote since the last pass, on this thread as the notifications are - none under pubsub/write, which watch nothing.
			for( let& command : _plc->Drain() )
				OnCommand( command );
		}
		if( let now = steady_clock::now(); now>=_nextCycle ){
			Cycle( duration_cast<Duration>(now-_lastCycle), sessionUp );
			_lastCycle = now;
			_nextCycle += _period;
			if( _nextCycle<now )
				_nextCycle = now+_period;//a long stall (a blocking connect) must not queue up a burst of catch-up cycles.
		}
		if( let now = steady_clock::now(); now>=_nextStatus ){
			LogStatus();
			_nextStatus += _statusPeriod;
			if( _nextStatus<now )
				_nextStatus = now+_statusPeriod;
		}
	}

	α Emulator::Reconnect()ε->void{
		_consecutiveFailures = 0;
		//Two phases, one loop:  before the first activation nothing was lost - Run logged why the initial connect failed,
		//and this is the retry schedule (#17);  after it, a session really did go away, and that is worth a WARN.
		if( _connected ){
			++_reconnects;
			WARN( "Session to '{}' lost - reconnecting (#{}) in {}.", _url, _reconnects, Chrono::ToString(_reconnectDelay) );
		}
		else
			INFO( "Not yet connected to '{}' - attempt #{} in {}.", _url, ++_attempts+1, Chrono::ToString(_reconnectDelay) );
		_subscription = 0;//the session is already gone; there is nothing left to delete it on.
		_client->Disconnect();
		for( let until = steady_clock::now()+_reconnectDelay; steady_clock::now()<until && !Process::ShuttingDown(); std::this_thread::sleep_for(100ms) )
			Tick( false );//the process values keep evolving - the PLC's own server and publisher run on their thread.  A PLC does not freeze because a client left (#7).
		if( Process::ShuttingDown() )
			return;
		_reconnectDelay = std::min( _reconnectDelay*2, _reconnectMax );
		try{
			//Synchronous: UA_Client_connect blocks this thread for up to /emulator's 10 s client timeout, and the clock is not
			//advanced inside it - the PLC server and its publisher run on their own thread - so one failed attempt is one stall
			//of the generated values that long.  Tolerable against an async connect's state machine (UA_Client_connectAsync);
			//Tick's catch-up guard absorbs the gap.
			Connect();
		}
		catch( const std::exception& e ){
			WARN( "{} failed: {}", _connected ? "Reconnect" : Ƒ("Connect attempt #{}", _attempts+1), e.what() );
		}
	}

	α Emulator::LogStatus( bool onlyIfChanged )ι->void{
		vector<string> values;
		for( let& device : _devices ){
			vector<string> tags;
			for( let& tag : device.Tags ){
				//Branch on the spec, not on Generator:  a toggle has a generator and is written as UA_Boolean (Cycle), so it
				//rendered `status=1.0` (#13).  Both boolean modes print as the bool they are; command tags have no Value.
				let value = tag.Spec.IsBool()
					? Ƒ( "{}={}", tag.Spec.Name, tag.Generator ? tag.Value!=0 : device.Command )
					: Ƒ( "{}={:.1f}", tag.Spec.Name, tag.Value );
				tags.push_back( tag.Status ? Ƒ("{}({})", value, ToString(tag.Status)) : value );//Good is the unmarked case.
			}
			values.push_back( Ƒ("{}[{}]", device.Name, Str::Join(tags, " ")) );
		}
		let summary = Ƒ( "cycles={} published={} writes={} writeFailures={} externalChanges={} reconnects={} - {}", _cycles, _published, _writes, _writeFailures, _externalChanges, _reconnects, Str::Join(values, " ") );
		if( onlyIfChanged && summary==_lastStatus )
			return;//the deadline landed on a statusPeriod boundary - Tick just wrote this exact line, and it is the record (#12).
		_lastStatus = summary;
		INFO( "{}", summary );//pre-formatted: a `{:.1f}` inside the log message itself is a MemoryLog::Find self-deadlock.
	}

	α Emulator::Run()ε->int{
		if( _client ){
			try{
				Connect();
			}
			catch( const std::exception& e ){
				WARN( "Initial connect to '{}' failed: {} - retrying.", _url, e.what() );
			}
		}
		let start = steady_clock::now();
		_lastCycle = start;
		_nextCycle = start+_period;
		_nextStatus = start+_statusPeriod;
		optional<steady_clock::time_point> deadline;
		if( _duration )
			deadline = start+*_duration;
		INFO( "Emulator running: transport={}, period={}, devices={}{}.", toString(_transport), Chrono::ToString(_period), _devices.size(), _duration ? Ƒ(", duration={}", Chrono::ToString(*_duration)) : string{} );
		while( !Process::ShuttingDown() && (!deadline || steady_clock::now()<*deadline) ){
			let slice = std::clamp( duration_cast<milliseconds>(_nextCycle-steady_clock::now()), 0ms, 50ms );
			if( !_client ){//the server transport:  the PLC server runs on its own thread, its clients' commands wait for Tick.
				std::this_thread::sleep_for( slice );
				Tick( true );
				continue;
			}
			if( !_client->IsActivated() || _consecutiveFailures>=3 ){
				Reconnect();
				continue;
			}
			//A restarted AppServer logs the app socket in under a new session id and never revives the old one.  An OPC session
			//activated with the old id keeps working until the OpcServer's snapshot of it lapses (up to a session lifetime), then
			//is denied everything - commands included - with nothing here to notice under pubsub.  Rebind now instead (#1).
			if( let session = _app->SessionId(); session && session!=_session ){
				INFO( "AppServer session replaced - reactivating on '{}' with the new token.", _url );
				Reconnect();
				continue;
			}
			_client->Iterate( (uint32)slice.count() );//command notifications arrive in here.
			Tick( true );
		}
		LogStatus( true );
		if( !_client )
			return EXIT_SUCCESS;//the PLC server stops with the emulator.
		//Delete the subscription before disconnecting, and iterate once so the answers land.  The client keeps ten publish
		//requests in flight (outStandingPublishRequests, ua_config_default.c);  disconnecting first drops its subscription
		//table while those are unanswered, and each of the server's ten replies then fails findSubscriptionById at a
		//WARNING - the last ten lines of every run's log, reading like a defect (#10).  Deleted first, the server answers
		//them BadNoSubscription, which the vendor demotes to debug.
		if( _subscription && _client->IsActivated() ){
			_client->DeleteSubscription( _subscription );
			_client->Iterate( 100 );
			_subscription = 0;
		}
		_client->Disconnect();
		return EXIT_SUCCESS;
	}

	α Run( sp<App::Client::IAppClient> client )ε->int{
		Emulator emulator{ move(client) };
		return emulator.Run();
	}

	α GrantWriteRights( const sp<App::Client::IAppClient>& client )ι->void{
		try{
			let schema = Settings::FindArgString( "-opcSchema", "/emulator/opcSchema" ).value_or( _debug ? "opc.debug" : "opc.release" );
			//`allowed` is the generic Access::ERights vocabulary - NOT the UA access-level byte (OpcAuthorize::ToAccess is
			//what crosses to that, and the two share no bit positions).  A PLC reads, writes and subscribes; it has no
			//business holding Administer (status/timestamp/semantic-change), Delete (history write) or Purge on `nodeIds`,
			//the root resource every node inherits.  Read also covers the subscription (ToAccess: "a subscription needs
			//Read"), and these same bits serve -transport=write, whose tags go over the session rather than PubSub.
			using enum Access::ERights;
			client->GrantRights( client->UserPK(), Read | Update | Subscribe, schema, "nodeIds" );
			INFO( "Granted OPC node access for user {} on '{}' - restart the OpcServer to load it.", client->UserPK().Value, schema );
		}
		catch( const std::exception& e ){
			INFO( "createAcl failed (already granted on a previous run?): {}", e.what() );
		}
	}

	α CreateCertificates()ε->void{
		Crypto::CryptoSettings http{ Json::FindDefaultObject(Settings::AsObject("/http"), "ssl"), Process::ProductName() };
		Crypto::EnsureKeyCertificate( http );
		INFO( "AppServer login certificate: {}", http.Certificate.Path.string() );
		let opc = opcCertificate();
		Crypto::EnsureKeyCertificate( opc );
		INFO( "OPC UA client certificate: {} (SAN '{}') - its directory must be in the OpcServer's /access/trustedCertDirs.", opc.Certificate.Path.string(), opc.Certificate.SubjectAltName );
	}
}