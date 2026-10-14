#pragma once
#include <jde/fwk/utils/collections.h>
#include <jde/db/IDataSource.h>
#include <jde/db/Key.h>
#include <jde/db/Row.h>
#include <jde/db/awaits/SelectAwait.h>
#include <jde/db/generators/Statement.h>
#include <jde/historian/Group.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include "../opcInternal.h"
#include "../usings.h"

//The gateway historian's four tables (opcGateway-meta.jsonnet, spec *Collection* and *Templates*) as rows, with the
//selects the host and the hooks make and the spellings the node and path columns hold.
namespace Jde::Opc::Gateway{
	using HistGroupPK = uint32;
	using HistGroupNodePK = Opc::Hist::NodeIndex;//the row id is the node's index in the group's files.
	using HistTemplatePK = uint32;
	using HistTemplateNodePK = uint32;

	//The five threshold columns as a row holds them, null where it inherits:  a group node from its template member, then
	//its group, then the library's defaults (Opc::Hist::Thresholds).  The intervals are milliseconds, as UA's Duration is.
	struct HistThresholdColumns final{
		Ω Names()ι->vector<string>{ return {"exception_deviation", "exception_deviation_format", "min_time_interval", "max_time_interval", "stepped"}; }
		Ω Read( const DB::Row& row, uint first )ι->HistThresholdColumns;//the five cells from `first`, in Names' order.
		Ω FromJson( const jobject& args, SRCE )ε->HistThresholdColumns;//the QL names; a null is none.  A format that isn't a whole number from 0 to 255 is a BadRequest, as Check's.
		α Over( const HistThresholdColumns& fallback )Ι->HistThresholdColumns;//this, with fallback where this is null.
		α Thresholds()Ι->Opc::Hist::Thresholds;//the library's, defaults where null.
		//A value the library would refuse - a negative or infinite deviation, a negative interval, an unknown format - as a
		//BadRequest naming it, so the row never holds one.
		α Check( SL sl )Ε->void;
		optional<double> ExceptionDeviation;
		optional<uint8> DeviationFormat;//UA_ExceptionDeviationFormat's value.
		optional<double> MinTimeInterval;
		optional<double> MaxTimeInterval;
		optional<bool> Stepped;
	};
	//row, then template member, then group (spec *Templates*).
	α ResolveThresholds( const HistThresholdColumns& row, const HistThresholdColumns* member, const HistThresholdColumns& group )ι->Opc::Hist::Thresholds;
	//Each threshold as ResolveThresholds resolves it, with where it comes from, for a group's member list (#338):  the column's QL
	//name, the library's value - null for a deviation none sets, the intervals in milliseconds - and whether it is the row's own
	//(node), its template member's (template), its group's (group) or the library's (default).
	struct HistThresholdSource final{ sv Name; jvalue Value; sv Source; };
	α ResolveThresholdSources( const HistThresholdColumns& row, const HistThresholdColumns* member, const HistThresholdColumns& group )ι->vector<HistThresholdSource>;

	//The node columns' spelling, hist_group_nodes.node and hist_groups.root:  ExNodeId::ToJson with the namespace URI set and
	//the index 0, `{"nsu":"urn:jde:pumps","i":6042}`, so equal nodes spell alike for the unique index.
	α HistNodeText( const ExNodeId& node )ι->string;
	//Back, from that text or from what a caller sends - the json object, or a NodeId's UA spelling - as an ExNodeId that may
	//still carry an index in place of its URI, which the caller's connection then resolves (HistGroupMAwait).  Throws for
	//anything else, as a BadRequest.
	α ParseHistNode( const jvalue& node, SL sl )ε->ExNodeId;
	α ParseHistNode( sv text, SL sl )ε->ExNodeId;

	//A template member's path, hist_template_nodes.path:  `[{"nsu":"urn:…","name":"Motor"},…]`, each segment a browse name by
	//its namespace URI, never an index, so one template applies across servers whose namespace tables differ.
	struct HistPathSegment final{ string Uri; string Name; };
	α ParseHistPath( sv json, SL sl )ε->vector<HistPathSegment>;//throws for an empty path or a segment without both, as a BadRequest.
	α HistPathText( const vector<HistPathSegment>& path )ι->string;

	struct HistGroupRow final{
		HistGroupPK Id;
		string Guid;//the column's 16 bytes as text, the stem of the group's files.
		ServerCnnctnPK Connection;
		optional<HistTemplatePK> Template;
		optional<ExNodeId> Root;//the Objects folder when none.
		HistThresholdColumns Thresholds;
		string Name;
		bool Deleted;
	};
	struct HistGroupNodeRow final{
		HistGroupNodePK Id;
		HistGroupPK Group;
		ServerCnnctnPK Connection;
		ExNodeId Node;
		optional<HistTemplateNodePK> TemplateNode;
		HistThresholdColumns Thresholds;
	};
	struct HistTemplateNodeRow final{
		HistTemplateNodePK Id;
		HistTemplatePK Template;
		string Path;
		HistThresholdColumns Thresholds;
	};

	//A select's rows, each made by `make`.
	template<class TRow>
	struct HistRowsAwait final : TAwait<vector<TRow>>{
		using base = TAwait<vector<TRow>>;
		using Make = TRow(*)( DB::Row&& );
		HistRowsAwait( DB::Statement&& statement, Make make, SRCE )ι:base{ sl }, _statement{ move(statement) }, _make{ make }{}
		α Suspend()ι->void override{ Select(); }
	private:
		α Select()ι->DB::SelectAwait::Task{
			try{
				auto rows = co_await DS()->SelectAsync( _statement.Move() );
				auto y = Reserve<TRow>( rows.size() );
				for( auto&& row : rows )
					y.push_back( _make(move(row)) );
				base::Resume( move(y) );
			}
			catch( runtime_error& e ){
				base::ResumeExp( move(e) );
			}
		}
		DB::Statement _statement;
		Make _make;
	};
	//hist_groups:  by key, by template, or all; deleted rows only when asked.
	struct HistGroupFilter final{ optional<DB::Key> Key; optional<HistTemplatePK> Template; bool IncludeDeleted{}; };
	α SelectHistGroups( HistGroupFilter filter={}, SRCE )ι->HistRowsAwait<HistGroupRow>;
	//hist_group_nodes:  by row, by group, by template member, by connection and node - the unique index, the node as HistNodeText
	//spells it - or all.
	struct HistGroupNodeFilter final{ optional<HistGroupNodePK> Id; optional<HistGroupPK> Group; optional<HistTemplateNodePK> TemplateNode; optional<ServerCnnctnPK> Connection; optional<string> Node; };
	α SelectHistGroupNodes( HistGroupNodeFilter filter={}, SRCE )ι->HistRowsAwait<HistGroupNodeRow>;
	//hist_template_nodes:  by row, by template, or all.
	struct HistTemplateNodeFilter final{ optional<HistTemplateNodePK> Id; optional<HistTemplatePK> Template; };
	α SelectHistTemplateNodes( HistTemplateNodeFilter filter={}, SRCE )ι->HistRowsAwait<HistTemplateNodeRow>;
	//Template members by row, as a group node's TemplateNode names them, and the one a group node was resolved from:  null for a
	//node added by hand, or one whose member isn't among them.
	α HistTemplateNodesById( vector<HistTemplateNodeRow>&& rows )ι->flat_map<HistTemplateNodePK,HistTemplateNodeRow>;
	α FindHistTemplateNode( const flat_map<HistTemplateNodePK,HistTemplateNodeRow>& members, optional<HistTemplateNodePK> id )ι->const HistTemplateNodeRow*;
}