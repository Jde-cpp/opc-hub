
export function arraysEqual(a:readonly unknown[], b:readonly unknown[]) {
	if (a == b) return true;
	if (a == null || b == null) return false;
	if (a.length !== b.length) return false;
	return a.every( (v,i)=>v==b[i] );
}

//No `debugger;` here:  it froze the page wherever DevTools or automation was attached, and DevTools' pause on exceptions
//stops on the throw anyway.
export function verify( expr:unknown, msg?:string ):asserts expr{
	if( !expr )
		throw new Error( msg ?? "Assertion failed" );
}

export function clone<T>( obj:T ):T{//a JSON round trip, so the result really is the same shape - not `any`
	return JSON.parse( JSON.stringify(obj) );
}

export function cloneClassArray<T>( from:Array<T>, ctor: new (item: T) => T ):T[]{
	if( from==undefined )
		return undefined as unknown as T[];
	let clone = [];
	for( let item of from )
		clone.push( new ctor(item) );
	return clone;
}

//The inverse of Chrono::ToString (include/jde/fwk/chrono.h), which writes a year and a month as std::chrono's average
//Gregorian ones - 365.2425 days, not 365.25 - a sub-minute remainder as decimal seconds (PT1.5S), and a negative duration
//with a sign on each component (P-1DT-1.5S).  As strict as Chrono::ToDuration:  a designator with no number before it,
//or a second T, is an error rather than a zero.
const daysPerYear = 365.2425;
export function fromIsoDuration( str:string ):number{
	if( str.length==0 )
		return 0;
	if( str[0]!='P' )
		throw new Error( `Expected 'P' as first character. ${str}` );
	let parsingTime = false;
	let seconds = 0;
	for( let i=1; i<str.length; i++ ){
		if( str[i]=='T' && !parsingTime ){
			parsingTime = true;
			continue;
		}
		const start = i;
		if( str[i]=='+' || str[i]=='-' )
			++i;
		const digits = i;
		while( i<str.length && ((str[i]>='0' && str[i]<='9') || str[i]=='.') )
			++i;
		if( i==digits )
			throw new Error( `Missing number before '${str[i]}' in duration. '${str}'` );
		const value = +str.substring( start, i );
		if( isNaN(value) )
			throw new Error( `Bad number '${str.substring(start, i)}' in duration. '${str}'` );
		let type = str[i];//the for's i++ advances past the designator
		let multiplier;
		if( type=='S' && parsingTime )
			multiplier = 1;
		else if( type=='M' && parsingTime )
			multiplier = 60;
		else if( type=='H' && parsingTime )
			multiplier = 3600;
		else if( type=='D' && !parsingTime )
			multiplier = 86400;
		else if( type=='W' && !parsingTime )
			multiplier = 7*86400;
		else if( type=='M' && !parsingTime )
			multiplier = 86400*daysPerYear/12;
		else if( type=='Y' && !parsingTime )
			multiplier = 86400*daysPerYear;
		else
			throw new Error( `Unknown type '${type}' in duration. '${str}'` );
		seconds += multiplier*value;
	}
	return seconds;
}

export function getEnumName(enumObj: Record<string, unknown>, enumValue: number | string): string {
	return Object.keys(enumObj).find((key) => enumObj[key] === enumValue)!;
}

export type PropertyNames<T> = {
	[K in keyof T]: T[K] extends Function ? never : K;
}[keyof T];


export function toIdArray( from:number[] ):{id:number}[]{ return from.map( id=>({id}) ); }