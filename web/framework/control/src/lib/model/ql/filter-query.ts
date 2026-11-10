import { FieldKind, type Field } from './schema/field';

//The filter box's text as terms, and the terms as a GraphQL list's arguments or as a predicate over rows already in the browser
//(docs/views/spec.md, The filter box).  Pure functions:  the text and the columns in, the terms and what could not be read out.
//
//	field:a  field:a,b  -field:a,b  no:field  has:field  field:>3  field:>=3  field:<3  field:<=3  field:a..b  field:*..b
//	field:a..*  field:2026-01-01  field:2026-01-01T14:30  field:@today-7d  "a b"  'a b'  word
//
//A field is named as its header reads or by its QL name, in any case, and quoted when it has a space.  Repeating a field ands
//its terms;  there is no or between fields.

//A column the box can name:  a ViewField is one.
export type FilterColumn = { qlField:Field, displayName:string, liveToggle?:unknown };
//`server`: a GraphQL list, whose database runs the filter, so a term it cannot run is refused;  a list that filters its own
//rows takes a comparison on any column.  `enums`: an enum's values by its type's name, as `schemaWithEnums` reads them - an
//enum column's value is then one of them, in any case, and is sent as the schema spells it.
export type FilterHost = { columns:readonly FilterColumn[], server:boolean, enums?:ReadonlyMap<string, readonly {name:string}[]> };
//A span of time in the browser's time zone, as typed:  a calendar date or `@today` and the days from it, each a whole day, or
//a date with a time, the minute or the second it names.
export type FilterDay = { year:number, month:number, day:number, hour?:number, minute?:number, second?:number }|{ offset:number };
export type FilterValue = string|number|boolean|FilterDay|null;
//`in`: any of the values, or none of them when negated.  `null`: the column is empty, or is not when negated.  `range`: two
//values, null for an open end.  `word`: a bare word, with no field.
export type FilterOperator = "in"|"null"|"gt"|"gte"|"lt"|"lte"|"range"|"word";
export type FilterTerm = { field:string|undefined, operator:FilterOperator, values:FilterValue[], negated:boolean, start:number, end:number };
//A term the box cannot read, by its place in the text, left out of the terms.
export type FilterError = { start:number, end:number, message:string };
export type FilterParse = { terms:FilterTerm[], errors:FilterError[] };
export type FilterArguments = { args:string[], vars:Record<string,unknown> };

type Atom = { text:string, quoted:boolean, closed:boolean };
type RawTerm = { start:number, end:number, negated:boolean, word?:Atom, field?:Atom, values:Atom[], trailing:boolean };
type Bounds = { gt?:unknown, gte?:unknown, lt?:unknown, lte?:unknown };
type Comparison = keyof Bounds;

const isSpace = ( c:string )=>/\s/.test( c );
const columnName = ( c:FilterColumn )=>c.qlField.name;
const isDay = ( v:FilterValue ):v is FilterDay=>v!=null && typeof v=="object";

//The terms as typed, split at white space outside quotes.  A quote opens a value only where one starts - at the head of a term
//and after `:` or `,` - so the apostrophe of o'brien is a letter.
function scan( text:string ):RawTerm[]{
	const terms:RawTerm[] = [];
	let i = 0;
	const atom = ( stop:string ):Atom=>{
		const quote = text[i];
		if( quote=='"' || quote=="'" ){
			const close = text.indexOf( quote, i+1 );
			const y = { text: text.slice(i+1, close<0 ? undefined : close), quoted: true, closed: close>=0 };
			i = close<0 ? text.length : close+1;
			return y;
		}
		const from = i;
		while( i<text.length && !isSpace(text[i]) && text[i]!=stop )
			++i;
		return { text: text.slice(from, i), quoted: false, closed: true };
	};
	for( ;; ){
		while( i<text.length && isSpace(text[i]) )
			++i;
		if( i>=text.length )
			return terms;
		const term:RawTerm = { start: i, end: i, negated: text[i]=="-" && i+1<text.length && !isSpace(text[i+1]), values: [], trailing: false };
		if( term.negated )
			++i;
		const first = atom( ":" );
		if( first.closed && text[i]==":" ){
			term.field = first;
			do{
				++i;
				term.values.push( atom(",") );
			}while( text[i]=="," );
		}
		else
			term.word = first;
		term.trailing = i<text.length && !isSpace( text[i] );//only after a closing quote:  an unquoted value runs to the space.
		while( i<text.length && !isSpace(text[i]) )
			++i;
		term.end = i;
		terms.push( term );
	}
}

