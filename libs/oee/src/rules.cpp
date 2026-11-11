#include <jde/oee/rules.h>
#include <jde/fwk/exceptions/refuse.h>

#define let const auto

namespace Jde::Opc{
	constexpr ELogTags _tags{ ELogTags::App };

	α Oee::ToString( ERole role )ι->sv{
		constexpr std::array<sv,6> names{ "State", "Total", "Rate", "Good", "Reject", "Reason" };
		return (uint8)role<names.size() ? names[(uint8)role] : sv{ "?" };
	}
	α Oee::ToString( EState state )ι->sv{
		constexpr std::array<sv,3> names{ "running", "stopped", "unknown" };
		return (uint8)state<names.size() ? names[(uint8)state] : sv{ "?" };
	}
	constexpr std::array<sv,4> _levels{ "Site", "Area", "Line", "Cell" };
	α Oee::ToString( ELevel level )ι->sv{ return (uint8)level<_levels.size() ? _levels[(uint8)level] : sv{ "?" }; }
	α Oee::ToLevel( sv name )ι->optional<ELevel>{
		let p = std::ranges::find( _levels, name );
		return p==_levels.end() ? nullopt : optional<ELevel>{ (ELevel)(p-_levels.begin()) };
	}
	constexpr std::array<sv,3> _categories{ "Unplanned", "Planned", "Excluded" };
	α Oee::ToString( ECategory category )ι->sv{ return (uint8)category<_categories.size() ? _categories[(uint8)category] : sv{ "?" }; }
	α Oee::ToRole( sv name )ι->optional<ERole>{
		for( uint8 i=0; i<=(uint8)ERole::Reason; ++i ){
			if( ToString((ERole)i)==name )
				return (ERole)i;
		}
		return nullopt;
	}
	α Oee::ToCategory( sv name )ι->optional<ECategory>{
		let p = std::ranges::find( _categories, name );
		return p==_categories.end() ? nullopt : optional<ECategory>{ (ECategory)(p-_categories.begin()) };
	}
	α Oee::Find( std::span<const Signal> signals, ERole role )ι->const Signal*{
		let p = std::ranges::find( signals, role, &Signal::Role );
		return p==signals.end() ? nullptr : &*p;
	}
	α Oee::HasCount( std::span<const Signal> signals )ι->bool{
		return std::ranges::any_of( signals, []( const Signal& s ){ return s.Role==ERole::Total || s.Role==ERole::Rate || s.Role==ERole::Good || s.Role==ERole::Reject; } );
	}
	α Oee::MeasuresQuality( std::span<const Signal> signals )ι->bool{
		let total = Find( signals, ERole::Total ) || Find( signals, ERole::Rate );
		return total+(bool)Find( signals, ERole::Good )+(bool)Find( signals, ERole::Reject )>=2;
	}

	α Oee::ValidateWidth( Duration width, SL sl )ε->void{
		let listed = std::ranges::any_of( Widths, [width]( uint8_t m ){ return width==minutes{m}; } );
		REFUSE_IF( !listed, "width {} is not 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30 or 60 minutes, the widths that divide an hour.", Chrono::ToString(width) );
	}

	α Oee::ParseRunning( const jvalue& running, SL sl )ε->vector<StateValue>{
		vector<StateValue> y;
		if( running.is_null() || (running.is_string() && running.get_string().empty()) )
			return { StateValue{1.0} };
		try{
			jvalue parsed;
			const jvalue* p = &running;
			if( running.is_string() ){
				parsed = Json::ParseValue( running.get_string(), sl );
				p = &parsed;
			}
			for( let& v : Json::AsArray(*p, sl) ){
				if( v.is_bool() )
					y.emplace_back( v.get_bool() ? 1.0 : 0.0 );
				else if( v.is_number() )
					y.emplace_back( v.to_number<double>() );
				else if( v.is_string() )
					y.emplace_back( string{v.get_string()} );
				else
					THROW( "{} is not a Boolean, a number or a string", serialize(v) );
			}
		}
		catch( const Jde::Exception& e ){
			REFUSE( "running must be a json list of the State values that mean running, such as [true] or [2,3], not {}:  {}", serialize(running), e.what() );
		}
		REFUSE_IF( y.empty(), "running lists no value, so the machine would never run." );
		return y;
	}

	α Oee::ValidateSignals( std::span<const Signal> signals, SL sl )ε->void{
		for( auto p=signals.begin(); p!=signals.end(); ++p ){
			REFUSE_IF( (uint8)p->Role>(uint8)ERole::Reason, "role {} is not State, Total, Rate, Good, Reject or Reason.", (uint8)p->Role );
			REFUSE_IF( std::ranges::find(signals.begin(), p, p->Role, &Signal::Role)!=p, "The machine has two {} signals:  a role takes one.", ToString(p->Role) );
			REFUSE_IF( p->Role==ERole::State && p->Running.empty(), "The State signal's running lists no value, so the machine would never run." );
			REFUSE_IF( p->Role==ERole::Rate && p->RatePeriod<=Duration::zero(), "The Rate signal's ratePeriod must be above zero:  it is the rate's time unit." );
		}
		let total = Find( signals, ERole::Total ), rate = Find( signals, ERole::Rate );
		REFUSE_IF( total && rate, "The machine has a Total and a Rate signal:  a count is one or the other." );
		REFUSE_IF( !Find(signals, ERole::State) && !total && !rate, "The machine has neither a State signal nor a count, a Total or a Rate:  it needs one, or both." );
	}

