import { arraysEqual, verify } from '../../utils/utils';
import { StringUtils } from '../../utils/string-utils';
import { Query } from '../../services/graphql';
import { TableSettings } from '../../services/ql-list-resolver';
import { Field, FieldKind } from "./schema/field";
import { TableSchema } from "./schema/table-schema";
import { Sort } from "@angular/material/sort";

export class Days{
	constructor( startDate:Date ){
		let eod = new Date();
		eod.setHours( 0, 0, 0, 0 );
		eod.setDate( eod.getDate()+1 );
		let bod = new Date( startDate );
		bod.setHours( 0, 0, 0, 0 );
		this.days = Math.round( (eod.getTime()-bod.getTime())/(1000*60*60*24) );
	}
	static fromJson( json:{days:number} ):Days{
		let y = new Days( new Date() );
		y.days = json.days;
		return y;
	}
	fromNow():Date{
		let start = new Date();
		start.setHours( 0, 0, 0, 0 );
		start.setDate( start.getDate()-this.days+1 );
		return start;
	}
	days:number;
}
export type DbScalar = string|number|boolean|null|Date|Days;
export enum Operator{
	None,
	In,
	NotIn,
	Less,
	LessOrEqual,
	Greater,
	GreaterOrEqual,
	Regex,
	ElementMatch,
	Between
}
//hidden = queried but not necessarily shown eg id
//A column whose values are a small closed set reads as a chip rather than as text - the same treatment the log table gives
//its level.  The map is value -> tone, not value -> colour:  the palette belongs to the table (graphql-table.scss), so a
//caller's own vocabulary ("Connected", "Idle") never reaches the framework's stylesheet.  A value the map does not name
//falls back to the theme's default chip.
export type ChipTone = "ok"|"neutral"|"error";
export type ChipTones = Record<string,ChipTone>;
//The `deleted` column rendered as a switch in the row rather than as a timestamp.  The switch is ON for a LIVE row, so the
//column is named for what being live MEANS on that table - on `resources` a live row is an ENFORCED one, since the
//authorizer skips a deleted resource (Authorize::Test, "not enabled") - and flipping it runs the same restore/delete
//mutations the detail pages use.  For a table whose soft-delete flag is the feature rather than a trash can, and where
//there is consequently no detail page to put a Delete button on.  `enable`/`disable` are the verbs the confirmation,
//tooltip and aria-label read; the messages spell out the consequence, which is domain wording the framework cannot guess.
export type LiveToggle = { enable:string, disable:string, enableMessage?:string, disableMessage?:string };
export type ViewFieldSettings = {name?:string, displayName?:string, style?:Style, defaultView?:boolean/*=!hidden*/, hidden?:boolean, selection?:string/*OBJECT/UNION sub-selection, e.g. "count" -> `name{count}`; defaults to "id name"/"id"*/, chip?:ChipTones, liveToggle?:LiveToggle};
type ViewFieldJson = {name:string, hidden?:boolean, displayName?:string, style?:Style, selection?:string, chip?:ChipTones, liveToggle?:LiveToggle};
export type Filter = { operator: Operator, value: DbScalar[] };
export class Flex{
	constructor( value:string|number ){
		if( typeof value === "string" ){
			if( value=="1" ){
				this.flexGrow = this.flexShrink = 1;
				this.flexBasis = "0%";
			}else if( value=="initial" ){
				this.flexGrow = 0;
				this.flexShrink = 1;
				this.flexBasis = "auto";
			}
			else if( value=="none" ){
				this.flexGrow = this.flexShrink = 0;
				this.flexBasis = "auto";
			}
			else {
				let parts = value.split(" ");
				verify( parts.length==3, `Invalid flex string: ${value}` );
				this.flexGrow = parseFloat( parts[0] );
				this.flexShrink = parseFloat( parts[1] );
				this.flexBasis = parts[2];
			}
		}else{
			this.flexBasis = `${value}px`;
		}
	}
	toString(){
		return `${this.flexGrow ?? 0} ${this.flexShrink ?? 0} ${this.flexBasis ?? "auto"}`;
	}
	flexGrow?:number;
	flexShrink?:number;
	flexBasis?:string
};
//How a column's header and its cells sit in the width the flex row gives them.  Both are flex containers, so `text-align`
//alone never moves the content - graphql-table.columnStyle() expands this to justify-content as well, and writes the pair
//onto the header and the cell alike, which is what keeps a heading over its values.
export type Align = "left"|"center"|"right";
export class Style{
	constructor( value:Partial<Style>|number ){
		if( typeof value === "number" )
			this.flex = new Flex( value );
		else
			Object.assign( this, value );
	}
	toJSON(){
		return { ...(this.flex ? {flex: this.flex.toString()} : {}), ...(this.align ? {align: this.align} : {}) };//a saved view keeps both, or a user view reloads left-aligned while the default view it was edited from is not
	}
	flex?: Flex;
	align?: Align;
}
export class ViewField{
	constructor( args: {qlField: Field, settings?: ViewFieldSettings}|{field: ViewFieldJson, schema: TableSchema}|ViewField ){
		if( args instanceof ViewField ){
			const copyFrom = args as ViewField;
			this.qlField = copyFrom.qlField;
			this.style = copyFrom.style;
			this._displayed = copyFrom._displayed;
			this.displayName = copyFrom.displayName;
			this.selection = copyFrom.selection;
			this.chip = copyFrom.chip;
			this.liveToggle = copyFrom.liveToggle;
		}else if( "qlField" in args ){
			const settings = args.settings;
			this.qlField = args.qlField;
			this.style = settings?.style;
			this._displayed = settings?.hidden ? false : undefined;
			this.displayName = settings?.displayName ?? StringUtils.idToDisplay( this.name );
			this.selection = settings?.selection;
			this.chip = settings?.chip;
			this.liveToggle = settings?.liveToggle;
		}else{
			const serialized = args as {field: ViewFieldJson, schema: TableSchema};
			const json = serialized.field;
			this.qlField = serialized.schema.fields.find( f=>f.name==json.name )!;
			this.style = json.style;
			this._displayed = !json.hidden;//explicit, not the type default: toJson always stamps hidden when a field isn't displayed, so no flag means displayed - the default would re-hide an ID/list column the user checked
			this.displayName = json.displayName ?? StringUtils.idToDisplay( this.name );
			this.selection = json.selection;
			this.chip = json.chip;
			this.liveToggle = json.liveToggle;
		}
	}
	toJson( customDisplay:boolean=false ):ViewFieldJson{
		let y: ViewFieldJson = { name: this.name };
		if( !this.displayed )
			y["hidden"] = true;
		if( customDisplay )
			y["displayName"] = this.displayName;
		if( this.style )
			y["style"] = this.style;
		if( this.selection )
			y["selection"] = this.selection;
		if( this.chip )
			y["chip"] = this.chip;
		if( this.liveToggle )
			y["liveToggle"] = this.liveToggle;//persisted like `chip`:  a user view saved off the resources list has to come back still holding its Enforced switch, not a bare timestamp
		return y;
	}
	//what query() emits for this field: composite kinds need a sub-selection - an explicit one from settings, else the framework's {id name}/{id} convention (proto.service.fieldColumns).
	get queryText():string{
		const selection = this.selection ?? (this.type.underlyingKind==FieldKind.OBJECT ? "id name" : this.type.underlyingKind==FieldKind.UNION ? "id" : undefined);
		return selection ? `${this.name}{${selection}}` : this.name;
	}
	get displayed():boolean{ return this._displayed ?? (this.type.ofType?.name!="ID" && this.name!="attributes" && this.type.kind!=FieldKind.LIST); }
	set displayed(x){this._displayed=x;} private _displayed:boolean|undefined;

