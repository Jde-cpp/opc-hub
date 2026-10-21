#pragma once
//The fixture HistCollectorTests was built on (#224), shared with the subscription suite, HistSubscribeTests (#225).
#include <thread>
#include <jde/fwk/utils/Stopwatch.h>
#include <jde/ql/LocalQL.h>
#include <jde/access/Authorize.h>
#include "HistReadFixture.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/hist/HistHost.h"
#include "../../src/ql/GatewayQL.h"
#include "../../src/UAClient.h"
#ifndef let
#define let const auto
#endif

namespace Jde::Opc::Gateway::Tests{
	using namespace std::chrono;
	//A gateway group over the embedded OpcServer's historizing pump nodes, as HistCollectorTests and HistSubscribeTests drive it:
	//made through the group mutations, fed by the server's writes, read through the socket's QL, edited under an enforced grant.
	struct HistGroupFixture : HistReadFixture{
	protected:
		Ω SetUpTestSuite()ε->void{
			HistReadFixture::SetUpTestSuite();
			_connection = SelectServerCnnctn( OpcServerSlug )->Id;
		}
		α SetUp()->void override{
			HistReadFixture::SetUp();
			if( IsSkipped() )
				return;
			if( !HistHost::Instance().Enabled() )
				GTEST_SKIP() << "The gateway keeps no history:  /gateway/hist.";
		}
		Ω User()ι->UserPK{ return AppClient()->UserPK(); }
		//A mutation's result:  the object its result request names, or, with none, the empty list the engine answers.
		Ω Mutate( sv ql, jobject vars={} )ε->jvalue{ return BlockTAwait<jvalue>( QL::QLAwait<jvalue>{string{ql}, move(vars), User(), QLPtr()} ); }
		Ω Id( const jvalue& v )ε->uint32{ return Json::AsNumber<uint32>( Json::AsObject(v), "id" ); }
		Ω Ms( TimePoint t )ι->TimePoint{ return floor<milliseconds>( t ); }//what a UA DateTime holds exactly.
		//Before any member's first value:  an item's first is the node's value now, stored as the first after its join.
		inline static const TimePoint Joined{ Clock::now()-24h*365 };
		//Where a test's writes start:  just ahead, once each item's first value is in, so a first value the server stamped as it
		//sampled it, for a node whose value came with no source time, falls before the range the test compares.
		Ω Since()ι->TimePoint{ return Ms( Clock::now() )+10ms; }
		//An ad hoc group, its nodes added one by one.
		Ω CreateGroup( sv slug )ε->uint32{
			return Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", Ƒ("Collector {}", slug)}, {"slug", slug}, {"connection", _connection}}) );
		}
		Ω AddNode( uint32 group, UA_UInt32 node, jobject thresholds={} )ε->uint32{ return AddNode( group, Ex(node), move(thresholds) ); }
		Ω AddNode( uint32 group, const jvalue& node, jobject thresholds={} )ε->uint32{
			string ql{ "createHistGroupNode( histGroupId:$group, node:$node" };
			jobject vars{ {"group", group}, {"node", serialize(node)} };
			for( let& [key,value] : thresholds ){
				ql += Ƒ( ", {}:${}", key, key );
				vars[key] = value;
			}
			return Id( Mutate(ql+" ){ id }", move(vars)) );
		}
		//The group goes, and its items with it, a second on (DeleteMonitoring):  waited for, so the next test counts none of them.
		Ω Purge( uint32 group, uint itemsBefore )ι->void{
			try{
				Mutate( "purgeHistGroup( id:$id )", {{"id", group}} );
				WaitItems( itemsBefore );
			}
			catch( const std::exception& e ){
				WARNT( ELogTags::Test, "purgeHistGroup({}):  {}", group, e.what() );
			}
		}
		Ω Items()ι->uint{ auto c = Collector(); return c && c->TryMonitoredNodes() ? c->TryMonitoredNodes()->Count() : 0; }
		Ω WaitItems( uint count )ε->void{
			Stopwatch sw;
			while( Items()!=count )
				sw.CheckTimeout( 15s, 10ms );
		}
		Ω Host( uint32 group )ε->sp<HistGroupHost>{
			auto host = HistHost::Instance().Find( group );
			THROW_IF( !host, "Group {} isn't live.", group );
			return host;
		}
		Ω Collector()ι->sp<UAClient>{ return UAClient::Find( OpcServerSlug, HistHost::Instance().Collector(OpcServerSlug) ); }
		Ω WaitSubscribed( const sp<HistGroupHost>& host, uint count )ε->void{
			Stopwatch sw;
			while( host->Subscribed()!=count )
				sw.CheckTimeout( 15s, 10ms );
		}
		//Until the server has described a member's node (HistGroupHost::Describe), the group takes no band for it.
		Ω WaitDescribed( const sp<HistGroupHost>& host, uint32 index )ε->HistDescription{
			Stopwatch sw;
			for( ;; ){
				if( auto d = host->Description(index); d && d->Enumeration && d->Ranged )
					return *d;
				sw.CheckTimeout( 15s, 10ms );
			}
		}
		//The group's records of a node from `from` on, read through the library, buffer and files both.
		Ω Stored( const sp<HistGroupHost>& host, uint32 index, TimePoint from )ε->vector<Opc::Hist::ReadValue>{
			return host->Group()->Read( {.Nodes={index}, .Start=UADateTime{from}.UA(), .Limit=10'000} ).Values;
		}
		Ω WaitStored( const sp<HistGroupHost>& host, uint32 index, TimePoint from, uint count )ε->vector<Opc::Hist::ReadValue>{
			vector<Opc::Hist::ReadValue> stored;
			if( WaitUntil([&]{ stored = Stored( host, index, from ); return stored.size()>=count; }, 15s) )
				return stored;
			string values;
			for( let& v : stored )
				values += Ƒ( " {}@{}", v.Value.has_value() ? serialize(Hist::ToUA(v.Value).ToJson()) : "none", v.Value.source_ts() );
			THROW( "Member {} stored {} of {} values from {}:{}", index, stored.size(), count, ToIsoString(from), values );
		}
		Ω WriteRpm( double value, TimePoint source )ε->void{ Write( RpmManual, &value, UA_TYPES[UA_TYPES_DOUBLE], source ); }
		Ω WriteStatus( bool value, TimePoint source )ε->void{ UA_Boolean b = value; Write( Status1, &b, UA_TYPES[UA_TYPES_BOOLEAN], source ); }
		Ω Current( UA_UInt32 id )ι->Value{ return Opc::ReadValue( Server::GetUAServer(), Node(id) ); }
		//A first write's value, off RpmManual's now by more than any band.
		Ω BaseRpm()ι->double{ return Current( RpmManual ).AsNumber<double>()+1000; }
		//A test's group of one member, subscribed, its first value stored:  purged as it goes, its items waited for, as
		//`let [group, rpm, host, itemsBefore] = OneNodeGroup{ "slug", RpmManual };`.
		struct OneNodeGroup final{
			OneNodeGroup( sv slug, UA_UInt32 node, jobject thresholds={} )ε{
				ItemsBefore = Items();
				Id = CreateGroup( slug );
				try{
					Index = AddNode( Id, node, move(thresholds) );
					Host = HistGroupFixture::Host( Id );
					WaitSubscribed( Host, 1 );
					WaitStored( Host, Index, Joined, 1 );
				}
				catch( ... ){
					Purge( Id, ItemsBefore );
					throw;
				}
			}
			OneNodeGroup( const OneNodeGroup& )=delete;
			~OneNodeGroup(){
				Host = nullptr;//the test's hold, gone before the purge as its locals were.
				Purge( Id, ItemsBefore );
			}
			uint32 Id;
			uint32 Index;
			sp<HistGroupHost> Host;
			uint ItemsBefore;
		};

		//What the two hosts' reads must agree on:  each value's source time, status and value, in order.
		Ω Agree( const vector<Row>& group, const vector<Row>& server )ι->::testing::AssertionResult{
			if( group.size()!=server.size() )
				return ::testing::AssertionFailure() << "the group read " << group.size() << " values, the server " << server.size();
			for( uint i=0; i<group.size(); ++i ){
				let& g = group[i]; let& s = server[i];
				if( g.Source!=s.Source || g.Status!=s.Status || g.Value!=s.Value )
					return ::testing::AssertionFailure() << "value " << i << ":  the group's " << (g.Source ? ToIsoString(*g.Source) : "no source") << " " << serialize(g.Value) << " status " << g.Status
						<< ", the server's " << (s.Source ? ToIsoString(*s.Source) : "no source") << " " << serialize(s.Value) << " status " << s.Status;
			}
			return ::testing::AssertionSuccess();
		}

		inline static ServerCnnctnPK _connection{};
	};
}