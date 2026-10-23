#include "HistRows.h"
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/WhereClause.h>
#include <jde/db/meta/Table.h>

#define let const auto
namespace Jde::Opc::Gateway{
	using Opc::Hist::Thresholds;

	α HistThresholdColumns::Read( const DB::Row& row, uint i )ι->HistThresholdColumns{
		return {
			.ExceptionDeviation = row.GetOpt<double>( i ),
			.DeviationFormat = row.GetOpt<uint8>( i+1 ),
			.MinTimeInterval = row.GetOpt<double>( i+2 ),
			.MaxTimeInterval = row.GetOpt<double>( i+3 ),
			.Stepped = row.GetBitOpt( i+4 )
		};
	}
	α HistThresholdColumns::FromJson( const jobject& args, SL sl )ε->HistThresholdColumns{
		let number = [&]( sv name )->optional<double> { let p = args.if_contains( name ); return p && p->is_number() ? optional<double>{ p->to_number<double>() } : nullopt; };
		optional<uint8> format;
		if( let p = args.if_contains("exceptionDeviationFormat"); p && !p->is_null() ){
			let value = p->try_to_number<uint8>();//not a cast:  a double out of uint8's range is undefined, and 259 passed Check as 3.
			if( !value )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "exceptionDeviationFormat must be 0 (AbsoluteValue) to 3 (PercentOfEURange), not {}.", serialize(*p) };
			format = *value;
		}
		return {
			.ExceptionDeviation = number( "exceptionDeviation" ),
			.DeviationFormat = format,
			.MinTimeInterval = number( "minTimeInterval" ),
			.MaxTimeInterval = number( "maxTimeInterval" ),
			.Stepped = Json::FindBool( args, "stepped" )
		};
	}
	α HistThresholdColumns::Over( const HistThresholdColumns& fallback )Ι->HistThresholdColumns{
		return {
			.ExceptionDeviation = ExceptionDeviation ? ExceptionDeviation : fallback.ExceptionDeviation,
			.DeviationFormat = DeviationFormat ? DeviationFormat : fallback.DeviationFormat,
			.MinTimeInterval = MinTimeInterval ? MinTimeInterval : fallback.MinTimeInterval,
			.MaxTimeInterval = MaxTimeInterval ? MaxTimeInterval : fallback.MaxTimeInterval,
			.Stepped = Stepped ? Stepped : fallback.Stepped
		};
	}
	α HistThresholdColumns::Thresholds()Ι->Opc::Hist::Thresholds{
		Opc::Hist::Thresholds y;
		y.ExceptionDeviation = ExceptionDeviation;
		if( DeviationFormat )
			y.DeviationFormat = (UA_ExceptionDeviationFormat)*DeviationFormat;
		if( MinTimeInterval )
			y.MinTimeInterval = Opc::Hist::ToDuration( *MinTimeInterval ).value_or( Duration::zero() );
		if( MaxTimeInterval )
			y.MaxTimeInterval = Opc::Hist::ToDuration( *MaxTimeInterval ).value_or( Duration::zero() );
		if( Stepped )
			y.Stepped = *Stepped;
		return y;
	}
	α HistThresholdColumns::Check( SL sl )Ε->void{
		let refuse = [&]( string what ){ throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "{}", move(what) }; };
		if( ExceptionDeviation && (!std::isfinite(*ExceptionDeviation) || *ExceptionDeviation<0) )
			refuse( Ƒ("exceptionDeviation must be a finite number, 0 or more, not {}.", *ExceptionDeviation) );
		if( DeviationFormat && *DeviationFormat>UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE )
			refuse( Ƒ("exceptionDeviationFormat must be 0 (AbsoluteValue) to 3 (PercentOfEURange), not {}.", *DeviationFormat) );
		if( MinTimeInterval && !Opc::Hist::ToDuration(*MinTimeInterval) )
			refuse( Ƒ("minTimeInterval must be milliseconds, 0 or more, not {}.", *MinTimeInterval) );
		if( MaxTimeInterval && !Opc::Hist::ToDuration(*MaxTimeInterval) )
			refuse( Ƒ("maxTimeInterval must be milliseconds, 0 or more, not {}.", *MaxTimeInterval) );
	}

	Ω select( sv table, vector<string> columns, DB::WhereClause&& where, SL sl )ε->DB::Statement{
		let view = GetViewPtr( table );
		return DB::Statement{ {view->GetColumns(move(columns), sl)}, {view}, move(where) };
	}
	Ω groupRow( DB::Row&& r )->HistGroupRow{
		let root = r.GetOpt<string>( 4 );
		return {
			.Id = r.Get<uint32>( 0 ),
			.Guid = Jde::ToString( r.GetGuid(1) ),
			.Connection = r.Get<uint32>( 2 ),
			.Template = r.GetOpt<uint32>( 3 ),
			.Root = root && root->size() ? optional<ExNodeId>{ ParseHistNode(*root, SRCE_CUR) } : nullopt,
			.Thresholds = HistThresholdColumns::Read( r, 5 ),
			.Name = r.TakeString( 10 ),
			.Deleted = !r.IsNull( 11 )
		};
	}
	Ω groupNodeRow( DB::Row&& r )->HistGroupNodeRow{
		return {
			.Id = r.Get<uint32>( 0 ),
			.Group = r.Get<uint32>( 1 ),
			.Connection = r.Get<uint32>( 2 ),
			.Node = ParseHistNode( r.GetString(3), SRCE_CUR ),
			.TemplateNode = r.GetOpt<uint32>( 4 ),
			.Thresholds = HistThresholdColumns::Read( r, 5 ),
			.Attributes = HistNodeAttributes::Read( r, 10 )
		};
	}
	Ω rangeJson( const Opc::Hist::Range& range )ι->string{ return serialize( jobject{ {"low", range.Low}, {"high", range.High} } ); }
	Ω unitsJson( const HistEngineeringUnits& units )ι->string{
		return serialize( jobject{ {"namespaceUri", units.NamespaceUri}, {"unitId", (int64_t)units.UnitId}, {"displayName", units.DisplayName}, {"description", units.Description} } );
	}
	Ω dimensionsJson( const vector<uint32>& dimensions )ι->string{
		jarray y;
		for( let d : dimensions )
			y.emplace_back( (uint64_t)d );
		return serialize( y );
	}
	α HistNodeAttributes::Read( const DB::Row& r, uint i )ε->HistNodeAttributes{
		HistNodeAttributes y;
		y.DisplayName = r.GetOpt<string>( i );
		y.Description = r.GetOpt<string>( i+1 );
		if( let text = r.GetOpt<string>(i+2); text )
			y.DataType = ParseHistNode( *text, SRCE_CUR );
		y.ValueRank = r.GetOpt<int32>( i+3 );
		if( let text = r.GetOpt<string>(i+4); text ){
			vector<uint32> dimensions;
			for( let& d : Json::ParseValue(*text, SRCE_CUR).as_array() )
				dimensions.push_back( d.to_number<uint32_t>() );
			y.ArrayDimensions = move( dimensions );
		}
		y.AccessLevel = r.GetOpt<uint8>( i+5 );
		if( let text = r.GetOpt<string>(i+6); text ){
			let o = Json::Parse( *text, SRCE_CUR );
			y.EURange = Opc::Hist::Range{ Json::AsNumber<double>(o, "low"), Json::AsNumber<double>(o, "high") };
		}
		if( let text = r.GetOpt<string>(i+7); text ){
			let o = Json::Parse( *text, SRCE_CUR );
			y.EngineeringUnits = HistEngineeringUnits{ string{Json::FindDefaultSV(o, "namespaceUri")}, Json::FindNumber<int32>(o, "unitId").value_or(0), string{Json::FindDefaultSV(o, "displayName")}, string{Json::FindDefaultSV(o, "description")} };
		}
		if( let text = r.GetOpt<string>(i+8); text )
			y.Path = ParseHistPath( *text, SRCE_CUR );
		return y;
	}
	α HistNodeAttributes::Values()Ι->vector<DB::Value>{
		let text = []( optional<string> s )ι->DB::Value{ return s ? DB::Value{move(*s)} : DB::Value{}; };
		return {
			text( DisplayName ),
			text( Description ),
			text( DataType ? optional<string>{HistNodeText(*DataType)} : nullopt ),
			ValueRank ? DB::Value{(int)*ValueRank} : DB::Value{},
			text( ArrayDimensions ? optional<string>{dimensionsJson(*ArrayDimensions)} : nullopt ),
			AccessLevel ? DB::Value{(uint32_t)*AccessLevel} : DB::Value{},
			text( EURange ? optional<string>{rangeJson(*EURange)} : nullopt ),
			text( EngineeringUnits ? optional<string>{unitsJson(*EngineeringUnits)} : nullopt ),
			text( Path ? optional<string>{HistPathText(*Path)} : nullopt )
		};
	}
	Ω templateNodeRow( DB::Row&& r )->HistTemplateNodeRow{
		return {
			.Id = r.Get<uint32>( 0 ),
			.Template = r.Get<uint32>( 1 ),
			.Path = r.TakeString( 2 ),
			.Thresholds = HistThresholdColumns::Read( r, 3 )
		};
	}
}