	α Oee::ValidateThresholds( Duration width, optional<Duration> idealCycle, optional<Duration> idleTimeout, SL sl )ε->void{
		ValidateWidth( width, sl );
		REFUSE_IF( idealCycle && *idealCycle<=Duration::zero(), "idealCycle must be above zero:  it is the time one unit takes." );
		REFUSE_IF( idleTimeout && *idleTimeout<=Duration::zero(), "idleTimeout must be above zero." );
	}
	α Oee::ValidateMachine( std::span<const Signal> signals, Duration width, optional<Duration> idealCycle, optional<Duration> idleTimeout, SL sl )ε->void{
		ValidateSignals( signals, sl );
		ValidateThresholds( width, idealCycle, idleTimeout, sl );
		REFUSE_IF( !idleTimeout && !Find(signals, ERole::State), "The machine has no State signal, so its counts are its state:  it needs an idleTimeout, how long without a count is a stop." );
	}

	namespace Oee{
		using Lines::Graph;
		Ω find( const Graph& graph, uint32 id )ι->const Lines::Line*{
			let p = std::ranges::find( graph.Lines, id, &Lines::Line::Id );
			return p==graph.Lines.end() ? nullptr : &*p;
		}
		Ω machineName( const Graph& graph, uint32 id )ι->string{
			let p = graph.Machines.find( id );
			return p==graph.Machines.end() ? Ƒ( "{}", id ) : p->second;
		}
		//The machines under `line`, depth first in member order, each once, with the lines under it.  `path` is the lines from
		//the one asked for down to this one.
		struct Walk final{ vector<uint32> Machines; vector<uint32> Under; optional<uint32> Twice; };
		Ω walk( const Graph& graph, const Lines::Line& line, vector<uint32>& path, Walk& y, SL sl )ε->void{
			path.push_back( line.Id );
			for( let& m : graph.Members ){
				if( m.LineId!=line.Id )
					continue;
				if( m.MachineId ){
					if( !std::ranges::contains(y.Machines, *m.MachineId) )
						y.Machines.push_back( *m.MachineId );
					else if( !y.Twice )
						y.Twice = *m.MachineId;
				}
				else if( let member = m.MemberLineId ? find(graph, *m.MemberLineId) : nullptr; member ){
					REFUSE_IF( std::ranges::contains(path, member->Id), "Line '{}' would reach itself through '{}'.", member->Name, line.Name );
					y.Under.push_back( member->Id );
					walk( graph, *member, path, y, sl );
				}
			}
			path.pop_back();
		}
	}

	α Oee::Lines::Validate( const Graph& graph, SL sl )ε->void{
		for( let& m : graph.Members ){
			let line = find( graph, m.LineId );
			REFUSE_IF( !line, "Line {} is not one of the gateway's.", m.LineId );
			REFUSE_IF( m.MachineId.has_value()==m.MemberLineId.has_value(), "A member of '{}' is one machine or one line, not both and not neither.", line->Name );
			if( !m.MemberLineId )
				continue;
			let member = find( graph, *m.MemberLineId );
			REFUSE_IF( !member, "Line {}, a member of '{}', is not one of the gateway's.", *m.MemberLineId, line->Name );
			REFUSE_IF( member->Level<line->Level, "'{}' is {} {}, above '{}', {} {}:  a member's level is no higher than its line's.",
				member->Name, member->Level==ELevel::Area ? "an" : "a", ToString(member->Level), line->Name, line->Level==ELevel::Area ? "an" : "a", ToString(line->Level) );
		}
		flat_map<uint32,Walk> twice;
		for( let& line : graph.Lines ){
			vector<uint32> path;
			Walk y;
			walk( graph, line, path, y, sl );
			if( y.Twice )
				twice.emplace( line.Id, move(y) );
		}
		for( let& line : graph.Lines ){//the lowest line that does, which every line above it does too.
			let p = twice.find( line.Id );
			if( p==twice.end() || std::ranges::any_of(p->second.Under, [&]( uint32 id ){ return twice.contains(id); }) )
				continue;
			REFUSE( "'{}' would reach machine '{}' by two paths:  a machine counts once in a line.", line.Name, machineName(graph, *p->second.Twice) );
		}
	}
	α Oee::Lines::Reach( const Graph& graph, uint32 lineId, SL sl )ε->vector<uint32>{
		let line = find( graph, lineId );
		REFUSE_IF( !line, "Line {} is not one of the gateway's.", lineId );
		vector<uint32> path;
		Walk y;
		walk( graph, *line, path, y, sl );
		return move( y.Machines );
	}
}