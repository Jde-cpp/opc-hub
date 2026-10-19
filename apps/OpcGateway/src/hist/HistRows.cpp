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
			.Thresholds = HistThresholdColumns::Read( r, 5 )
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
		return { select(HistTable::GroupNodes, move(columns), move(where), sl), groupNodeRow, sl };
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
	α Gateway::FindHistTemplateNode( const flat_map<HistTemplateNodePK,HistTemplateNodeRow>& members, optional<HistTemplateNodePK> id )ι->const HistTemplateNodeRow*{
		auto p = id ? members.find( *id ) : members.end();
		return p==members.end() ? nullptr : &p->second;
	}
}