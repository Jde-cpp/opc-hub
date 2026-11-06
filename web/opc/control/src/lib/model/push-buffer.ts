import { signal, WritableSignal } from '@angular/core';
import { HistValue } from './hist';

//The live trend's buffer (clients spec *Live trend*, #420):  each watch row's pushes as history values, in source-time order, over a
//window behind the row's newest push - 1, 10 or 60 minutes - and the newest push before the window with them, so a stepped line
//reaches the window's left edge.  Every row's, not the ticked rows' alone, so a row ticked shows its window at once;  a cap per
//row bounds a fast sampler, under the window where it bites.  The window is the row's own clock, not the browser's:  a server
//whose clock runs behind would lose every push, and a row gone quiet keeps what it did.  Pruning is a batch, once the oldest is a
//tenth of the window past the edge, since the chart adds a point to a line that only grew and rebuilds one whose first point went
//(HistTrend.setData).  `version( key )` moves with every change of the row, for a computed over `values`:  a push to a row the
//trend doesn't draw moves no other's (420 #5).  A total over every row bounds a long list (420 #6):  past it, the rows the trend
//doesn't draw (`drawn`) give way, the largest first.
export class PushBuffer{
	constructor( window:number, drawn:( key:string )=>boolean=()=>false ){ this.#window = window; this.#drawn = drawn; }
	get window():number{ return this.#window; }
	set window( ms:number ){
		this.#window = ms;
		for( const values of this.#rows.values() )
			this.#prune( values, true );
		for( const key of this.#versions.keys() )
			this.#bump( key );
	}
	//`v` kept under `key`:  the row's newest, nearly always - an append - else in its place in time, and not at all with no time
	//or at a time the row holds, as mergeHistValues keeps the record it holds - unless its status differs:  a server that doesn't
	//restamp a status change would draw Good through a Bad (420 #8)
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
		if( i>0 && time( values[i-1] )==t ){
			if( values[i-1].status!=v.status ){
				values[i-1] = v;
				this.#bump( key );
			}
			return;
		}
		if( i==values.length )
			values.push( v );
		else
			values.splice( i, 0, v );
		++this.#size;
		this.#prune( values, false );
		this.#bump( key );
		if( this.#size>PushBuffer.total )
			this.#trim();
	}
	//the rows' values, each row's in time order, in the order of `keys`
	values( keys:Iterable<string> ):HistValue[]{
		const y:HistValue[] = [];
		for( const key of keys ){
			for( const v of this.#rows.get( key ) ?? [] )
				y.push( v );
		}
		return y;
	}
	size( key:string ):number{ return this.#rows.get( key )?.length ?? 0; }
	remove( key:string ):void{
		this.#size -= this.size( key );
		if( this.#rows.delete( key ) )
			this.#bump( key );
		this.#versions.delete( key );//a computed it moved reads the row again, and makes another
	}
	clear():void{
		this.#rows.clear();
		this.#size = 0;
		for( const key of this.#versions.keys() )
			this.#bump( key );
		this.#versions.clear();
	}
	//made on the first read:  a row no computed reads has no version to move
	version( key:string ):number{
		let version = this.#versions.get( key );
		if( !version )
			this.#versions.set( key, version = signal(0) );
		return version();
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
				this.#size -= values.splice( 0, i-1 ).length;
		}
		if( values.length>PushBuffer.capacity )
			this.#size -= values.splice( 0, values.length-Math.floor( PushBuffer.capacity*0.9 ) ).length;
	}
	//over the total:  the oldest tenth of the largest row not drawn, again until under - a fast sampler's goes before a quiet row's
	//window, and a row keeps its newest.  The drawn rows alone never reach it.
	#trim(){
		while( this.#size>PushBuffer.total ){
			let largest:[string, HistValue[]]|undefined;
			for( const row of this.#rows ){
				if( row[1].length>(largest?.[1].length ?? 1) && !this.#drawn( row[0] ) )
					largest = row;
			}
			if( !largest )
				return;
			this.#size -= largest[1].splice( 0, Math.ceil( largest[1].length/10 ) ).length;
			this.#bump( largest[0] );
		}
	}
	#bump( key:string ){ this.#versions.get( key )?.update( n=>n+1 ); }
	#versions = new Map<string,WritableSignal<number>>();
	static readonly capacity = 10_000;//values a row holds at most:  an hour at a push every 360 ms
	static readonly total = 100_000;//values every row holds at most, some 15 MB:  above the trend's eight rows at their cap
	#drawn:( key:string )=>boolean;
	#size = 0;
	static readonly windows = [1, 10, 60];//minutes
	#window:number;//ms
	#rows = new Map<string,HistValue[]>();
}
const time = ( v:HistValue )=>v.source!.getTime();