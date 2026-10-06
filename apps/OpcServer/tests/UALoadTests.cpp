#include <fstream>
#include <jde/fwk/settings.h>
#include "../src/globals.h"
#include "../src/UAServer.h"
#include <jde/opc/uatypes/BrowsePath.h>
#define let const auto

namespace Jde::Opc::Server::Tests{
	namespace{
		struct Model final{ string Uri; string PublicationDate; vector<string> Required; };
		constexpr sv Ns0Uri{ "http://opcfoundation.org/UA/" };

		Ω read( const fs::path& file, uintmax_t limit=std::numeric_limits<uintmax_t>::max() )ε->string{
			std::ifstream f{ file, std::ios::binary };
			THROW_IF( !f, "Could not open '{}'", file.string() );
			string y( std::min(limit, fs::file_size(file)), '\0' );
			f.read( y.data(), y.size() );
			return y;
		}
		Ω section( sv xml, sv tag )ι->sv{
			let open = Ƒ( "<{}>", tag );
			let begin = xml.find( open );
			let end = begin==sv::npos ? sv::npos : xml.find( Ƒ("</{}>", tag), begin );
			return end==sv::npos ? sv{} : xml.substr( begin+open.size(), end-begin-open.size() );
		}
		Ω unescape( sv text )ι->string{
			string y{ text };
			for( let& [entity, c] : std::array<std::pair<sv,sv>,5>{{ {"&lt;","<"}, {"&gt;",">"}, {"&quot;","\""}, {"&apos;","'"}, {"&amp;","&"} }} )
				y = Str::Replace( y, entity, c );
			return y;
		}
		Ω attribute( sv startTag, sv name )ι->string{
			let key = Ƒ( "{}=", name );
			for( auto at = startTag.find(key); at!=sv::npos; at = startTag.find(key, at+1) ){
				let begin = at+key.size()+1;
				if( at==0 || !std::isspace((unsigned char)startTag[at-1]) || begin>startTag.size() )
					continue;//ParentNodeId= for NodeId=
				let quote = startTag[begin-1];//MTConnect quotes with '
				return unescape( startTag.substr(begin, startTag.find(quote, begin)-begin) );
			}
			return {};
		}
		//The <Models> block:  the models the file defines and the ones each requires.  It precedes the nodes, so the head is enough.
		Ω models( const fs::path& file )ε->vector<Model>{
			let xml = read( file, 1<<20 );
			let block = section( xml, "Models" );
			vector<Model> y;
			for( auto at = block.find("<Model"); at!=sv::npos; at = block.find("<Model", at+1) ){
				if( !std::isspace((unsigned char)block[at+6]) )
					continue;
				let tagEnd = block.find( '>', at );
				let tag = block.substr( at, tagEnd-at );
				Model model{ attribute(tag, "ModelUri"), attribute(tag, "PublicationDate"), {} };
				if( !tag.ends_with('/') ){
					let end = block.find( "</Model>", tagEnd );
					let children = block.substr( tagEnd, end==sv::npos ? sv::npos : end-tagEnd );
					for( auto r = children.find("<RequiredModel"); r!=sv::npos; r = children.find("<RequiredModel", r+1) )
						model.Required.push_back( attribute(children.substr(r, children.find('>', r)-r), "ModelUri") );
				}
				y.push_back( move(model) );
			}
			return y;
		}
		//Every file under the UA-Nodeset tree by the model it defines - the latest PublicationDate where several do.
		Ω modelFiles( const fs::path& root )ε->const flat_map<string,fs::path>&{
			static const flat_map<string,fs::path> y = [&]{
				flat_map<string,std::pair<string,fs::path>> latest;
				for( let& entry : fs::recursive_directory_iterator(root) ){
					if( !entry.is_regular_file() || entry.path().extension()!=".xml" )
						continue;
					for( auto& model : models(entry.path()) ){
						auto& [date, path] = latest[model.Uri];
						if( path.empty() || model.PublicationDate>date )
							latest[model.Uri] = { move(model.PublicationDate), entry.path() };
					}
				}
				flat_map<string,fs::path> files;
				for( let& [uri, dated] : latest )
					files.emplace( uri, dated.second );
				return files;
			}();
			return y;
		}
		//Required models before the models that require them.
		Ω loadOrder( const fs::path& file, const flat_map<string,fs::path>& files, flat_set<fs::path>& seen, vector<fs::path>& order )ε->void{
			if( !seen.emplace(file).second )
				return;
			for( let& model : models(file) ){
				for( let& uri : model.Required ){
					if( uri==Ns0Uri )
						continue;
					let p = files.find( uri );
					THROW_IF( p==files.end(), "'{}' requires '{}', which no file under the UA-Nodeset tree defines.", file.string(), uri );
					loadOrder( p->second, files, seen, order );
				}
			}
			order.push_back( file );
		}
		//Every node the file declares outside namespace 0, its NodeId mapped from the file's namespace table to the server's.
		Ω declaredNodes( UA_Server& ua, const fs::path& file )ε->vector<NodeId>{
			let xml = read( file );
			vector<string> uris;
			let table = section( xml, "NamespaceUris" );
			for( auto at = table.find("<Uri>"); at!=sv::npos; at = table.find("<Uri>", at+1) )
				uris.push_back( unescape(table.substr(at+5, table.find("</Uri>", at)-at-5)) );
			constexpr std::array classes{ "UAObject"sv, "UAVariable"sv, "UAMethod"sv, "UAObjectType"sv, "UAVariableType"sv, "UADataType"sv, "UAReferenceType"sv, "UAView"sv };
			vector<NodeId> y;
			for( auto at = xml.find("<UA"); at!=string::npos; at = xml.find("<UA", at+1) ){
				let tag = sv{xml}.substr( at+1, xml.find('>', at)-at-1 );
				if( std::ranges::find(classes, tag.substr(0, tag.find_first_of(" \t\r\n")))==classes.end() )
					continue;
				let text = attribute( tag, "NodeId" );
				UA_NodeId id;
				THROW_IF( UA_NodeId_parse(&id, UA_String{text.size(), (UA_Byte*)text.data()}), "'{}' declares an unparsable NodeId '{}'.", file.filename().string(), text );
				if( id.namespaceIndex ){
					THROW_IF( id.namespaceIndex>uris.size(), "'{}' declares '{}' outside its namespace table.", file.filename().string(), text );
					id.namespaceIndex = NamespaceIndex( ua, uris[id.namespaceIndex-1] );
					y.emplace_back( move(id) );
				}
				else
					UA_NodeId_clear( &id );
			}
			return y;
		}
	}

