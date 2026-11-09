#include <jde/oee/Engine.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/proto/opc.Common.h>
#include <jde/oee/rules.h>

#define let const auto

namespace Jde::Opc::Oee{
	using Hist::IClock;
	namespace{
		using Reading = std::variant<std::monostate,double,string>;
		//A record as the engine keeps it, by its source time.
		struct Sample final{ TimePoint Time; uint32 Node; bool Good; Reading Value; bool Applied{}; };
		constexpr uint8 TotalCount{ 0 }, GoodCount{ 1 }, RejectCount{ 2 };
		constexpr std::array Counters{ std::pair{ERole::Total, TotalCount}, std::pair{ERole::Good, GoodCount}, std::pair{ERole::Reject, RejectCount} };
		//How far a record may be ahead of the engine's clock, or its offset from the one held, before it is doubted.
		constexpr Duration Skew{ minutes{1} };
		constexpr uint Recent{ 8 };//the values the source offset is the least of.

		//The machine at an instant:  what a record changes and a snapshot restores.
		struct Core final{
			TimePoint At;//tallied to here.
			EState State{ EState::Unknown };
			TimePoint Since;
			optional<Stop> Open;//the stop, or the span of unknown time, in progress:  the last of the rows.
			optional<TimePoint> Quiet;//with no State signal, a running machine's last count:  the idle timeout runs from it.
			optional<int64_t> QuietCode;//the Reason held at Quiet, else the first above zero since:  the idle stop's code.
			optional<TimePoint> LastCount;
			std::array<optional<double>,3> Last;//each counter's last Good value.
			optional<double> Rate;//the Rate, while it is Good.
			int64_t Reason{};//the Reason signal's value, 0 for none.
			Interval Row;//the interval being filled:  its bounds and its times.
			std::array<double,3> Counts{};//its counts by signal, the Rate's integral under the total.
		};
		struct LiveKey final{
			EState State; TimePoint Since; optional<Stop> Open; bool Small;
			bool operator==( const LiveKey& )const = default;
		};

		constexpr α bit( ERole role )ι->uint8{ return (uint8)( 1u<<(uint8)role ); }
		Ω isGood( uint32_t status )ι->bool{ return !UA_StatusCode_isBad( status ) && !UA_StatusCode_isUncertain( status ); }//Uncertain is Bad, as the aggregates' TreatUncertainAsBad has it.
		Ω primaryTime( const Hist::Proto::DataValue& v )ι->optional<TimePoint>{
			if( v.has_source_ts() )
				return UADateTime{ UA_DateTime{v.source_ts()} }.Time();
			if( v.has_server_ts() )
				return UADateTime{ UA_DateTime{v.server_ts()} }.Time();
			return nullopt;
		}
		Ω reading( const Hist::Proto::DataValue& v )ι->Reading{
			if( !v.has_value() )
				return {};
			let& x = v.value();
			if( x.of_case()==Opc::Proto::Value::kBoolean )
				return x.boolean() ? 1.0 : 0.0;
			if( x.of_case()==Opc::Proto::Value::kStringValue )
				return x.string_value();
			let n = ProtoUtils::Number( x );
			return n ? Reading{ *n } : Reading{};
		}
		Ω sample( const Hist::Proto::DataValue& v, TimePoint t )ι->Sample{
			auto value = isGood( v.status() ) ? reading( v ) : Reading{};
			let good = !std::holds_alternative<std::monostate>( value );//a Good status with no value to read says nothing of the machine either.
			return Sample{ t, v.node_index(), good, move(value) };
		}
		Ω number( const Sample& s )ι->optional<double>{
			let p = std::get_if<double>( &s.Value );
			return p ? optional<double>{ *p } : nullopt;
		}
		Ω matches( const vector<StateValue>& running, const Reading& r )ι->bool{
			return std::ranges::any_of( running, [&r]( const StateValue& v ){
				if( let d = std::get_if<double>(&v); d )
					return std::holds_alternative<double>( r ) && std::get<double>( r )==*d;
				return std::holds_alternative<string>( r ) && std::get<string>( r )==std::get<string>( v );
			} );
		}
		//The latest multiple of width at or before t:  the widths divide an hour, so the rows align to it.
		Ω boundary( TimePoint t, Duration width )ι->TimePoint{
			auto r = t.time_since_epoch()%width;
			if( r<Duration::zero() )
				r += width;
			return t-r;
		}
		Ω minute( TimePoint t )ι->TimePoint{ return boundary( t, minutes{1} ); }
	}