	qlField: Field;
	get name(){ return this.qlField?.name; }
	displayName:string;
	get type(){ return this.qlField.type; }
	style?: Style;
	selection?:string;
	chip?:ChipTones;
	liveToggle?:LiveToggle;
};

//A filter a route declares on a system view (TableSettings.views):  the column, the operator (In when unset) and the values,
//in the same vocabulary the settings panel uses - "<null>"/"<not null>" included.
export type ViewFilterSettings = { name:string, operator?:Operator, value:DbScalar[] };
//A further system view a route declares beside the default one.  Whatever it leaves unset comes from the default view:  the
//table's columns and its sort.  The default view itself is the TableSettings' own columns/sort under `viewName`.
export type ViewSettings = { name:string, columns?:(string|ViewFieldSettings)[], sort?:Sort[]|string, filters?:ViewFilterSettings[] };
type ViewConfigArgs = { name?:string, configColumns:(string|ViewFieldSettings)[], sort:Sort[], filters?:ViewFilterSettings[] };
type ViewJson = { name:string|undefined, collectionName:string, fields:ViewFieldJson[], filters:{ field?: Field, name:string, filter: Filter }[], limit?:number, showSelector:boolean|undefined, sort:Sort[]|undefined };
export type ViewSerializedArgs = { name:string|undefined, collectionName:string, fields:ViewField[], limit?:number, showSelector:boolean, sort?:Sort[], filters?:{name: string, filter: Filter}[] };//sort optional:  toJson omits one that matched the default view's
export type FieldFilter = { field: Field, filter: Filter };
export enum ViewType{
	System,
	Adhoc,
	User
}
export class View{
	constructor( value:ViewConfigArgs|ViewSerializedArgs|View|TableSettings, schema?:TableSchema, defaultSort?:Sort[] ){
		if( value instanceof View )
			this.copyConstructor( value as View );
		else if( (value as ViewConfigArgs).configColumns )
			this.configConstructor( value as ViewConfigArgs, schema! );
		else if( (value as ViewSerializedArgs).fields )
			this.serializedConstructor( value as ViewSerializedArgs, schema!, defaultSort );
		else if( (value as TableSettings).columns )
			this.tableConstructor( value as TableSettings, schema! );//the cast, as above: TableSettings.filters and ViewSerializedArgs.filters differ in shape, so the union no longer narrows by itself
	}
	//The settings dialog edits filters in place, so Cancel is only honest if the dialog never holds the live view's Filter objects.  structuredClone is unusable: it flattens Days into a plain object and breaks the `instanceof` checks in the filter UI and query().
	static copyFilter( filter:Filter ):Filter{
		return { operator: filter.operator, value: (filter.value ?? []).map( v=>v instanceof Days ? Days.fromJson({days:v.days}) : v instanceof Date ? new Date(v) : v ) };//`?? []` so a stale persisted view without a value array can't turn a copy into a load-time crash
	}
	private copyConstructor( view:View ):void{
		this.name = view.name;
		this.collectionName = view.collectionName;
		this.limit = view.limit;
		this.fields = [...view.fields];
		if( view.fieldFilters )
			this.fieldFilters = view.fieldFilters.map( ff=>({field: ff.field, filter: View.copyFilter(ff.filter)}) );//`field` is shared schema metadata, but the filter is per-view state: a shallow [...] left the "copy" pointing at the original's Filter objects and value arrays, so it protected nothing
		this.showSelector = view.showSelector;
		this.sort = structuredClone( view.sort );
		this.type = view.type;
	}
	private configConstructor( config:ViewConfigArgs, schema:TableSchema ):void{
		this.name = config.name;
		this.sort = config.sort;
		this.collectionName = schema.collectionName;
		this.fields = this.columns( schema, config.configColumns, ["id", "attributes"] );
		for( const f of config.filters ?? [] ){//a route's filter names a column that must exist - unlike a saved view's, which is dropped with a warning, this is a config error
			const field = schema.fields.find( x=>x.name==f.name ); verify( field, `View '${config.name}' (${schema.collectionName}) filters on '${f.name}' - not in the schema.` );
			this.fieldFilters.push( {field, filter: {operator: f.operator ?? Operator.In, value: [...f.value]}} );
		}
		this.appendAlwaysQueried( schema );
	}
	private serializedConstructor( config:ViewSerializedArgs, schema:TableSchema, defaultSort:Sort[]|undefined ):void{
		this.name = config.name;
		this.collectionName = config.collectionName;
		if( config.limit )
			this.limit = config.limit;
		const missing:(string|undefined)[] = [];
		for( const f of config.fields ){
			if( schema.fields.some(s=>s.name==f.name) )
				this.fields.push( new ViewField({field: f, schema: schema}) );
			else
				missing.push( f.name );
		}
		for( let fieldFilter of config.filters ?? [] ){//{name: string, filter: Filter}
			const field = schema.fields.find( f=>f.name==fieldFilter.name );
			if( !field ){
				missing.push( fieldFilter.name );
				continue;
			}
			if( field.isDateTime )//JSON round-trip leaves Days as {days:n} and Date as an ISO string — the filter UI's instanceof checks need real instances
				fieldFilter.filter.value = fieldFilter.filter.value.map( v=>View.reviveDateValue(v) );
			this.fieldFilters.push( {field: field, filter: fieldFilter.filter} );
		}
		if( missing.length )
			console.warn( `View '${config.name}' (${config.collectionName}) references ${missing.join(", ")} - not in the schema, dropped.` );

		this.showSelector = config.showSelector;
		this.sort = config.sort ?? defaultSort ?? [];
		this.type = ViewType.User;
		this.appendAlwaysQueried( schema );//toJson only persists displayed fields, so a saved view arrives without id/slug
	}
	private tableConstructor(config:TableSettings, schema:TableSchema){
		this.collectionName = schema.collectionName;
		this.fields = this.columns( schema, config.columns!, config.excludedColumns ?? ["id", "attributes"] );
		this.sort = View.toSort( config.sort ) ?? [];
		this.appendAlwaysQueried( schema );
	}
	//TableSettings/ViewSettings accept a bare column name as shorthand for an ascending sort on it
	static toSort( sort:Sort[]|string|undefined ):Sort[]|undefined{
		return typeof sort=="string" ? [{active: sort, direction: "asc"}] : sort;
	}
	//id is the mutation key, slug the navigation key (ql-list.onRowActivate) and deleted drives show-deleted - query() asks for them whether or not they are displayed, so every view must carry them
	private appendAlwaysQueried( schema:TableSchema ):void{
		for( let field of schema.fields.filter( f=>["id","deleted","slug"].includes(f.name) && !this.fields.find(c=>c.name==f.name) ) ){
			const viewField = new ViewField( {qlField:field, settings: {name: field.name, hidden: true}} );
			if( field.name=="id" )
				this.fields.unshift( viewField );
			else
				this.insertBeforeDescription( viewField );//deleted becomes visible with show-deleted, and columns() keeps description last
		}
	}
	private insertBeforeDescription( field:ViewField ):void{
		const description = this.fields.findIndex( f=>f.name=="description" );
		if( description<0 )
			this.fields.push( field );
		else
			this.fields.splice( description, 0, field );
	}
	private columnsToQuery( excludedColumns:string[], includeDeleted:boolean ):ViewField[]{
		return this.fields.filter( (x)=>
			(x.displayed || x.name=="id")
		&& !excludedColumns?.includes(x.name)
		&& (includeDeleted || x.name!="deleted") );
	}
	private columnsToQuerySorted( orderedSpec:string[], excludedColumns:string[]=[], includeDeleted:boolean=false ):ViewField[]{
		const notInSpec = this.fields.filter( f=>!orderedSpec.includes(f.name) ).map( f=>f.name );
		let sort = [...orderedSpec, ...notInSpec ];
		const f = ( x:ViewField,y:ViewField )=>{ return sort.indexOf( x.name )-sort.indexOf( y.name ); };
		return this.columnsToQuery( excludedColumns, includeDeleted ).sort( f );
	}
	private columns( schema:TableSchema, configColumns:(string|ViewFieldSettings)[], defaultHidden:string[] ):ViewField[]{
		let selectCols = [];
		let description; //want last
		for( let col of configColumns ){
			const fieldName = typeof col=="string" ? col : col.name;
			const settings = typeof col=="string" ? {} : col;
			const field = schema.fields.find( f=>f.name==fieldName )!; verify(field);
			const viewField = new ViewField( {qlField: field, settings: settings} );
			if( field.name=="description" )
				description = viewField;
			else
				selectCols.push( new ViewField({qlField:field, settings: settings}) );
		}
		if( description )
			selectCols.push( description );
		return selectCols;
	}
	private static comparisonOperator( op:Operator ):string|undefined{
		switch( op ){
			case Operator.Less:           return "lt";
			case Operator.LessOrEqual:    return "lte";
			case Operator.Greater:        return "gt";
			case Operator.GreaterOrEqual: return "gte";
			case Operator.NotIn:          return "nin";
			default: return undefined;//In keeps the bare-array form the server treats as `in`
		}
	}
	private static comparisonJson( v:DbScalar ):DbScalar{
		if( v instanceof Days )
			v = v.fromNow();//resolve the relative "last N days" at query time
		return v instanceof Date ? v.toISOString() : v;
	}
	private static reviveDateValue( v:DbScalar ):DbScalar{
		if( v==null || v instanceof Date || v instanceof Days )
			return v;
		if( typeof v=="string" ){
			const d = new Date( v );
			return isNaN( d.getTime() ) ? v : d;//"<not null>" etc. stay strings
		}
		return typeof v=="object" && "days" in (v as object) ? Days.fromJson( <{days:number}>v ) : v;
	}
	query( showDeleted:boolean, skip:number ):Query{
		let deletedField = this.fields.find( f=>f.name=="deleted" );
		if( deletedField )
				deletedField.displayed = showDeleted;

		let fieldStr = this.fields.filter( f=>f.displayed || f.name=="id" || f.name=="slug" ).map( f=>f.queryText ).join(" ");//id/slug are queried even when hidden:  id is the mutation key, slug the navigation key
		let args = [];
		let vars:Record<string, DbScalar[]|DbScalar|null> = {};
		if( this.limit )
			args.push( `limit:${this.limit}` );
		if( skip )
			args.push( `skip:${skip}` );
		if( this.sort?.length ){
			let sortStr = '';
			for( let s of this.sort )
				sortStr += `{${s.active}:${StringUtils.qlString(s.direction)}},`;//`active` is a FIELD NAME - identifiers are not quoted and must not be escaped; only the direction is a string literal
			args.push( `orderBy:[${sortStr.slice(0,-1)}]` );
		}

		for( const fieldFilter of this.fieldFilters ){
			const filter = fieldFilter.filter;
			const values = filter.value;
			if( !values.length )
				continue;
			const name = fieldFilter.field.name;
			if( values.length==1 && values[0]=="<not null>" )
				args.push( `${name}:{"ne":null}` );
			else if( values.length==1 && values[0]=="<null>" && View.comparisonOperator(filter.operator)!="nin" ){//under any operator but NotIn (which means not null):  a DateTime column offers only < and >, and `{gt:"<null>"}` reached the server as a timestamp to parse - the bare-array form is the one it reads as `is null` (reviews/m3-closing.md #16)
				args.push( `${name}:$${name}` );
				vars[name] = [null];
			}
			else{
				const op = View.comparisonOperator( filter.operator );
				if( op=="nin" ){//NotIn takes the full value array (server nin now fixed)
					args.push( `${name}:{nin:$${name}}` );
					vars[name] = values.map( v=>v=="<null>" ? null : v );
				}
				else if( op ){
					args.push( `${name}:{${op}:$${name}}` );
					vars[name] = View.comparisonJson( values[0] );
				}
				else{//In → bare-array form the server treats as `in`
					args.push( `${name}:$${name}` );
					//colSuggestions() offers "<null>" for a nullable column, and it has to leave as a JSON null:  QL::ToWhereClause
					//turns a null array element into `is null`, while the literal string went out as `in ('<null>')` and matched nothing.
					vars[name] = values.map( v=>v=="<null>" ? null : v );
				}
			}
		}
		const iDeletedArg = args.findIndex( a=>a.startsWith("deleted:") );
		//Show deleted wins over a filter on the trash-can column - the checkbox is the way out.  A live-toggle page has no checkbox:
		//`deleted` is its switch (the Resources page's Enforced), it queries with showDeleted always on, and a filter on it is the
		//point - it used to be spliced out, so "enforced only" listed everything (reviews/m3-closing.md #16).
		const liveToggle = this.fields.some( f=>f.name=="deleted" && f.liveToggle );
		if( showDeleted && iDeletedArg!=-1 && !liveToggle ){
			args.splice( iDeletedArg, 1 );
			delete vars["deleted"];
		}
		else if( !showDeleted && iDeletedArg==-1 ){
			args.push( `deleted:$deleted` );
			vars["deleted"] = null;
		}

		return { text: `${this.collectionName}${args.length>0 ? `(${args.join(",")})` : ""}{ ${fieldStr} }`, vars: vars };
	}
	append( fields: Field[] ){
		for( let field of fields.filter(f=>!this.fields.find(v=>v.name==f.name)) )
			this.fields.push( new ViewField({qlField: field, settings: {name: field.name, hidden: true}}) );
	}
	//the same filters in any order:  a view that was only re-sorted or re-columned shows the same rows
	static sameFilters( a:View, b:View ):boolean{
		const keys = ( v:View )=>v.fieldFilters.map( ff=>JSON.stringify([ff.field.name, ff.filter.operator, ff.filter.value]) ).sort();
		return arraysEqual( keys(a), keys(b) );
	}
	setDeletedDisplayed( show:boolean ){ this.fields.find(f=>f.name=="deleted")!.displayed = show; }
	toJson( defaultSettings:TableSettings|undefined ):ViewJson{
		let fields = [];
		for( let field of this.fields ){
			const settings = defaultSettings?.columns!.find( c=>typeof c=="string" ? null : c.name==field.name ) as ViewFieldSettings;
			const customDisplay = (settings?.displayName ?? StringUtils.idToDisplay( field.name ))!=field.displayName;
			if( field.displayed || customDisplay )
				fields.push( field.toJson(customDisplay) );
		}
		let filters = new Array<{field?: Field, name:string,filter:Filter}>;
		for( let ff of this.fieldFilters )
			filters.push( {name: ff.field.name, filter: ff.filter} );
		let sort = defaultSettings && JSON.stringify(this.sort)==JSON.stringify(defaultSettings.sort) ? undefined : this.sort;
		return {
			name: this.name,
			filters: filters,
			collectionName: this.collectionName,
			fields: fields,
			limit: this.limit,
			showSelector: this.showSelector,
			sort: sort
		};
	}
	fieldFilters:FieldFilter[] = [];
	name:string|undefined;
	collectionName!:string;
	fields:ViewField[]=[];
	limit:number=25;
	get isUser(){ return this.type==ViewType.User; }
	get isSystem(){ return this.type==ViewType.System; }
	get isAdhoc(){ return this.type==ViewType.Adhoc; }
	showSelector:boolean=false;
	sort:Sort[] = [];
	type=ViewType.System;
};