	struct UALoadTests : ::testing::Test{
	protected:
		Ω SetUpTestCase()ι->void{}
		Ω TearDownTestCase()ι->void{}
		α SetUp()ε->void{
			Server::Initialize( GetSchemaPtr() );
		}
		Ω Path()ι->fs::path{ return *Settings::FindPath("/testing/UANodeSets"); }
		//NodesetLoader_loadFile reports success whatever its node adds did, so a load that dropped every node passed
		//(open62541-1.5.9 review #5).  Load the models the file requires first, each from the UA-Nodeset tree, then check that
		//every node it declares outside namespace 0 is in the server - all but `knownDrops`, the nodes open62541 refuses today.
		Ω LoadWithDependencies( fs::path file, size_t knownDrops=0 )ε->void{
			file = Path()/file;
			auto& ua = GetUAServer();
			flat_set<fs::path> seen; vector<fs::path> order;
			loadOrder( file, modelFiles(Path()), seen, order );
			for( let& f : order )
				ua.Load( f );
			let declared = declaredNodes( ua, file );
			EXPECT_FALSE( declared.empty() ) << file.filename().string() << " declares no node outside namespace 0";
			vector<string> missing;
			for( let& id : declared ){
				UA_NodeClass nodeClass;
				if( UA_Server_readNodeClass(ua.Ptr(), id, &nodeClass) )
					missing.push_back( id.ToString() );
			}
			EXPECT_EQ( missing.size(), knownDrops ) << missing.size() << " of the " << declared.size() << " nodes " << file.filename().string() << " declares are not in the server, where "
				<< knownDrops << " are known to be refused - a change either way is news;  update the count once it is understood.  e.g. "
				<< Str::Join( std::span{missing}.first(std::min<size_t>(missing.size(), 5)), ", " );
		}
	};

