#pragma once
//`Result` and `Counters` of an Open Protocol row as the driver's suites read them (DriverResultTests, DriverBackfillTests):  a
//rundown with every field, what each variable is to hold of it, the row's variables as a web session's client reads them, and a
//group over them.
#include "EndpointFixture.h"
#include "DriverRows.h"
#include "../../src/async/ReadValueAwait.h"
#include "../../src/drivers/openProtocol/TighteningResult.h"

namespace Jde::Opc::Gateway::Tests{
	struct DriverResultFixture : EndpointFixture{
	protected:
		using LocalTime = OpenProtocol::LocalTime;
		using Mid0061 = OpenProtocol::Mid0061;
		static constexpr sv NewYork{ R"({"timeZone":"America/New_York"})" };//the row's zone, the suite's proto log's.
		static constexpr StatusCode Waiting{ UA_STATUSCODE_BADWAITINGFORINITIALDATA }, NotSupported{ UA_STATUSCODE_BADNOTSUPPORTED };
		//A rundown with every field of MID 0061's seven revisions, each a value of its own:  the controller sends what the
		//subscription's revision carries.  Its parameter set last changed in January, under the zone's other offset.
		Ω Rundown( uint32_t id, optional<LocalTime> at )ι->Mid0061{
			return { .CellId=1, .ChannelId=2, .ControllerName="Line 3 station 7", .Vin="VIN0123456789", .JobId=5, .Pset=3, .Strategy=2, .StrategyOptions=3,
				.BatchSize=8, .BatchCounter=6, .Status=1, .BatchStatus=3, .TorqueStatus=1, .AngleStatus=2, .RundownAngleStatus=0, .CurrentMonitoringStatus=1,
				.SelfTapStatus=2, .PrevailTorqueMonitoringStatus=0, .PrevailTorqueCompensateStatus=1, .TighteningErrorStatus=4, .TorqueMin=8.4, .TorqueMax=14,
				.TorqueFinalTarget=12, .Torque=12.23, .AngleMin=10, .AngleMax=9999, .AngleFinalTarget=360, .Angle=355, .RundownAngleMin=20, .RundownAngleMax=720,
				.RundownAngle=405, .CurrentMonitoringMin=10, .CurrentMonitoringMax=120, .CurrentMonitoringValue=87, .SelfTapMin=1.5, .SelfTapMax=4.25,
				.SelfTapTorque=2.75, .PrevailTorqueMonitoringMin=0.5, .PrevailTorqueMonitoringMax=3.5, .PrevailTorque=1.25, .TighteningId=id, .JobSequenceNumber=7,
				.SyncTighteningId=9, .ToolSerialNumber="T-000123", .Time=at, .PsetChangeTime=LocalTime{ local_days{year{2026}/1/15}+8h }, .PsetName="M8 bracket",
				.TorqueUnit=1, .ResultType=1, .IdentifierPart2="part 2", .IdentifierPart3="part 3", .IdentifierPart4="part 4", .CustomerErrorCode="E042",
				.PrevailTorqueCompensateValue=0.75, .TighteningErrorStatus2=16, .CompensatedAngle=354.5, .AngleDecimal=355.25 };
		}
		static constexpr TimePoint PsetChanged{ sys_days{year{2026}/1/15}+13h };//Rundown's, in New York's winter.
		//`Result`'s variables but its times and its id, in the order the driver writes them:  the first revision of MID 0061 that
		//carries each (Tables 108 to 114), its type, and what Rundown gives it.
		struct Field final{ sv Name; uint8_t Since; uint16_t Type; jvalue Value; };
		Ω Fields()ι->const vector<Field>&{
			static const vector<Field> fields{
				{"Vin", 1, UA_TYPES_STRING, "VIN0123456789"}, {"JobId", 1, UA_TYPES_UINT16, 5}, {"Pset", 1, UA_TYPES_UINT16, 3}, {"Strategy", 2, UA_TYPES_BYTE, 2},
				{"StrategyOptions", 2, UA_TYPES_UINT32, 3}, {"BatchSize", 1, UA_TYPES_UINT16, 8}, {"BatchCounter", 1, UA_TYPES_UINT16, 6}, {"Status", 1, UA_TYPES_BOOLEAN, true},
				{"BatchStatus", 1, UA_TYPES_BYTE, 3}, {"TorqueStatus", 1, UA_TYPES_BYTE, 1}, {"AngleStatus", 1, UA_TYPES_BYTE, 2}, {"RundownAngleStatus", 2, UA_TYPES_BYTE, 0},
				{"CurrentMonitoringStatus", 2, UA_TYPES_BYTE, 1}, {"SelfTapStatus", 2, UA_TYPES_BYTE, 2}, {"PrevailTorqueMonitoringStatus", 2, UA_TYPES_BYTE, 0},
				{"PrevailTorqueCompensateStatus", 2, UA_TYPES_BYTE, 1}, {"TighteningErrorStatus", 2, UA_TYPES_UINT32, 4}, {"TorqueMin", 1, UA_TYPES_DOUBLE, 8.4},
				{"TorqueMax", 1, UA_TYPES_DOUBLE, 14.0}, {"TorqueFinalTarget", 1, UA_TYPES_DOUBLE, 12.0}, {"Torque", 1, UA_TYPES_DOUBLE, 12.23}, {"AngleMin", 1, UA_TYPES_DOUBLE, 10.0},
				{"AngleMax", 1, UA_TYPES_DOUBLE, 9999.0}, {"AngleFinalTarget", 1, UA_TYPES_DOUBLE, 360.0}, {"Angle", 1, UA_TYPES_DOUBLE, 355.0}, {"RundownAngleMin", 2, UA_TYPES_DOUBLE, 20.0},
				{"RundownAngleMax", 2, UA_TYPES_DOUBLE, 720.0}, {"RundownAngle", 2, UA_TYPES_DOUBLE, 405.0}, {"CurrentMonitoringMin", 2, UA_TYPES_UINT16, 10},
				{"CurrentMonitoringMax", 2, UA_TYPES_UINT16, 120}, {"CurrentMonitoringValue", 2, UA_TYPES_UINT16, 87}, {"SelfTapMin", 2, UA_TYPES_DOUBLE, 1.5},
				{"SelfTapMax", 2, UA_TYPES_DOUBLE, 4.25}, {"SelfTapTorque", 2, UA_TYPES_DOUBLE, 2.75}, {"PrevailTorqueMonitoringMin", 2, UA_TYPES_DOUBLE, 0.5},
				{"PrevailTorqueMonitoringMax", 2, UA_TYPES_DOUBLE, 3.5}, {"PrevailTorque", 2, UA_TYPES_DOUBLE, 1.25}, {"JobSequenceNumber", 2, UA_TYPES_UINT16, 7},
				{"SyncTighteningId", 2, UA_TYPES_UINT16, 9}, {"ToolSerialNumber", 2, UA_TYPES_STRING, "T-000123"}, {"PsetName", 3, UA_TYPES_STRING, "M8 bracket"},
				{"TorqueUnit", 3, UA_TYPES_BYTE, 1}, {"ResultType", 3, UA_TYPES_BYTE, 1}, {"IdentifierPart2", 4, UA_TYPES_STRING, "part 2"}, {"IdentifierPart3", 4, UA_TYPES_STRING, "part 3"},
				{"IdentifierPart4", 4, UA_TYPES_STRING, "part 4"}, {"CustomerErrorCode", 5, UA_TYPES_STRING, "E042"}, {"PrevailTorqueCompensateValue", 6, UA_TYPES_DOUBLE, 0.75},
				{"TighteningErrorStatus2", 6, UA_TYPES_UINT32, 16}, {"CompensatedAngle", 7, UA_TYPES_DOUBLE, 354.5}
			};
			return fields;
		}
		Ω Path( sv name )ι->string{ return Ƒ( "Result.{}", name ); }
		//Every variable of `Result` and `Counters` as a web session's client reads them, with their timestamps.
		Ω Variables( const DriverRow& row )ε->flat_map<NodeId,Value>{
			flat_set<NodeId> nodes;
			for( let& field : Fields() )
				nodes.emplace( DriverNode(Path(field.Name)) );
			for( let path : {"Result.Time", "Result.PsetChangeTime", "Result.TighteningId", "Counters.Total", "Counters.Ok", "Counters.Nok"} )
				nodes.emplace( DriverNode(path) );
			return BlockAwait( ReadValueAwait{move(nodes), BlockAwait(UAClient::GetClient(row.Slug, SessionCredential())), UA_TIMESTAMPSTORETURN_BOTH} );
		}
		Ω Of( const flat_map<NodeId,Value>& read, sv path )ε->const Value&{ return read.at( DriverNode(path) ); }
		Ω Is( const Value& v, uint16_t type )ι->bool{ return v.hasValue && UA_Variant_hasScalarType( &v.value, &UA_TYPES[type] ); }
		Ω Time( const Value& v )ι->optional<TimePoint>{ return Is( v, UA_TYPES_DATETIME ) ? optional<TimePoint>{ UADateTime{v.Get<UA_DateTime>(0)}.Time() } : nullopt; }
		Ω ResultId( const flat_map<NodeId,Value>& read )ι->optional<uint>{ let& v = Of( read, "Result.TighteningId" ); return !v.status && Is( v, UA_TYPES_UINT32 ) ? optional<uint>{ v.Get<UA_UInt32>(0) } : nullopt; }
		Ω Counters( const flat_map<NodeId,Value>& read )ι->array<optional<uint>,3>{ return { OpCount(Of(read, "Counters.Total")), OpCount(Of(read, "Counters.Ok")), OpCount(Of(read, "Counters.Nok")) }; }
		//The subscription accepted, or a result read, at `revision`:  `_Op.Revisions` names it under the MID it pushes.
		Ω WaitRevision( const DriverRow& row, uint16_t revision )ι->bool{ return WaitOp( row, "Revisions", [text=Ƒ("0061:{}", revision)]( let& v ){ return OpText( v ).contains( text ); } ); }
		//`_Op.Results`, a result's last write:  the result is whole.
		Ω WaitResults( const DriverRow& row, uint count )ι->bool{ return WaitOp( row, "Results", [=]( let& v ){ return OpCount( v )==count; } ); }
		//A node of the row's server as a group's member and a `history` read spell it, by its namespace's URI.
		Ω Member( const DriverRow& row, sv path )ι->jvalue{ return jobject{ {"nsu", DriverHost::NamespaceUri(row.Slug, ServerCnnctn::KindOpenProtocol)}, {"s", path} }; }
		//A value as `history` answers it:  one with a status that is not Good is {v, sc}.
		Ω ValueOf( const Row& row )ι->jvalue{ return row.Value.is_object() && row.Value.as_object().contains( "v" ) ? row.Value.as_object().at( "v" ) : row.Value; }