	struct Engine::Impl final : std::enable_shared_from_this<Impl>{
		Impl( EngineConfig&& config, Opening&& opening, sp<ISink>&& sink, sp<IClock>&& clock, SL sl )ε;
		α Replay( const vector<Hist::ReadValue>& page )ι->void;
		α Live( Duration offset )ι->void;
		α Finish( TimePoint end )ι->void;
		α Records( vector<Hist::Proto::HistoryRecord>&& records )ι->void;
		α SetWidth( Duration width, SL sl )ε->void;
		α End()ι->void;
		α State()Ι->LiveState{ ul _{ _mutex }; return Build( Now() ); }
		α Processed()Ι->TimePoint{ ul _{ _mutex }; return _processed; }
	private:
		α Now()Ι->TimePoint{ return _live ? std::max( _clock->Now()-_offset, _core.At ) : _core.At; }
		α Horizon()Ι->TimePoint{ return _clock->Now()-_offset+Skew; }//a record past it waits for the clock.
		α Code( const Core& c )Ι->optional<int64_t>{ return c.Reason ? optional<int64_t>{ c.Reason } : nullopt; }
		α Small( TimePoint now )Ι->bool{ return _core.Open && !_core.Open->Unknown && now-_core.Open->Start<_config.SmallStop; }
		α IdleDue( const Core& c )Ι->optional<TimePoint>{ return _config.IdleTimeout && c.Quiet && c.State==EState::Running ? optional<TimePoint>{ *c.Quiet+*_config.IdleTimeout } : nullopt; }
		α WidthAt( TimePoint t )Ι->Duration;
		α Next( TimePoint start )Ι->Interval;
		α Derive( const Core& c )Ι->Interval;
		α Tally( Core& c, Duration d )Ι->void;
		α Build( TimePoint now )Ι->LiveState;

		α Take( const vector<Hist::Proto::HistoryRecord>& records, optional<TimePoint> arrival )ι->void;
		α Clocked( TimePoint t, TimePoint arrival )ι->void;
		α Place( Sample&& s )ι->void;
		α Modify( const Hist::Proto::Modification& m )ι->void;
		α Late( uint32 node, TimePoint t )ι->void;
		α Ask( uint32 node, Span span )ι->void;
		α Run()ι->void;
		α Drain( TimePoint through )ι->void;
		α Rerun( TimePoint t )ι->void;
		α Advance( TimePoint to )ι->void;
		α Roll()ι->void;
		α Idle()ι->void;
		α Apply( const Sample& s )ι->void;
		α Enter( EState state, TimePoint t )ι->void;
		α Counted( TimePoint t, bool positive, bool rate )ι->void;
		α Write( TimePoint through )ι->void;
		α Settle()ι->void;
		α Arm( TimePoint now )ι->void;
		α Tick( TimePoint due )ι->void;
		α Emit()ι->void;

		const EngineConfig _config;
		const sp<ISink> _sink;
		const sp<IClock> _clock;
		flat_map<uint32,uint8> _roles;//a node's, as bits.
		bool _state{}, _total{}, _rate{}, _good{}, _reject{};
		vector<StateValue> _running;
		Duration _ratePeriod{ minutes{1} };
		uint32 _countNode{};//the Total's or the Rate's:  what an idle stop's rebuild names.

		mutable absl::Mutex _mutex;
		Core _core;
		std::deque<Core> _snapshots;//by At:  the opening, then one per minute crossed, from the newest at or before _processed.
		vector<Sample> _samples;//by Time, from the oldest snapshot on.
		std::deque<Interval> _done;//ended by the cursor, not yet written.
		vector<Stop> _rows;//the stops and the unknown spans, open or ended since the oldest snapshot.
		vector<Stop> _told;//_rows as the sink has them.
		optional<LiveKey> _liveKey;//as the sink has it.
		vector<std::pair<uint32,Span>> _rebuilds;//asked in this call.
		optional<TimePoint> _idleAsked;//the idle stop a rebuild was last asked for.
		TimePoint _processed;//the end of the last row written.
		vector<std::pair<TimePoint,Duration>> _widths;//the width of the intervals that start at or after a time.
		flat_map<uint32,TimePoint> _pruned;//a node's newest record no longer held.
		optional<TimePoint> _dirty;//the earliest time behind the cursor at which this call changed the samples.
		bool _live{}, _ended{};
		vector<Hist::Proto::HistoryRecord> _queued;//the stream's, until Live.
		Duration _offset{};//the source offset:  the least of _offsets, or what Live was given.
		vector<Duration> _offsets;//arrival less source time, of the last Recent Good values the stream delivered.
		optional<Duration> _doubted;//the last value's, when it was too far from _offset to believe alone.
		IClock::TimerId _timer{};
		TimePoint _timerDue;
	};