function findColumn( host:FilterHost, name:string ):FilterColumn|undefined{
	const lower = name.toLowerCase();
	return host.columns.find( c=>c.displayName.toLowerCase()==lower ) ?? host.columns.find( c=>columnName(c).toLowerCase()==lower );
}
//orderBy's test (graphql-table.sortable):  an object is grafted onto the row after the select, so the server has no column for it.
function serverHasColumn( c:FilterColumn ):boolean{
	const type = c.qlField.type;
	return type.kind!=FieldKind.LIST && ![FieldKind.OBJECT, FieldKind.UNION, FieldKind.LIST].includes( type.underlyingKind );
}
//the column a bare word searches on a GraphQL list.
function nameColumn( host:FilterHost ):FilterColumn|undefined{
	return host.columns.find( c=>columnName(c)=="name" && serverHasColumn(c) );
}

function parseDay( text:string ):FilterDay|undefined{
	const relative = /^@today(?:([+-])(\d+)d)?$/i.exec( text );
	if( relative )
		return { offset: relative[2] ? (relative[1]=="-" ? -1 : 1)*+relative[2] : 0 };
	const date = /^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2})(?::(\d{2}))?)?$/.exec( text );
	if( !date )
		return undefined;
	const y:FilterDay = { year: +date[1], month: +date[2], day: +date[3] };
	const check = new Date( y.year, y.month-1, y.day );
	if( check.getFullYear()!=y.year || check.getMonth()!=y.month-1 || check.getDate()!=y.day )
		return undefined;
	if( date[4]===undefined )
		return y;
	const time = { hour: +date[4], minute: +date[5], ...(date[6]===undefined ? {} : {second: +date[6]}) };
	return time.hour<24 && time.minute<60 && (time.second ?? 0)<60 ? { ...y, ...time } : undefined;
}
//The local time that starts the span, `add` spans on:  built from the date's parts, since `new Date('2026-01-01')` reads as
//UTC, and the next day is the next calendar midnight, not 24 hours on.
function startOfDay( day:FilterDay, add:number, today:Date ):Date{
	if( "offset" in day )
		return new Date( today.getFullYear(), today.getMonth(), today.getDate()+day.offset+add );
	if( day.hour===undefined )
		return new Date( day.year, day.month-1, day.day+add );
	return day.second===undefined
		? new Date( day.year, day.month-1, day.day, day.hour, day.minute!+add )
		: new Date( day.year, day.month-1, day.day, day.hour, day.minute, day.second+add );
}

//A value as its column types it, or the reason it is none.
function typed( column:FilterColumn, text:string, host:FilterHost ):FilterValue|Error{
	const field = column.qlField;
	if( field.isDateTime )
		return parseDay( text ) ?? new Error( `'${text}' is not a date - write 2026-01-31, 2026-01-31T14:30 or @today-7d.` );
	if( field.isNumber ){
		const n = text.trim()==="" ? NaN : Number( text );
		if( !Number.isFinite(n) )
			return new Error( `'${text}' is not a number.` );
		//the server converts to the column's own type, and fails the whole query on a value it cannot hold exactly.
		const type = field.type.underlyingName;
		if( type!="Float" && !Number.isInteger(n) )
			return new Error( `'${text}' is not a whole number.` );
		return type=="UInt" && n<0 ? new Error( `'${text}' is below 0, which '${column.displayName}' cannot hold.` ) : n;
	}
	if( field.isBoolean ){
		const lower = text.toLowerCase();
		return lower=="true" || lower=="false" ? lower=="true" : new Error( `'${text}' is not true or false.` );
	}
	const values = field.isEnum ? host.enums?.get( field.type.underlyingName ) : undefined;
	if( values ){//the server looks the name up exactly, and fails the whole query on one it does not find.
		const lower = text.toLowerCase();
		return values.find( v=>v.name.toLowerCase()==lower )?.name
			?? new Error( `'${text}' is not a value of '${column.displayName}' - ${values.map( v=>v.name ).join( ", " )}.` );
	}
	return text;
}

