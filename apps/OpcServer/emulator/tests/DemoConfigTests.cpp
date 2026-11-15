//The demo's five PLC overlays (docs/demo/spec.md *The PLCs*, config/demo), evaluated as the installed service evaluates them:  over
//the install args and no ext var.  Five PLCs apart - ports, uris, product dirs, names, logs - the one Machine each parsed as the
//emulator parses it at startup, and no login dir that is a channel dir, so the hub's two trust lists never meet.
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/io/json.h>
#include <jde/fwk/process/process.h>
#include "../Devices.h"

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	Ω source()ι->fs::path{ return fs::path{ Process::GetEnv("REPO_SOURCE_DIR").value_or(".") }; }
	Ω overlay( uint n )ε->jobject{
		let config = source()/"apps"/"OpcServer"/"emulator"/"config";
		return Json::ReadJsonNet( config/"demo"/Ƒ("plc{}.jsonnet", n), {config/"args"/"install"} );
	}
	Ω certDir( const jobject& o, sv block )ε->fs::path{ return Crypto::CryptoSettings{ Json::AsObject(o, block) }.Certificate.Path.parent_path(); }
	Ω text( const jobject& o, sv path )ε->string{ return Json::AsString( o, path ); }

	TEST( DemoConfigTests, TheNodesetIsBesideThePumps ){
		EXPECT_TRUE( fs::exists(source()/"apps"/"OpcServer"/"config"/"nodesets"/"demo.NodeSet2.xml") );
	}
	TEST( DemoConfigTests, EachOverlayIsItsOwnPlc ){
		flat_set<uint> ports;
		flat_set<string> uris, products, names, logs;
		flat_set<fs::path> dirs;
		for( uint n=1; n<=5; ++n ){
			let o = overlay( n );
			let name = Ƒ( "plc{}", n );
			let uri = "urn:jde:demo:"+name;
			EXPECT_EQ( text(o, "/instanceName"), name );
			EXPECT_EQ( text(o, "/emulator/transport"), "server" ) << name;
			EXPECT_EQ( text(o, "/emulator/zone"), "America/New_York" ) << name;
			EXPECT_EQ( text(o, "/emulator/namespace"), "urn:jde:demo" ) << name;
			EXPECT_EQ( Json::AsNumber<uint>(o, "/emulator/plc/port"), 4841+n ) << name;
			EXPECT_EQ( text(o, "/emulator/applicationUri"), uri );
			EXPECT_EQ( text(o, "/emulator/ssl/certificate/subjectAltName"), "URI:"+uri );
			EXPECT_EQ( text(o, "/http/ssl/productName"), "PlcEmulator."+name );
			EXPECT_EQ( text(o, "/http/ssl/certificate/commonName"), name );
			EXPECT_EQ( text(o, "/emulator/ssl/productName"), "PlcEmulator."+name+".opc" );
			EXPECT_EQ( text(o, "/emulator/ssl/certificate/commonName"), name+".opc" );
			let sessions = Json::AsNumber<uint>( o, "/emulator/plc/limits/maxSessions" );
			EXPECT_GT( sessions, 100u ) << name;//open62541's default, which every visitor's session on the node browser counts against.
			EXPECT_GE( Json::AsNumber<uint>(o, "/emulator/plc/limits/maxSecureChannels"), sessions ) << name;
			let& trusted = Json::AsArray( o, "/emulator/plc/trustedCertDirs" );
			ASSERT_EQ( trusted.size(), 1u ) << name;
			EXPECT_EQ( Json::AsSV(trusted[0]), "$(ProgramData)/Jde-Cpp/OpcHub/ssl/certs" );//where the hub's issued per-connection certificates land.
			let nodeset = text( o, "/emulator/plc/nodeset" );//$(ProgramData) is the one placeholder, Settings' own built-in.
			EXPECT_TRUE( nodeset.starts_with("$(ProgramData)/") && nodeset.ends_with("/demo.NodeSet2.xml") && nodeset.find("$(", 1)==string::npos ) << nodeset;
			EXPECT_FALSE( Json::FindValue(o, "/emulator/pubsub") ) << name;//the source tree's contract, hidden.
			EXPECT_FALSE( Json::FindValue(o, "/emulator/tightening") ) << name;//five processes can't share 4545.
			ports.insert( Json::AsNumber<uint>(o, "/emulator/plc/port") );
			uris.insert( uri );
			products.insert( text(o, "/http/ssl/productName") ); products.insert( text(o, "/emulator/ssl/productName") );
			names.insert( text(o, "/http/ssl/certificate/commonName") ); names.insert( text(o, "/emulator/ssl/certificate/commonName") );
			logs.insert( text(o, "/logging/spd/sinks/file/path") );
			dirs.insert( certDir(o, "/http/ssl") ); dirs.insert( certDir(o, "/emulator/ssl") );
			let devices = ParseDevices( Json::AsArray(o, "/emulator/devices"), {}, &Chrono::ToTimeZone(text(o, "/emulator/zone"), "zone"), TimePoint{sys_days{2026y/November/15}} );
			ASSERT_EQ( devices.size(), 1u ) << name;
			let& machine = devices[0];
			EXPECT_EQ( machine.Name, "Machine" );
			EXPECT_TRUE( machine.StatusTag && machine.ReasonTag ) << name;//a generated `status` and the stops' `reasonCode`.
			EXPECT_TRUE( std::ranges::any_of(machine.Tags, [](let& t){ return t.Spec.Mode==EMode::Production; }) ) << name;
			EXPECT_TRUE( std::ranges::any_of(machine.Tags, [](let& t){ return t.Spec.Mode==EMode::Follow && t.Spec.Follows=="totalCount"; }) ) << name;
			EXPECT_EQ( machine.Slows.empty(), n!=2 ) << name;//the capper's slow half hours alone.
		}
		EXPECT_EQ( ports.size(), 5u );
		EXPECT_EQ( uris.size(), 5u );
		EXPECT_EQ( products.size(), 10u );
		EXPECT_EQ( names.size(), 10u );
		EXPECT_EQ( logs.size(), 5u );
		EXPECT_EQ( dirs.size(), 10u );//no login dir is a channel dir:  the hub enrolls from the first five, the gateway trusts the other five.
	}
}