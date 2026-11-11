import { verify } from '../../utils/utils';
import { StringUtils } from '../../utils/string-utils';
import { Query } from '../../services/graphql';
import { TableSettings } from '../../services/ql-list-resolver';
import { Field, FieldKind } from "./schema/field";
import { filterArguments, FilterHost, FilterParse, FilterTerm, parseFilter, serverHasField, toggleFilterTerm } from './filter-query';
import { SliceCount, SliceValue } from './slice';
import type { Grouping } from './group';
import { TableSchema } from "./schema/table-schema";
import { Sort, SortDirection } from "@angular/material/sort";

//hidden = queried but not necessarily shown eg id
//A column whose values are a small closed set reads as a chip rather than as text - the same treatment the log table gives
//its level.  The map is value -> tone, not value -> colour:  the palette belongs to the table (graphql-table.scss), so a
//caller's own vocabulary ("Connected", "Idle") never reaches the framework's stylesheet.  A value the map does not name
//falls back to the theme's default chip.
export type ChipTone = "ok"|"neutral"|"warn"|"error"|"critical";
export type ChipTones = Record<string,ChipTone>;
//The `deleted` column rendered as a switch in the row rather than as a timestamp.  The switch is ON for a LIVE row, so the
//column is named for what being live MEANS on that table - on `resources` a live row is an ENFORCED one, since the
//authorizer skips a deleted resource (Authorize::Test, "not enabled") - and flipping it runs the same restore/delete
//mutations the detail pages use.  For a table whose soft-delete flag is the feature rather than a trash can, and where
//there is consequently no detail page to put a Delete button on.  `enable`/`disable` are the verbs the confirmation,
//tooltip and aria-label read; the messages spell out the consequence, which is domain wording the framework cannot guess.
export type LiveToggle = { enable:string, disable:string, enableMessage?:string, disableMessage?:string };
export type ViewFieldSettings = {name?:string, displayName?:string, style?:Style, defaultView?:boolean/*=!hidden*/, hidden?:boolean, selection?:string/*OBJECT/UNION sub-selection, e.g. "count" -> `name{count}`; defaults to "id name"/"id"*/, chip?:ChipTones, liveToggle?:LiveToggle};
type ViewFieldJson = {name:string, hidden?:boolean, displayName?:string, style?:Style, selection?:string, chip?:ChipTones};
export class Flex{
	constructor( value:string|number|Flex ){
		if( value instanceof Flex )
			Object.assign( this, value );
		else if( typeof value === "string" ){
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
	//the basis in pixels, where it is one
	get px():number|undefined{ return this.flexBasis?.endsWith( "px" ) ? parseFloat( this.flexBasis ) : undefined; }
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
		else{
			Object.assign( this, value );
			if( value.flex!=null )//its own:  a view's copy is resized without reaching the view.  A saved view's is the string toJSON wrote.
				this.flex = new Flex( value.flex );
		}
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
			this.style = copyFrom.style ? new Style( copyFrom.style ) : undefined;
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
		return y;
	}
	//what query() emits for this field: composite kinds need a sub-selection - an explicit one from settings, else the framework's {id name}/{id} convention (proto.service.fieldColumns).
	get queryText():string{
		const selection = this.selection ?? (this.type.underlyingKind==FieldKind.OBJECT ? "id name" : this.type.underlyingKind==FieldKind.UNION ? "id" : undefined);
		return selection ? `${this.name}{${selection}}` : this.name;
	}
	//the width a view sets, or the route:  undefined where the column's sheet has it
	get flex():string|undefined{ return this.style?.flex!=null ? `${this.style.flex}` : undefined; }
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

//A further system view a route declares beside the default one.  Whatever it leaves unset comes from the default view:  the
//table's columns and its sort.  The default view itself is the TableSettings' own columns/sort under `viewName`.  `filter` is
//the filter box's text, in its grammar (filter-query.ts):  "no:issuer", "provider:Key".  `sliceBy` is the column its slice panel
//lists, `groupBy` the column its rows are grouped by and `sums` the columns a group's header sums, its own and not the default
//view's.
export type ViewSettings = { name:string, columns?:(string|ViewFieldSettings)[], sort?:Sort[]|string, filter?:string, sliceBy?:string, groupBy?:string, sums?:string[] };
type ViewConfigArgs = { name?:string, configColumns:(string|ViewFieldSettings)[], sort:Sort[], filter?:string, queried?:string[], sliceBy?:string, groupBy?:string, sums?:string[] };
type ViewJson = { name:string|undefined, collectionName:string, fields:ViewFieldJson[], filter?:string, limit?:number, showSelector:boolean|undefined, sort:Sort[]|undefined, sliceBy?:string, groupBy?:string, sums?:string[] };
export type ViewSerializedArgs = { name:string|undefined, collectionName:string, fields:ViewField[], limit?:number, showSelector:boolean, sort?:Sort[], filter?:string, sliceBy?:string, groupBy?:string, sums?:string[] };//sort optional:  toJson omits one that matched the default view's
export enum ViewType{
	System,
	Adhoc,
	User
}
//A view class - View, LogView, NodeView - as the statics below and ViewSet take it:  a static runs on the class it is called on,
//so LogView.copy( view ) makes a LogView.
export type ViewClass<V extends View> = { prototype:V } & Pick<typeof View, "copy"|"fromJson">;
export class View{
	//Made by the statics, one per source.  The constructor took the four sources as one union and told them apart by shape,
	//which TypeScript could no longer narrow.  Protected, so LogView and NodeView extend it.
	protected constructor(){}
	static copy<V extends View>( this:{prototype:V}, view:View ):V{ return View.#make( this, y=>y.copyFrom(view) ); }
	//a route's view:  its columns by name or settings, its sort and filters
	static fromConfig<V extends View>( this:{prototype:V}, config:ViewConfigArgs, schema:TableSchema ):V{ return View.#make( this, y=>y.fromConfig(config, schema) ); }
	//a saved view, as toJson wrote it;  `defaultView` is the route's, for what toJson leaves out
	static fromJson<V extends View>( this:{prototype:V}, json:ViewSerializedArgs, schema:TableSchema, defaultView?:View ):V{ return View.#make( this, y=>y.fromJson(json, schema, defaultView) ); }
	//a route's TableSettings, which carry no name
	static fromTable<V extends View>( this:{prototype:V}, settings:TableSettings, schema:TableSchema ):V{ return View.#make( this, y=>y.fromTable(settings, schema) ); }
	static #make<V extends View>( type:{prototype:V}, init:( view:V )=>void ):V{
		const y = new ( type as unknown as new()=>V )();
		init( y );
		return y;
	}
	private copyFrom( view:View ):void{
		this.number = view.number;
		this.name = view.name;
		this.collectionName = view.collectionName;
		this.limit = view.limit;
		this.fields = view.fields.map( f=>new ViewField(f) );//its own fields, and their styles:  a draft hides and resizes without reaching the view it is of
		this.filter = view.filter;
		this.fixedFilter = view.fixedFilter;
		this.schema = view.schema;
		this.showSelector = view.showSelector;
		this.sort = structuredClone( view.sort );
		this.type = view.type;
		this.queried = view.queried;
		this.sliceBy = view.sliceBy;
		this.slice = view.slice;
		this.groupBy = view.groupBy;
		this.sums = [...view.sums];
	}
	private fromConfig( config:ViewConfigArgs, schema:TableSchema ):void{
		this.name = config.name;
		this.sort = config.sort;
		this.queried = config.queried ?? [];
		this.collectionName = schema.collectionName;
		this.fields = this.columns( schema, config.configColumns, ["id", "attributes"] );
		this.schema = schema;
		this.appendAlwaysQueried( schema );
		this.filter = config.filter?.trim() ?? "";
		const unread = this.parsedFilter().errors;//a route's filter must read - unlike a saved view's, whose bad term the box underlines, this is a config error
		verify( !unread.length, `View '${config.name}' (${schema.collectionName}) filters on '${this.filter}' - ${unread[0]?.message}` );
		this.#setRoute( config );
	}
	//a route's slice and group columns must be ones a list is sliced by, and its sums numbers - a config error, as its filter's
	#setRoute( config:{sliceBy?:string, groupBy?:string, sums?:string[]} ):void{
		for( const by of [config.sliceBy, config.groupBy] )
			verify( !by || this.sliceable(by), `View '${this.name}' (${this.collectionName}) slices or groups by '${by}', which is not a number, a boolean or an enum.` );
		const unsummed = config.sums?.find( name=>!this.summable(name) );
		verify( !unsummed, `View '${this.name}' (${this.collectionName}) sums '${unsummed}', which is not a number.` );
		this.sliceBy = config.sliceBy;
		this.groupBy = config.groupBy;
		this.sums = [...config.sums ?? []];
		this.#queryGrouped();
	}
	private fromJson( config:ViewSerializedArgs, schema:TableSchema, defaultView:View|undefined ):void{
		this.name = config.name;
		this.collectionName = config.collectionName;
		if( config.limit )
			this.limit = config.limit;
		const missing:(string|undefined)[] = [];
		for( const f of config.fields ){
			if( !schema.fields.some(s=>s.name==f.name) ){
				missing.push( f.name );
				continue;
			}
			const field = new ViewField( {field: f, schema: schema} );
			if( f.displayName===undefined )//toJson leaves out the route's name, which the schema's must not take the place of
				field.displayName = defaultView?.fields.find( d=>d.name==f.name )?.displayName ?? field.displayName;
			this.fields.push( field );
		}
		if( missing.length )
			console.warn( `View '${config.name}' (${config.collectionName}) references ${missing.join(", ")} - not in the schema, dropped.` );

		this.filter = config.filter ?? "";
		this.schema = schema;
		this.showSelector = config.showSelector;
		this.sort = config.sort ?? defaultView?.sort ?? [];
		this.type = ViewType.User;
		this.queried = defaultView?.queried ?? [];//the route's, as the live toggle below
		this.appendAlwaysQueried( schema );//toJson only persists displayed fields, so a saved view arrives without id/slug
		//The route owns the live toggle, not the saved view:  toJson drops a hidden Enforced column, and `deleted` re-added
		//without it had query() filter every unenforced resource out.
		for( const field of this.fields )
			field.liveToggle = defaultView?.fields.find( f=>f.name==field.name )?.liveToggle;
		this.sliceBy = config.sliceBy && this.sliceable( config.sliceBy ) ? config.sliceBy : undefined;//a column since gone, or retyped, slices nothing
		this.groupBy = config.groupBy && this.sliceable( config.groupBy ) ? config.groupBy : undefined;//nor groups
		this.sums = ( Array.isArray(config.sums) ? config.sums : [] ).filter( name=>this.summable(name) );
		this.#queryGrouped();//toJson persists displayed fields alone
	}
	private fromTable( config:TableSettings, schema:TableSchema ):void{
		this.collectionName = schema.collectionName;
		this.schema = schema;
		this.fields = this.columns( schema, config.columns!, config.excludedColumns ?? ["id", "attributes"] );
		this.sort = View.toSort( config.sort ) ?? [];
		this.queried = config.rowLink?.fields ?? [];
		this.appendAlwaysQueried( schema );
		this.#setRoute( config );
	}
	//TableSettings/ViewSettings accept a bare column name as shorthand for an ascending sort on it
	static toSort( sort:Sort[]|string|undefined ):Sort[]|undefined{
		return typeof sort=="string" ? [{active: sort, direction: "asc"}] : sort;
	}
	//id is the mutation key, slug the navigation key (ql-list.onRowActivate), deleted is what query() hides rows on and `queried`
	//what a route's rowLink builds the link from - so every view must carry them, displayed or not
	private appendAlwaysQueried( schema:TableSchema ):void{
		for( let field of schema.fields.filter( f=>["id","deleted","slug",...this.queried].includes(f.name) && !this.fields.find(c=>c.name==f.name) ) ){
			const viewField = new ViewField( {qlField:field, settings: {name: field.name, hidden: true}} );
			if( field.name=="id" )
				this.fields.unshift( viewField );
			else
				this.insertBeforeDescription( viewField );//the Deleted view shows it, and columns() keeps description last
		}
	}
	private insertBeforeDescription( field:ViewField ):void{
		const description = this.fields.findIndex( f=>f.name=="description" );
		if( description<0 )
			this.fields.push( field );
		else
			this.fields.splice( description, 0, field );
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
	//What the filter box names and how its terms are run (filter-query.ts):  the view's columns under their headers, then the
	//schema's others by their QL names, on the server.  Undefined for a view that takes no filter - the logs'.
	filterHost():FilterHost|undefined{
		const own = new Set( this.fields.map( f=>f.name ) );
		const rest = ( this.schema?.fields ?? [] ).filter( f=>!own.has(f.name) ).map( f=>({qlField: f, displayName: StringUtils.idToDisplay(f.name)}) );
		return { columns: [...this.fields.filter( f=>f.qlField ), ...rest], server: true, enums: this.schema?.enums };
	}
	//a column's value into the filter as `field:a,b`, or out of it:  a chip's click, and a pick of the header's `Filter by…`
	toggleFilterValue( name:string, value:string ):void{
		const host = this.filterHost(), field = this.fields.find( f=>f.name==name );
		if( host && field )
			this.filter = toggleFilterTerm( this.filter, host, field, value );
	}
	//the filter's terms and what the box could not read, the host's fixed terms after the view's
	parsedFilter():FilterParse{
		const host = this.filterHost();
		if( !host )
			return { terms: [], errors: [] };
		const typed = parseFilter( this.filter, host );
		return this.fixedFilter ? { terms: [...typed.terms, ...parseFilter(this.fixedFilter, host).terms], errors: typed.errors } : typed;
	}
	query( skip:number ):Query{
		const { fields, args, vars } = this.#queryParts( skip );
		return { text: `${this.collectionName}${args.length>0 ? `(${args.join(",")})` : ""}{ ${fields.join(" ")} }`, vars: vars };
	}
	//The slice (views spec *Slice by*).  A column a list is sliced by:  a number, a boolean or an enum, never an `ID` - a key has a
	//value a row - nor a live toggle, whose column is a switch.  LogView refuses every one.  A `Float` only where the browser
	//narrows the rows:  a server compares `in:[0.1]` with the value it stored, which the one it answered need not equal.
	sliceable( name:string ):boolean{
		const field = this.#field( name ), type = field?.type;
		return !!field && type!.kind!=FieldKind.LIST && type!.underlyingName!="ID" && ( field.isNumber || field.isBoolean || field.isEnum )
			&& !( type!.underlyingName=="Float" && this.filterHost()?.server!==false ) && !this.fields.find( f=>f.name==name )?.liveToggle;
	}
	//the columns `Slice by` offers, of those the host names, each as its header reads
	sliceChoices( offered:Record<string,string> ):{name:string, label:string}[]{
		return Object.keys( offered ).filter( name=>this.sliceable(name) ).map( name=>({name, label: this.columnLabel(name, offered)}) );
	}
	columnLabel( name:string, offered:Record<string,string> ):string{ return this.fields.find( f=>f.name==name )?.displayName ?? offered[name] ?? StringUtils.idToDisplay( name ); }
	//an enum slice column's values as its schema lists them:  the panel's order, where the ids carry one - a level, a status
	sliceOrder():string[]|undefined{
		const field = this.sliceBy ? this.#field( this.sliceBy ) : undefined;
		return field?.isEnum ? this.schema?.enums?.get( field.type.underlyingName )?.map( v=>v.name ) : undefined;
	}
	//The panel's counts:  a row per value of the slice column under the view's filter, with none of its sort, its page or its
	//slice - `users( groupBy:"provider", …filters ){ provider count }`.  None where the view is not sliced.
	sliceQuery():Query|undefined{
		return this.sliceBy ? this.#countQuery( this.sliceBy, false ) : undefined;
	}
	//the counts of `by`'s values:  under the view's filter and, `sliced`, its slice's value
	#countQuery( by:string, sliced:boolean ):Query{
		const { args, vars } = this.#filterParts( sliced );
		return { text: `${this.collectionName}(${[`groupBy:${StringUtils.qlString( by )}`, ...args].join( "," )}){ ${by} count }`, vars };
	}
	//The groups (views spec *Group by*).  A list is grouped by a column it is sliced by, the same rule (sliceable), and the group
	//column is its rows' first sort, ahead of the view's own, so the rows of a value are one run:  ascending, or as the view sorts
	//by the column.
	get ordered():Sort[]{
		const by = this.groupBy;
		return by ? [this.sort.find( s=>s.active==by && s.direction ) ?? {active: by, direction: "asc"}, ...this.sort.filter( s=>s.active!=by )] : this.sort;
	}
	//the view grouped by `name`, or by none
	setGroupBy( name:string|undefined ):void{
		this.groupBy = name;
		this.#queryGrouped();
	}
	//A column a group's header sums:  a number the schema marks as one, never an `ID` nor a live toggle.
	summable( name:string ):boolean{
		const field = this.#field( name );
		return !!field && field.isNumber && field.type.kind!=FieldKind.LIST && field.type.underlyingName!="ID" && !this.fields.find( f=>f.name==name )?.liveToggle;
	}
	//the columns `Field sum` offers, of those the host names
	sumChoices( offered:Record<string,string> ):{name:string, label:string}[]{
		return Object.keys( offered ).filter( name=>this.summable(name) ).map( name=>({name, label: this.columnLabel(name, offered)}) );
	}
	//a column into the sums, after the others, or out of them
	toggleSum( name:string ):void{
		this.sums = this.sums.includes( name ) ? this.sums.filter( s=>s!=name ) : [...this.sums, name];
		this.#queryGrouped();
	}
	//The group column and the sums are read with the rows though the view hides them:  a column the view lacks joins it, hidden.
	#queryGrouped():void{
		for( const name of [this.groupBy, ...this.sums] ){
			const field = name && !this.fields.some( f=>f.name==name ) ? this.schema?.fields.find( f=>f.name==name ) : undefined;
			if( field )
				this.fields.push( new ViewField({qlField: field, settings: {name: field.name, hidden: true}}) );
		}
	}
	//What the rows are grouped by, for groupRows:  none where the view is not grouped.  `totals`, the counts groupQuery answers.
	grouping( offered:Record<string,string>, totals?:readonly SliceCount[] ):Grouping|undefined{
		const by = this.groupBy;
		return by ? { by, label: this.columnLabel(by, offered), chip: this.fields.find( f=>f.name==by )?.chip, sums: this.sums.map( name=>({name, label: this.columnLabel(name, offered)}) ), totals } : undefined;
	}
	//A group's count over the whole list, which a page shows a part of:  a row per value of the group column under the view's
	//filter and its slice's value, the query the slice panel's counts are read with.  None where the view is not grouped.
	groupQuery():Query|undefined{ return this.groupBy ? this.#countQuery( this.groupBy, true ) : undefined; }
	#field( name:string ):Field|undefined{ return this.fields.find( f=>f.name==name )?.qlField ?? this.schema?.fields.find( f=>f.name==name ); }
	//the filter as arguments, the slice's value a term of its column among the view's when `sliced`
	#filterParts( sliced:boolean ):{args:string[], vars:Record<string,unknown>}{
		const terms:FilterTerm[] = [...this.parsedFilter().terms];
		if( sliced && this.sliceBy && this.slice!==undefined )
			terms.push( {field: this.sliceBy, operator: this.slice===null ? "null" : "in", values: this.slice===null ? [] : [this.slice], negated: false, start: 0, end: 0} );
		const { args, vars } = filterArguments( terms );//a column's terms as one argument, a term the box could not read left out
		//Deleted rows stay out unless the view filters on `deleted` - the Deleted system view does, and so can any saved one.
		//A live-toggle column (the Resources page's Enforced) lists every row:  `deleted` is its switch, not a trash can.
		const deleted = this.fields.find( f=>f.name=="deleted" );
		if( deleted && !deleted.liveToggle && !args.some(a=>a.startsWith("deleted:")) ){
			args.push( `deleted:$deleted` );
			vars["deleted"] = null;
		}
		return { args, vars };
	}
	#queryParts( skip:number ):{fields:string[], args:string[], vars:Record<string,unknown>}{
		const fields = this.fields.filter( f=>f.displayed || f.name=="id" || f.name=="slug" || this.queried.includes(f.name) || f.name==this.groupBy || this.sums.includes(f.name) ).map( f=>f.queryText );//id/slug/queried are queried even when hidden:  id is the mutation key, slug and queried the navigation key;  the group column and the sums are what a group's header reads
		let args = [];
		let vars:Record<string,unknown> = {};
		if( this.limit )
			args.push( `limit:${this.limit}` );
		if( skip )
			args.push( `skip:${skip}` );
		const sort = this.ordered;
		if( sort?.length ){
			let sortStr = '';
			for( let s of sort )
				sortStr += `{${s.active}:${StringUtils.qlString(s.direction)}},`;//`active` is a FIELD NAME - identifiers are not quoted and must not be escaped; only the direction is a string literal
			args.push( `orderBy:[${sortStr.slice(0,-1)}]` );
		}
		const filter = this.#filterParts( true );
		args.push( ...filter.args );
		Object.assign( vars, filter.vars );
		return { fields, args, vars };
	}
	//The column pinned at the left as the table scrolls sideways:  the name, or the first shown where the view shows none.
	get pinned():string|undefined{
		const shown = this.fields.filter( f=>f.displayed );
		return ( shown.find( f=>f.name=="name" ) ?? shown[0] )?.name;
	}
	//A column shown, after those that are, or hidden:  the Fields checklist's tick and the header's Hide field.  One the view lacks
	//is `route`'s - the route's field for it, with its width, chip and selection - else the schema's, under the host's name for it.
	//The last column shown stays.
	setDisplayed( name:string, displayed:boolean, displayName?:string, route?:ViewField ):void{
		let field = this.fields.find( f=>f.name==name );
		if( !displayed ){
			if( field?.displayed && this.fields.filter( f=>f.displayed ).length>1 )
				field.displayed = false;
			return;
		}
		if( field?.displayed )
			return;
		const qlField = field?.qlField ?? this.schema?.fields.find( f=>f.name==name );
		if( !qlField )
			return;
		field ??= route?.name==name ? new ViewField( route ) : new ViewField( {qlField, settings: {name, displayName}} );
		field.displayed = true;
		this.fields = [...this.fields.filter( f=>f!==field ), field];
	}
	//to the place `to` among the columns shown:  a header dropped, or its Move left and Move right
	moveField( name:string, to:number ):void{
		const shown = this.fields.filter( f=>f.displayed );
		const from = shown.findIndex( f=>f.name==name );
		to = Math.max( 0, Math.min(to, shown.length-1) );
		if( from<0 || from==to )
			return;
		const fields = this.fields.filter( f=>f!==shown[from] );
		fields.splice( fields.indexOf(shown[to])+( to>from ? 1 : 0 ), 0, shown[from] );
		this.fields = fields;
	}
	//A column's width, a fixed basis as a route writes one, never under the floor - room for the header's menu button and its
	//handle - nor narrowed under `from`, the width it had, where that is less:  a column its sheet draws narrower is not widened.
	static readonly minWidth = 64;
	static widthOf( width:number, from=Infinity ):number{ return Math.max( Math.min(View.minWidth, from), Math.round(width) ); }
	static flexOf( width:number ):string{ return `${new Flex( width )}`; }
	setWidth( name:string, width:number, from=Infinity ):void{
		const field = this.fields.find( f=>f.name==name );
		if( field )
			field.style = new Style( {...field.style, flex: new Flex( View.widthOf(width, from) )} );
	}
	//the width, and the alignment with it, as the route's view has the column:  none where its sheet sizes it
	restoreWidth( name:string, route:View|undefined ):void{
		const field = this.fields.find( f=>f.name==name );
		const style = route?.fields.find( f=>f.name==name )?.style;
		if( field )
			field.style = style ? new Style( style ) : undefined;
	}
	//The sort (views spec *Sort*).  A column this refuses is in neither menu and its header does not sort:  orderBy on a column the
	//server lacks is an error.  The lists that sort their own rows have none of those kinds.
	sortable( name:string ):boolean{
		const field = this.#field( name );
		return !!field && serverHasField( field );
	}
	//the sort `No sorting` leaves:  none, but where the rows need an order of their own (LogView)
	clearedSort():Sort[]{ return []; }
	//a header's click:  the column first, the others after it as they were
	sortFirst( sort:Sort ):void{ this.sort = [sort, ...this.sort.filter( s=>s.active!=sort.active )]; }
	//a header menu's two sorts:  the direction, where the column stands in the sort, else the column first
	setSort( name:string, direction:SortDirection ):void{
		if( this.sort.some( s=>s.active==name ) )
			this.sort = this.sort.map( s=>s.active==name ? {active: name, direction} : s );
		else
			this.sortFirst( {active: name, direction} );
	}
	//`Sort by`'s click:  the column after the others, ascending, or - the view sorting by it - the other way
	toggleSort( name:string ):void{
		const flipped = ( s:Sort ):Sort=>({active: name, direction: s.direction=="asc" ? "desc" : "asc"});
		this.sort = this.sort.some( s=>s.active==name ) ? this.sort.map( s=>s.active==name ? flipped(s) : s ) : [...this.sort, {active: name, direction: "asc"}];
	}
	//to the place `to` in the sort
	moveSort( name:string, to:number ):void{
		const from = this.sort.findIndex( s=>s.active==name );
		to = Math.max( 0, Math.min(to, this.sort.length-1) );
		if( from<0 || from==to )
			return;
		const sort = [...this.sort];
		sort.splice( to, 0, ...sort.splice(from, 1) );
		this.sort = sort;
	}
	append( fields: Field[] ){
		for( let field of fields.filter(f=>!this.fields.find(v=>v.name==f.name)) )
			this.fields.push( new ViewField({qlField: field, settings: {name: field.name, hidden: true}}) );
	}
	//the same filters, however they are typed and in any order:  a view that was only re-sorted or re-columned shows the same rows
	static sameFilters( a:View, b:View ):boolean{
		const key = ( v:View )=>{
			const { args, vars } = filterArguments( v.parsedFilter().terms, new Date(0) );
			return JSON.stringify( [[...args].sort(), Object.entries( vars ).sort( ([x], [y])=>x<y ? -1 : 1 )] );
		};
		return key( a )==key( b );
	}
	//the same but for the name:  a view renamed, or a copy of one, shows the rows it did in the columns it did
	static alike( a:View, b:View|undefined ):boolean{
		const json = ( v:View )=>JSON.stringify( {...v.toJson(undefined), name: undefined} );
		return !!b && json( a )==json( b );
	}
	//The rows read for `queried` are those `asked` shows:  the same arguments - so a filter that differs only in a term the box
	//cannot read, which is left out of them - and no column the read left out, so a column hidden or moved.
	static answers( queried:View|undefined, asked:View ):boolean{ return View.sameRows( queried, asked ) && View.#holds( queried!, asked ); }
	//the same arguments, whatever the columns:  a column shown is read again on the same page
	static sameRows( queried:View|undefined, asked:View ):boolean{
		if( !queried )
			return false;
		const had = queried.#queryParts( 0 ), wants = asked.#queryParts( 0 );
		return JSON.stringify( [had.args, had.vars] )==JSON.stringify( [wants.args, wants.vars] );
	}
	static #holds( queried:View, asked:View ):boolean{
		const had = queried.#queryParts( 0 ).fields;
		return asked.#queryParts( 0 ).fields.every( f=>had.includes(f) );
	}
	toJson( defaultSettings:TableSettings|undefined ):ViewJson{
		let fields = [];
		for( let field of this.fields ){
			const settings = defaultSettings?.columns!.find( c=>typeof c=="string" ? null : c.name==field.name ) as ViewFieldSettings;
			const customDisplay = (settings?.displayName ?? StringUtils.idToDisplay( field.name ))!=field.displayName;
			if( field.displayed || customDisplay )
				fields.push( field.toJson(customDisplay) );
		}
		let sort = defaultSettings && JSON.stringify(this.sort)==JSON.stringify(defaultSettings.sort) ? undefined : this.sort;
		return {
			name: this.name,
			filter: this.filter || undefined,
			collectionName: this.collectionName,
			fields: fields,
			limit: this.limit,
			showSelector: this.showSelector,
			sort: sort,
			sliceBy: this.sliceBy,
			groupBy: this.groupBy,
			sums: this.sums.length ? this.sums : undefined
		};
	}
	filter = "";//the filter box's text, as typed:  a saved view keeps it, and a day in it is the day of the browser that runs the query
	sliceBy:string|undefined;//the column the slice panel lists, saved with the view
	groupBy:string|undefined;//the column the rows are grouped by, saved with the view
	sums:string[] = [];//the columns a group's header sums, saved with the view
	slice:SliceValue|undefined;//The value the rows are narrowed to, on the copy a host reads its rows with (ViewSet.sliced):  the browser's, never saved, and no part of a view's draft.
	fixedFilter = "";//the host's own terms, anded with the view's and never shown or saved - a pick list's (QLListData.fixedFilter)
	protected schema:TableSchema|undefined;
	number:number = 0;//what its ViewSet knows it by, since names repeat:  the key of its profile row, never in the row.  0 until it is one of a set's.
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
	queried:string[] = [];//TableSettings.rowLink's fields
};