const comparisons:[string,Comparison][] = [ [">=", "gte"], ["<=", "lte"], [">", "gt"], ["<", "lt"] ];

//One typed term as the terms it means, or why the box cannot read it.
function read( raw:RawTerm, host:FilterHost ):FilterTerm[]|string{
	const place = { start: raw.start, end: raw.end };
	if( [raw.word, raw.field, ...raw.values].some(a=>a && !a.closed) )
		return "A quote is not closed.";
	if( raw.trailing )
		return "Text follows a closing quote - put a space or a comma after it.";
	if( raw.word ){
		if( raw.negated )
			return `A bare word cannot be excluded - name a field, as in -name:${raw.word.text}.`;
		if( raw.word.text=="" )
			return "An empty word matches nothing.";
		if( host.server && !nameColumn(host) )
			return "This list has no name for a bare word to search - name a field.";
		return [ {field: undefined, operator: "word", values: [raw.word.text], negated: false, ...place} ];
	}
	const values = raw.values.filter( v=>v.quoted || v.text!="" );
	const refusal = ( name:string ):FilterColumn|string=>{
		const column = findColumn( host, name );
		if( !column )
			return `No field '${name}'.`;
		return host.server && !serverHasColumn( column ) ? `'${column.displayName}' is not a column the server can filter.` : column;
	};
	const keyword = raw.field!.quoted ? "" : raw.field!.text.toLowerCase();
	if( keyword=="no" || keyword=="has" ){
		if( !values.length )
			return `'${keyword}:' needs a field.`;
		const terms:FilterTerm[] = [];
		for( const name of values ){
			const column = refusal( name.text );
			if( typeof column=="string" )
				return column;
			//A live-toggle column's header names the live state, and a row is live when the column is null:  has:enforced is
			//`deleted is null`.  By its QL name the column is what it holds, so has:deleted is the deleted rows.
			const live = !!column.liveToggle && column.displayName.toLowerCase()==name.text.toLowerCase();
			const empty = ( keyword=="no" )!=raw.negated!=live;
			terms.push( {field: columnName(column), operator: "null", values: [], negated: !empty, ...place} );
		}
		return terms;
	}
	const column = refusal( raw.field!.text );
	if( typeof column=="string" )
		return column;
	const display = column.displayName;
	if( column.liveToggle )
		return `Only has:${display.toLowerCase()} and no:${display.toLowerCase()} are read on '${display}'.`;
	if( !values.length )
		return `'${raw.field!.text}:' needs a value.`;
	if( values.some(v=>v.text=="") ){//a quoted one:  the QL saves '' as null in a nullable text column, so `in('')` finds nothing.
		const named = /\s/.test( raw.field!.text ) ? `"${raw.field!.text}"` : raw.field!.text;
		return `An empty value is not read - write no:${named} for an empty '${display}'.`;
	}
	const field = column.qlField, isDate = field.isDateTime;
	const term = ( operator:FilterOperator, termValues:FilterValue[] ):FilterTerm[]=>[ {field: field.name, operator, values: termValues, negated: raw.negated, ...place} ];
	const compared = values.length==1 && !values[0].quoted ? values[0].text : undefined;
	const comparison = compared===undefined ? undefined : comparisons.find( ([sign])=>compared.startsWith(sign) );
	const range = compared===undefined || comparison ? -1 : compared.indexOf( ".." );
	if( values.some(v=>!v.quoted && (comparisons.some(([sign])=>v.text.startsWith(sign)) || v.text.includes(".."))) ){
		if( !comparison && range<0 )
			return "A comparison or a range takes one value.";
		if( raw.negated )
			return "A comparison or a range cannot be excluded - turn it round.";
		if( host.server && !isDate && !field.isNumber )
			return `'${display}' is not a number or a date - the server compares those alone.`;
	}
	if( comparison ){
		const operand = compared!.slice( comparison[0].length );
		if( operand=="" )
			return `'${comparison[0]}' needs a value.`;
		const value = typed( column, operand, host );
		return value instanceof Error ? value.message : term( comparison[1], [value] );
	}
	if( range>=0 ){
		const ends:FilterValue[] = [];
		for( const end of [compared!.slice(0, range), compared!.slice(range+2)] ){
			if( end=="" )
				return "A range needs both ends - write * for an open one.";
			const value = end=="*" ? null : typed( column, end, host );
			if( value instanceof Error )
				return value.message;
			ends.push( value );
		}
		return ends[0]==null && ends[1]==null ? "A range needs one end at least." : term( "range", ends );
	}
	if( isDate && raw.negated )
		return "A day cannot be excluded - that would need an or.";
	if( isDate && values.length>1 )
		return "A date takes one day or a range - a list of days would need an or.";
	const list:FilterValue[] = [];
	for( const v of values ){
		const value = typed( column, v.text, host );
		if( value instanceof Error )
			return value.message;
		list.push( value );
	}
	return term( "in", list );
}

