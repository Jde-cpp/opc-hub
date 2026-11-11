#include "OeeMAwait.h"
#include <jde/fwk/exceptions/refuse.h>
#include <jde/access/Authorize.h>
#include <jde/ql/ops/MutationAwait.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/calendar/client/Calendars.h>
#include <jde/oee/rules.h>
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "../oee/OeeHost.h"
#include "HistGroupQLAwait.h"
#include "OeeQLAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	using QL::EMutationQL;
	using namespace OeeTable;
	using std::chrono::milliseconds;

	α OeeMAwait::IsApplicable( const QL::MutationQL& m )ι->bool{
		using enum EMutationQL;
		if( m.CommandName==RecomputeCommand || m.CommandName==CodeCommand || m.CommandName==UncodeCommand )
			return true;
		let table = m.TableName();
		if( table==Stops || table==Intervals || table==Codings )
			return true;
		if( table==Machines || table==Signals || table==Reasons || table==Lines || table==LineMembers )
			return m.Type==Create || m.Type==Update || m.Type==Delete || m.Type==Restore || m.Type==Purge;
		let& host = OeeHost::Instance();
		if( table==HistTable::Groups && (m.Type==Delete || m.Type==Purge) ){
			let id = m.FindId<uint32>();
			return id ? host.HolderOf( *id ).has_value() : host.HasMachines();//by its slug:  Query reads the row.
		}
		if( table==HistTable::GroupNodes && m.Type==Purge ){
			let id = m.FindId<uint32>();
			return id && host.UserOf( *id );
		}
		if( table==HistTable::TemplateNodes && m.Type==Purge )//Query reads the members made from it.
			return m.FindId<uint32>() && host.HasMachines();
		return false;
	}
	α OeeMAwait::Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>{
		return IsApplicable( m ) ? mu<OeeMAwait>( move(m), move(executer), sl ) : nullptr;
	}

	[[noreturn]] Ω notFound( string what, SL sl )ε->void{ throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "{}", move(what) }; }
	Ω rowKey( const QL::MutationQL& m, SL sl )ε->DB::Key{
		let key = m.FindKey();
		REFUSE_IF( !key, "'{}' needs the row's id or slug.", m.CommandName );
		return *key;
	}
	Ω requireId( const jobject& args, sv name, SL sl )ε->uint32{
		let p = args.if_contains( name );
		REFUSE_IF( !p || p->is_null(), "{} is required, a row's id.", name );
		let id = p->try_to_number<uint32>();
		REFUSE_IF( !id, "{} is a row's id, not {}.", name, serialize(*p) );
		return *id;
	}
	Ω requireName( const jobject& args, SL sl )ε->void{
		let p = args.if_contains( "name" );
		REFUSE_IF( !p || !p->is_string() || p->get_string().empty(), "name is required." );
	}
	Ω optionalId( const jobject& args, sv name, SL sl )ε->optional<uint32>{
		let p = args.if_contains( name );
		return p && !p->is_null() ? optional<uint32>{ requireId(args, name, sl) } : nullopt;
	}
	//A column of whole milliseconds:  none when the arguments leave it out or null it.
	Ω wholeMs( const jobject& args, sv name, SL sl )ε->optional<Duration>{
		let p = args.if_contains( name );
		if( !p || p->is_null() )
			return nullopt;
		let ms = p->try_to_number<uint32>();
		REFUSE_IF( !ms, "{} is milliseconds, a whole number from 0, not {}.", name, serialize(*p) );
		return milliseconds{ *ms };
	}
	//The engine's thresholds as a machines mutation would leave them:  `row`'s, or a create's defaults, under the arguments.
	struct Thresholds final{ Duration Width{ minutes{1} }; optional<Duration> IdealCycle, IdleTimeout; };
	Ω thresholds( const jobject& args, const OeeMachineRow* row, SL sl )ε->Thresholds{
		Thresholds y;
		if( row )
			y = { row->Width, row->IdealCycle, row->IdleTimeout };
		if( args.contains("width") ){
			let width = wholeMs( args, "width", sl );
			REFUSE_IF( !width, "width can't be null:  it is the width of the machine's rows, in milliseconds." );
			y.Width = *width;
		}
		if( let p = args.if_contains("idealCycle"); p ){
			y.IdealCycle = p->is_number() ? Hist::ToDuration( p->to_number<double>() ) : nullopt;
			REFUSE_IF( !p->is_null() && !y.IdealCycle, "idealCycle is milliseconds per unit, above zero, not {}.", serialize(*p) );
		}
		if( args.contains("idleTimeout") )
			y.IdleTimeout = wholeMs( args, "idleTimeout", sl );
		if( args.contains("smallStop") )
			REFUSE_IF( !wholeMs(args, "smallStop", sl), "smallStop can't be null:  0 makes no stop a small one." );
		return y;
	}
	Ω checkCalendar( const jobject& args, SL sl )ε->void{
		let p = args.if_contains( "calendarId" );
		if( !p || p->is_null() )
			return;
		let id = p->try_to_number<uint32>();
		REFUSE_IF( !id, "calendarId is a calendar's id, not {}.", serialize(*p) );
		REFUSE_IF( !Calendar::Client::Calendars::Find(*id), "calendarId {} is no calendar the gateway holds:  the AppServer has none by that id, it is deleted, or the gateway may not read the calendars.", *id );
	}
	Ω checkMachine( const Thresholds& t, const vector<OeeSignalRow>& signals, SL sl )ε->void{
		if( signals.empty() )//just made, or every signal purged:  nothing runs until it has one.
			Oee::ValidateThresholds( t.Width, t.IdealCycle, t.IdleTimeout, sl );
		else
			Oee::ValidateMachine( ToSignals(signals), t.Width, t.IdealCycle, t.IdleTimeout, sl );
	}
	//A signal as its mutation would leave it:  `row`'s, or a create's defaults, under the arguments.
	Ω wouldSignal( const jobject& args, const OeeSignalRow* row, SL sl )ε->OeeSignalRow{
		OeeSignalRow y{ .Id=0, .Machine=0, .Role=nullopt, .Node=0, .Running={Oee::StateValue{1.0}}, .RatePeriod=minutes{1} };
		if( row )
			y = *row;
		if( let p = args.if_contains("role"); p || !row ){
			y.Role = p && p->is_string() ? Oee::ToRole( p->get_string() ) : nullopt;
			REFUSE_IF( !y.Role, "role is State, Total, Rate, Good, Reject or Reason{}.", p ? Ƒ(", not {}", serialize(*p)) : string{} );
		}
		if( args.contains("histGroupNodeId") || !row )
			y.Node = requireId( args, "histGroupNodeId", sl );
		if( let p = args.if_contains("running"); p )
			y.Running = Oee::ParseRunning( *p, sl );
		if( args.contains("ratePeriod") ){
			let period = wholeMs( args, "ratePeriod", sl );
			REFUSE_IF( !period, "ratePeriod can't be null:  it is a Rate's time unit, in milliseconds." );
			y.RatePeriod = *period;
		}
		return y;
	}
	//What the page shows beside a signal (spec *Machines*):  a counter's band, and a member that holds values past `settle`.
	Ω warnings( const OeeMachineRow& machine, const OeeSignalRow& signal, Duration settle )ι->jarray{
		jarray y;
		let group = HistHost::Instance().Find( machine.Group );
		let thresholds = group ? group->FindThresholds( signal.Node ) : nullopt;
		if( !thresholds )
			return y;
		using enum Oee::ERole;
		let counter = signal.Role==Total || signal.Role==Good || signal.Role==Reject;
		if( let band = group->Band(signal.Node); counter && (band==EHistBand::Applied || band==EHistBand::Waiting) )
			y.emplace_back( Ƒ("Member {} has an ExceptionDeviation of {}:  a band holds a counter's record back by up to the band, so a count lands in a later interval.  Clear it on the group's Members tab, so every change is stored.", signal.Node, *thresholds->ExceptionDeviation) );
		if( thresholds->MinTimeInterval>settle )
			y.emplace_back( Ƒ("Member {} has a MinTimeInterval of {}, past the {} an interval waits for its records (settle):  each value it holds is stored late, and is a rebuild.", signal.Node, Chrono::ToString(thresholds->MinTimeInterval), Chrono::ToString(settle)) );
		return y;
	}

	//A mutation's required id argument, refused as the reads refuse one.
	Ω idArgument( const QL::MutationQL& m, sv name, sv what, SL sl )ε->uint32{
		let id = m.TryNumber<uint32>( name );
		REFUSE_IF( !id, "{} needs {}, {}.", m.CommandName, name, what );
		return *id;
	}

	α OeeMAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			using enum EMutationQL;
			let sl = _sl;//REFUSE's.
			auto& host = OeeHost::Instance();
			if( _query.CommandName==RecomputeCommand ){
				_query.CheckVariables( sl );//before an argument is read:  an unbound one would read as absent.
				let machineId = idArgument( _query, "machine", "an oee_machines id", sl );
				const Oee::Span span{ OeeQL::Time(_query, "start", sl), OeeQL::Time(_query, "end", sl) };
				REFUSE_IF( span.Start>=span.End, "start, {}, must be before end, {}.", OeeQL::Iso(span.Start), OeeQL::Iso(span.End) );
				AppClient()->Acl()->Test( "gateway", "oeeMachines", Access::ERights::Update, UserPK(), sl );//the table's, before a caller learns of an id.
				auto machines = co_await Any( SelectOeeMachines({.Key=DB::Key{(uint)machineId}, .IncludeDeleted=true}) );
				if( machines.empty() )
					notFound( Ƒ("Machine {} not found.", machineId), sl );
				HistHost::Instance().Gate( machines.front().Group, Access::ERights::Read, false, UserPK(), sl );
				host.Recompute( machineId, span, sl );
				Resume( jobject{ {"machine", machineId}, {"start", OeeQL::Iso(span.Start)}, {"end", OeeQL::Iso(span.End)} } );
				co_return;
			}
			if( _query.CommandName==CodeCommand ){
				GetViewPtr( Codings )->Authorize( Access::ERights::Update, UserPK(), sl );//the table's, which an unenforced resource passes, before a caller learns of an id.
				_query.CheckVariables( sl );
				let machineId = idArgument( _query, "machine", "an oee_machines id", sl );
				const Oee::Span span{ OeeQL::Time(_query, "start", sl), OeeQL::Time(_query, "end", sl) };
				REFUSE_IF( span.Start>=span.End, "start, {}, must be before end, {}.", OeeQL::Iso(span.Start), OeeQL::Iso(span.End) );
				let reasonId = _query.TryNumber<uint32>( "reason" );
				REFUSE_IF( !reasonId, "reason is required, an oee_reasons id." );
				let commented = _query.FindPtr( "comment" );
				REFUSE_IF( commented && !commented->is_null() && !commented->is_string(), "comment is text, not {}.", serialize(*commented) );
				let comment = commented && commented->is_string() ? sv{ commented->get_string() } : sv{};
				let characters = std::ranges::count_if( comment, []( char c ){ return ((unsigned char)c & 0xC0)!=0x80; } );//UTF-8's, as the column counts them.
				REFUSE_IF( characters>2048, "comment is {} characters, over the column's 2048.", characters );
				let machine = host.Find( machineId );
				if( !machine ){
					AppClient()->Acl()->Test( "gateway", "histGroups", Access::ERights::Read, UserPK(), sl );//the table's, before a caller learns of an id.
					notFound( Ƒ("Machine {} not found.", machineId), sl );
				}
				HistHost::Instance().Gate( machine->Row.Group, Access::ERights::Read, false, UserPK(), sl );
				auto reasons = co_await Any( DS()->SelectAsync(OeeSql::SelectReason(*reasonId)) );
				if( reasons.empty() )
					notFound( Ƒ("Reason {} not found.", *reasonId), sl );
				let reason = OeeSql::ToReason( reasons.front() );
				REFUSE_IF( !reasons.front().IsNull(4), "Reason '{}' is deleted:  it keeps resolving the stops it coded, and codes no more.", reason.Name );
				auto held = co_await host.Codings().Lock();//the overlap check and the write together.
				optional<OeeCodingRow> same;//the coding at the same start, which this replaces.
				for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectCodingsOver(machineId, span))) ){
					auto other = OeeSql::ToCoding( row );
					REFUSE_IF( other.Value.Start!=span.Start, "Coding {} already covers {} to {} of machine '{}':  a machine's codings don't overlap.  Code around it, or uncodeOee it first.", other.Value.Id, OeeQL::Iso(other.Value.Start), OeeQL::Iso(other.Value.End), machine->Row.Name );
					same = move( other );
				}
				let by = HistHost::Instance().Writer( UserPK() );
				let identity = (uint32)std::min<uint64_t>( by.IdentityId.Value, std::numeric_limits<uint32_t>::max() );//System, wider on Linux, as the historian stores it.
				let clock = host.Time();
				let now = clock ? clock->Now() : Clock::now();
				if( same )
					co_await Any( DS()->Execute(OeeSql::UpdateCoding(same->Value.Id, span.End, *reasonId, comment, identity, by.UserName, now)) );
				else
					co_await Any( DS()->Execute(OeeSql::InsertCoding(machineId, span, *reasonId, comment, identity, by.UserName, now)) );
				auto written = co_await Any( DS()->SelectAsync(OeeSql::SelectCodingAt(machineId, span.Start)) );
				THROW_IFSL( written.empty(), "codeOee wrote no coding at {} of machine {}.", OeeQL::Iso(span.Start), machineId );
				held.unlock();
				host.Coded( machineId );
				Resume( _query.ResultRequest ? jvalue{_query.ResultRequest->TrimColumns(OeeQL::CodingJson(OeeSql::ToCoding(written.front()), &reason))} : jvalue{} );
				co_return;
			}
			if( _query.CommandName==UncodeCommand ){
				GetViewPtr( Codings )->Authorize( Access::ERights::Delete, UserPK(), sl );
				_query.CheckVariables( sl );
				let id = idArgument( _query, "id", "an oee_codings id", sl );
				auto held = co_await host.Codings().Lock();//no codeOee replaces it between the read and the delete.
				auto rows = co_await Any( DS()->SelectAsync(OeeSql::SelectCoding(id)) );
				let machine = rows.empty() ? optional<OeeHost::Machine>{} : host.Find( OeeSql::ToCoding(rows.front()).Machine );
				if( !machine ){
					AppClient()->Acl()->Test( "gateway", "histGroups", Access::ERights::Read, UserPK(), sl );//the table's, before a caller learns of an id.
					notFound( Ƒ("Coding {} not found.", id), sl );
				}
				HistHost::Instance().Gate( machine->Row.Group, Access::ERights::Read, false, UserPK(), sl );
				co_await Any( DS()->Execute(OeeSql::DeleteCoding(id)) );
				held.unlock();
				host.Coded( machine->Row.Id );
				Resume( jobject{ {"id", id} } );
				co_return;
			}
			let type = _query.Type;
			let table = _query.TableName();
			REFUSE_IF( table==Stops || table==Intervals, "{} is the OEE engine's:  it alone writes the table, and '{}' is refused.", table, _query.CommandName );
			REFUSE_IF( table==Codings, "{} is written by codeOee and uncodeOee alone:  '{}' is refused.", table, _query.CommandName );
			_query.DBTable->Authorize( QL::StockRights(type), UserPK(), sl );//asked first:  a refusal below says what holds what.
			jvalue y;
			if( table==HistTable::Groups ){
				auto groups = co_await Any( SelectHistGroups({.Key=rowKey(_query, sl), .IncludeDeleted=true}) );
				if( let holder = groups.empty() ? nullopt : host.HolderOf( groups.front().Id ); holder )
					REFUSE( "Group '{}' is machine '{}'s{}:  purge the machine before the group is {}.", groups.front().Name, holder->Row.Name, holder->Row.Deleted ? ", which is deleted" : "", type==Purge ? "purged" : "deleted" );
				y = co_await HistGroupMAwait{ QL::MutationQL{_query}, _creds, sl };
			}
			else if( table==HistTable::GroupNodes ){
				let node = MutationRowId( _query, sl );
				if( let user = host.UserOf(node); user ){
					let p = std::ranges::find( user->Signals, node, &OeeSignalRow::Node );
					REFUSE( "Member {} is machine '{}'s {} signal:  purge the signal before the member.", node, user->Row.Name, p->Role ? Oee::ToString(*p->Role) : sv{} );
				}
				y = co_await HistGroupMAwait{ QL::MutationQL{_query}, _creds, sl };
			}
			else if( table==HistTable::TemplateNodes ){//before any group's member goes.
				let templateNode = MutationRowId( _query, sl );
				for( let& node : co_await Any(SelectHistGroupNodes({.TemplateNode=templateNode})) ){
					if( let user = host.UserOf(node.Id); user )
						REFUSE( "Template member {} made member {}, a signal of machine '{}':  purge the signal before the template member.", templateNode, node.Id, user->Row.Name );
				}
				y = co_await HistGroupMAwait{ QL::MutationQL{_query}, _creds, sl };
			}
			else{
				_query.CheckVariables( sl );
				let args = _query.ExtrapolateVariables();
				jobject spelled;//the columns spelled here, which the result carries over what the caller sent.
				jarray warned;
				optional<TimePoint> from;
				optional<DB::Key> machineKey;//the machine whose live side runs after.
				optional<OeeMachinePK> purging;
				optional<string> recompute;//an engine input changed:  the rows before now were made with the old one.
				CoGuard lineEdit;//a line's or a member's:  its checks, its write and the host's read of both tables together.
				if( table==Machines ){
					if( type==Create ){
						requireName( args, sl );
						let groupId = requireId( args, "histGroupId", sl );
						auto groups = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)groupId}, .IncludeDeleted=true}) );
						if( groups.empty() )
							notFound( Ƒ("Group {} not found.", groupId), sl );
						REFUSE_IF( groups.front().Deleted, "Group '{}' is deleted:  restore it before a machine is made over it.", groups.front().Name );
						if( let holder = host.HolderOf(groupId); holder )
							REFUSE( "Group '{}' already has a machine, '{}'{}:  a group has one.", groups.front().Name, holder->Row.Name, holder->Row.Deleted ? ", deleted - restore it, or purge it to make another" : "" );
						checkMachine( thresholds(args, nullptr, sl), {}, sl );
						checkCalendar( args, sl );
						if( let p = args.if_contains("from"); p && !p->is_null() ){
							try{
								THROW_IF( !p->is_string(), "not a time" );
								from = Chrono::ToTimePoint( string{p->get_string()}, sl );
							}
							catch( const std::exception& ){
								REFUSE( "from is where the machine's rows start, a time such as 2026-11-09T06:00:00Z, not {}.", serialize(*p) );
							}
							let clock = host.Time();
							REFUSE_IF( *from>(clock ? clock->Now() : Clock::now()), "from, {}, is after now:  a machine starts at a time that has passed.", ToIsoString(*from) );
						}
						_query.Args.erase( "from" );//of no column.
					}
					else{
						machineKey = rowKey( _query, sl );
						if( type==Purge )
							purging = MutationRowId( _query, sl );
						auto rows = co_await Any( SelectOeeMachines({.Key=*machineKey, .IncludeDeleted=true}) );
						if( rows.empty() )
							notFound( Ƒ("Machine {} not found.", machineKey->IsPK() ? std::to_string(machineKey->PK()) : machineKey->NK()), sl );
						if( type==Update ){
							REFUSE_IF( args.contains("from"), "from is a create's:  it is where a new machine's rows start." );
							auto signals = co_await Any( SelectOeeSignals({.Machine=rows.front().Id}) );
							let would = thresholds( args, &rows.front(), sl );
							checkMachine( would, signals, sl );
							checkCalendar( args, sl );
							if( would.IdleTimeout!=rows.front().IdleTimeout && std::ranges::none_of(signals, []( const OeeSignalRow& s ){ return s.Role==Oee::ERole::State; }) )
								recompute = "idleTimeout";//only a machine whose count is its state reads it.
						}
					}
				}
				else if( table==Signals ){
					REFUSE_IF( type==Delete || type==Restore, "A signal has no deleted:  purge it." );
					optional<OeeSignalRow> row;
					if( type!=Create ){
						let id = MutationRowId( _query, sl );
						auto rows = co_await Any( SelectOeeSignals({.Id=id}) );
						if( rows.empty() )
							notFound( Ƒ("Signal {} not found.", id), sl );
						row = move( rows.front() );
					}
					let machineId = row ? row->Machine : requireId( args, "oeeMachineId", sl );
					auto machines = co_await Any( SelectOeeMachines({.Key=DB::Key{(uint)machineId}, .IncludeDeleted=true}) );
					if( machines.empty() )
						notFound( Ƒ("Machine {} not found.", machineId), sl );
					let& machine = machines.front();
					auto signals = co_await Any( SelectOeeSignals({.Machine=machineId}) );
					if( row )
						std::erase_if( signals, [&]( const OeeSignalRow& s ){ return s.Id==row->Id; } );
					if( type!=Purge ){
						auto after = wouldSignal( args, row ? &*row : nullptr, sl );
						if( type==Create || args.contains("histGroupNodeId") ){
							auto members = co_await Any( SelectHistGroupNodes({.Id=after.Node}) );
							REFUSE_IF( members.empty(), "histGroupNodeId {} is no member of a group.", after.Node );
							REFUSE_IF( members.front().Group!=machine.Group, "Member {} is group {}'s, not the machine's, {}:  a machine's signals are members of its own group.", after.Node, members.front().Group, machine.Group );
						}
						if( let p = args.if_contains("running"); p && p->is_array() )
							spelled["running"] = serialize( *p );
						if( row && after.Role==Oee::ERole::State && after.Running!=row->Running )//the roles that read them.
							recompute = "running";
						else if( row && after.Role==Oee::ERole::Rate && after.RatePeriod!=row->RatePeriod )
							recompute = "ratePeriod";
						signals.push_back( after );
						warned = warnings( machine, after, host.Settle() );
					}
					checkMachine( {machine.Width, machine.IdealCycle, machine.IdleTimeout}, signals, sl );
					machineKey = DB::Key{ (uint)machineId };
				}
				else if( table==Lines ){
					lineEdit = co_await host.LineEdits().Lock();
					let lines = host.Lines();
					if( type==Create )
						requireName( args, sl );
					let level = args.if_contains( "level" );
					if( (level || type==Create) && (type==Create || type==Update) )
						REFUSE_IF( !level || !level->is_string() || !Oee::ToLevel(level->get_string()), "level is Site, Area, Line or Cell{}.", level ? Ƒ(", not {}", serialize(*level)) : string{} );
					if( type==Create || type==Update )
						checkCalendar( args, sl );
					if( type!=Create ){
						let key = rowKey( _query, sl );
						let row = lines.Find( key );
						if( !row )
							notFound( Ƒ("Line {} not found.", key.IsPK() ? std::to_string(key.PK()) : key.NK()), sl );
						if( type==Update && level ){//against its lines' levels above, and its member lines' below.
							auto graph = lines.Graph( host.MachineNames() );
							std::ranges::find( graph.Lines, row->Id, &Oee::Lines::Line::Id )->Level = *Oee::ToLevel( level->get_string() );
							Oee::Lines::Validate( graph, sl );
						}
					}
				}
				else if( table==LineMembers ){
					REFUSE_IF( type==Delete || type==Restore, "A member has no deleted:  purge it." );
					lineEdit = co_await host.LineEdits().Lock();
					let lines = host.Lines();
					if( type==Create ){
						let lineId = requireId( args, "oeeLineId", sl );
						let line = lines.Find( lineId );
						if( !line )
							notFound( Ƒ("Line {} not found.", lineId), sl );
						let machineId = optionalId( args, "oeeMachineId", sl );
						let memberId = optionalId( args, "memberLineId", sl );
						REFUSE_IF( machineId.has_value()==memberId.has_value(), "A member of '{}' is a machine or a line:  oeeMachineId or memberLineId, one of the two.", line->Name );
						let machine = machineId ? host.Find( *machineId ) : nullopt;
						if( machineId && !machine )
							notFound( Ƒ("Machine {} not found.", *machineId), sl );
						let member = memberId ? lines.Find( *memberId ) : nullptr;
						if( memberId && !member )
							notFound( Ƒ("Line {} not found.", *memberId), sl );
						let same = std::ranges::any_of( lines.Members, [&]( const OeeLineMemberRow& m ){ return m.Line==lineId && m.Machine==machineId && m.MemberLine==memberId; } );
						REFUSE_IF( same, "'{}' is a member of '{}' already.", machine ? machine->Row.Name : member->Name, line->Name );
						auto graph = lines.Graph( host.MachineNames() );
						graph.Members.push_back( {lineId, machineId, memberId} );
						Oee::Lines::Validate( graph, sl );
					}
					else if( let id = MutationRowId(_query, sl); !std::ranges::contains(lines.Members, id, &OeeLineMemberRow::Id) )
						notFound( Ƒ("Member {} not found.", id), sl );
				}
				else{//a reason.
					if( type==Create )
						requireName( args, sl );
					if( let p = args.if_contains("category"); p || type==Create )
						REFUSE_IF( !p || !p->is_string() || !Oee::ToCategory(p->get_string()), "category is Unplanned, Planned or Excluded{}.", p ? Ƒ(", not {}", serialize(*p)) : string{} );
					if( let p = args.if_contains("code"); p && !p->is_null() && (type==Create || type==Update) ){
						let code = p->try_to_number<int64_t>();
						REFUSE_IF( !code, "code is the Reason signal's value for the reason, a whole number, not {}.", serialize(*p) );
						optional<uint32> self;
						if( type==Update ){
							auto ids = co_await Any( DS()->SelectAsync(OeeSql::ReasonId(rowKey(_query, sl))) );
							if( ids.size() )
								self = ids.front().Get<uint32>( 0 );
						}
						for( let& other : co_await Any(DS()->SelectAsync(OeeSql::ReasonsCoded(*code))) ){
							if( other.Get<uint32>(0)!=self )
								REFUSE( "code {} is reason '{}'s{}:  a code names one reason.", *code, other.GetString(1), other.IsNull(2) ? "" : ", which is deleted and still resolves the stops it coded" );
						}
					}
					if( type==Purge ){
						let id = MutationRowId( _query, sl );
						auto counted = co_await Any( DS()->SelectAsync(OeeSql::CountCodings(id)) );
						let codings = counted.size() ? counted.front().Get<uint>( 0 ) : 0;
						REFUSE_IF( codings, "Reason {} is named by {} coding{}:  a reason a coding names is deleted, not purged, and keeps resolving.", id, codings, codings==1 ? "" : "s" );
					}
				}
				for( let& [key,value] : spelled )
					_query.Args[key] = value;

				if( purging )
					co_await host.MachinePurging( *purging );
				std::exception_ptr failed;//the stock op's, rethrown once the host has its rows again:  no co_await in a handler.
				try{
					y = co_await QL::MutationAwait{ QL::MutationQL{_query}, _creds, nullptr, sl };//the stock op:  no QL, so nothing routes it back here.
				}
				catch( const runtime_error& ){
					if( !purging )
						throw;
					failed = std::current_exception();
				}
				auto row0 = y.is_array() && y.get_array().size() ? y.get_array()[0].if_object() : nullptr;
				if( failed ){
					host.PurgeFailed( *purging );
					co_await host.MachineChanged( *machineKey );
					std::rethrow_exception( failed );
				}
				if( purging ){
					host.MachinePurged( *purging );
					co_await host.LinesChanged();//its memberships went with it.
				}
				else if( lineEdit )
					co_await host.LinesChanged();
				else if( table==Machines && type==Create ){
					let pk = row0 ? Json::FindNumber<uint32>( *row0, "id" ).value_or( 0 ) : 0;
					THROW_IFSL( !pk, "'{}' answered no id:  {}", _query.CommandName, serialize(y) );
					co_await host.MachineCreated( pk, from );
				}
				else if( machineKey )
					co_await host.MachineChanged( *machineKey );
				else if( table==Reasons )
					host.ReasonsChanged();
				if( recompute ){
					warned.emplace_back( Ƒ("{} changed:  the rows written before now were made with the old one, and recomputeOee( machine, start, end ) makes a range of them again.", *recompute) );
					spelled["recompute"] = true;
				}
				for( let& warning : warned )
					WARN( "{}:  {}", _query.CommandName, sv{warning.get_string()} );
				if( (table==Signals && type!=Purge) || recompute )
					spelled["warnings"] = move( warned );
				if( row0 ){
					for( let& [key,value] : spelled )
						(*row0)[key] = value;
				}
				else if( y.is_number() && spelled.size() )//an update's row count:  these answer, and the caller's arguments fill in the rest.
					y = move( spelled );
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}