	TEST_F( UALoadTests, LoadMyKitchen ){
		LoadWithDependencies( "CommercialKitchenEquipment/Opc.Ua.CommercialKitchenEquipment.NodeSet2.xml" );
		GetUAServer().Load( fs::path{*Process::GetEnv("JDE_DIR")}/"apps/OpcServer/config/nodesets/kitchen.xml" );
	}

	//config/nodesets/pumps.NodeSet2.xml - the PLC emulator's tags.  Instances are explicit in NodeSet2 (no type
	//instantiation), so every pump must carry its own status/motorRpm; the values are the file's <Value> elements.
	TEST_F( UALoadTests, Pumps ){
		auto& ua = GetUAServer();
		ua.Load( fs::path{*Process::GetEnv("JDE_DIR")}/"apps/OpcServer/config/nodesets/pumps.NodeSet2.xml" );
		let ns = NamespaceIndex( ua, "urn:jde:pumps" );
		const flat_map<string,NsIndex> aliases{ {"pumps", ns} };
		for( let& pump : {"pump1", "pump2", "pump3", "pump4", "pumpManual"} ){
			let status = BrowsePath::Resolve( ua, Ƒ("pumps~{}/pumps~status", pump), 0, aliases );
			EXPECT_EQ( status.namespaceIndex, ns ) << pump;
			UA_Variant v; UA_Variant_init( &v );
			ASSERT_EQ( UA_Server_readValue(ua.Ptr(), status, &v), UA_STATUSCODE_GOOD ) << pump;
			ASSERT_TRUE( UA_Variant_hasScalarType(&v, &UA_TYPES[UA_TYPES_BOOLEAN]) ) << pump;
			EXPECT_TRUE( *(UA_Boolean*)v.data ) << pump;
			UA_Variant_clear( &v );
			let rpm = BrowsePath::Resolve( ua, Ƒ("pumps~{}/pumps~motorRpm", pump), 0, aliases );
			ASSERT_EQ( UA_Server_readValue(ua.Ptr(), rpm, &v), UA_STATUSCODE_GOOD ) << pump;
			ASSERT_TRUE( UA_Variant_hasScalarType(&v, &UA_TYPES[UA_TYPES_DOUBLE]) ) << pump;
			EXPECT_EQ( *(UA_Double*)v.data, 0.0 ) << pump;
			UA_Variant_clear( &v );
		}
		EXPECT_EQ( *BrowsePath::Resolve(ua, "pumps~pump1/pumps~motorRpm", 0, aliases).Numeric(), 6012u );
		EXPECT_THROW( BrowsePath::Resolve(ua, "pumps~pump1/pumps~nope", 0, aliases), Exception );
		EXPECT_THROW( BrowsePath::Resolve(ua, "nope~pump1", 0, aliases), Exception );
	}