//The text as terms.  A term the box cannot read - an unknown field, a comparison on text on a GraphQL list, an unbalanced
//quote, a negated or listed date, a term on a column the server cannot filter - is an error, and is not among the terms.
export function parseFilter( text:string, host:FilterHost ):FilterParse{
	const y:FilterParse = { terms: [], errors: [] };
	for( const raw of scan(text) ){
		const terms = read( raw, host );
		if( typeof terms=="string" )
			y.errors.push( {start: raw.start, end: raw.end, message: terms} );
		else
			y.terms.push( ...terms );
	}
	return y;
}

//What a comparison, a range or a day asks of a cell, or undefined for the other terms.  A day is whole:  with start(d) its
//local midnight, `d` is [start(d), start(d+1)), `>d` is from start(d+1) and `<=d` is before it.  A date with a time is its
//minute, or its second where one is typed, by the same rules.
function bounds( term:FilterTerm, today:Date ):Bounds|undefined{
	const [a, b] = term.values;
	const start = ( day:FilterValue, add:number )=>startOfDay( day as FilterDay, add, today );
	switch( term.operator ){
		case "in":
			return term.values.length==1 && isDay( a ) ? { gte: start(a, 0), lt: start(a, 1) } : undefined;
		case "range":{
			const days = isDay( a ) || isDay( b );
			return { ...(a==null ? {} : {gte: days ? start(a, 0) : a}), ...(b==null ? {} : days ? {lt: start(b, 1)} : {lte: b}) };
		}
		case "gt": return isDay( a ) ? { gte: start(a, 1) } : { gt: a };
		case "gte": return { gte: isDay(a) ? start(a, 0) : a };
		case "lt": return { lt: isDay(a) ? start(a, 0) : a };
		case "lte": return isDay( a ) ? { lt: start(a, 1) } : { lte: a };
	}
	return undefined;
}

//A typed `*`, `?` or `[` as a class of one, so the word is searched as it was typed.
function globWord( word:string ):string{
	return `*${word.replace( /[*?[]/g, c=>`[${c}]` )}*`;
}

type Merged = { in?:unknown[], nin:unknown[], bounds:Bounds, notNull:boolean, glob:string[] };

