#pragma once
#include "../hist/HistRows.h"

//The flows' tables (opcGateway-meta.jsonnet, docs/flows/spec.md *Flows*, *Context*) as rows:  the selects the host and
//FlowMAwait make, and the statements the host writes a version and the context with.
namespace Jde::Opc::Gateway{
	using FlowPK = uint32;
	namespace FlowTable{ constexpr sv Flows{ "flows" }, Versions{ "flow_versions" }, Contexts{ "flow_contexts" }, Runs{ "flow_runs" }; }

	struct FlowRow final{
		FlowPK Id;
		string Guid;//the column's 16 bytes as text, the criteria of the flow's own access row.
		bool Enabled;
		optional<uint32> Version;//none until the first deploy.
		string Name;
		string Slug;
		bool Deleted;
	};
	//Deployer:  the row's identity_id, UserPK::System for the UINT32_MAX it is stored as (Hist::Writer).
	struct FlowVersionRow final{ FlowPK Flow; uint32 Version; string Graph; UserPK Deployer; };
	//A context value:  the global scope's with no flow, the flow's own with no node.  Value is json text.
	struct FlowContextRow final{ optional<FlowPK> Flow; string Node; string Name; string Value; };

	//flows:  by key, or all;  deleted rows only when asked.
	struct FlowFilter final{ optional<DB::Key> Key; bool IncludeDeleted{}; };
	α SelectFlows( FlowFilter filter={}, SRCE )ι->HistRowsAwait<FlowRow>;
	α SelectFlowVersion( FlowPK flow, uint32 version, SRCE )ι->HistRowsAwait<FlowVersionRow>;
	α SelectFlowContexts( optional<FlowPK> flow, SRCE )ι->HistRowsAwait<FlowContextRow>;//a flow's, or with none the global scope's.

	//What the host writes, each for DS()->Execute.
	namespace FlowSql{
		α LastVersion( FlowPK flow )ε->DB::Sql;//the flow's highest version, null for none:  a select.
		α CurrentGraphs()ε->DB::Sql;//every flow's current version, deleted or not, as flow_id and graph:  a select.
		α InsertVersion( FlowPK flow, uint32 version, sv graph, TimePoint deployed, UserPK deployer, sv userName, sv comment )ε->DB::Sql;
		α SetVersion( FlowPK flow, uint32 version )ε->DB::Sql;//flows.version.
		//A context value is written as a delete and an insert:  the scope's columns are nullable, which no dialect's upsert keys on alike.
		α DeleteContext( optional<FlowPK> flow, sv node, sv name )ε->DB::Sql;
		α InsertContext( optional<FlowPK> flow, sv node, sv name, sv value )ε->DB::Sql;
	}
}