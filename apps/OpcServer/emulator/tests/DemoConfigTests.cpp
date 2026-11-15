//The demo's five PLC overlays (docs/demo/spec.md *The PLCs*, config/demo), evaluated as the installed service evaluates them:  no
//ext var, and in a copy of the tree with no pubsub contract, as the box has none - an import the chain forced would fail here.
//Five PLCs apart - ports, uris, product dirs, names, logs, rejected dirs - the one Machine each parsed as the emulator parses it at
//startup, and no login dir that is a channel dir, so the hub's two trust lists never meet;  over args/install and args/dev both.
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/io/json.h>
#include <jde/fwk/process/process.h>
#include "../Devices.h"

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	Ω source()ι->fs::path{ return fs::path{ Process::GetEnv("REPO_SOURCE_DIR").value_or(".") }; }
	//The emulator's config dir and the paths args/install imports six levels up, at their source-tree places, and nothing else.
	struct Tree final{
		Tree()ε{
			let src = source();
			fs::create_directories( Root/"libs"/"db"/"config" );
			fs::copy( src/"libs"/"db"/"config"/"paths-common.libsonnet", Root/"libs"/"db"/"config" );
			fs::create_directories( Config() );
			fs::copy( src/"apps"/"OpcServer"/"emulator"/"config", Config(), fs::copy_options::recursive );
		}
		~Tree(){ std::error_code ec; fs::remove_all( Root, ec ); }
		α Config()Ι->fs::path{ return Root/"apps"/"OpcServer"/"emulator"/"config"; }
		const fs::path Root{ fs::temp_directory_path()/Ƒ("jde-demo-config-{}", Process::ProcessId()) };
	};
	Ω tree()ε->const Tree&{ static const Tree y; return y; }
	Ω read( const fs::path& file, sv args )ε->jobject{ return Json::ReadJsonNet( tree().Config()/file, {tree().Config()/"args"/args} ); }
	Ω certDir( const jobject& o, sv block )ε->fs::path{ return Crypto::CryptoSettings{ Json::AsObject(o, block) }.Certificate.Path.parent_path(); }
	Ω text( const jobject& o, sv path )ε->string{ return Json::AsString( o, path ); }
	//args/dev's nodeset, $(JDE_DIR) the source root, or "" where it names another.
	Ω devNodeset( sv nodeset )ι->fs::path{
		constexpr sv prefix{ "$(JDE_DIR)/" };
		return nodeset.starts_with(prefix) ? source()/nodeset.substr(prefix.size()) : fs::path{};
	}

	TEST( DemoConfigTests, EachOverlayIsItsOwnPlc ){
		ASSERT_FALSE( fs::exists(tree().Root/"apps"/"OpcServer"/"config"/"pubsub") );
		for( sv args : {"install"sv, "dev"sv} ){
			SCOPED_TRACE( args );
			flat_set<uint> ports;
			flat_set<string> uris, products, names, logs, rejected;
			flat_set<fs::path> dirs;
			for( uint n=1; n<=5; ++n ){
				let o = read( fs::path{"demo"}/Ƒ("plc{}.jsonnet", n), args );
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
				EXPECT_EQ( text(o, "/emulator/plc/rejectedCertDir"), "$(ProgramData)/Jde-Cpp/PlcEmulator."+name+".opc/ssl/rejected" );//beside the channel's certificates.
				let sessions = Json::AsNumber<uint>( o, "/emulator/plc/limits/maxSessions" );
				EXPECT_GT( sessions, 100u ) << name;//open62541's default, which every visitor's session on the node browser counts against.
				EXPECT_GE( Json::AsNumber<uint>(o, "/emulator/plc/limits/maxSecureChannels"), sessions ) << name;
				let& trusted = Json::AsArray( o, "/emulator/plc/trustedCertDirs" );
				ASSERT_EQ( trusted.size(), 1u ) << name;
				EXPECT_EQ( Json::AsSV(trusted[0]), "$(ProgramData)/Jde-Cpp/OpcHub/ssl/certs" );//where the hub's issued per-connection certificates land.
				let nodeset = text( o, "/emulator/plc/nodeset" );
				if( args=="install" )//$(ProgramData) is the one placeholder, Settings' own built-in.
					EXPECT_TRUE( nodeset.starts_with("$(ProgramData)/") && nodeset.ends_with("/demo.NodeSet2.xml") && nodeset.find("$(", 1)==string::npos ) << nodeset;
				else{
					EXPECT_TRUE( nodeset.ends_with("/demo.NodeSet2.xml") ) << nodeset;
					EXPECT_TRUE( fs::exists(devNodeset(nodeset)) ) << nodeset;
				}
				EXPECT_FALSE( Json::FindValue(o, "/emulator/pubsub") ) << name;//the source tree's contract, hidden.
				EXPECT_FALSE( Json::FindValue(o, "/emulator/tightening") ) << name;//five processes can't share 4545.
				ports.insert( Json::AsNumber<uint>(o, "/emulator/plc/port") );
				uris.insert( uri );
				products.insert( text(o, "/http/ssl/productName") ); products.insert( text(o, "/emulator/ssl/productName") );
				names.insert( text(o, "/http/ssl/certificate/commonName") ); names.insert( text(o, "/emulator/ssl/certificate/commonName") );
				logs.insert( text(o, "/logging/spd/sinks/file/path") );
				rejected.insert( text(o, "/emulator/plc/rejectedCertDir") );
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
			EXPECT_EQ( rejected.size(), 5u );
			EXPECT_EQ( dirs.size(), 10u );//no login dir is a channel dir:  the hub enrolls from the first five, the gateway trusts the other five.
		}
	}
	//The Install config alone on a dev box:  the pumps nodeset from the source tree and its namespace, the contract's, which the
	//config names where the base would read the contract it hides - else every pump path resolves in namespace 0.
	TEST( DemoConfigTests, TheInstallConfigServesThePumpsAlone ){
		let o = read( "Opc.PlcEmulator.Install.jsonnet", "dev" );
		let contract = Json::ReadJsonNet( source()/"apps"/"OpcServer"/"config"/"pubsub"/"pumps.libsonnet" );
		EXPECT_EQ( text(o, "/emulator/namespace"), text(contract, "/dataSet/namespace") );
		EXPECT_EQ( text(o, "/emulator/transport"), "server" );
		let nodeset = text( o, "/emulator/plc/nodeset" );
		EXPECT_TRUE( nodeset.ends_with("/pumps.NodeSet2.xml") ) << nodeset;
		EXPECT_TRUE( fs::exists(devNodeset(nodeset)) ) << nodeset;
		EXPECT_FALSE( Json::FindValue(o, "/emulator/pubsub") );
		EXPECT_EQ( text(o, "/web/client/ssl/caFile"), "$(ProgramData)/Jde-Cpp/OpcHub/ssl/certs/OpcHub.pem" );//the hub overlay's anchor.
	}
}