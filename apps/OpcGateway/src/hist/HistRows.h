#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/utils/collections.h>
#include <jde/db/IDataSource.h>
#include <jde/db/Key.h>
#include <jde/db/Row.h>
#include <jde/db/awaits/SelectAwait.h>
#include <jde/db/generators/Sql.h>
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
	namespace HistTable{ constexpr sv Groups{ "hist_groups" }, GroupNodes{ "hist_group_nodes" }, Templates{ "hist_templates" }, TemplateNodes{ "hist_template_nodes" }; }

	//The five threshold columns as a row holds them, null where it inherits:  a group node from its template member, then
	//its group, then the library's defaults (Opc::Hist::Thresholds).  The intervals are milliseconds, as UA's Duration is.
	struct HistThresholdColumns final{
		static constexpr array<sv,5> Names{ "exception_deviation", "exception_deviation_format", "min_time_interval", "max_time_interval", "stepped" };
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
	//A browse name by its namespace URI, and for a served node's parents the node's id by its URI, which the endpoint's folder for it
	//takes (servers spec *Namespaces*);  a template member's has none.
	struct HistPathSegment final{ string Uri; string Name; optional<ExNodeId> Id; α operator==( const HistPathSegment& )Ι->bool=default; };
	α ParseHistPath( sv json, SL sl )ε->vector<HistPathSegment>;//throws for an empty path, a segment without both or an id that doesn't parse, as a BadRequest.
	α HistPathText( const vector<HistPathSegment>& path )ι->string;
	//A browse name as a segment:  none without both - no URI for its index, an empty one, or an empty name - which ParseHistPath
	//refuses, so a path the host writes (HistGroupHost::Describe) reads back.
	α ToHistPathSegment( optional<sv> uri, sv name )ι->optional<HistPathSegment>;

	//The EngineeringUnits property, Part 8's EUInformation, as hist_group_nodes.engineering_units holds it.
	struct HistEngineeringUnits final{ string NamespaceUri; int32 UnitId{}; string DisplayName; string Description; };
	//What a member's server says of its node, as its hist_group_nodes row stores it (docs/servers/spec.md *Address space*):  read
	//once its item is made and written to the row by the host itself (HistGroupHost::Describe), never by the QL, so a restart serves
	//the address space from the rows before any source is up.  Each none until the server has said;  a restart's rows carry the last
	//answer.  DataType is the type served:  the source's where that is a namespace-0 type, abstract or not, else the built-in the
	//historian stores the node's values by (Group::DataType), an enumeration Int32 and a structure Structure.  Path is the node's
	//browse path from the Objects folder, in a template member's spelling;  none for a node outside that tree, or one with a browse
	//name on the way that makes no segment (ToHistPathSegment).
	struct HistNodeAttributes final{
		static constexpr array<sv,9> Names{ "display_name", "description", "data_type", "value_rank", "array_dimensions", "access_level", "eu_range", "engineering_units", "path" };
		Ω Read( const DB::Row& row, uint first, HistGroupNodePK id )ε->HistNodeAttributes;//the nine cells from `first`, in Names' order;  a json or node cell that doesn't parse is none, and logged as row `id`'s.
		α Values()Ι->vector<DB::Value>;//the nine, in Names' order, as a statement binds them:  the json columns spelled.
		//What the server said, as its columns hold it:  each text cut to its column's length in bytes, before a character - no dialect
		//counts more than bytes - the units' description then name cut until their json fits, and dimensions, units or a path whose
		//json still won't fit none.  One cell too long fails the row's whole update on mysql and SQL Server.  DataType, a type of
		//namespace 0, and EURange's two numbers always fit.
		α FitColumns()ι->void;
		optional<string> DisplayName;
		optional<string> Description;
		optional<ExNodeId> DataType;
		optional<int32> ValueRank;
		optional<vector<uint32>> ArrayDimensions;
		optional<uint8> AccessLevel;
		optional<Opc::Hist::Range> EURange;
		optional<HistEngineeringUnits> EngineeringUnits;
		optional<vector<HistPathSegment>> Path;
	};
	//The update that writes a row's attribute columns, every one of Names, for DS()->Execute:  the host's own write, which the QL
	//never makes - the columns are neither insertable nor updateable.
	α UpdateHistNodeAttributesSql( HistGroupNodePK id, const HistNodeAttributes& attributes )ε->DB::Sql;

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
		HistNodeAttributes Attributes;
	};
	struct HistTemplateNodeRow final{
		HistTemplateNodePK Id;
		HistTemplatePK Template;
		string Path;
		HistThresholdColumns Thresholds;
	};

	//A select's rows, each made by `make`.
	template<class TRow>
	struct HistRowsAwait final : TAwaitEx<vector<TRow>,DB::SelectAwait::Task>{
		using base = TAwaitEx<vector<TRow>,DB::SelectAwait::Task>;
		using Make = TRow(*)( DB::Row&& );
		HistRowsAwait( DB::Statement&& statement, Make make, SRCE )ι:base{ sl }, _statement{ move(statement) }, _make{ make }{}
	private:
		α Execute()ι->DB::SelectAwait::Task override{
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
	//A template's members, or one member, by id - empty without a select when the filter names neither:  a group with no
	//template, a node no member made.  Not SelectHistTemplateNodes' "all".
	struct HistTemplateMembersAwait final : AnyAwait<flat_map<HistTemplateNodePK,HistTemplateNodeRow>>{
		HistTemplateMembersAwait( HistTemplateNodeFilter filter, SRCE )ι:AnyAwait{ sl }, _filter{ move(filter) }{}
		α await_ready()ι->bool override;
	protected:
		α Suspend()ι->void override{ Select(); }
	private:
		α Select()ι->VoidTask;
		HistTemplateNodeFilter _filter;
	};
	Ξ SelectHistTemplateMembers( HistTemplateNodeFilter filter, SRCE )ι->HistTemplateMembersAwait{ return HistTemplateMembersAwait{ move(filter), sl }; }
	//The row of the unique (connection, node) index behind a hist_group_nodes create's 409, as `group` sees it.  None for a
	//refusal no row explains - a foreign key, another constraint.  Else another group's, this group's `member` itself (a
	//concurrent create made it), or this group's as another member or an ad hoc node.
	struct NodeHolder final{
		enum class EKind : uint8{ None, OtherGroup, SameMember, ThisGroup };
		EKind Kind{};
		optional<HistGroupNodeRow> Row;//all but None.
	};
	struct NodeHolderAwait final : AnyAwait<NodeHolder>{
		NodeHolderAwait( const HistGroupRow& group, string text, optional<HistTemplateNodePK> member, SRCE )ι:
			AnyAwait{ sl }, _group{ group.Id }, _connection{ group.Connection }, _text{ move(text) }, _member{ member }{}
	protected:
		α Suspend()ι->void override{ Select(); }
	private:
		α Select()ι->VoidTask;
		HistGroupPK _group; ServerCnnctnPK _connection; string _text; optional<HistTemplateNodePK> _member;
	};
	Ξ SelectNodeHolder( const HistGroupRow& group, string text, optional<HistTemplateNodePK> member={}, SRCE )ι->NodeHolderAwait{ return NodeHolderAwait{ group, move(text), member, sl }; }
	α FindHistTemplateNode( const flat_map<HistTemplateNodePK,HistTemplateNodeRow>& members, optional<HistTemplateNodePK> id )ι->const HistTemplateNodeRow*;
}