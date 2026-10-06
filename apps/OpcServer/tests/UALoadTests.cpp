#include <jde/fwk/settings.h>
#include "../src/globals.h"
#include "../src/UAServer.h"
#include <jde/opc/uatypes/BrowsePath.h>
#define let const auto

namespace Jde::Opc::Server::Tests{
	struct UALoadTests : ::testing::Test{
	protected:
		Ω SetUpTestCase()ι->void{}
		Ω TearDownTestCase()ι->void{}
		α SetUp()ε->void{
			Server::Initialize( GetSchemaPtr() );
		}
		Ω Path()ι->fs::path{ return *Settings::FindPath("/testing/UANodeSets"); }
	};

	TEST_F( UALoadTests, LoadMyKitchen ){
		GetUAServer().Load( Path()/"CommercialKitchenEquipment/Opc.Ua.CommercialKitchenEquipment.NodeSet2.xml" );
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
	//this write was BadTypeMismatch, which is what the SPA hit changing ExampleStacklight's DeviceHealth.
	TEST_F( UALoadTests, WriteNodesetEnum ){
		auto& ua = GetUAServer();
		ua.Load( Path()/"DI/Opc.Ua.Di.NodeSet2.xml" );
		ua.Load( Path()/"IA/Opc.Ua.IA.NodeSet2.xml" );
		ua.Load( Path()/"IA/Opc.Ua.IA.NodeSet2.examples.xml" );
		ua.PublishDataTypes();
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

	TEST_F( UALoadTests, AdditiveManufacturing ){
		GetUAServer().Load( Path()/"AdditiveManufacturing/Opc.Ua.AdditiveManufacturing.Nodeset2.xml" );
	}

	TEST_F( UALoadTests, Server_loadADINodeset ){
		GetUAServer().Load( Path()/"ADI/Opc.Ua.Adi.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadAMBNodeset ){
		GetUAServer().Load( Path()/"AMB/Opc.Ua.AMB.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadAMLBaseTypesNodeset ){
		GetUAServer().Load( Path()/"AML/Opc.Ua.AMLBaseTypes.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadAutoIDNodeset ){
		GetUAServer().Load( Path()/"AutoID/Opc.Ua.AutoID.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadBACnetNodeset ){
		GetUAServer().Load( Path()/"BACnet/Opc.Ua.BACnet.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadCASNodeset ){
		GetUAServer().Load( Path()/"CAS/Opc.Ua.CAS.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadCommercialKitchenEquipmentNodeset ){
		GetUAServer().Load( Path()/"CommercialKitchenEquipment/Opc.Ua.CommercialKitchenEquipment.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadCSPPlusForMachineNodeset ){
		GetUAServer().Load( Path()/"CSPPlusForMachine/Opc.Ua.CSPPlusForMachine.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadDEXPINodeset ){
		GetUAServer().Load( Path()/"DEXPI/Opc.Ua.DEXPI.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadDINodeset ){
		GetUAServer().Load( Path()/"DI/Opc.Ua.Di.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadDotNetNodeset ){
		GetUAServer().Load( Path()/"DotNet/Opc.Ua.NodeSet.xml" );
	}

	TEST_F( UALoadTests, LoadFDI5Nodeset ){
		GetUAServer().Load( Path()/"FDI/Opc.Ua.Fdi5.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadFDI7Nodeset ){
		GetUAServer().Load( Path()/"FDI/Opc.Ua.Fdi7.NodeSet2.xml" );
	}


	TEST_F( UALoadTests, LoadFDTNodeset ){
		GetUAServer().Load( Path()/"FDT/Opc.Ua.FDT.NodeSet.xml" );
	}

	TEST_F( UALoadTests, LoadGDSNodeset ){
		GetUAServer().Load( Path()/"GDS/Opc.Ua.Gds.NodeSet2.xml" );
	}

	//The nodeset loader refuses it:  "Infinite loop in the references" (open62541-nodeset-loader src/Nodeset.c), open62541 1.5.9.
	// TEST_F( UALoadTests, LoadServer_loadGlassNodeset ){
	// 	GetUAServer().Load( Path()/"Glass/Flat/Opc.Ua.Glass.NodeSet2.xml" );
	// }


	TEST_F( UALoadTests, LoadI4AASNodeset ){
		GetUAServer().Load( Path()/"I4AAS/Opc.Ua.I4AAS.NodeSet2.xml" );
	}


	TEST_F( UALoadTests, LoadIANodeset ){
		GetUAServer().Load( Path()/"IA/Opc.Ua.IA.NodeSet2.xml" );
	}

/*
	TEST_F( UALoadTests, LoadIAExamplesNodeset ){
		GetUAServer().Load( Path()/"IA/Opc.Ua.IA.NodeSet2.examples.xml" );
	}
*/

	TEST_F( UALoadTests, LoadIOLinkIODDNodeset ){
		GetUAServer().Load( Path()/"IOLink/Opc.Ua.IOLinkIODD.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadIOLinkNodeset ){
		GetUAServer().Load( Path()/"IOLink/Opc.Ua.IOLink.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadISA95Nodeset ){
		GetUAServer().Load( Path()/"ISA-95/Opc.ISA95.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMachineryNodeset ){
		GetUAServer().Load( Path()/"Machinery/Opc.Ua.Machinery.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMachineryExamplesNodeset ){
		GetUAServer().Load( Path()/"Machinery/Opc.Ua.Machinery.Examples.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMachineToolNodeset ){
		GetUAServer().Load( Path()/"MachineTool/Opc.Ua.MachineTool.NodeSet2.xml" );
	}


	TEST_F( UALoadTests, LoadMDISNodeset ){
		GetUAServer().Load( Path()/"MDIS/Opc.MDIS.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMiningDevelopmentSupportGeneralNodeset ){
		GetUAServer().Load( Path()/"Mining/DevelopmentSupport/General/1.0.0/Opc.Ua.Mining.DevelopmentSupport.General.NodeSet2.xml" );
	}


	TEST_F( UALoadTests, LoadMiningExtractionGeneralNodeset ){
		GetUAServer().Load( Path()/"Mining/Extraction/General/1.0.0/Opc.Ua.Mining.Extraction.General.NodeSet2.xml" );
	}


	TEST_F( UALoadTests, LoadMiningMineralProcessingGeneralNodeset ){
		GetUAServer().Load( Path()/"Mining/MineralProcessing/General/1.0.0/Opc.Ua.Mining.MineralProcessing.General.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMiningMonitoringSupervisionServicesGeneralNodeset ){
		GetUAServer().Load( Path()/"Mining/MonitoringSupervisionServices/General/1.0.0/Opc.Ua.Mining.MonitoringSupervisionServices.General.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadMTConnectNodeset ){
		GetUAServer().Load( Path()/"MTConnect/Opc.Ua.MTConnect.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadOPENSCSNodeset ){
		GetUAServer().Load( Path()/"OpenSCS/Opc.Ua.OPENSCS.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPackMLNodeset ){
		GetUAServer().Load( Path()/"PackML/Opc.Ua.PackML.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionCalenderNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/Calender/1.00/Opc.Ua.PlasticsRubber.Extrusion.Calender.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionCalibratorNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/Calibrator/1.00/Opc.Ua.PlasticsRubber.Extrusion.Calibrator.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionCorrugatorNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/Corrugator/1.00/Opc.Ua.PlasticsRubber.Extrusion.Corrugator.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionCutterNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/Cutter/1.00/Opc.Ua.PlasticsRubber.Extrusion.Cutter.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionDieNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/Die/1.00/Opc.Ua.PlasticsRubber.Extrusion.Die.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionExtruderNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/Extruder/1.00/Opc.Ua.PlasticsRubber.Extrusion.Extruder.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionExtrusionLineNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/ExtrusionLine/1.00/Opc.Ua.PlasticsRubber.Extrusion.ExtrusionLine.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionFilterNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/Filter/1.00/Opc.Ua.PlasticsRubber.Extrusion.Filter.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionGeneralTypes_v1_0_0_Nodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/GeneralTypes/1.00/Opc.Ua.PlasticsRubber.Extrusion.GeneralTypes.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionGeneralTypes_v1_0_1_Nodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/GeneralTypes/1.01/Opc.Ua.PlasticsRubber.Extrusion.GeneralTypes.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionHaulOffNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/HaulOff/1.00/Opc.Ua.PlasticsRubber.Extrusion.HaulOff.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionMeltPumpNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/MeltPump/1.00/Opc.Ua.PlasticsRubber.Extrusion.MeltPump.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionPelletizerNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion/Pelletizer/1.00/Opc.Ua.PlasticsRubber.Extrusion.Pelletizer.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2CalenderNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/Calender/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Calender.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2CalibratorNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/Calibrator/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Calibrator.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2CorrugatorNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/Corrugator/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Corrugator.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2CutterNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/Cutter/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Cutter.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2DieNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/Die/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Die.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2ExtruderNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/Extruder/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Extruder.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2ExtrusionLineNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/ExtrusionLine/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.ExtrusionLine.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2FilterNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/Filter/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Filter.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2GeneralTypesNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/GeneralTypes/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.GeneralTypes.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2HaulOffNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/HaulOff/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.HaulOff.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2MeltPumpNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/MeltPump/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.MeltPump.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberExtrusionv2PelletizerNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/Extrusion_v2/Pelletizer/2.00/Opc.Ua.PlasticsRubber.Extrusion_v2.Pelletizer.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberGeneralTypes_v1_0_2_Nodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/GeneralTypes/1.02/Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberGeneralTypes_v1_0_3_Nodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/GeneralTypes/1.03/Opc.Ua.PlasticsRubber.GeneralTypes.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberHotRunnerNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/HotRunner/1.00/Opc.Ua.PlasticsRubber.HotRunner.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberIMM2MESNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/IMM2MES/1.01/Opc.Ua.PlasticsRubber.IMM2MES.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberLDSNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/LDS/1.00/Opc.Ua.PlasticsRubber.LDS.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPlasticsRubberTCDNodeset ){
		GetUAServer().Load( Path()/"PlasticsRubber/TCD/1.01/Opc.Ua.PlasticsRubber.TCD.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPLCopenNodeset ){
		GetUAServer().Load( Path()/"PLCopen/Opc.Ua.PLCopen.NodeSet2_V1.02.xml" );
	}


	TEST_F( UALoadTests, LoadPnEmNodeset ){
		GetUAServer().Load( Path()/"PNEM/Opc.Ua.PnEm.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadPnRioNodeset ){
		GetUAServer().Load( Path()/"PNRIO/Opc.Ua.PnRio.Nodeset2.xml" );
	}

	TEST_F( UALoadTests, LoadPROFINETNodeset ){
		GetUAServer().Load( Path()/"PROFINET/Opc.Ua.Pn.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadRoboticsNodeset ){
		GetUAServer().Load( Path()/"Robotics/Opc.Ua.Robotics.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadSafetyNodeset ){
		GetUAServer().Load( Path()/"Safety/Opc.Ua.Safety.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadSercosNodeset ){
		GetUAServer().Load( Path()/"Sercos/Sercos.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadWeihenstephanNodeset ){
		GetUAServer().Load( Path()/"Weihenstephan/Opc.Ua.Weihenstephan.NodeSet2.xml" );
	}

	TEST_F( UALoadTests, LoadWoodworkingEumaboisNodeset ){
		GetUAServer().Load( Path()/"Woodworking/Opc.Ua.Eumabois.Nodeset2.xml" );
	}

	TEST_F( UALoadTests, LoadWoodworkingNodeset ){
		GetUAServer().Load( Path()/"Woodworking/Opc.Ua.Woodworking.NodeSet2.xml" );
	}

}