	Engine::Impl::Impl( EngineConfig&& config, Opening&& opening, sp<ISink>&& sink, sp<IClock>&& clock, SL sl )ε:
		_config{ move(config) }, _sink{ move(sink) }, _clock{ move(clock) }, _processed{ opening.From }{
		ValidateSignals( _config.Signals, sl );
		ValidateWidth( _config.Width, sl );
		for( let& s : _config.Signals ){
			_roles[s.Node] |= bit( s.Role );
			switch( s.Role ){
			case ERole::State: _state = true; _running = s.Running; break;
			case ERole::Total: _total = true; _countNode = s.Node; break;
			case ERole::Rate: _rate = true; _ratePeriod = s.RatePeriod; _countNode = s.Node; break;
			case ERole::Good: _good = true; break;
			case ERole::Reject: _reject = true; break;
			case ERole::Reason: break;
			}
		}
		_widths.emplace_back( TimePoint::min(), _config.Width );
		auto& c = _core;
		let from = opening.From;
		c.At = c.Since = from;
		c.Row = Next( from );
		optional<EState> state;
		for( let& b : opening.Bounds ){
			let p = _roles.find( b.node_index() );
			let t = primaryTime( b ).value_or( from );
			if( p==_roles.end() || b.status()==UA_STATUSCODE_BADBOUNDNOTFOUND || t>from )//a bounded read's closing bound is not the opening's.
				continue;
			_pruned[b.node_index()] = t;
			let s = sample( b, t );
			let n = s.Good ? number( s ) : nullopt;
			let counted = b.has_heartbeat() ? UADateTime{ UA_DateTime{b.heartbeat()} }.Time() : t;//a heartbeat's own time is the timer's, not the count's.
			let roles = p->second;
			if( roles & bit(ERole::State) )
				state = !s.Good ? EState::Unknown : matches( _running, s.Value ) ? EState::Running : EState::Stopped;
			if( (roles & bit(ERole::Total)) && n ){
				c.Last[TotalCount] = *n;
				c.LastCount = counted;
			}
			if( (roles & bit(ERole::Good)) && n )
				c.Last[GoodCount] = *n;
			if( (roles & bit(ERole::Reject)) && n )
				c.Last[RejectCount] = *n;
			if( roles & bit(ERole::Rate) )
				c.Rate = n;
			if( !_state && (roles & (bit(ERole::Total)|bit(ERole::Rate))) ){//the count is the state.
				state = n ? EState::Running : EState::Unknown;
				if( n && !((roles & bit(ERole::Rate)) && *n>0) )
					c.Quiet = counted;
			}
			if( roles & bit(ERole::Reason) )
				c.Reason = n ? std::llround( *n ) : 0;
		}
		for( let& v : opening.LastGood ){
			let p = _roles.find( v.node_index() );
			let n = p==_roles.end() ? nullopt : number( sample(v, from) );
			for( let [role, i] : Counters ){
				if( n && (p->second & bit(role)) && !c.Last[i] )
					c.Last[i] = *n;
			}
		}
		c.QuietCode = Code( c );
		if( c.Quiet && _config.IdleTimeout && from-*c.Quiet>=*_config.IdleTimeout ){//idle before it opened:  stopped from here, nothing before From being its to rewrite.
			state = EState::Stopped;
			c.Quiet.reset();
		}
		c.State = state.value_or( EState::Unknown );
		if( c.State!=EState::Running )
			c.Quiet.reset();
		auto loaded = move( opening.Open );
		if( loaded ){
			loaded->End.reset();
			_told.push_back( *loaded );
		}
		if( loaded && loaded->Start<from && c.State!=EState::Running && loaded->Unknown==(c.State==EState::Unknown) ){
			if( !loaded->Unknown && !loaded->Code )
				loaded->Code = Code( c );
			c.Open = loaded;
			c.Since = loaded->Start;
			_rows.push_back( *loaded );
		}
		else{
			if( loaded && loaded->Start<from ){//the bound says otherwise:  it ended here.
				loaded->End = from;
				_rows.push_back( *loaded );
			}
			if( c.State!=EState::Running ){
				c.Open = Stop{ from, nullopt, c.State==EState::Stopped ? Code(c) : nullopt, c.State==EState::Unknown };
				_rows.push_back( *c.Open );
			}
		}
		_snapshots.push_back( c );
	}