	//A client can only ever put an Int32 on the wire for an enum, so the server has to widen it back to the node's
	//DataType before the type check - and it can only do that when the enum has a UA_DataType registered where
	//adjustValueType() looks.  Before 1.5.9 that was config.customDataTypes alone, so without UAServer::PublishDataTypes
	//this write was BadTypeMismatch, which is what the SPA hit changing ExampleStacklight's DeviceHealth.  1.5.9 looks
	//through the server's internal lists too, and only Run publishes, so this writes with nothing published
	//(open62541-1.5.9 review #7, #11).
	TEST_F( UALoadTests, WriteNodesetEnum ){
		auto& ua = GetUAServer();
		ua.Load( Path()/"DI/Opc.Ua.Di.NodeSet2.xml" );
		ua.Load( Path()/"IA/Opc.Ua.IA.NodeSet2.xml" );
		ua.Load( Path()/"IA/Opc.Ua.IA.NodeSet2.examples.xml" );
		ASSERT_FALSE( UA_Server_getConfig(ua.Ptr())->customDataTypes ) << "nothing published";
		let ns = NamespaceIndex( ua, "http://opcfoundation.org/UA/IA/Examples/" );
		let deviceHealth = NodeId{ ns, (uint32)6002 };//ExampleStacklight/DeviceHealth - DataType DeviceHealthEnumeration (DI).

		UA_Int32 failure{ 1 };//DeviceHealthEnumeration::FAILURE
		UA_Variant v; UA_Variant_init( &v );
		UA_Variant_setScalar( &v, &failure, &UA_TYPES[UA_TYPES_INT32] );//no clear: setScalar does not copy, and writeValue does.
		ASSERT_EQ( UA_Server_writeValue(ua.Ptr(), deviceHealth, v), UA_STATUSCODE_GOOD );

		UA_Variant read; UA_Variant_init( &read );
		ASSERT_EQ( UA_Server_readValue(ua.Ptr(), deviceHealth, &read), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( UA_Variant_isScalar(&read) );
		EXPECT_EQ( *(UA_Int32*)read.data, failure );
		UA_Variant_clear( &read );
	}

	//open62541-1.5.9 review #11:  Load published after every file, so every lookup that missed in a later Load scanned each
	//type twice.  Run publishes once, after the last nodeset and before PubSub, the one reader of the snapshot.
	TEST_F( UALoadTests, RunPublishesTheNodesetTypesOnce ){
		auto& ua = GetUAServer();
		ua.Load( Path()/"DI/Opc.Ua.Di.NodeSet2.xml" );
		let config = UA_Server_getConfig( ua.Ptr() );
		ASSERT_FALSE( config->customDataTypes ) << "Load publishes nothing";
		ua.Run();
		let deviceHealthEnumeration = NodeId{ NamespaceIndex(ua, "http://opcfoundation.org/UA/DI/"), (uint32)6244 };
		EXPECT_TRUE( UA_findDataTypeWithCustom(&deviceHealthEnumeration, config->customDataTypes) ) << "DI's enum, published";
	}

	//open62541 1.5.9 refuses structure-typed variables it cannot give a default value:  "Could not create a default value".
	TEST_F( UALoadTests, AdditiveManufacturing ){
		LoadWithDependencies( "AdditiveManufacturing/Opc.Ua.AdditiveManufacturing.Nodeset2.xml", 2 );
	}

	//open62541 1.5.9 refuses 6 variables whose VariableType's attributes fail the type check, and their children.
	TEST_F( UALoadTests, Server_loadADINodeset ){
		LoadWithDependencies( "ADI/Opc.Ua.Adi.NodeSet2.xml", 36 );
	}

	TEST_F( UALoadTests, LoadAMBNodeset ){
		LoadWithDependencies( "AMB/Opc.Ua.AMB.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadAMLBaseTypesNodeset ){
		LoadWithDependencies( "AML/Opc.Ua.AMLBaseTypes.NodeSet2.xml" );
	}

	//open62541 1.5.9 refuses structure-typed variables it cannot give a default value:  "Could not create a default value".
	TEST_F( UALoadTests, LoadAutoIDNodeset ){
		LoadWithDependencies( "AutoID/Opc.Ua.AutoID.NodeSet2.xml", 3 );
	}

	//open62541 1.5.9 refuses structure-typed variables it cannot give a default value:  "Could not create a default value".
	TEST_F( UALoadTests, LoadBACnetNodeset ){
		LoadWithDependencies( "BACnet/Opc.Ua.BACnet.NodeSet2.xml", 59 );
	}

	TEST_F( UALoadTests, LoadCASNodeset ){
		LoadWithDependencies( "CAS/Opc.Ua.CAS.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadCommercialKitchenEquipmentNodeset ){
		LoadWithDependencies( "CommercialKitchenEquipment/Opc.Ua.CommercialKitchenEquipment.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadCSPPlusForMachineNodeset ){
		LoadWithDependencies( "CSPPlusForMachine/Opc.Ua.CSPPlusForMachine.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadDEXPINodeset ){
		LoadWithDependencies( "DEXPI/Opc.Ua.DEXPI.NodeSet2.xml" );
	}

	//open62541 1.5.9 refuses WarningValues (i=472):  ValueRank -3 with ArrayDimensions 0.
	TEST_F( UALoadTests, LoadDINodeset ){
		LoadWithDependencies( "DI/Opc.Ua.Di.NodeSet2.xml", 1 );
	}


	//open62541 1.5.9 refuses the UIPlugInType VariableType (ValueRank 1 with ArrayDimensions 0) and its 8 properties.
	TEST_F( UALoadTests, LoadFDI5Nodeset ){
		LoadWithDependencies( "FDI/Opc.Ua.Fdi5.NodeSet2.xml", 9 );
	}

	TEST_F( UALoadTests, LoadFDI7Nodeset ){
		LoadWithDependencies( "FDI/Opc.Ua.Fdi7.NodeSet2.xml" );
	}


	TEST_F( UALoadTests, LoadFDTNodeset ){
		LoadWithDependencies( "FDT/Opc.Ua.FDT.NodeSet.xml" );
	}

	//open62541 1.5.9 refuses the well-known roles:  they hang off ns0's RoleSet (i=15606), which open62541's namespace 0 lacks.
	TEST_F( UALoadTests, LoadGDSNodeset ){
		LoadWithDependencies( "GDS/Opc.Ua.Gds.NodeSet2.xml", 90 );
	}

	//The nodeset loader refuses it:  "Infinite loop in the references" (open62541-nodeset-loader src/Nodeset.c), open62541 1.5.9.
	// TEST_F( UALoadTests, LoadServer_loadGlassNodeset ){
	// 	LoadWithDependencies( "Glass/Flat/Opc.Ua.Glass.NodeSet2.xml" );
	// }


	//open62541 1.5.9 refuses variables whose values fail its type check.
	TEST_F( UALoadTests, LoadI4AASNodeset ){
		LoadWithDependencies( "I4AAS/Opc.Ua.I4AAS.NodeSet2.xml", 27 );
	}


	TEST_F( UALoadTests, LoadIANodeset ){
		LoadWithDependencies( "IA/Opc.Ua.IA.NodeSet2.xml" );
	}

/*
	TEST_F( UALoadTests, LoadIAExamplesNodeset ){
		LoadWithDependencies( "IA/Opc.Ua.IA.NodeSet2.examples.xml" );
	}
*/

	TEST_F( UALoadTests, LoadIOLinkIODDNodeset ){
		LoadWithDependencies( "IOLink/Opc.Ua.IOLinkIODD.NodeSet2.xml" );
	}

	//open62541 1.5.9 refuses a variable whose ValueRank and ArrayDimensions disagree, and one whose value fails the type check.
	TEST_F( UALoadTests, LoadIOLinkNodeset ){
		LoadWithDependencies( "IOLink/Opc.Ua.IOLink.NodeSet2.xml", 2 );
	}

	TEST_F( UALoadTests, LoadISA95Nodeset ){
		LoadWithDependencies( "ISA-95/Opc.ISA95.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMachineryNodeset ){
		LoadWithDependencies( "Machinery/Opc.Ua.Machinery.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMachineryExamplesNodeset ){
		LoadWithDependencies( "Machinery/Opc.Ua.Machinery.Examples.NodeSet2.xml" );
	}

	//open62541 1.5.9 refuses structure-typed variables it cannot give a default value:  "Could not create a default value".
	TEST_F( UALoadTests, LoadMachineToolNodeset ){
		LoadWithDependencies( "MachineTool/Opc.Ua.MachineTool.NodeSet2.xml", 2 );
	}


	TEST_F( UALoadTests, LoadMDISNodeset ){
		LoadWithDependencies( "MDIS/Opc.MDIS.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMiningDevelopmentSupportGeneralNodeset ){
		LoadWithDependencies( "Mining/DevelopmentSupport/General/1.0.0/Opc.Ua.Mining.DevelopmentSupport.General.NodeSet2.xml" );
	}


	TEST_F( UALoadTests, LoadMiningExtractionGeneralNodeset ){
		LoadWithDependencies( "Mining/Extraction/General/1.0.0/Opc.Ua.Mining.Extraction.General.NodeSet2.xml" );
	}


	TEST_F( UALoadTests, LoadMiningMineralProcessingGeneralNodeset ){
		LoadWithDependencies( "Mining/MineralProcessing/General/1.0.0/Opc.Ua.Mining.MineralProcessing.General.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMiningMonitoringSupervisionServicesGeneralNodeset ){
		LoadWithDependencies( "Mining/MonitoringSupervisionServices/General/1.0.0/Opc.Ua.Mining.MonitoringSupervisionServices.General.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMTConnectNodeset ){
		LoadWithDependencies( "MTConnect/Opc.Ua.MTConnect.NodeSet2.xml" );
	}

	//open62541 1.5.9 refuses a structure-typed variable it cannot give a default value:  "Could not create a default value".
	TEST_F( UALoadTests, LoadOPENSCSNodeset ){
		LoadWithDependencies( "OpenSCS/Opc.Ua.OPENSCS.NodeSet2.xml", 1 );
	}

	//open62541 1.5.9 refuses structure-typed variables it cannot give a default value:  "Could not create a default value".
	TEST_F( UALoadTests, LoadPackMLNodeset ){
		LoadWithDependencies( "PackML/Opc.Ua.PackML.NodeSet2.xml", 2 );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionCalenderNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/Calender/1.00/Opc.Ua.PlasticsRubber.Extrusion.Calender.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionCalibratorNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/Calibrator/1.00/Opc.Ua.PlasticsRubber.Extrusion.Calibrator.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionCorrugatorNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/Corrugator/1.00/Opc.Ua.PlasticsRubber.Extrusion.Corrugator.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionCutterNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/Cutter/1.00/Opc.Ua.PlasticsRubber.Extrusion.Cutter.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionDieNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/Die/1.00/Opc.Ua.PlasticsRubber.Extrusion.Die.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionExtruderNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/Extruder/1.00/Opc.Ua.PlasticsRubber.Extrusion.Extruder.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionExtrusionLineNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/ExtrusionLine/1.00/Opc.Ua.PlasticsRubber.Extrusion.ExtrusionLine.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionFilterNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/Filter/1.00/Opc.Ua.PlasticsRubber.Extrusion.Filter.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionGeneralTypes_v1_0_0_Nodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/GeneralTypes/1.00/Opc.Ua.PlasticsRubber.Extrusion.GeneralTypes.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionGeneralTypes_v1_0_1_Nodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/GeneralTypes/1.01/Opc.Ua.PlasticsRubber.Extrusion.GeneralTypes.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionHaulOffNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/HaulOff/1.00/Opc.Ua.PlasticsRubber.Extrusion.HaulOff.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionMeltPumpNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/MeltPump/1.00/Opc.Ua.PlasticsRubber.Extrusion.MeltPump.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionPelletizerNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion/Pelletizer/1.00/Opc.Ua.PlasticsRubber.Extrusion.Pelletizer.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2CalenderNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/Calender/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Calender.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2CalibratorNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/Calibrator/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Calibrator.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2CorrugatorNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/Corrugator/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Corrugator.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2CutterNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/Cutter/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Cutter.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2DieNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/Die/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Die.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2ExtruderNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/Extruder/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Extruder.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2ExtrusionLineNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/ExtrusionLine/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.ExtrusionLine.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2FilterNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/Filter/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Filter.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2GeneralTypesNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/GeneralTypes/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.GeneralTypes.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2HaulOffNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/HaulOff/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.HaulOff.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2MeltPumpNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/MeltPump/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.MeltPump.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2PelletizerNodeset ){
		LoadWithDependencies( "PlasticsRubber/Extrusion_v2/Pelletizer/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Pelletizer.NodeSet2.xml" );
	}

	//open62541 1.5.9 refuses variables whose values fail its type check.
	TEST_F( UALoadTests, LoadPlasticsRubberGeneralTypes_v1_0_2_Nodeset ){
		LoadWithDependencies( "PlasticsRubber/GeneralTypes/1.02/Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.xml", 3 );
	}

	//open62541 1.5.9 refuses variables whose values fail its type check.
	TEST_F( UALoadTests, LoadPlasticsRubberGeneralTypes_v1_0_3_Nodeset ){
		LoadWithDependencies( "PlasticsRubber/GeneralTypes/1.03/Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.xml", 3 );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberHotRunnerNodeset ){
		LoadWithDependencies( "PlasticsRubber/HotRunner/1.00/Opc.Ua.PlasticsRubber.HotRunner.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberIMM2MESNodeset ){
		LoadWithDependencies( "PlasticsRubber/IMM2MES/1.01/Opc.Ua.PlasticsRubber.IMM2MES.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberLDSNodeset ){
		LoadWithDependencies( "PlasticsRubber/LDS/1.00/Opc.Ua.PlasticsRubber.LDS.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberTCDNodeset ){
		LoadWithDependencies( "PlasticsRubber/TCD/1.01/Opc.Ua.PlasticsRubber.TCD.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPLCopenNodeset ){
		LoadWithDependencies( "PLCopen/Opc.Ua.PLCopen.NodeSet2_V1.02.xml" );
	}


	TEST_F( UALoadTests, LoadPnEmNodeset ){
		LoadWithDependencies( "PNEM/Opc.Ua.PnEm.NodeSet2.xml" );
	}

	//open62541 1.5.9 refuses structure-typed variables it cannot give a default value:  "Could not create a default value".
	TEST_F( UALoadTests, LoadPnRioNodeset ){
		LoadWithDependencies( "PNRIO/Opc.Ua.PnRio.Nodeset2.xml", 3 );
	}

	//open62541 1.5.9 refuses structure-typed variables it cannot give a default value:  "Could not create a default value".
	TEST_F( UALoadTests, LoadPROFINETNodeset ){
		LoadWithDependencies( "PROFINET/Opc.Ua.Pn.NodeSet2.xml", 3 );
	}

	TEST_F( UALoadTests, LoadRoboticsNodeset ){
		LoadWithDependencies( "Robotics/Opc.Ua.Robotics.NodeSet2.xml" );
	}

	//open62541 1.5.9 refuses a structure-typed variable it cannot give a default value:  "Could not create a default value".
	TEST_F( UALoadTests, LoadSafetyNodeset ){
		LoadWithDependencies( "Safety/Opc.Ua.Safety.NodeSet2.xml", 1 );
	}

	TEST_F( UALoadTests, LoadSercosNodeset ){
		LoadWithDependencies( "Sercos/Sercos.NodeSet2.xml" );
	}

	//open62541 1.5.9 refuses the WSAnalogUnitType VariableType, whose value does not match its DataType, and its WSTagNumber property.
	TEST_F( UALoadTests, LoadWeihenstephanNodeset ){
		LoadWithDependencies( "Weihenstephan/Opc.Ua.Weihenstephan.NodeSet2.xml", 2 );
	}

	TEST_F( UALoadTests, LoadWoodworkingEumaboisNodeset ){
		LoadWithDependencies( "Woodworking/Opc.Ua.Eumabois.Nodeset2.xml" );
	}

	TEST_F( UALoadTests, LoadWoodworkingNodeset ){
		LoadWithDependencies( "Woodworking/Opc.Ua.Woodworking.NodeSet2.xml" );
	}

}