		//A group over the row, a member per path, subscribed and each member's first value stored:  purged as it goes, and the
		//collector's items gone before the row's purge closes their client.
		struct ResultGroup final{
			ResultGroup( const DriverRow& row, sv slug, vector<std::pair<sv,jobject>> members )ε:
				Connection{ row.Slug },
				Id{ HistGroupFixture::Id(Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", Ƒ("Results {}", slug)}, {"slug", slug}, {"connection", row.Id}})) }
			{
				try{
					for( auto& [path, thresholds] : members )
						Members.push_back( AddNode(Id, Member(row, path), move(thresholds)) );
					Host = DriverResultFixture::Host( Id );
					WaitSubscribed( Host, Members.size() );
					for( let member : Members )
						WaitStored( Host, member, Joined, 1 );
				}
				catch( ... ){
					Purge();
					throw;
				}
			}
			ResultGroup( const ResultGroup& )=delete;
			~ResultGroup(){ Purge(); }
			const ServerCnnctnNK Connection;
			const uint32 Id;
			vector<uint32> Members;//in the order given.
			sp<HistGroupHost> Host;
		private:
			α Purge()ι->void{
				Host = nullptr;
				try{
					Mutate( "purgeHistGroup( id:$id )", {{"id", Id}} );
					let dropped = WaitUntil( [&]{ let collector = UAClient::Find( Connection, HistHost::Instance().Collector(Connection) ); return !collector || !collector->TryMonitoredNodes() || !collector->TryMonitoredNodes()->Count(); } );
					EXPECT_TRUE( dropped ) << "the collector's items outlived group " << Id;
				}
				catch( const std::exception& e ){
					ADD_FAILURE() << "purgeHistGroup(" << Id << "): " << e.what();
				}
			}
		};
		//The group's records of a member from `from` on, as the library holds them, each as the UA value it was.
		Ω Records( const ResultGroup& group, uint member, TimePoint from )ε->vector<Value>{
			vector<Value> y;
			for( let& stored : Stored(group.Host, group.Members.at(member), from) )
				y.push_back( Hist::ToUA(stored.Value) );
			return y;
		}
	};
}