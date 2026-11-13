#include "FlowRows.h"
#include <jde/db/generators/WhereClause.h>

#define let const auto
namespace Jde::Opc::Gateway{
	Ω select( sv table, vector<string> columns, DB::WhereClause&& where, SL sl )ε->DB::Statement{
		let view = GetViewPtr( table );
		return DB::Statement{ {view->GetColumns(move(columns), sl)}, {view}, move(where) };
	}
	Ω name( sv table )ε->string{ return GetViewPtr( table )->SqlName(); }
	Ω text( sv value )ι->DB::Value{ return value.empty() ? DB::Value{} : DB::Value{ string{value} }; }//the QL engine's rule:  an empty string is null.
	constexpr uint _systemIdentity{ std::numeric_limits<uint32_t>::max() };//what UserPK::System, wider on Linux, is stored as.

	Ω flowRow( DB::Row&& r )->FlowRow{
		return { .Id=r.Get<uint32>(0), .Guid=Jde::ToString(r.GetGuid(1)), .Enabled=r.GetBit(2), .Version=r.GetOpt<uint32>(3), .Name=r.TakeString(4), .Slug=r.TakeString(5), .Deleted=!r.IsNull(6) };
	}
	Ω versionRow( DB::Row&& r )->FlowVersionRow{
		let identity = r.Get<uint>( 3 );
		return { .Flow=r.Get<uint32>(0), .Version=r.Get<uint32>(1), .Graph=r.TakeString(2), .Deployer=UserPK{identity>=_systemIdentity ? UserPK::System : (UserPK::Type)identity} };
	}
	Ω contextRow( DB::Row&& r )->FlowContextRow{
		return { .Flow=r.GetOpt<uint32>(0), .Node=r.TakeString(1), .Name=r.TakeString(2), .Value=r.TakeString(3) };
	}
	//The scope's two columns as a where clause's text, and the flow's parameter when it has one.
	Ω scope( optional<FlowPK> flow, vector<DB::Value>& params )ι->sv{
		if( flow )
			params.emplace_back( (uint)*flow );
		return flow ? "flow_id=?" : "flow_id is null";
	}
}
namespace Jde::Opc{
	α Gateway::SelectFlows( FlowFilter filter, SL sl )ι->HistRowsAwait<FlowRow>{
		let view = GetViewPtr( FlowTable::Flows );
		DB::WhereClause where;
		if( !filter.IncludeDeleted )
			where.Add( view->GetColumnPtr("deleted"), nullptr );
		if( filter.Key )
			where.Add( view->GetColumnPtr(filter.Key->IsPK() ? "flow_id" : "slug"), filter.Key->IsPK() ? DB::Value{filter.Key->PK()} : DB::Value{filter.Key->NK()} );
		return { select(FlowTable::Flows, {"flow_id", "guid", "enabled", "version", "name", "slug", "deleted"}, move(where), sl), flowRow, sl };
	}
	α Gateway::SelectFlowVersion( FlowPK flow, uint32 version, SL sl )ι->HistRowsAwait<FlowVersionRow>{
		let view = GetViewPtr( FlowTable::Versions );
		DB::WhereClause where;
		where.Add( view->GetColumnPtr("flow_id"), (uint)flow );
		where.Add( view->GetColumnPtr("version"), (uint)version );
		return { select(FlowTable::Versions, {"flow_id", "version", "graph", "identity_id"}, move(where), sl), versionRow, sl };
	}
	α Gateway::SelectFlowContexts( optional<FlowPK> flow, SL sl )ι->HistRowsAwait<FlowContextRow>{
		let view = GetViewPtr( FlowTable::Contexts );
		DB::WhereClause where;
		if( flow )
			where.Add( view->GetColumnPtr("flow_id"), (uint)*flow );
		else
			where.Add( view->GetColumnPtr("flow_id"), nullptr );
		return { select(FlowTable::Contexts, {"flow_id", "node_id", "name", "value"}, move(where), sl), contextRow, sl };
	}

	α Gateway::FlowSql::LastVersion( FlowPK flow )ε->DB::Sql{
		return { Ƒ("select max(version) from {} where flow_id=?", name(FlowTable::Versions)), {DB::Value{(uint)flow}} };
	}
	α Gateway::FlowSql::CurrentGraphs()ε->DB::Sql{
		return { Ƒ("select v.flow_id, v.graph from {} v join {} f on f.flow_id=v.flow_id and f.version=v.version", name(FlowTable::Versions), name(FlowTable::Flows)), {} };
	}
	α Gateway::FlowSql::InsertVersion( FlowPK flow, uint32 version, sv graph, TimePoint deployed, UserPK deployer, sv userName, sv comment )ε->DB::Sql{
		return { Ƒ("insert into {}( flow_id, version, graph, deployed, identity_id, user_name, comment ) values( ?, ?, ?, ?, ?, ?, ? )", name(FlowTable::Versions)),
			{DB::Value{(uint)flow}, DB::Value{(uint)version}, DB::Value{string{graph}}, DB::Value{DB::DBTimePoint{deployed}}, DB::Value{std::min<uint>(deployer.Value, _systemIdentity)}, DB::Value{string{userName}}, text(comment)} };
	}
	α Gateway::FlowSql::SetVersion( FlowPK flow, uint32 version )ε->DB::Sql{
		return { Ƒ("update {} set version=? where flow_id=?", name(FlowTable::Flows)), {DB::Value{(uint)version}, DB::Value{(uint)flow}} };
	}
	α Gateway::FlowSql::DeleteContext( optional<FlowPK> flow, sv node, sv name_ )ε->DB::Sql{
		vector<DB::Value> params;
		let scoped = scope( flow, params );
		params.emplace_back( string{node} );
		params.emplace_back( string{name_} );
		return { Ƒ("delete from {} where {} and node_id=? and name=?", name(FlowTable::Contexts), scoped), move(params) };
	}
	α Gateway::FlowSql::InsertContext( optional<FlowPK> flow, sv node, sv name_, sv value )ε->DB::Sql{
		return { Ƒ("insert into {}( flow_id, node_id, name, value ) values( ?, ?, ?, ? )", name(FlowTable::Contexts)),
			{flow ? DB::Value{(uint)*flow} : DB::Value{}, DB::Value{string{node}}, DB::Value{string{name_}}, DB::Value{string{value}}} };
	}
}