namespace Jde::Opc{
	α Gateway::ResolveThresholds( const HistThresholdColumns& row, const HistThresholdColumns* member, const HistThresholdColumns& group )ι->Opc::Hist::Thresholds{
		return ( member ? row.Over(*member) : row ).Over( group ).Thresholds();
	}
	α Gateway::ResolveThresholdSources( const HistThresholdColumns& row, const HistThresholdColumns* member, const HistThresholdColumns& group )ι->vector<HistThresholdSource>{
		let t = ResolveThresholds( row, member, group );
		let source = [&]( auto HistThresholdColumns::*column )ι->sv{ return (row.*column) ? "node" : member && (member->*column) ? "template" : (group.*column) ? "group" : "default"; };
		let ms = []( Duration d )ι->double{ return std::chrono::duration<double,std::milli>( d ).count(); };
		return {
			{ "exceptionDeviation", t.ExceptionDeviation ? jvalue{*t.ExceptionDeviation} : jvalue{}, source(&HistThresholdColumns::ExceptionDeviation) },
			{ "exceptionDeviationFormat", jvalue{(uint64_t)t.DeviationFormat}, source(&HistThresholdColumns::DeviationFormat) },
			{ "minTimeInterval", jvalue{ms(t.MinTimeInterval)}, source(&HistThresholdColumns::MinTimeInterval) },
			{ "maxTimeInterval", jvalue{ms(t.MaxTimeInterval)}, source(&HistThresholdColumns::MaxTimeInterval) },
			{ "stepped", jvalue{t.Stepped}, source(&HistThresholdColumns::Stepped) }
		};
	}

