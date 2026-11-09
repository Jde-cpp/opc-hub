import { Field, FieldKind, Operator, TableSchema, View, ViewSet, ViewType } from 'jde-framework';
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
	static schema:TableSchema = new TableSchema( { name: "OpcWatch", fields: Object.keys( WatchView.columns ).map( name=>new Field({name, ofType: {kind: FieldKind.SCALAR, name: "String"}}) ) } );
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
		const y = rows.filter( r=>this.matches( r, names ) );
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
	matches( row:WatchRow, names:( slug:string )=>string ):boolean{
		return this.fieldFilters.every( ff=>{
			const values = ff.filter.value;
			if( !values.length )
				return true;
			const cell = WatchView.cellValue( row, ff.field.name, names );
			const text = cell==null ? "" : String( cell );
			switch( ff.filter.operator ){
				case Operator.NotIn:           return !values.some( v=>String(v)==text );
				case Operator.Less:            return WatchView.compare( cell, values[0] )<0;
				case Operator.LessOrEqual:     return WatchView.compare( cell, values[0] )<=0;
				case Operator.Greater:         return WatchView.compare( cell, values[0] )>0;
				case Operator.GreaterOrEqual:  return WatchView.compare( cell, values[0] )>=0;
				default:                       return values.some( v=>String(v)==text );
			}
		} );
	}
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
		const na = WatchView.toNumber( a ), nb = WatchView.toNumber( b );
		return !isNaN(na) && !isNaN(nb) ? na-nb : String( a ).localeCompare( String(b), undefined, {numeric: true, sensitivity: "base"} );
	}
	private static toNumber( x:unknown ):number{ return typeof x=="number" ? x : typeof x=="string" && x.trim()!=="" ? Number( x ) : NaN; }
}