import { compareCells, Field, FieldKind, FilterHost, filterPredicate, TableSchema, View, ViewSet, ViewType } from 'jde-framework';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import { NodeView } from './node-view';
import { OpcError } from './opc-error';
import { discardText, filterText, queueText, samplingText, WatchRow } from './watch';
import { valueString } from './value';

//The watch list's view (clients spec *Watch list*):  as NodeView runs the children table, over a hand-built schema with the filters
//and sort applied in the browser.  Its columns are the spec's:  the connection, the node, the reading and its status and times,
//what the row asks and what the server revised it to, and the discard rule and filter.
export class WatchView extends View{
	static readonly collectionName = "opcWatch";//the profile rows under `opcWatch/views` and the browser's `opcWatch/viewNumber`:  one set of views for every gateway's list
	static columns:Record<string,string> = { connection: "Connection", node: "Node", value: "Value", status: "Status", sourceTime: "Source Time", serverTime: "Server Time", sampling: "Sampling (ms)", revisedSampling: "Revised Sampling", queue: "Queue", revisedQueue: "Revised Queue", discard: "Discard", filter: "Filter" };
	//the columns by what they hold, as NodeView's:  numbers for what a row asks and is revised to, enums for the small sets, the two times
	static types:Record<string,{kind:FieldKind, name:string}> = { connection: {kind: FieldKind.ENUM, name: "Connection"}, status: {kind: FieldKind.ENUM, name: "StatusCode"}, discard: {kind: FieldKind.ENUM, name: "Discard"}, sourceTime: {kind: FieldKind.SCALAR, name: "DateTime"}, serverTime: {kind: FieldKind.SCALAR, name: "DateTime"}, sampling: {kind: FieldKind.SCALAR, name: "Float"}, revisedSampling: {kind: FieldKind.SCALAR, name: "Float"}, queue: {kind: FieldKind.SCALAR, name: "UInt"}, revisedQueue: {kind: FieldKind.SCALAR, name: "UInt"} };
	static schema:TableSchema = new TableSchema( { name: "OpcWatch", fields: Object.keys( WatchView.columns ).map( name=>new Field({name, ofType: WatchView.types[name] ?? {kind: FieldKind.SCALAR, name: "String"}}) ) } );
	static default():WatchView{
		const view = WatchView.fromConfig( {configColumns: ["connection", "node", "value", "status", {name: "sourceTime", hidden: true}, {name: "serverTime", hidden: true}, "sampling", "revisedSampling", {name: "queue", hidden: true}, {name: "revisedQueue", hidden: true}, {name: "discard", hidden: true}, "filter"], sort: [{active: "connection", direction: "asc"}, {active: "node", direction: "asc"}]}, WatchView.schema );
		view.type = ViewType.System;
		return view;
	}
	get displayedColumns():string[]{ return this.fields.filter( f=>f.displayed ).map( f=>f.name ); }
	static viewSet( profileStore:ProfileStore, prefs:LocalPrefs ):ViewSet<WatchView>{
		return new ViewSet( {type: WatchView, key: WatchView.collectionName, profileStore, prefs} );
	}
	static loadViews( views:ViewSet<WatchView> ):Promise<void>{
		return views.load( [WatchView.default()], WatchView.schema );
	}
	//the rows in view order:  filtered, then sorted by each Sort in turn.  `names` gives a connection's name by its slug.
	apply( rows:readonly WatchRow[], names:( slug:string )=>string ):WatchRow[]{
		const y = rows.filter( this.predicate(names) );
		const sorts = this.sort.filter( s=>s.active && s.direction );
		if( sorts.length ){
			y.sort( (a,b)=>{
				for( const s of sorts ){
					const c = WatchView.compare( WatchView.cellValue(a, s.active, names), WatchView.cellValue(b, s.active, names) );
					if( c )
						return s.direction=="asc" ? c : -c;
				}
				return 0;
			} );
		}
		return y;
	}
	override filterHost():FilterHost{ return { ...super.filterHost()!, server: false, enums: undefined }; }
	//the filter's terms as a test of a row, as NodeView's
	predicate( names:( slug:string )=>string ):( row:WatchRow )=>boolean{
		return filterPredicate( this.parsedFilter().terms, this.filterHost(), {cell: ( row:WatchRow, column )=>WatchView.cellValue(row, column, names), time: NodeView.time} );
	}
	matches( row:WatchRow, names:( slug:string )=>string ):boolean{ return this.predicate( names )( row ); }
	//what a column shows for a row - the filter and sort work on this, so they agree with the screen
	static cellValue( row:WatchRow, column:string, names:( slug:string )=>string ):string|number|undefined{
		switch( column ){
			case "connection":      return names( row.opc );
			case "node":            return row.name;
			case "value":           return row.valueName ?? (typeof row.value=="number" ? row.value : row.value===undefined ? undefined : valueString( <any>row.value ));
			case "status":          return row.error ? row.error : row.sc==undefined ? undefined : OpcError.text( row.sc );
			case "sourceTime":      return NodeView.time( row.sourceTime );
			case "serverTime":      return NodeView.time( row.serverTime );
			case "sampling":        return Number( samplingText( row.parameters ) );
			case "revisedSampling": return row.revised?.samplingInterval;
			case "queue":           return Number( queueText( row.parameters ) );
			case "revisedQueue":    return row.revised?.queueSize;
			case "discard":         return discardText( row.parameters );
			case "filter":          return filterText( row.parameters );
		}
		return undefined;
	}
	private static compare( a:unknown, b:unknown ):number{
		if( a==null || b==null )
			return a==null ? (b==null ? 0 : -1) : 1;
		return compareCells( a, b );
	}
}