	α Engine::Impl::WidthAt( TimePoint t )Ι->Duration{
		auto y = _widths.front().second;
		for( let& [from, width] : _widths ){
			if( from>t )
				break;
			y = width;
		}
		return y;
	}
	α Engine::Impl::Next( TimePoint start )Ι->Interval{
		let width = WidthAt( start );
		return Interval{ start, boundary(start, width)+width-start };
	}
	//Good is the Good signal's, else the total less the rejects;  Reject the Reject signal's, else the total less the good;
	//the total the Total signal's or the Rate's, else the two summed.
	α Engine::Impl::Derive( const Core& c )Ι->Interval{
		auto y = c.Row;
		let t = c.Counts[TotalCount], g = c.Counts[GoodCount], r = c.Counts[RejectCount];
		y.Total = _total || _rate ? t : g+r;
		y.Good = _good ? g : y.Total-r;
		y.Reject = _reject ? r : y.Total-y.Good;
		return y;
	}
	α Engine::Impl::Tally( Core& c, Duration d )Ι->void{
		switch( c.State ){
		case EState::Running: c.Row.Run += d; break;
		case EState::Stopped: c.Row.Stop += d; break;
		case EState::Unknown: c.Row.Unknown += d; break;
		}
		if( c.Rate && *c.Rate>0 ){//stepped:  the value times the time it held, over the period.  A negative rate counts nothing.
			c.Counts[TotalCount] += *c.Rate*( (double)d.count()/(double)_ratePeriod.count() );
			c.LastCount = c.At+d;
		}
	}
	α Engine::Impl::Build( TimePoint now )Ι->LiveState{
		LiveState y{ _core.State, _core.Since, _core.Open, Small(now), {}, _processed, now, _offset, {_done.begin(), _done.end()} };
		auto c = _core;
		while( c.At<now ){
			let next = std::min( now, c.Row.End() );
			Tally( c, next-c.At );
			c.At = next;
			if( c.At==c.Row.End() ){
				y.Rows.push_back( Derive(c) );
				c.Row = Next( c.At );
				c.Counts = {};
			}
		}
		if( c.At>c.Row.Start ){
			y.Rows.push_back( Derive(c) );
			y.Rows.back().Width = c.At-c.Row.Start;
		}
		y.LastCount = c.LastCount;
		return y;
	}

	α Engine::Impl::Replay( const vector<Hist::ReadValue>& page )ι->void{
		ul _{ _mutex };
		if( _ended )
			return;
		for( let& v : page ){
			if( v.Bound || v.Value.has_heartbeat() || !_roles.contains(v.Value.node_index()) )
				continue;
			if( let t = primaryTime(v.Value); t )
				Place( sample(v.Value, *t) );
		}
		Run();
		Emit();
	}
	α Engine::Impl::Live( Duration offset )ι->void{
		ul _{ _mutex };
		if( _ended || _live )
			return;
		_live = true;
		_offset = offset;
		Take( std::exchange(_queued, {}), nullopt );
		Settle();
		Emit();
	}
	α Engine::Impl::Finish( TimePoint end )ι->void{
		ul _{ _mutex };
		if( _ended || _live )
			return;
		Drain( end );//the stream is its clock, so what waited for the wall clock is applied.
		Advance( end );
		Write( end );
		Emit();
	}
	α Engine::Impl::Records( vector<Hist::Proto::HistoryRecord>&& records )ι->void{
		ul _{ _mutex };
		if( _ended )
			return;
		if( !_live ){
			std::ranges::move( records, std::back_inserter(_queued) );
			return;
		}
		Take( records, _clock->Now() );
		Settle();
		Emit();
	}
	α Engine::Impl::SetWidth( Duration width, SL sl )ε->void{
		ValidateWidth( width, sl );
		ul _{ _mutex };
		auto end = _core.Row.End();//the open interval, the one the engine's time is in, closes at its old end.
		for( let now = Now(); end<=now; end = Next(end).End() );
		std::erase_if( _widths, [end]( let& w ){ return w.first>=end; } );//a change that was still to come.
		let old = _widths.back().second;
		if( old==width )
			return;
		for( ; end!=boundary(end, width); end = boundary(end, old)+old );//the rows between keep the old width.
		_widths.emplace_back( end, width );
	}
	α Engine::Impl::End()ι->void{
		ul _{ _mutex };
		_ended = true;
		if( _timer )
			_clock->Cancel( std::exchange(_timer, 0) );
	}

