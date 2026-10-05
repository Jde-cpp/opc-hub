#pragma once
#include <boost/uuid/random_generator.hpp>
#include <jde/fwk/str.h>
#include <jde/historian/Historian.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/Value.h>
#include "ManualClock.h"

//The two shapes the library serves from its first commit, so building for OpcServer first doesn't bake in its shape.
namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	Ξ ticks( TimePoint t )ι->UA_DateTime{ return UADateTime{ t }.UA(); }//t as UA's 100 ns ticks since 1601.
	struct HostFixture : ::testing::Test{
		static ExNodeId Node( sv id, sv uri="urn:jde:pumps" )ι{
			return ExNodeId{ flat_map<string,string>{ {"nsu", string{uri}}, {"s", string{id}} } };
		}
		static Value Reading( double v, optional<TimePoint> source={}, optional<TimePoint> server={} )ι{
			UA_DataValue dv{};
			UA_Variant_setScalarCopy( &dv.value, &v, &UA_TYPES[UA_TYPES_DOUBLE] );
			dv.hasValue = true;
			if( source ){
				dv.sourceTimestamp = UADateTime{ *source }.UA();
				dv.hasSourceTimestamp = true;
			}
			if( server ){
				dv.serverTimestamp = UADateTime{ *server }.UA();
				dv.hasServerTimestamp = true;
			}
			return Value{ move(dv) };
		}
		//A String value, with no timestamp for Enqueue to stamp its server time.
		static Value Text( sv text, optional<TimePoint> source={} )ι{
			UA_DataValue dv{};
			const UA_String s{ text.size(), (UA_Byte*)text.data() };
			UA_Variant_setScalarCopy( &dv.value, &s, &UA_TYPES[UA_TYPES_STRING] );
			dv.hasValue = true;
			if( source ){
				dv.sourceTimestamp = UADateTime{ *source }.UA();
				dv.hasSourceTimestamp = true;
			}
			return Value{ move(dv) };
		}
		//A flush of what group buffered, waited for:  false when it didn't write all it took.
		static bool Flush( Group& group )ε{ return BlockAny( group.Flush() ); }
		//Waits out the flushes the clock started, which write on the executor's threads.
		static bool Settle( Group& group )ε{ return BlockAny( group.Settled() ); }
		Ŧ Records()Ι->vector<T>{
			vector<T> y;
			for( auto& r : _group->Buffer() ){
				if( auto p = std::get_if<T>(&r); p )
					y.push_back( move(*p) );
			}
			return y;
		}

		//The host's process ending and starting again on its hist.path:  the library is destroyed, which writes what each
		//group buffered, and made again.  The host then adds its groups, with their members at start.
		α Restart( optional<Settings> config={} )ε->void{
			_group.reset();
			Library.reset();
			Library = mu<Historian>( config ? move(*config) : Config(Delay), Time );
		}
		//A hist.path of the test's own, beside the logs.
		static fs::path Path()ε{
			const auto test = ::testing::UnitTest::GetInstance()->current_test_info();
			return fs::current_path()/"hist-tests"/Ƒ( "{}.{}", test->test_suite_name(), test->name() );
		}
		static Settings Config( Duration delay )ε{
			Settings y{ Path() };
			y.Delay = delay;
			return y;
		}
		//Leaves what it can't remove, rather than ending the binary from a destructor.
		~HostFixture(){
			_group.reset();
			Library.reset();
			try{
				const auto path = Path();
				std::error_code ec;
				fs::remove_all( path, ec );
				fs::remove( path.parent_path(), ec );//once the last test's is gone.
			}
			catch( const std::exception& )
			{}
		}

		//`delay` is a year unless a fixture sets its own, so that a test moving the clock still reads what its group
		//buffered.
		const Duration Delay;
		sp<ManualClock> Time{ ms<ManualClock>(sys_days{2026y/March/7}+17h) };
		up<Historian> Library{ New(Config(Delay), Time) };
	protected:
		HostFixture( Duration delay=days{365} )ε:Delay{ delay }{}
		//After clearing what a run that crashed left behind.
		static up<Historian> New( Settings config, sp<IClock> clock )ε{
			fs::remove_all( config.Path );
			return mu<Historian>( move(config), move(clock) );
		}
		sp<Group> _group;//the one Records reads.
	};

	//OpcServer:  one group, `server`, whose node_indexes the historian issues itself, since there is no hist_group_nodes
	//row to take one from.  Each node's thresholds come from its HA Configuration and its membership from the nodesets at
	//start, so no change carries a writer.  Values arrive as they are written, under open62541's service lock, and there
	//is no subscription to break.
	struct ServerHost : HostFixture{
		ServerHost( Duration delay=days{365} )ε:HostFixture{ delay }{ _group = Server; }
		//The nodeset loader, for a variable marked Historizing.
		α Historize( sv id, Thresholds config={} )ε->NodeIndex{ return Server->Add( {Node(id), move(config)} ); }
		//open62541's setValue:  the writer's source timestamp, stamped now when it sent none, and no server timestamp.
		α SetValue( NodeIndex index, double v )ι->bool{ return Server->Enqueue( index, Reading(v, Time->Now()) ); }

		sp<Group> Server{ Library->AddGroup({.Name="server", .Indexes=EIndexes::Issued}) };
	};

	//The gateway:  a group per hist_groups row, named by its guid, whose node_indexes are hist_group_nodes row ids - one
	//autoincrement sequence across every group.  It resolves each node's thresholds itself, and every membership change
	//is a QL mutation with a caller.  Values arrive on each connection's strand, and a connection can break.
	struct GatewayHost : HostFixture{
		GatewayHost( Duration delay=days{365} )ε:HostFixture{ delay }{}
		//The nullable threshold columns of a hist_group_nodes row or a hist_template_nodes member.
		struct Columns{ optional<double> ExceptionDeviation; optional<Duration> MaxTimeInterval; };
		//row, then template member, then group.
		static Thresholds Resolve( const Columns& row, const Columns& member, Duration groupMaxTimeInterval )ι{
			return {
				.ExceptionDeviation = row.ExceptionDeviation ? row.ExceptionDeviation : member.ExceptionDeviation,
				.MaxTimeInterval = row.MaxTimeInterval.value_or( member.MaxTimeInterval.value_or(groupMaxTimeInterval) )
			};
		}
		α AddGroup()ε->sp<Group>{
			auto group = Library->AddGroup( {.Name=Jde::ToString(_guids()), .Indexes=EIndexes::Host, .PublishingInterval=500ms} );
			if( !_group )
				_group = group;
			return group;
		}
		//The hist_group_nodes insert:  a new row id, and the caller as the writer.
		α Join( Group& group, sv id, Thresholds config={} )ε->NodeIndex{ return group.Add( {Node(id), move(config), ++_rowId}, Admin ); }
		//IDataChange::SendDataChange, from the connection's strand:  both timestamps, which the collector's items ask for.
		α DataChange( Group& group, NodeIndex index, double v, TimePoint source )ι->bool{ return group.Enqueue( index, Reading(v, source, source+5ms) ); }

		Writer Admin{ {{7}}, "admin" };
	private:
		NodeIndex _rowId{ 100 };
		boost::uuids::random_generator _guids;
	};
}