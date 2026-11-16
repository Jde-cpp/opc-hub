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

	//A column's json list:  `value` itself, or the list its text spells, which `parsed` holds.
	Ω listOf( const jvalue& value, jvalue& parsed, SL sl )ε->const jarray&{
		if( !value.is_string() )
			return Json::AsArray( value, sl );
		parsed = Json::ParseValue( value.get_string(), sl );
		return Json::AsArray( parsed, sl );
	}

	α Oee::ParseRunning( const jvalue& running, SL sl )ε->vector<StateValue>{
		vector<StateValue> y;
		if( running.is_null() || (running.is_string() && running.get_string().empty()) )
			return { StateValue{1.0} };
		try{
			jvalue parsed;
			for( let& v : listOf(running, parsed, sl) ){
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
					if( !std::ranges::contains(y.Under, member->Id) )
						y.Under.push_back( member->Id );
					walk( graph, *member, path, y, sl );//again when reached twice:  its machines are, which Twice records.
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
	α Oee::Lines::ReachLines( const Graph& graph, uint32 lineId, SL sl )ε->vector<uint32>{
		let line = find( graph, lineId );
		REFUSE_IF( !line, "Line {} is not one of the gateway's.", lineId );
		vector<uint32> path;
		Walk y;
		walk( graph, *line, path, y, sl );
		return move( y.Under );
	}

	constexpr std::array<sv,6> _kinds{ "Machine", "Line", "Zone", "Wall", "Aisle", "Label" };
	α Oee::Floors::ToString( EKind kind )ι->sv{ return (uint8)kind<_kinds.size() ? _kinds[(uint8)kind] : sv{ "?" }; }
	α Oee::Floors::ToKind( sv name )ι->optional<EKind>{
		let p = std::ranges::find( _kinds, name );
		return p==_kinds.end() ? nullopt : optional<EKind>{ (EKind)(p-_kinds.begin()) };
	}
	α Oee::Floors::ParsePoints( const jvalue& points, SL sl )ε->vector<Point>{
		vector<Point> y;
		try{
			jvalue parsed;
			for( let& v : listOf(points, parsed, sl) ){
				let& pair = Json::AsArray( v, sl );
				THROW_IF( pair.size()!=2 || !pair[0].is_number() || !pair[1].is_number(), "{} is not an [x, y] pair of numbers", serialize(v) );
				Point point{ pair[0].to_number<double>(), pair[1].to_number<double>() };
				THROW_IF( !std::isfinite(point.X) || !std::isfinite(point.Y), "{} is not finite", serialize(v) );
				y.push_back( point );
			}
		}
		catch( const Jde::Exception& e ){
			REFUSE( "points must be a json list of [x, y] pairs in metres from the floor's top-left, such as [[3,3],[9,3],[9,7],[3,7]], not {}:  {}", serialize(points), e.what() );
		}
		REFUSE_IF( y.empty(), "points lists no point." );
		return y;
	}
	α Oee::Floors::ToJson( std::span<const Point> points )ι->string{//by hand:  Boost.JSON spells a double 3E0.
		string y{ "[" };
		for( let& p : points )
			y += Ƒ( "{}[{},{}]", y.size()>1 ? "," : "", p.X, p.Y );
		return y+"]";
	}
	α Oee::Floors::ValidateFloor( const Floor& floor, SL sl )ε->void{
		REFUSE_IF( !std::isfinite(floor.Width) || floor.Width<=0, "width must be above zero:  it is the floor's extent in metres from its left." );
		REFUSE_IF( !std::isfinite(floor.Height) || floor.Height<=0, "height must be above zero:  it is the floor's extent in metres from its top." );
		REFUSE_IF( !std::isfinite(floor.Grid) || floor.Grid<=0, "grid must be above zero:  it is the editor's snap in metres." );
	}
	namespace Oee::Floors{
		Ω inside( const Floor& floor, std::span<const Point> points, SL sl )ε->void{
			for( let& p : points )
				REFUSE_IF( p.X<0 || p.X>floor.Width || p.Y<0 || p.Y>floor.Height, "Point [{}, {}] is outside the floor, {} by {} metres from its top-left.", p.X, p.Y, floor.Width, floor.Height );
		}
		Ω lineName( const Lines::Graph& graph, uint32 id )ι->string{
			let p = find( graph, id );
			return p ? p->Name : Ƒ( "{}", id );
		}
	}
	α Oee::Floors::ValidateInside( const Floor& floor, std::span<const Shape> shapes, SL sl )ε->void{
		for( let& shape : shapes )
			inside( floor, shape.Points, sl );
	}
	α Oee::Floors::ValidatePoints( const Floor& floor, const Shape& shape, SL sl )ε->void{
		using enum EKind;
		REFUSE_IF( !shape.Kind, "kind is Machine, Line, Zone, Wall, Aisle or Label." );
		let kind = *shape.Kind;
		let name = ToString( kind );
		let& points = shape.Points;
		let count = points.size();
		if( kind==Machine || kind==Line || kind==Zone ){
			REFUSE_IF( count<3, "A {} shape is a closed polygon of three points or more, not {}.", name, count );
			double twice{};//the shoelace:  twice the signed area.
			for( uint i=0; i<count; ++i ){
				let& a = points[i];
				let& b = points[(i+1)%count];
				twice += a.X*b.Y-b.X*a.Y;
			}
			REFUSE_IF( std::abs(twice)<1e-9, "A {} shape encloses no area:  a polygon's points can't all lie on one line.", name );
		}
		else if( kind==Wall || kind==Aisle ){
			REFUSE_IF( count<2, "A {} is a polyline of two points or more, not {}.", name, count );
			REFUSE_IF( std::ranges::all_of(points, [&]( const Point& p ){ return p==points.front(); }), "A {} has no length:  its points are all one.", name );
		}
		else
			REFUSE_IF( count!=1, "A Label is one point, not {}.", count );
		inside( floor, points, sl );
	}
	α Oee::Floors::ValidateShape( const Lines::Graph& graph, const Floor& floor, const Shape& shape, std::span<const Shape> others, SL sl )ε->void{
		using enum EKind;
		REFUSE_IF( !shape.Kind, "kind is Machine, Line, Zone, Wall, Aisle or Label." );
		let kind = *shape.Kind;
		if( kind==Machine ){
			REFUSE_IF( !shape.MachineId, "A Machine shape names its machine:  oeeMachineId is required." );
			REFUSE_IF( shape.MemberLineId, "A Machine shape names a machine, not a line:  memberLineId is a Line shape's." );
			let machine = machineName( graph, *shape.MachineId );
			REFUSE_IF( !std::ranges::contains(Lines::Reach(graph, floor.LineId, sl), *shape.MachineId), "Machine '{}' isn't one line '{}' reaches:  a floor draws the machines of its line.", machine, lineName(graph, floor.LineId) );
			REFUSE_IF( std::ranges::any_of(others, [&]( const Shape& o ){ return o.Id!=shape.Id && o.MachineId==shape.MachineId; }), "Machine '{}' is on the floor already:  a machine appears once on a floor.", machine );
		}
		else if( kind==Line ){
			REFUSE_IF( !shape.MemberLineId, "A Line shape names its line:  memberLineId is required." );
			REFUSE_IF( shape.MachineId, "A Line shape names a line, not a machine:  oeeMachineId is a Machine shape's." );
			let line = lineName( graph, *shape.MemberLineId );
			REFUSE_IF( *shape.MemberLineId==floor.LineId, "Line '{}' is the floor's own:  a Line shape names a line the floor's line reaches, and the floor is drawn around it.", line );
			REFUSE_IF( !std::ranges::contains(Lines::ReachLines(graph, floor.LineId, sl), *shape.MemberLineId), "Line '{}' isn't one line '{}' reaches:  a floor draws the lines of its line.", line, lineName(graph, floor.LineId) );
			REFUSE_IF( std::ranges::any_of(others, [&]( const Shape& o ){ return o.Id!=shape.Id && o.MemberLineId==shape.MemberLineId; }), "Line '{}' is on the floor already:  a line appears once on a floor.", line );
		}
		else
			REFUSE_IF( shape.MachineId || shape.MemberLineId, "A {} shape names no machine or line:  oeeMachineId is a Machine shape's and memberLineId a Line shape's.", ToString(kind) );
		ValidatePoints( floor, shape, sl );
	}
}