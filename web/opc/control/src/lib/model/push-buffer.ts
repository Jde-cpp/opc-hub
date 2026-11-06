import { signal } from '@angular/core';
import { HistValue } from './hist';

//The live trend's buffer (clients spec *Live trend*, #420):  each watch row's pushes as history values, in source-time order, over a
//window behind the row's newest push - 1, 10 or 60 minutes - and the newest push before the window with them, so a stepped line
//reaches the window's left edge.  Every row's, not the ticked rows' alone, so a row ticked shows its window at once;  a cap per
//row bounds a fast sampler, under the window where it bites.  The window is the row's own clock, not the browser's:  a server
//whose clock runs behind would lose every push, and a row gone quiet keeps what it did.  Pruning is a batch, once the oldest is a
//tenth of the window past the edge, since the chart adds a point to a line that only grew and rebuilds one whose first point went
//(HistTrend.setData).  `version` moves with every change, for a computed over `values`.
export class PushBuffer{
	constructor( window:number ){ this.#window = window; }
	get window():number{ return this.#window; }
	set window( ms:number ){
		this.#window = ms;
		for( const values of this.#rows.values() )
			this.#prune( values, true );
		this.#bump();
	}
	//`v` kept under `key`:  the row's newest, nearly always - an append - else in its place in time, and not at all with no time
	//or at a time the row holds, as mergeHistValues keeps the record it holds
	push( key:string, v:HistValue ):void{
		if( !v.source )
			return;
		let values = this.#rows.get( key );
		if( !values )
			this.#rows.set( key, values = [] );
		const t = v.source.getTime();
		let i = values.length;
		while( i>0 && time( values[i-1] )>t )
			--i;
		if( i>0 && time( values[i-1] )==t )
			return;
		if( i==values.length )
			values.push( v );
		else
			values.splice( i, 0, v );
		this.#prune( values, false );
		this.#bump();
	}
	//the rows' values, each row's in time order, in the order of `keys`
	values( keys:Iterable<string> ):HistValue[]{
		let y:HistValue[] = [];
		for( const key of keys ){
			const values = this.#rows.get( key );
			if( values )
				y = y.concat( values );
		}
		return y;
	}
	size( key:string ):number{ return this.#rows.get( key )?.length ?? 0; }
	remove( key:string ):void{
		if( this.#rows.delete( key ) )
			this.#bump();
	}
	clear():void{
		this.#rows.clear();
		this.#bump();
	}
	//what is older than the window behind the newest goes, but the newest of it:  once the oldest is `slack` past the edge, or
	//at once when the window changed (`now`);  and the oldest past the cap, a tenth at a time
	#prune( values:HistValue[], now:boolean ):void{
		const edge = time( values[values.length-1] )-this.#window;
		if( time( values[0] )<edge-(now ? 0 : this.#window/10) ){
			let i = 0;
			while( i<values.length && time( values[i] )<edge )
				++i;
			if( i>1 )
				values.splice( 0, i-1 );
		}
		if( values.length>PushBuffer.capacity )
			values.splice( 0, values.length-Math.floor( PushBuffer.capacity*0.9 ) );
	}
	#bump(){ this.version.update( n=>n+1 ); }
	readonly version = signal( 0 );
	static readonly capacity = 10_000;//values a row holds at most:  an hour at a push every 360 ms
	static readonly windows = [1, 10, 60];//minutes
	#window:number;//ms
	#rows = new Map<string,HistValue[]>();
}
const time = ( v:HistValue )=>v.source!.getTime();