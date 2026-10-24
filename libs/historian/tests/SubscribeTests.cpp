//A subscription (#225, spec *Subscriptions*):  registered under the group's locks with the snapshot it reads first,
//so a record is in the snapshot or the stream and never both;  the snapshot frozen as the flushes move on;  every stored
//value streamed as it is stored, a late record included, and a heartbeat once a flush takes it;  each edit's Modification
//records once durable;
//and the registration ended by the Subscription, or by the group's removal or stop, which tells the subscriber.
#include <jde/fwk/co/BlockAwait.h>
#include <jde/opc/UAException.h>
#include "reads.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;

	//What a subscriber received, in order, under its own lock:  the group delivers on whatever thread stored the record.
	struct Received final : ISubscriber{
		α OnRecords( vector<Proto::HistoryRecord>&& records )ι->void override{
			ul _{ _mutex };
			std::ranges::move( records, std::back_inserter(_records) );
		}
		α Records()Ι->vector<Proto::HistoryRecord>{ ul _{ _mutex }; return _records; }
		α Values()Ι->vector<double>{
			vector<double> y;
			for( let& r : Records() ){
				if( r.has_value() )
					y.push_back( r.value().has_value() ? r.value().value().double_value() : std::numeric_limits<double>::quiet_NaN() );
			}
			return y;
		}
		α Modifications()Ι->vector<Proto::Modification>{
			vector<Proto::Modification> y;
			for( let& r : Records() ){
				if( r.has_modification() )
					y.push_back( r.modification() );
			}
			return y;
		}
		α Count()Ι->uint{ ul _{ _mutex }; return _records.size(); }
		α OnEnded( ESubscriptionEnd why )ι->void override{ ul _{ _mutex }; _ended.push_back( why ); }
		α Ended()Ι->vector<ESubscriptionEnd>{ ul _{ _mutex }; return _ended; }
	private:
		mutable absl::Mutex _mutex;
		vector<Proto::HistoryRecord> _records ABSL_GUARDED_BY(_mutex);
		vector<ESubscriptionEnd> _ended ABSL_GUARDED_BY(_mutex);
	};

	//March 7 archived, March 8 live with a record in its file and one in the buffer, as Reads::TwoDays has it.
	struct Subscriptions : GatewayFiles{
		α TwoDays()ε->void{
			Pump = AddGroup();
			Speed = Join( *Pump, "Pump1.Speed" );
			Temp = Join( *Pump, "Pump1.Temp" );
			DataChange( *Pump, Speed, 1, T0+1s );
			DataChange( *Pump, Speed, 2, T0+2s );
			DataChange( *Pump, Temp, 10, T0+2s );
			EXPECT_TRUE( Flush(*Pump) );
			Time->AdvanceTo( Eighth+1min );
			Settle( *Pump );
			DataChange( *Pump, Speed, 3, Eighth+2min );
			EXPECT_TRUE( Flush(*Pump) );
			Time->AdvanceTo( Eighth+3min );
			DataChange( *Pump, Speed, 4, Eighth+3min );
		}
		//Every page of the snapshot, each page's size in pages.
		Ω All( Subscription& subscription, uint limit=0, vector<uint>* pages=nullptr )ε->vector<ReadValue>{
			vector<ReadValue> y;
			for( uint count{}; ; ++count ){
				auto page = subscription.Next( limit );
				if( pages )
					pages->push_back( page.Values.size() );
				std::ranges::move( page.Values, std::back_inserter(y) );
				if( page.Continuation.empty() )
					break;
				THROW_IF( count>100'000, "A snapshot that never ends." );
			}
			return y;
		}
		α Edit( vector<EditDetails> details )ε->vector<EditResult>{ return BlockAwait( Pump->Edit(move(details), Admin) ); }
		const TimePoint T0{ Time->Now() };
		const TimePoint Eighth{ sys_days{March8} };
		sp<Group> Pump;
		NodeIndex Speed{}, Temp{};
		sp<Received> Listener{ ms<Received>() };
	};

	//The snapshot is the history from the time to the registration, the archive, the live file and the buffer alike, with
	//each node's value before the time first as its bound, and what is stored after streams.
	TEST_F( Subscriptions, SnapshotThenStream ){
		TwoDays();
		auto subscription = Pump->Subscribe( Listener, {Speed, Temp}, ticks(T0+2s) );
		DataChange( *Pump, Speed, 5, Eighth+4min );//after the registration:  streamed, not in the snapshot.
		let all = All( *subscription );
		EXPECT_EQ( doubles(all), (vector<double>{2, 10, 3, 4}) );
		EXPECT_EQ( bounds(all), (vector<bool>{true, true, false, false}) ) << "each node's record at T0+2s serves as its bound there";
		EXPECT_TRUE( subscription->Next(0).Values.empty() ) << "a page after the last";
		EXPECT_EQ( Listener->Values(), vector<double>{5} );
		DataChange( *Pump, Temp, 11, Eighth+5min );
		EXPECT_EQ( Listener->Values(), (vector<double>{5, 11}) );
		let records = Listener->Records();
		let& last = records.back().value();
		EXPECT_EQ( last.node_index(), Temp );
		EXPECT_EQ( last.source_ts(), ticks(Eighth+5min) );
		EXPECT_EQ( last.server_ts(), ticks(Eighth+5min+5ms) );
	}

	//Nothing stored after the registration reaches a later page, however the flushes move the files on meanwhile:  the
	//snapshot reads a live file through the runs it had, and an archive through the file then there, and the buffer as copied.
	TEST_F( Subscriptions, FrozenAsTheFlushesMoveOn ){
		TwoDays();
		auto subscription = Pump->Subscribe( Listener, {Speed}, ticks(T0) );
		DataChange( *Pump, Speed, 6, Eighth+4min );
		DataChange( *Pump, Speed, 2.5, T0+2500ms );//a late record for the archive.
		EXPECT_TRUE( Flush(*Pump) );//the buffer's 4 into the live file, 6 after it, and 2.5 merged into March 7's archive.
		vector<uint> pages;
		let all = All( *subscription, 2, &pages );
		ASSERT_EQ( all.size(), 5u );
		EXPECT_TRUE( notFound(all[0], Speed, T0) ) << "nothing before T0";
		EXPECT_EQ( doubles({all.begin()+1, all.end()}), (vector<double>{1, 2, 3, 4}) );
		EXPECT_EQ( pages, (vector<uint>{2, 2, 1}) );
		EXPECT_EQ( Listener->Values(), (vector<double>{6, 2.5}) ) << "the late record streams like any other";
		//Another subscription now sees the files as they are.
		auto again = ms<Received>();
		auto second = Pump->Subscribe( again, {Speed}, ticks(T0) );
		let now = All( *second );
		ASSERT_EQ( now.size(), 7u );
		EXPECT_EQ( doubles({now.begin()+1, now.end()}), (vector<double>{1, 2, 2.5, 3, 4, 6}) );
	}

	//The snapshot holds its files open until Next reads the last page, which lets them go, and the stream goes on.
	TEST_F( Subscriptions, TheLastPageLetsTheFilesGo ){
		TwoDays();
		Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_REPLACE, {Reading(2.2, T0+2s)}}} );
		EXPECT_TRUE( Settle(*Pump) );
		auto subscription = Pump->Subscribe( Listener, {Speed}, ticks(T0) );
		EXPECT_EQ( subscription->OpenFiles(), 3u ) << "March 7's archive and modifications, and March 8's live file";
		EXPECT_FALSE( subscription->Next(2).Continuation.empty() );
		EXPECT_EQ( subscription->OpenFiles(), 3u ) << "a page before the last";
		All( *subscription, 2 );
		EXPECT_EQ( subscription->OpenFiles(), 0u );
		EXPECT_TRUE( subscription->Next(2).Values.empty() ) << "a page after the last";
		DataChange( *Pump, Speed, 6, Eighth+4min );
		EXPECT_EQ( Listener->Values(), vector<double>{6} );
	}

	//A time past every record snapshots the bound alone, each node's value now, and a subscription to a node no record
	//holds yet starts with Bad_BoundNotFound at the time.
	TEST_F( Subscriptions, FromNow ){
		TwoDays();
		let level = Join( *Pump, "Pump1.Level" );
		auto subscription = Pump->Subscribe( Listener, {Speed, level}, ticks(Time->Now()) );
		let all = All( *subscription );
		ASSERT_EQ( all.size(), 2u );
		EXPECT_TRUE( notFound(all[0], level, Time->Now()) ) << "the bounds found nowhere come first";
		EXPECT_TRUE( isBound(all[1], Speed, 4, Eighth+3min) ) << "the buffered record at the time serves as the bound";
		DataChange( *Pump, level, 50, Time->Now() );
		EXPECT_EQ( Listener->Values(), vector<double>{50} );
	}

	//Each subscriber hears of its nodes alone, a heartbeat and a break's marker among them, and no more once its
	//Subscription has ended, or the group stops.
	TEST_F( Subscriptions, NodesHeartbeatsAndTheEnd ){
		TwoDays();
		Pump->SetThresholds( Speed, {.MaxTimeInterval=1min} );
		auto speed = ms<Received>();
		auto temp = ms<Received>();
		auto speedOnly = Pump->Subscribe( speed, {Speed}, ticks(Time->Now()) );
		auto tempOnly = Pump->Subscribe( temp, {Temp}, ticks(Time->Now()) );
		All( *speedOnly );
		All( *tempOnly );
		DataChange( *Pump, Speed, 7, Time->Now() );
		DataChange( *Pump, Temp, 12, Time->Now() );
		EXPECT_EQ( speed->Values(), vector<double>{7} );
		EXPECT_EQ( temp->Values(), vector<double>{12} );
		Time->Advance( 61s );//Speed's heartbeat.
		EXPECT_EQ( speed->Count(), 1u ) << "held until a flush takes it";
		Time->Advance( 1s );//past the publishing interval.
		EXPECT_TRUE( Flush(*Pump) );
		Time->Advance( 1ms );//the clock's hop.
		ASSERT_EQ( speed->Count(), 2u );
		EXPECT_TRUE( speed->Records()[1].value().has_heartbeat() );
		EXPECT_EQ( temp->Count(), 1u );
		Pump->Disconnected( Time->Now() );
		Pump->Connected();
		DataChange( *Pump, Speed, 8, Time->Now()+1s );//later than the last delivered:  a marker, then the value.
		ASSERT_EQ( speed->Count(), 4u );
		EXPECT_EQ( speed->Records()[2].value().status(), UA_STATUSCODE_BADDATALOST );
		EXPECT_EQ( speed->Records()[3].value().value().double_value(), 8 );
		speedOnly->End();
		DataChange( *Pump, Speed, 9, Time->Now()+2s );
		EXPECT_EQ( speed->Count(), 4u ) << "ended";
		speedOnly->End();//idempotent
		tempOnly.reset();//the destructor ends it.
		DataChange( *Pump, Temp, 13, Time->Now()+2s );
		EXPECT_EQ( temp->Count(), 1u );
		EXPECT_THROW( Pump->Subscribe(Listener, {}, ticks(T0)), Exception );
		EXPECT_THROW( Pump->Subscribe(nullptr, {Speed}, ticks(T0)), Exception );
	}

	//A heartbeat streams once a flush takes it, out of a drop's reach:  one that a change sampled before it drops never
	//streams, so the stream holds what the file does.  A snapshot leaves out a heartbeat no flush has taken, which streams
	//when one does.
	TEST_F( Subscriptions, HeartbeatsStreamOnceTaken ){
		TwoDays();
		Pump->SetThresholds( Speed, {.MaxTimeInterval=10s} );
		auto subscription = Pump->Subscribe( Listener, {Speed}, ticks(Time->Now()) );
		All( *subscription );
		Time->Advance( 1s );
		let changed = Time->Now();
		DataChange( *Pump, Speed, 5, changed );
		Time->Advance( 10s );//its heartbeat, which a flush leaves for the publishing interval.
		Time->Advance( 50ms );
		DataChange( *Pump, Speed, 6, changed+10s-50ms );//sampled before the heartbeat, so it drops it.
		Time->Advance( 1s );
		EXPECT_TRUE( Flush(*Pump) );
		Time->Advance( 1ms );
		EXPECT_EQ( Listener->Values(), (vector<double>{5, 6}) ) << "no heartbeat of 5 after 6";
		auto check = ms<Received>();
		let filed = All( *Pump->Subscribe(check, {Speed}, ticks(changed)) );
		EXPECT_EQ( doubles(filed), (vector<double>{5, 6}) ) << "nor in the file";

		Time->AdvanceTo( changed+20s+100ms );//6's heartbeat, 50 ms ago.
		auto late = ms<Received>();
		auto second = Pump->Subscribe( late, {Speed}, ticks(changed) );
		let snapshot = All( *second );
		EXPECT_EQ( doubles(snapshot), (vector<double>{5, 6}) ) << "the heartbeat no flush has taken";
		EXPECT_EQ( Listener->Count(), 2u );
		Time->Advance( 1s );
		EXPECT_TRUE( Flush(*Pump) );
		Time->Advance( 1ms );
		for( let& heard : {Listener, late} ){
			let records = heard->Records();
			ASSERT_FALSE( records.empty() );
			EXPECT_TRUE( records.back().value().has_heartbeat() );
			EXPECT_EQ( records.back().value().value().double_value(), 6 );
			EXPECT_EQ( std::ranges::count_if(records, []( let& r ){ return r.value().has_heartbeat(); }), 1 );
		}
	}

	//A heartbeat a failed write returns streamed as the flush took it:  the next flush doesn't stream it again, and a
	//change sampled before it, too late to drop it, follows it into the file as into the stream.
	TEST_F( Subscriptions, AReturnedHeartbeatStreamsOnce ){
		TwoDays();
		Pump->SetThresholds( Speed, {.MaxTimeInterval=10s} );
		auto subscription = Pump->Subscribe( Listener, {Speed}, ticks(Time->Now()) );
		All( *subscription );
		Time->Advance( 1s );
		let changed = Time->Now();
		DataChange( *Pump, Speed, 5, changed );
		Time->Advance( 11s );//its heartbeat at changed+10s, past the publishing interval.
		let file = File( *Pump, March8 ), aside = file.parent_path()/"aside";
		fs::rename( file, aside );
		fs::create_directory( file );//nothing appends to a directory.
		EXPECT_FALSE( Flush(*Pump) );
		Time->Advance( 1ms );
		EXPECT_EQ( Listener->Values(), (vector<double>{5, 5}) ) << "streamed as the flush took it";
		DataChange( *Pump, Speed, 6, changed+10s-50ms );
		fs::remove( file );
		fs::rename( aside, file );
		EXPECT_TRUE( Flush(*Pump) );
		Time->Advance( 1ms );
		EXPECT_EQ( Listener->Values(), (vector<double>{5, 5, 6}) ) << "once";
		auto check = ms<Received>();
		let filed = All( *Pump->Subscribe(check, {Speed}, ticks(changed)) );
		ASSERT_EQ( doubles(filed), (vector<double>{5, 6, 5}) );
		EXPECT_TRUE( filed[2].Value.has_heartbeat() );
	}

	//An edit's Modification records are pushed once they are durable, to the subscribers of their nodes, and a subscription
	//made after an edit has them in its snapshot, applied, not in its stream.
	TEST_F( Subscriptions, ModificationsOnceDurable ){
		TwoDays();
		auto subscription = Pump->Subscribe( Listener, {Speed}, ticks(T0) );
		All( *subscription );
		let results = Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_REPLACE, {Reading(2.2, T0+2s)}}, UpdateData{Temp, UA_PERFORMUPDATETYPE_REPLACE, {Reading(10.1, T0+2s)}}, DeleteAtTime{Speed, {ticks(T0+1s)}}} );
		ASSERT_EQ( results.size(), 3u );
		EXPECT_EQ( results[0].Results, vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED} );
		EXPECT_TRUE( Settle(*Pump) );
		//Delivered on the clock's hop, off the flush.
		Time->Advance( 1ms );
		let mods = Listener->Modifications();
		ASSERT_EQ( mods.size(), 2u ) << "Speed's two, not Temp's";
		EXPECT_EQ( mods[0].node_index(), Speed );
		EXPECT_EQ( mods[0].target_source_ts(), ticks(T0+1s) );
		EXPECT_EQ( mods[0].update_type(), Proto::UPDATE_TYPE_DELETE );
		EXPECT_EQ( mods[1].target_source_ts(), ticks(T0+2s) );
		EXPECT_EQ( mods[1].update_type(), Proto::UPDATE_TYPE_REPLACE );
		EXPECT_EQ( mods[1].new_value().value().double_value(), 2.2 );
		EXPECT_EQ( mods[1].user_name(), "admin" );
		EXPECT_TRUE( Listener->Values().empty() ) << "an edit stores no value";
		auto after = ms<Received>();
		auto second = Pump->Subscribe( after, {Speed}, ticks(T0) );
		let corrected = All( *second );
		ASSERT_EQ( corrected.size(), 4u );
		EXPECT_TRUE( notFound(corrected[0], Speed, T0) );
		EXPECT_EQ( doubles({corrected.begin()+1, corrected.end()}), (vector<double>{2.2, 3, 4}) );
		Time->Advance( 1ms );
		EXPECT_EQ( after->Count(), 0u );
	}

	//A removed or stopped group takes no subscription, and ends those it has, telling each subscriber once why:  the
	//removal its group's, the historian's end every group's, after the value it settles streams.  One its subscriber
	//ended first hears nothing.
	TEST_F( Subscriptions, RemovedAndStopped ){
		using enum ESubscriptionEnd;
		TwoDays();
		auto subscription = Pump->Subscribe( Listener, {Speed}, ticks(T0) );
		All( *subscription );
		auto unsubscribed = ms<Received>();
		Pump->Subscribe( unsubscribed, {Temp}, ticks(T0) )->End();
		Library->RemoveGroup( Pump->Name(), Admin );
		EXPECT_EQ( Listener->Ended(), vector{Removed} );
		EXPECT_TRUE( unsubscribed->Ended().empty() );
		EXPECT_THROW( Pump->Subscribe(Listener, {Speed}, ticks(T0)), Exception );
		EXPECT_FALSE( DataChange(*Pump, Speed, 9, Time->Now()) );
		EXPECT_EQ( Listener->Count(), 0u );
		subscription.reset();
		EXPECT_EQ( Listener->Ended().size(), 1u ) << "an End after:  harmless";
		auto other = AddGroup();
		let flow = Join( *other, "Pump2.Flow", {.MinTimeInterval=1s} );
		auto flows = ms<Received>();
		auto live = other->Subscribe( flows, {flow}, ticks(Time->Now()) );
		DataChange( *other, flow, 1, Time->Now() );
		Time->Advance( 100ms );
		DataChange( *other, flow, 1.5, Time->Now() );//held:  inside MinTimeInterval.
		EXPECT_EQ( flows->Count(), 1u );
		Restart();//the historian's end:  Stopping settles the held value, then ends the registrations.
		EXPECT_EQ( flows->Values(), (vector<double>{1, 1.5}) ) << "the value the stop settles streams";
		EXPECT_EQ( flows->Ended(), vector{Stopped} );
		EXPECT_FALSE( DataChange(*other, flow, 2, Time->Now()) );
		EXPECT_EQ( flows->Count(), 2u );
		live.reset();//after the group stopped:  harmless.
		EXPECT_EQ( flows->Ended().size(), 1u );
	}
}