//The terms as a collection query's arguments.  The QL keys a filter by column and an operator object by operator, so a
//column's terms are merged into one argument:  different operators are the keys of one object, each with a variable of its
//own;  the same comparison twice keeps the tighter bound;  two exclusions are one `nin`;  and two `in`s intersect.  Bare words
//are patterns on the name, a list when there are several, all of which must match.  `today` is the day `@today` is.
export function filterArguments( terms:readonly FilterTerm[], today:Date=new Date() ):FilterArguments{
	const columns = new Map<string,Merged>();
	const merged = ( name:string ):Merged=>{
		if( !columns.has(name) )
			columns.set( name, {nin: [], bounds: {}, notNull: false, glob: []} );
		return columns.get( name )!;
	};
	for( const term of terms ){
		const column = merged( term.field ?? "name" );
		const asked = bounds( term, today );
		if( asked ){
			for( const [op, value] of Object.entries(asked) as [Comparison,unknown][] ){
				const held = column.bounds[op];
				const tighter = op=="gt" || op=="gte" ? +(value as number)>+(held as number) : +(value as number)<+(held as number);
				if( held===undefined || tighter )
					column.bounds[op] = value;
			}
		}
		else if( term.operator=="word" )
			column.glob.push( globWord(`${term.values[0]}`) );
		else if( term.operator=="null" && term.negated )
			column.notNull = true;
		else if( term.negated )
			column.nin.push( ...new Set(term.values.filter( v=>!column.nin.includes(v) )) );
		else{
			const values = term.operator=="null" ? [null] : term.values;
			column.in = column.in ? column.in.filter( v=>values.includes(v as FilterValue) ) : [...new Set( values )];
		}
	}
	const y:FilterArguments = { args: [], vars: {} };
	for( const [name, column] of columns ){
		const json = ( v:unknown )=>v instanceof Date ? v.toISOString() : v;
		const keys:[string,unknown][] = [];
		if( column.in )
			keys.push( ["in", column.in] );
		if( column.nin.length )
			keys.push( ["nin", column.nin] );
		for( const op of ["gte", "gt", "lte", "lt"] as Comparison[] ){
			if( column.bounds[op]!==undefined )
				keys.push( [op, json(column.bounds[op])] );
		}
		if( column.glob.length )
			keys.push( ["glob", column.glob.length==1 ? column.glob[0] : column.glob] );
		if( keys.length==1 && keys[0][0]=="in" && !column.notNull ){//the bare array is the QL's `in`.
			y.args.push( `${name}:$${name}` );
			y.vars[name] = column.in;
			continue;
		}
		const members = keys.map( ([op, value])=>{
			y.vars[`${name}_${op}`] = value;
			return `${op}:$${name}_${op}`;
		} );
		if( column.notNull )
			members.push( "ne:null" );
		y.args.push( `${name}:{${members.join( "," )}}` );
	}
	return y;
}

const isEmpty = ( cell:unknown )=>cell==null || cell==="";
const toNumber = ( x:unknown )=>typeof x=="number" ? x : typeof x=="string" && x.trim()!=="" ? Number( x ) : NaN;//Number("") is 0, which would rank an empty cell among the numbers
//Two cells, numeric when both sides are numbers and text otherwise:  the rule the node and watch lists sort by, too.
export function compareCells( a:unknown, b:unknown ):number{
	const na = toNumber( a ), nb = toNumber( b );
	return !isNaN( na ) && !isNaN( nb ) ? na-nb : String( a ).localeCompare( String(b), undefined, {numeric: true, sensitivity: "base"} );
}

//`cell`: what a column shows for a row.  `time`: a day's bound as a time column's cells are written, so the two compare.
export type FilterPredicateOptions<T> = { cell:( row:T, column:string )=>unknown, time?:( bound:Date )=>unknown, today?:Date };
//The terms as a test of a row, for a list that holds its rows.  `field:a` is the cell's text exactly;  an exclusion keeps an
//empty cell;  an empty cell matches no comparison, as a null matches none in SQL;  and a bare word is in any text cell, a
//String's or an enum's, whatever its case.
export function filterPredicate<T>( terms:readonly FilterTerm[], host:FilterHost, options:FilterPredicateOptions<T> ):( row:T )=>boolean{
	const { cell, time=( d:Date )=>d.toISOString(), today=new Date() } = options;
	const texts = host.columns.filter( c=>c.qlField.isString || c.qlField.isEnum ).map( columnName );
	const tests = terms.map( ( term ):( row:T )=>boolean=>{
		const asked = bounds( term, today );
		if( asked ){
			const limits = ( Object.entries(asked) as [Comparison,unknown][] ).map( ([op, v]):[Comparison,unknown]=>[op, v instanceof Date ? time(v) : v] );
			return row=>{
				const value = cell( row, term.field! );
				return !isEmpty( value ) && limits.every( ([op, limit])=>{
					const c = compareCells( value, limit );
					return op=="gt" ? c>0 : op=="gte" ? c>=0 : op=="lt" ? c<0 : c<=0;
				} );
			};
		}
		if( term.operator=="word" ){
			const word = `${term.values[0]}`.toLowerCase();
			return row=>texts.some( name=>`${cell( row, name ) ?? ""}`.toLowerCase().includes(word) );
		}
		if( term.operator=="null" )
			return row=>isEmpty( cell(row, term.field!) )!=term.negated;
		const values = term.values.map( v=>`${v}` );
		return row=>values.includes( `${cell( row, term.field! ) ?? ""}` )!=term.negated;
	} );
	return row=>tests.every( test=>test(row) );
}