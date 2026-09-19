#include "Emulator.h"
#include <jde/fwk/chrono.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/access/usings.h>//Access::ERights - the vocabulary -grant's acl is written in.
#include "Devices.h"
#include "EmulatorClient.h"
#include "PlcServer.h"
#include "Signals.h"

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ ELogTags::App };

	//-<arg>=<value> beats the settings file, as the soak driver's do.
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
	//the UA channel cert: /emulator/ssl puts it under the PlcEmulator product dir with the applicationUri as its SAN.
	Ω opcCertificate()ι->Crypto::CryptoSettings{ return Crypto::CryptoSettings{ Settings::FindDefaultObject("/emulator/ssl") }; }

	//pubsub: process values publish over Part 14 and anything the contract does not list is written over the session;
	//write: everything over the session - the PLC server is not started.  Commands are always subscribed.
	enum class ETransport : uint8{ PubSub, Write };
	//Tag, Device and ParseDevices live in Devices.h - a unit the tests build without a session or a PLC server (T2).

	struct Emulator final : noncopyable{
		Emulator( sp<App::Client::IAppClient> app )ε;
		α Run()ε->int;
	private:
		α Connect()ε->void;//session, browse-path resolution, command subscriptions.
		α Reconnect()ε->void;
		α Tick( bool sessionUp )ι->void;//one pass of the device clock; driven by BOTH loops - Run()'s and Reconnect()'s wait.
		α Cycle( Duration dt, bool sessionUp )ι->void;
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
		//The device clock.  Members rather than Run() locals because Reconnect()'s wait loop advances them too (#7).
		steady_clock::time_point _lastCycle{}, _nextCycle{}, _nextStatus{};
		UA_UInt32 _subscription{};//the command subscription this session holds, kept so a clean shutdown can delete it (#10).
		uint _cycles{}, _published{}, _writes{}, _writeFailures{}, _consecutiveFailures{}, _externalChanges{}, _reconnects{};
		bool _connected{};//a session has activated at least once.  Until then a failed attempt is the initial connect still failing, not a lost session (#17).
		uint _attempts{};//connect attempts before the first activation - the initial phase's own count; _reconnects counts only losses after it.
		string _lastStatus;//what LogStatus last wrote - the final line compares against it (#12).
	};

	Emulator::Emulator( sp<App::Client::IAppClient> app )ε:
		_app{ move(app) },
		_transport{ ETransport::PubSub },
		_url{ argString("-url", "/emulator/url", "opc.tcp://127.0.0.1:4840") },
		_applicationUri{ Settings::FindString("/emulator/applicationUri").value_or("urn:jde:plc-emulator") },
		_serverApplicationUri{ Settings::FindString("/emulator/serverApplicationUri").value_or("urn:open62541.server.application") },
		_period{ argDuration("-period", "/emulator/period", 1s) },
		_statusPeriod{ argDuration("-statusPeriod", "/emulator/statusPeriod", 1min) },
		_reconnectMin{ argDuration("-reconnectMin", "/emulator/reconnectMin", 1s) },
		_reconnectMax{ argDuration("-reconnectMax", "/emulator/reconnectMax", 30s) },
		_reconnectDelay{ _reconnectMin }{
		let transport = argString( "-transport", "/emulator/transport", "pubsub" );
		THROW_IF( transport!="pubsub" && transport!="write", "transport '{}' must be pubsub or write.", transport );
		_transport = transport=="write" ? ETransport::Write : ETransport::PubSub;
		if( let d = argDuration("-duration", "/emulator/duration", Duration::zero()); d>Duration::zero() )
			_duration = d;
		PubSub::Config contract{ Settings::AsObject("/emulator/pubsub") };
		_nsUri = contract.Namespace;
		if( _transport==ETransport::PubSub ){
			let nodeset = Settings::FindPath( "/emulator/plc/nodeset" ); THROW_IF( !nodeset, "/emulator/plc/nodeset is required - the nodeset the OpcServer loads." );
			//loopback by default - nothing is meant to connect to the PLC's own endpoint, and it is anonymous-full.
			let bind = Settings::FindString( "/emulator/plc/bind" ).value_or( "127.0.0.1" );
			_plc = mu<PlcServer>( Settings::FindNumber<UA_UInt16>("/emulator/plc/port").value_or(4841), bind, *nodeset, move(contract) );
		}
		_devices = ParseDevices( Settings::FindDefaultArray("/emulator/devices"), _plc ? FindField{[this](sv name){ return _plc->FindField(name); }} : FindField{}, SRCE_CUR );
		for( auto& device : _devices ){//the vectors are final: ParseDevices returned them.
			for( auto& tag : device.Tags )
				tag.Self = this;
		}
		let opc = opcCertificate();
		//On every start, not only -createCert:  the SAN must equal the applicationUri we advertise, and an edited uri would
		//otherwise present the stale SAN forever, refused BadCertificateUriInvalid with nothing naming the file to delete
		//(the gateway's CertTests lesson).  EnsureKeyCertificate re-issues on SAN drift and expiry as well as absence.
		Crypto::EnsureKeyCertificate( opc );
		_client = mu<EmulatorClient>( _url, _applicationUri, _serverApplicationUri, opc, Ƒ("{:x}", _app->SessionId()) );
	}

	α Emulator::Connect()ε->void{
		_client->Connect();
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

	//A real PLC's process values evolve whether or not anyone is connected, so every generator advances on every period
	//regardless of `sessionUp` - what the session gates is only the *write* over it.  Published tags (tag.Field) go to the
	//local PLC node, which the Part 14 writer samples on its own timer, so they keep flowing to the OpcServer through an
	//outage of the client session.  A session-written tag keeps evolving in memory and lands on the first cycle after
	//recovery, instead of the whole outage arriving as one `dt` (#7).
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
						let refused = e.Code()==UA_STATUSCODE_BADWRITENOTSUPPORTED || e.Code()==UA_STATUSCODE_BADUSERACCESSDENIED;
						if( !refused || !tag.Status || tag.StatusRefused )
							throw;
						tag.StatusRefused = true;
						WARN( "[{}]{}: the server refused this reading's status ({}) over the session - {}.  Writing the value alone from here on; the pubsub transport carries the status.", device.Name, tag.Spec.Name, ToString(tag.Status), UAException::Message((StatusCode)e.Code()) );
						_client->Write( tag.Node, v );
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
		for( let until = steady_clock::now()+_reconnectDelay; steady_clock::now()<until && !Process::ShuttingDown(); std::this_thread::sleep_for(100ms) ){
			if( _plc )
				_plc->Iterate();//keep the PLC's own server and publisher alive while the session is down.
			Tick( false );//...and its process values evolving.  A PLC does not freeze because a client left (#7).
		}
		if( Process::ShuttingDown() )
			return;
		_reconnectDelay = std::min( _reconnectDelay*2, _reconnectMax );
		try{
			//Synchronous: UA_Client_connect blocks this thread for up to /emulator's 10 s client timeout, and neither the
			//PLC server nor the clock is iterated inside it, so one failed attempt is one stall of that length.  Tolerable
			//against an async connect's state machine (UA_Client_connectAsync); Tick's catch-up guard absorbs the gap.
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
		try{
			Connect();
		}
		catch( const std::exception& e ){
			WARN( "Initial connect to '{}' failed: {} - retrying.", _url, e.what() );
		}
		let start = steady_clock::now();
		_lastCycle = start;
		_nextCycle = start+_period;
		_nextStatus = start+_statusPeriod;
		optional<steady_clock::time_point> deadline;
		if( _duration )
			deadline = start+*_duration;
		INFO( "Emulator running: transport={}, period={}, devices={}{}.", _transport==ETransport::PubSub ? "pubsub" : "write", Chrono::ToString(_period), _devices.size(), _duration ? Ƒ(", duration={}", Chrono::ToString(*_duration)) : string{} );
		while( !Process::ShuttingDown() && (!deadline || steady_clock::now()<*deadline) ){
			if( _plc )
				_plc->Iterate();//the publisher's timer fires in here.
			if( !_client->IsActivated() || _consecutiveFailures>=3 ){
				Reconnect();
				continue;
			}
			let slice = std::clamp( duration_cast<milliseconds>(_nextCycle-steady_clock::now()), 0ms, 50ms );
			_client->Iterate( (uint32)slice.count() );//command notifications arrive in here.
			Tick( true );
		}
		LogStatus( true );
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
			let schema = argString( "-opcSchema", "/emulator/opcSchema", _debug ? "opc.debug" : "opc.release" );
			//`allowed` is the generic Access::ERights vocabulary - NOT the UA access-level byte (OpcAuthorize::ToAccess is
			//what crosses to that, and the two share no bit positions).  A PLC reads, writes and subscribes; it has no
			//business holding Administer (status/timestamp/semantic-change), Delete (history write) or Purge on `nodeIds`,
			//the root resource every node inherits.  Read also covers the subscription (ToAccess: "a subscription needs
			//Read"), and these same bits serve -transport=write, whose tags go over the session rather than PubSub.
			using enum Access::ERights;
			constexpr uint plcAccess{ underlying(Read | Update | Subscribe) };
			client->QuerySync<jvalue>(
				"createAcl( identity:{id:$userId}, permissionRight:{ allowed:$allowed, denied:0, resource:{schemaName:$schemaName, slug:\"nodeIds\"}} )",
				{{"userId", client->UserPK().Value}, {"allowed", plcAccess}, {"schemaName", schema}} );
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
