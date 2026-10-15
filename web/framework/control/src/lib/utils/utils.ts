import { ActivatedRoute } from "@angular/router";

export function arraysEqual(a:readonly unknown[], b:readonly unknown[]) {
  if (a == b) return true;
  if (a == null || b == null) return false;
  if (a.length !== b.length) return false;
	return a.every( (v,i)=>v==b[i] );
}

export function verify( expr:unknown, msg?:string ):asserts expr{
	if( !expr ){
		debugger;
		throw new Error( msg ?? "Assertion failed" );
	}
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
//Gregorian ones - 365.2425 days, not 365.25 - and a sub-minute remainder as decimal seconds (PT1.5S).
const daysPerYear = 365.2425;
export function fromIsoDuration( str:string ):number{
	if( str.length==0 )
		return 0;
	if( str[0]!='P' )
		throw `Expected 'P' as first character. ${str}`;
	let parsingTime = false;
	let seconds = 0;
	for( let i=1; i<str.length; i++ ){
		if( str[i]=='T' ){
			parsingTime = true;
			continue;
		}
		const start = i;
		while( i<str.length && ((str[i]>='0' && str[i]<='9') || str[i]=='.') )
			++i;
		const value = +str.substring( start, i );
		if( isNaN(value) )
			throw `Bad number '${str.substring(start, i)}' in duration. '${str}'`;
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
			throw `Unknown type '${type}' in duration. '${str}'`;
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


export function subscribe( route: ActivatedRoute, who: string ){
	route.title.subscribe( (x)=>{
		console.log( `${who}.title: ${JSON.stringify(x)}` );
	});
	route.params.subscribe( (x)=>{
		console.log( `${who}.params: ${JSON.stringify(x)}` );
	});
	route.queryParams.subscribe( (x)=>{
		console.log( `${who}.queryParams: ${JSON.stringify(x)}` );
	});
	route.fragment.subscribe( (x)=>{
		console.log( `${who}.fragment: ${JSON.stringify(x)}` );
	});
	route.data.subscribe( (x)=>{
		console.log( `${who}.data: }` );
	});
	route.paramMap.subscribe( (x)=>{
		console.log( `${who}.paramMap: ${JSON.stringify(x)}` );
	});
	route.queryParamMap.subscribe( (x)=>{
		console.log( `${who}.queryParamMap: ${JSON.stringify(x)}` );
	});
	route.url.subscribe( (x)=>{
		console.log( `${who}.url: ${JSON.stringify(x)}` );
	});
}

export function toIdArray( from:number[] ):{id:number}[]{
	let clone = [];
	for( let id of from )
		clone.push( {id:id} );
	return clone;
}