	α Gateway::HistNodeText( const ExNodeId& node )ι->string{
		ExNodeId canonical{ node };
		if( canonical.namespaceUri.length )
			canonical.nodeId.namespaceIndex = 0;
		return serialize( canonical.ToJson() );
	}
	α Gateway::ParseHistNode( const jvalue& node, SL sl )ε->ExNodeId{
		try{
			if( let s = node.if_string(); s )
				return ParseHistNode( sv{*s}, sl );
			THROW_IFSL( !node.is_object(), "A node is a json object, {{nsu, i|s|g|b}} or {{ns, i|s|g|b}}." );
			return ExNodeId{ node };
		}
		catch( const Exception& e ){
			throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "'{}' is not a node id:  {}", serialize(node), e.what() };
		}
	}
	α Gateway::ParseHistNode( sv text, SL sl )ε->ExNodeId{
		try{
			if( text.starts_with('{') )
				return ExNodeId{ jvalue{Json::Parse(text, sl)} };
			return ExNodeId{ NodeId::DecodeJson(string{text}) };
		}
		catch( const std::exception& e ){
			throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "'{}' is not a node id:  {}", text, e.what() };
		}
	}

	α Gateway::ParseHistPath( sv json, SL sl )ε->vector<HistPathSegment>{
		let refuse = [&]( string why ){ throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "'{}' is not a path:  {}", json, move(why) }; };
		jvalue parsed;
		try{
			parsed = Json::ParseValue( json, sl );
		}
		catch( const std::exception& e ){
			refuse( e.what() );
		}
		let array = parsed.if_array();
		if( !array || array->empty() )
			refuse( "a path is a json list of {nsu, name} segments, at least one." );
		auto y = Reserve<HistPathSegment>( array->size() );
		for( let& segment : *array ){
			let o = segment.if_object();
			let uri = o ? Json::FindSV( *o, "nsu" ) : nullopt;
			let name = o ? Json::FindSV( *o, "name" ) : nullopt;
			if( !uri || uri->empty() || !name || name->empty() )
				refuse( Ƒ("segment {} needs a namespace URI (nsu) and a browse name (name).", serialize(segment)) );
			y.push_back( {string{*uri}, string{*name}} );
		}
		return y;
	}
	α Gateway::HistPathText( const vector<HistPathSegment>& path )ι->string{
		jarray y;
		for( let& segment : path )
			y.push_back( jobject{ {"nsu", segment.Uri}, {"name", segment.Name} } );
		return serialize( y );
	}
	α Gateway::SelectHistGroups( HistGroupFilter filter, SL sl )ι->HistRowsAwait<HistGroupRow>{
		let view = GetViewPtr( HistTable::Groups );
		DB::WhereClause where;
		if( !filter.IncludeDeleted )
			where.Add( view->GetColumnPtr("deleted"), nullptr );
		if( filter.Key )
			where.Add( view->GetColumnPtr(filter.Key->IsPK() ? "hist_group_id" : "slug"), filter.Key->IsPK() ? DB::Value{filter.Key->PK()} : DB::Value{filter.Key->NK()} );
		if( filter.Template )
			where.Add( view->GetColumnPtr("hist_template_id"), (uint)*filter.Template );
		auto columns = vector<string>{ "hist_group_id", "guid", "server_connection_id", "hist_template_id", "root" };
		for( let name : HistThresholdColumns::Names )
			columns.emplace_back( name );
		columns.push_back( "name" ); columns.push_back( "deleted" );
		return { select(HistTable::Groups, move(columns), move(where), sl), groupRow, sl };
	}

	α Gateway::SelectHistGroupNodes( HistGroupNodeFilter filter, SL sl )ι->HistRowsAwait<HistGroupNodeRow>{
		let view = GetViewPtr( HistTable::GroupNodes );
		DB::WhereClause where;
		if( filter.Id )
			where.Add( view->GetColumnPtr("hist_group_node_id"), (uint)*filter.Id );
		if( filter.Group )
			where.Add( view->GetColumnPtr("hist_group_id"), (uint)*filter.Group );
		if( filter.TemplateNode )
			where.Add( view->GetColumnPtr("hist_template_node_id"), (uint)*filter.TemplateNode );
		if( filter.Connection )
			where.Add( view->GetColumnPtr("server_connection_id"), (uint)*filter.Connection );
		if( filter.Node )
			where.Add( view->GetColumnPtr("node"), DB::Value{*filter.Node} );
		auto columns = vector<string>{ "hist_group_node_id", "hist_group_id", "server_connection_id", "node", "hist_template_node_id" };
		for( let name : HistThresholdColumns::Names )
			columns.emplace_back( name );
		for( let name : HistNodeAttributes::Names )
			columns.emplace_back( name );
		return { select(HistTable::GroupNodes, move(columns), move(where), sl), groupNodeRow, sl };
	}
	α Gateway::UpdateHistNodeAttributesSql( HistGroupNodePK id, const HistNodeAttributes& attributes )ε->DB::Sql{
		string set;
		for( let name : HistNodeAttributes::Names )
			set += Ƒ( "{}{}=?", set.empty() ? "" : ", ", name );
		auto params = attributes.Values();
		params.emplace_back( (uint)id );
		return { Ƒ("update {} set {} where hist_group_node_id=?", GetViewPtr(HistTable::GroupNodes)->SqlName(), set), move(params) };
	}

	α Gateway::SelectHistTemplateNodes( HistTemplateNodeFilter filter, SL sl )ι->HistRowsAwait<HistTemplateNodeRow>{
		let view = GetViewPtr( HistTable::TemplateNodes );
		DB::WhereClause where;
		if( filter.Id )
			where.Add( view->GetColumnPtr("hist_template_node_id"), (uint)*filter.Id );
		if( filter.Template )
			where.Add( view->GetColumnPtr("hist_template_id"), (uint)*filter.Template );
		auto columns = vector<string>{ "hist_template_node_id", "hist_template_id", "path" };
		for( let name : HistThresholdColumns::Names )
			columns.emplace_back( name );
		return { select(HistTable::TemplateNodes, move(columns), move(where), sl), templateNodeRow, sl };
	}
	α Gateway::HistTemplateNodesById( vector<HistTemplateNodeRow>&& rows )ι->flat_map<HistTemplateNodePK,HistTemplateNodeRow>{
		flat_map<HistTemplateNodePK,HistTemplateNodeRow> y;
		for( auto& row : rows )
			y.emplace( row.Id, move(row) );
		return y;
	}
	α Gateway::HistTemplateMembersAwait::await_ready()ι->bool{
		if( _filter.Id || _filter.Template )
			return false;
		_result.emplace();
		return true;
	}
	α Gateway::HistTemplateMembersAwait::Select()ι->VoidTask{
		try{
			auto select = SelectHistTemplateNodes( _filter, _sl );
			Resume( HistTemplateNodesById(co_await Any(select)) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α Gateway::NodeHolderAwait::Select()ι->VoidTask{
		try{
			auto select = SelectHistGroupNodes( {.Connection=_connection, .Node=_text}, _sl );
			auto holders = co_await Any( select );
			NodeHolder y;
			if( holders.size() ){
				auto& row = y.Row.emplace( move(holders.front()) );
				y.Kind = row.Group!=_group ? NodeHolder::EKind::OtherGroup
					: _member && row.TemplateNode==_member ? NodeHolder::EKind::SameMember
					: NodeHolder::EKind::ThisGroup;
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α Gateway::FindHistTemplateNode( const flat_map<HistTemplateNodePK,HistTemplateNodeRow>& members, optional<HistTemplateNodePK> id )ι->const HistTemplateNodeRow*{
		auto p = id ? members.find( *id ) : members.end();
		return p==members.end() ? nullptr : &p->second;
	}
}