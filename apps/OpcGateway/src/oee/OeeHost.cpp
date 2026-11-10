#include "OeeHost.h"
#include <jde/fwk/co/AllAwait.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/settings.h>
#include <jde/oee/rules.h>
#include "../hist/HistHost.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	constexpr Duration DefaultSettle{ 5s };

	Ω inputs( const OeeHost::Machine& machine )ι->OeeMachineHost::Inputs{ return { ToSignals(machine.Signals), machine.Row.IdleTimeout, machine.Row.SmallStop }; }
	//What a start from `processed` leaves to rebuild of the asks it carries:  its replay makes the rows from `processed` on.
	Ω carried( vector<OeeMachineHost::Ask>&& asks, TimePoint processed )ι->vector<OeeMachineHost::Ask>{
		std::erase_if( asks, [processed]( let& ask ){ return ask.Span.Start>=processed; } );
		for( auto& ask : asks )
			ask.Span.End = std::min( ask.Span.End, processed );
		return move( asks );
	}
	Ω same( const OeeMachineHost::Inputs& a, const OeeMachineHost::Inputs& b )ι->bool{
		return a.IdleTimeout==b.IdleTimeout && a.SmallStop==b.SmallStop && std::ranges::equal( a.Signals, b.Signals, []( const Oee::Signal& x, const Oee::Signal& y ){
			return x.Role==y.Role && x.Node==y.Node && x.Running==y.Running && x.RatePeriod==y.RatePeriod;
		} );
	}

	α OeeHost::Instance()ι->OeeHost&{
		static OeeHost instance;
		return instance;
	}
	α OeeHost::Start( sp<Hist::IClock> clock )ε->void{
		let oee = Settings::FindObject( "/gateway/oee" );
		auto settle = Settings::FindDuration( "/gateway/oee/settle" ).value_or( DefaultSettle );
		if( settle<=Duration::zero() ){
			WARNT( ELogTags::Settings, "'/gateway/oee/settle' of {} is not allowed:  it must be positive.  Using {}.", Chrono::ToString(settle), Chrono::ToString(DefaultSettle) );
			settle = DefaultSettle;
		}
		{
			lg _{ _mutex };
			_stopped = false;
			_configured = oee!=nullptr;
			_settle = settle;
			_clock = clock ? move( clock ) : Hist::SystemClock();
		}
		if( !oee )
			INFO( "No '/gateway/oee':  this gateway computes no OEE." );
		if( oee && !HistHost::Instance().Enabled() )
			CRITICAL( "OEE needs the gateway's historian, which isn't running:  this gateway runs without OEE." );
		struct LoadAwait final : AnyVoidAwait{
			LoadAwait( OeeHost& host )ι:_host{ host }{}
			α Suspend()ι->void override{ _host.Loading( *this ); }
			OeeHost& _host;
		};
		BlockAwait( LoadAwait{*this} );
	}
	α OeeHost::Stop()ι->void{
		vector<sp<OeeMachineHost>> engines;
		{
			lg _{ _mutex };
			_stopped = true;
			for( auto&& [_,entry] : _machines ){
				++entry.Serial;
				if( auto timer = std::exchange(entry.Timer, 0); timer )
					_clock->Cancel( timer );
				if( entry.Engine )
					engines.push_back( move(entry.Engine) );
				entry.Engine = nullptr;
			}
		}
		for( auto& engine : engines )
			engine->End();
		for( auto& engine : engines ){
			try{
				BlockAwait( engine->Quiet() );
			}
			catch( const std::exception& )
			{}
		}
	}
	α OeeHost::Enabled()Ι->bool{
		{
			lg _{ _mutex };
			if( !_configured || _stopped )
				return false;
		}
		return HistHost::Instance().Enabled();
	}
	α OeeHost::TestEnabled( SL sl )Ε->void{
		bool configured, stopped;
		{
			lg _{ _mutex };
			configured = _configured;
			stopped = _stopped;
		}
		const ExceptionArgs args{ EHttpStatus::ServiceUnavailable };//at Debug:  the configuration's, which the start logged, not a fault.
		if( stopped )
			throw Exception{ sl, args, "The gateway's OEE has stopped." };
		if( !configured )
			throw Exception{ sl, args, "This gateway computes no OEE:  it has no /gateway/oee block." };
		HistHost::Instance().TestEnabled( sl );
	}
	α OeeHost::Find( OeeMachinePK id )Ι->optional<Machine>{
		lg _{ _mutex };
		auto p = _machines.find( id );
		return p==_machines.end() ? nullopt : optional<Machine>{ p->second.Rows };
	}
	α OeeHost::HolderOf( HistGroupPK group )Ι->optional<Machine>{
		lg _{ _mutex };
		for( let& [_,entry] : _machines ){
			if( entry.Rows.Row.Group==group )
				return entry.Rows;
		}
		return nullopt;
	}
	α OeeHost::UserOf( HistGroupNodePK node )Ι->optional<Machine>{
		lg _{ _mutex };
		for( let& [_,entry] : _machines ){
			if( std::ranges::contains(entry.Rows.Signals, node, &OeeSignalRow::Node) )
				return entry.Rows;
		}
		return nullopt;
	}
	α OeeHost::Engine( OeeMachinePK id )Ι->sp<OeeMachineHost>{
		lg _{ _mutex };
		auto p = _machines.find( id );
		return p==_machines.end() ? nullptr : p->second.Engine;
	}
	α OeeHost::Recompute( OeeMachinePK id, Oee::Span span, SL sl )ε->void{
		let machine = Find( id );
		if( !machine )
			throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Machine {} not found.", id };
		TestEnabled( sl );
		let blocked = Blocked( *machine );
		for( sp<OeeMachineHost> ended;; ){
			sp<OeeMachineHost> engine;
			{
				lg _{ _mutex };
				auto p = _machines.find( id );
				if( p==_machines.end() )
					throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Machine {} not found.", id };
				if( p->second.Engine!=ended )
					engine = p->second.Engine;
				if( !engine && blocked.empty() ){
					p->second.Pending.push_back( {0, span} );
					DBG( "[{}]A recompute over {} to {} waits for the machine's next engine.", machine->Row.Name, ToIsoString(span.Start), ToIsoString(span.End) );
					return;
				}
			}
			if( !engine )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "Machine '{}' isn't running, so nothing recomputes its rows:  {}.", machine->Row.Name, blocked };
			if( engine->Recompute(span) )
				return;
			ended = engine;//a failure ended it, and the entry holds it until the retry's start, the next.
		}
	}
	α OeeHost::Blocked( const Machine& machine )Ι->string{
		if( machine.Row.Deleted )
			return "it is deleted";
		if( machine.Signals.empty() )
			return "it has no signals yet";
		if( !machine.Row.Processed )
			return "it has no `processed`:  its create's live side didn't run";
		if( let unread = std::ranges::find_if(machine.Signals, []( const OeeSignalRow& row ){ return !row.Role; }); unread!=machine.Signals.end() )
			return Ƒ( "signal {} has a role that isn't State, Total, Rate, Good, Reject or Reason", unread->Id );
		try{
			Oee::ValidateMachine( ToSignals(machine.Signals), machine.Row.Width, machine.Row.IdealCycle, machine.Row.IdleTimeout );
			TestEnabled();
		}
		catch( const std::exception& e ){
			return e.what();
		}
		if( !HistHost::Instance().Find(machine.Row.Group) )
			return Ƒ( "its group, {}, isn't one the historian hosts", machine.Row.Group );
		return {};
	}

	α OeeHost::Loading( AnyVoidAwait& await )ι->VoidTask{
		try{
			auto [machines, signals] = co_await All( SelectOeeMachines({.IncludeDeleted=true}), SelectOeeSignals() );
			vector<std::pair<OeeMachinePK,uint>> launches;
			{
				lg _{ _mutex };
				_machines.clear();
				for( auto& row : machines ){
					let id = row.Id;
					auto& entry = _machines[id];
					entry.Rows.Row = move( row );
					for( let& signal : signals ){
						if( signal.Machine==id )
							entry.Rows.Signals.push_back( signal );
					}
					launches.emplace_back( id, entry.Serial );
				}
			}
			if( Enabled() ){
				for( let [id, serial] : launches )
					Launch( id, serial );
			}
			INFO( "OEE:  {} machines, {} signals.", machines.size(), signals.size() );
			await.Resume();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α OeeHost::Schedule( OeeMachinePK id, Entry& entry, Duration after )ι->void{
		if( auto timer = std::exchange(entry.Timer, 0); timer )
			_clock->Cancel( timer );
		let serial = ++entry.Serial;
		entry.Timer = _clock->Schedule( after, [id, serial]{ Instance().Launch( id, serial ); } );
	}
	α OeeHost::Launch( OeeMachinePK id, uint serial )ι->VoidTask{
		let current = [&]()->Entry*{//under _mutex:  the entry, while this is still its start.
			auto p = _machines.find( id );
			return _stopped || p==_machines.end() || p->second.Serial!=serial || p->second.Engine ? nullptr : &p->second;
		};
		try{
			{
				lg _{ _mutex };
				auto entry = current();
				if( !entry )
					co_return;
				entry->Timer = 0;
			}
			auto rows = co_await Any( SelectOeeMachines({.Key=DB::Key{(uint)id}, .IncludeDeleted=true}) );
			if( rows.empty() )
				co_return;
			Machine machine{ move(rows.front()), co_await Any(SelectOeeSignals({.Machine=id})) };
			let blocked = Blocked( machine );
			auto group = blocked.empty() ? HistHost::Instance().Find( machine.Row.Group ) : nullptr;
			let historian = HistHost::Instance().Historian();
			sp<OeeMachineHost> engine;
			{
				lg _{ _mutex };
				auto entry = current();
				if( !entry )
					co_return;
				entry->Rows = machine;
				if( group && historian ){
					engine = ms<OeeMachineHost>( machine.Row, inputs(machine), move(group), _clock, OeeMachineHost::Settings{_settle, historian->Config().ReadLimit}, std::exchange(entry->Offset, {}) );
					engine->Start(//before a recomputeOee can find it.
						[]( OeeMachinePK machine, const OeeMachineHost* host, string why ){ Instance().Failed( machine, host, move(why) ); },
						[]( OeeMachinePK machine, const OeeMachineHost* host, TimePoint from, Duration offset, string why ){ Instance().Rebuild( machine, host, from, offset, move(why) ); },
						carried(std::exchange(entry->Pending, {}), *machine.Row.Processed) );
					entry->Engine = engine;
				}
			}
			if( !engine && !machine.Row.Deleted )
				INFO( "[{}]OEE isn't running:  {}.", machine.Row.Name, blocked );
		}
		catch( const std::exception& e ){
			Failed( id, nullptr, e.what() );
		}
	}
	α OeeHost::Failed( OeeMachinePK id, const OeeMachineHost* host, string why )ι->void{
		sp<OeeMachineHost> ended;
		{
			lg _{ _mutex };
			auto p = _machines.find( id );
			if( _stopped || p==_machines.end() || p->second.Engine.get()!=host )
				return;
			ERR( "[{}]OEE stopped, and starts again from `processed` in {}:  {}", p->second.Rows.Row.Name, Chrono::ToString(RetryInterval), why );
			ended = move( p->second.Engine );
			p->second.Engine = nullptr;
			Schedule( id, p->second, RetryInterval );
		}
		if( ended )
			Settled( id, move(ended) );
	}
	α OeeHost::Carry( OeeMachinePK id, OeeMachineHost& ended )ι->void{
		auto asks = ended.Unfinished();
		if( asks.empty() )
			return;
		lg _{ _mutex };
		if( auto p = _machines.find(id); p!=_machines.end() )
			std::ranges::move( asks, std::back_inserter(p->second.Pending) );
	}
	α OeeHost::Settled( OeeMachinePK id, sp<OeeMachineHost> ended )ι->VoidTask{
		co_await ended->Quiet();
		Carry( id, *ended );
	}
	α OeeHost::Rebuild( OeeMachinePK id, const OeeMachineHost* host, TimePoint from, Duration offset, string why )ι->VoidTask{
		sp<OeeMachineHost> ended;
		uint serial;
		{
			lg _{ _mutex };
			auto p = _machines.find( id );
			if( _stopped || p==_machines.end() || p->second.Engine.get()!=host )
				co_return;
			INFO( "[{}]OEE starts again from {}, its rows from there made again from the history:  {}", p->second.Rows.Row.Name, ToIsoString(from), why );
			if( auto timer = std::exchange(p->second.Timer, 0); timer )
				_clock->Cancel( timer );
			serial = ++p->second.Serial;
			ended = std::exchange( p->second.Engine, nullptr );
			p->second.Offset = offset;
		}
		ended->End();
		try{
			co_await ended->Quiet();//its last statement, before the restart deletes what it wrote.
			Carry( id, *ended );
			co_await Any( DS()->Execute(OeeSql::SetProcessed(id, from)) );
		}
		catch( const std::exception& e ){
			Failed( id, nullptr, Ƒ("`processed` couldn't be set back to {}, so the rows from there stand as they were:  {}", ToIsoString(from), e.what()) );
			co_return;
		}
		Launch( id, serial );
	}

	α OeeHost::MachineCreated( OeeMachinePK id, optional<TimePoint> from )ι->AnyVoidTask{
		let clock = Time();
		co_await Any( DS()->Execute(OeeSql::SetProcessed(id, OeeSecond(from.value_or(clock ? clock->Now() : Clock::now())))) );
		co_await MachineChanged( DB::Key{(uint)id} );
	}
	α OeeHost::MachineChanged( DB::Key key )ι->AnyVoidTask{
		uint read;
		{
			lg _{ _mutex };
			read = ++_reads;
		}
		auto rows = co_await Any( SelectOeeMachines({.Key=key, .IncludeDeleted=true}) );
		if( rows.empty() ){
			if( key.IsPK() )
				MachinePurged( (OeeMachinePK)key.PK() );
			co_return;
		}
		let id = rows.front().Id;
		Machine machine{ move(rows.front()), co_await Any(SelectOeeSignals({.Machine=id})) };
		let blocked = Blocked( machine );
		sp<OeeMachineHost> ending, running;
		{
			lg _{ _mutex };
			auto& entry = _machines[id];
			if( read<entry.Read )//a later edit's rows are in, and its engine decided.
				co_return;
			entry.Read = read;
			entry.Rows = machine;
			++entry.Serial;//a start it was waiting for is this edit's to ask for again.
			if( auto timer = std::exchange(entry.Timer, 0); timer && _clock )
				_clock->Cancel( timer );
			if( entry.Engine && blocked.empty() && same(entry.Engine->Made(), inputs(machine)) )
				running = entry.Engine;
			else
				ending = std::exchange( entry.Engine, nullptr );
		}
		if( running ){
			running->SetWidth( machine.Row.Width );
			co_return;
		}
		if( ending ){
			ending->End();
			co_await ending->Quiet();//its last write, before another engine's first.
			Carry( id, *ending );
		}
		if( blocked.size() ){
			if( !machine.Row.Deleted )
				INFO( "[{}]OEE isn't running:  {}.", machine.Row.Name, blocked );
			co_return;
		}
		lg _{ _mutex };
		if( auto p = _machines.find(id); !_stopped && _clock && p!=_machines.end() && !p->second.Engine )
			Schedule( id, p->second, _settle );
	}
	α OeeHost::MachinePurging( OeeMachinePK id )ι->AnyVoidTask{
		sp<OeeMachineHost> engine;
		{
			lg _{ _mutex };
			if( auto p = _machines.find(id); p!=_machines.end() ){
				++p->second.Serial;
				if( auto timer = std::exchange(p->second.Timer, 0); timer && _clock )
					_clock->Cancel( timer );
				engine = std::exchange( p->second.Engine, nullptr );
			}
		}
		if( engine ){
			engine->End();
			co_await engine->Quiet();
		}
	}
	α OeeHost::MachinePurged( OeeMachinePK id )ι->void{
		lg _{ _mutex };
		_machines.erase( id );
	}
}