	α Engine::Impl::Take( const vector<Hist::Proto::HistoryRecord>& records, optional<TimePoint> arrival )ι->void{
		for( let& r : records ){
			if( r.has_modification() )
				Modify( r.modification() );
			else if( r.has_value() && !r.value().has_heartbeat() && _roles.contains(r.value().node_index()) ){//a heartbeat repeats a value and changes nothing.
				let t = primaryTime( r.value() );
				if( !t )
					continue;
				if( arrival && isGood(r.value().status()) )
					Clocked( *t, *arrival );
				Place( sample(r.value(), *t) );
			}
		}
		Run();
	}
	//The source offset, from a Good value as it arrives:  the least of the last few, since a value that was held back says
	//the offset is no more than its own.  One more than Skew above the offset, an unset clock's, or more than Settle below
	//it, which would write rows before they end, counts only once the next agrees with it:  the source's clock stepped.
	α Engine::Impl::Clocked( TimePoint t, TimePoint arrival )ι->void{
		let offset = arrival-t;
		auto near = []( Duration a, Duration b ){ return a-b<=Skew && b-a<=Skew; };
		if( offset-_offset<=Skew && _offset-offset<=_config.Settle ){
			if( _offsets.size()==Recent )
				_offsets.erase( _offsets.begin() );
			_offsets.push_back( offset );
		}
		else if( _doubted && near(offset, *_doubted) )
			_offsets = { *_doubted, offset };
		else{
			_doubted = offset;
			return;
		}
		_doubted.reset();
		_offset = *std::ranges::min_element( _offsets );
	}
	α Engine::Impl::Place( Sample&& s )ι->void{
		if( s.Time<_processed )
			return Late( s.Node, s.Time );
		if( s.Time<_core.At )
			_dirty = std::min( _dirty.value_or(s.Time), s.Time );
		_samples.insert( std::ranges::upper_bound(_samples, s.Time, {}, &Sample::Time), move(s) );
	}
	//By the time it targets:  an insert, a replacement or a delete of the node's record there.
	α Engine::Impl::Modify( const Hist::Proto::Modification& m )ι->void{
		let node = m.node_index();
		if( !_roles.contains(node) )
			return;
		let t = UADateTime{ UA_DateTime{m.target_source_ts()} }.Time();
		if( t<_processed )
			return Late( node, t );
		auto changed = std::erase_if( _samples, [&]( const Sample& s ){ return s.Node==node && s.Time==t; } )>0;
		if( m.has_new_value() && m.update_type()!=Hist::Proto::UPDATE_TYPE_DELETE ){
			auto s = sample( m.new_value(), t );
			s.Node = node;
			_samples.insert( std::ranges::upper_bound(_samples, t, {}, &Sample::Time), move(s) );
			changed = true;
		}
		if( changed && t<=_core.At )
			_dirty = std::min( _dirty.value_or(t), t );
	}
	//To the node's next record the engine knows of:  one it no longer holds, else one it does, else its time now.
	α Engine::Impl::Late( uint32 node, TimePoint t )ι->void{
		if( let p = _pruned.find(node); p!=_pruned.end() && p->second>t )
			return Ask( node, {t, p->second} );
		let next = std::ranges::find_if( _samples, [&, horizon=Horizon()]( const Sample& s ){ return s.Node==node && s.Time>t && s.Time<=horizon; } );
		Ask( node, {t, next==_samples.end() ? Now() : next->Time} );
	}
	α Engine::Impl::Ask( uint32 node, Span span )ι->void{
		let p = std::ranges::find( _rebuilds, node, &std::pair<uint32,Span>::first );
		if( p==_rebuilds.end() )
			_rebuilds.emplace_back( node, span );
		else
			p->second = { std::min(p->second.Start, span.Start), std::max(p->second.End, span.End) };
	}
	α Engine::Impl::Run()ι->void{
		if( let t = std::exchange(_dirty, nullopt); t )
			return Rerun( *t );
		Drain( Horizon() );
	}
	α Engine::Impl::Drain( TimePoint through )ι->void{
		for( auto p = std::ranges::lower_bound(_samples, _core.At, {}, &Sample::Time); p!=_samples.end() && p->Time<=through; ++p ){
			if( p->Applied )
				continue;
			Advance( p->Time );
			Apply( *p );
			p->Applied = true;
		}
	}
	//From the newest snapshot at or before t:  the samples from there on applied again, in time order.  An idle stop found
	//after that snapshot is found again, or not, so from before it began:  the rows it took run time from are made again too.
	α Engine::Impl::Rerun( TimePoint t )ι->void{
		auto at = [this]( TimePoint time ){
			auto y = _snapshots.end();
			while( std::prev(y)!=_snapshots.begin() && std::prev(y)->At>time )
				--y;
			return std::prev( y );
		};
		auto p = at( t );
		if( _config.IdleTimeout && !_state ){
			for( let& s : _rows | std::views::reverse ){
				if( !s.Unknown && s.Start<p->At && s.Start+*_config.IdleTimeout>p->At )
					p = at( s.Start );
			}
		}
		let to = _core.At;
		_core = *p;
		_snapshots.erase( std::next(p), _snapshots.end() );
		let from = _core.At;
		std::erase_if( _done, [from]( const Interval& row ){ return row.End()>from; } );
		std::erase_if( _rows, [from]( const Stop& s ){ return !s.End || *s.End>from; } );
		if( _core.Open ){
			if( !_rows.empty() && _rows.back().Start==_core.Open->Start )//a record at `from` ended it.
				_rows.back() = *_core.Open;
			else
				_rows.push_back( *_core.Open );
		}
		let horizon = Horizon();
		for( auto& s : _samples ){
			if( s.Time<from )
				continue;
			s.Applied = s.Time<=horizon;
			if( !s.Applied )
				continue;
			Advance( s.Time );
			Apply( s );
		}
		Advance( to );
	}
	//Tallies to `to`:  a snapshot at each minute it crosses and a row at each interval's end, which are minutes too.  The
	//idle timeout resolves as it's crossed, so a snapshot before it holds the machine as running and a rerun decides it again.
	α Engine::Impl::Advance( TimePoint to )ι->void{
		auto& c = _core;
		while( c.At<to ){
			auto next = std::min( {to, c.Row.End(), minute(c.At)+minutes{1}} );
			let due = IdleDue( c );
			if( due && *due<next )
				next = std::max( *due, c.At );
			Tally( c, next-c.At );
			c.At = next;
			if( due && *due<=c.At )
				Idle();
			if( c.At==c.Row.End() )
				Roll();
			if( c.At==minute(c.At) )
				_snapshots.push_back( c );
		}
	}
	α Engine::Impl::Roll()ι->void{
		auto& c = _core;
		if( auto row = Derive(c); row.End()>_processed )
			_done.push_back( move(row) );
		c.Row = Next( c.At );
		c.Counts = {};
		if( !_live )//the stream is the clock:  a record past its end was seen.  A row the idle timeout may yet take back waits for it.
			Write( c.Quiet.value_or(c.At) );
	}
	//The timeout passed with no count:  stopped since the last one, so what was tallied as run since then is stop time, and
	//the rows written since are to be rebuilt.
	α Engine::Impl::Idle()ι->void{
		auto& c = _core;
		const Stop stop{ *c.Quiet, nullopt, c.QuietCode };
		if( let from = std::max(stop.Start, c.Row.Start); c.At>from ){
			c.Row.Run -= c.At-from;
			c.Row.Stop += c.At-from;
		}
		c.State = EState::Stopped;
		c.Since = stop.Start;
		c.Open = stop;
		c.Quiet.reset();
		for( auto& row : _done ){
			if( row.End()>stop.Start ){
				let d = row.End()-std::max( stop.Start, row.Start );
				row.Run -= d;
				row.Stop += d;
			}
		}
		_rows.push_back( stop );
		if( stop.Start<_processed && _idleAsked!=stop.Start ){
			_idleAsked = stop.Start;
			Ask( _countNode, {stop.Start, Now()} );
		}
	}
	α Engine::Impl::Apply( const Sample& s )ι->void{
		auto& c = _core;
		let roles = _roles.find( s.Node )->second;
		let n = s.Good ? number( s ) : nullopt;
		if( roles & bit(ERole::State) )
			Enter( !s.Good ? EState::Unknown : matches(_running, s.Value) ? EState::Running : EState::Stopped, s.Time );
		for( let [role, i] : Counters ){
			if( !(roles & bit(role)) )
				continue;
			if( n ){//below the last is a reset, which counts whole;  the first has nothing to count against.
				let d = !c.Last[i] ? 0 : *n>=*c.Last[i] ? *n-*c.Last[i] : *n;
				c.Last[i] = *n;
				c.Counts[i] += d;
				if( d>0 && (i==TotalCount || !(_total || _rate)) )
					c.LastCount = s.Time;
				if( i==TotalCount && !_state )
					Counted( s.Time, d>0, false );
			}
			else if( i==TotalCount && !_state )
				Enter( EState::Unknown, s.Time );
		}
		if( roles & bit(ERole::Rate) ){
			c.Rate = n;
			if( !_state && n )
				Counted( s.Time, *n>0, true );
			else if( !_state )
				Enter( EState::Unknown, s.Time );
		}
		if( roles & bit(ERole::Reason) ){
			c.Reason = n ? std::llround( *n ) : 0;
			if( c.Quiet && !c.QuietCode )
				c.QuietCode = Code( c );
			if( c.Reason && c.Open && !c.Open->Unknown && !c.Open->Code ){//the first value above zero before the stop's end.
				c.Open->Code = c.Reason;
				_rows.back().Code = c.Reason;
			}
		}
	}
	α Engine::Impl::Enter( EState state, TimePoint t )ι->void{
		auto& c = _core;
		if( state==c.State )
			return;
		if( c.Open ){
			if( t>c.Open->Start ){
				c.Open->End = t;
				_rows.back() = *c.Open;
			}
			else//began and ended at one instant, a marker with its value:  no row.
				_rows.pop_back();
			c.Open.reset();
		}
		c.State = state;
		c.Since = t;
		c.Quiet.reset();
		if( state!=EState::Running ){
			c.Open = Stop{ t, nullopt, state==EState::Stopped ? Code(c) : nullopt, state==EState::Unknown };
			_rows.push_back( *c.Open );
		}
	}
	//With no State signal, a Good record of the count:  one that counts runs the machine, and one that counts nothing
	//after unknown time reads stopped.  A rate above zero counts all the while, so its idle timeout runs from its fall.
	α Engine::Impl::Counted( TimePoint t, bool positive, bool rate )ι->void{
		auto& c = _core;
		if( positive ){
			Enter( EState::Running, t );
			c.Quiet = rate ? nullopt : optional<TimePoint>{ t };
			c.QuietCode = Code( c );
		}
		else if( c.State==EState::Unknown )
			Enter( EState::Stopped, t );
		else if( rate && c.State==EState::Running && !c.Quiet ){
			c.Quiet = t;
			c.QuietCode = Code( c );
		}
	}
	α Engine::Impl::Write( TimePoint through )ι->void{
		for( ; !_done.empty() && _done.front().End()<=through; _done.pop_front() ){
			_processed = _done.front().End();
			_sink->Closed( _done.front() );
		}
		while( _snapshots.size()>1 && _snapshots[1].At<=_processed )
			_snapshots.pop_front();
	}
	α Engine::Impl::Settle()ι->void{
		Run();//what waited for the clock.
		let now = _clock->Now()-_offset;//not Now():  a record ahead of the clock moves the engine's time, and no row is due for that.
		if( let due = IdleDue(_core); due && *due<=now )
			Advance( *due );//found by the clock, not a Settle later:  a record still on its way reruns it.
		let through = now-_config.Settle;
		do{//a row at a time, so a quiet year's rows are written as they are made.
			Advance( std::min(through, _core.Row.End()) );
			Write( through );
		}while( _core.At<through );
		Arm( now );
	}
	//One timer, for whichever is next:  an interval's end and Settle, the idle timeout, or the stop in progress reaching SmallStop.
	α Engine::Impl::Arm( TimePoint now )ι->void{
		let& c = _core;
		auto due = ( _done.empty() ? c.Row.End() : _done.front().End() )+_config.Settle;
		if( let idle = IdleDue(c); idle )
			due = std::min( due, *idle );
		if( Small(now) )
			due = std::min( due, c.Open->Start+_config.SmallStop );
		let wall = due+_offset;
		if( _timer && _timerDue<=wall )
			return;
		if( _timer )
			_clock->Cancel( _timer );
		_timerDue = wall;
		_timer = _clock->Schedule( wall, [weak=weak_from_this(), wall]{
			if( auto self = weak.lock(); self )
				self->Tick( wall );
		} );
	}
	α Engine::Impl::Tick( TimePoint due )ι->void{
		ul _{ _mutex };
		if( _timerDue==due )
			_timer = 0;
		if( _ended || !_live )
			return;
		Settle();
		Emit();
	}
	//What the call changed, to the sink:  the stop rows by their difference from what it was told, then the rebuilds and the live state.
	α Engine::Impl::Emit()ι->void{
		uint i{};
		while( i<_told.size() && i<_rows.size() && _told[i]==_rows[i] )
			++i;
		if( i<_told.size() ){
			let& was = _told[i];
			let ended = i+1==_told.size() && i<_rows.size() && !was.End && _rows[i].End && was.Start==_rows[i].Start && was.Code==_rows[i].Code && was.Unknown==_rows[i].Unknown;
			if( ended )
				_sink->Closed( _rows[i++] );
			else{
				_sink->Rewritten( i<_rows.size() ? std::min(was.Start, _rows[i].Start) : was.Start, {_rows.begin()+i, _rows.end()} );
				i = _rows.size();
			}
		}
		for( ; i<_rows.size(); ++i )
			_sink->Opened( _rows[i] );

		let floor = _snapshots.front().At;
		std::erase_if( _rows, [floor]( const Stop& s ){ return s.End && *s.End<floor; } );
		_told = _rows;
		let kept = std::ranges::lower_bound( _samples, floor, {}, &Sample::Time );
		for( auto p=_samples.begin(); p!=kept; ++p ){
			auto& newest = _pruned[p->Node];
			newest = std::max( newest, p->Time );
		}
		_samples.erase( _samples.begin(), kept );
		while( _widths.size()>1 && _widths[1].first<=floor )
			_widths.erase( _widths.begin() );

		for( let& [node, span] : std::exchange(_rebuilds, {}) )
			_sink->Rebuild( node, span );
		let now = Now();
		if( LiveKey key{_core.State, _core.Since, _core.Open, Small(now)}; _liveKey!=key ){
			_liveKey = key;
			_sink->Live( Build(now) );
		}
	}

	Engine::Engine( EngineConfig config, Opening opening, sp<ISink> sink, sp<IClock> clock, SL sl )ε:
		_impl{ ms<Impl>(move(config), move(opening), move(sink), move(clock), sl) }
	{}
	Engine::~Engine(){ _impl->End(); }
	α Engine::Replay( const vector<Hist::ReadValue>& page )ι->void{ _impl->Replay( page ); }
	α Engine::Live( Duration offset )ι->void{ _impl->Live( offset ); }
	α Engine::Finish( TimePoint end )ι->void{ _impl->Finish( end ); }
	α Engine::Records( vector<Hist::Proto::HistoryRecord>&& records )ι->void{ _impl->Records( move(records) ); }
	α Engine::SetWidth( Duration width, SL sl )ε->void{ _impl->SetWidth( width, sl ); }
	α Engine::End()ι->void{ _impl->End(); }
	α Engine::State()Ι->LiveState{ return _impl->State(); }
	α Engine::Processed()Ι->TimePoint{ return _impl->Processed(); }
}