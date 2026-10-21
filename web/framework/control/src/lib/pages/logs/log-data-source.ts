import {Signal, signal} from '@angular/core';
import { Guid } from '../../model/guid';
//import { TraceEntry } from './TraceEntry';
import { BehaviorSubject, Observable } from 'rxjs';

import * as Log from 'jde-proto/Log';
import { Entry, LogEntries, LogView } from './log-entry';
import { verify } from '../../utils/utils';
import { DataSource } from '@angular/cdk/table';
import { CollectionViewer } from '@angular/cdk/collections';
export type PageStats = { length?:number, startIndex?:number };

type Filter = {
	messageIds:Guid[], level:Log.ELogLevel
}
export class LogDataSource extends DataSource<Entry>{
	constructor( view:Signal<LogView> ){
		super();
		this.view = view;
	}

	connect( collectionViewer: CollectionViewer ):Observable<readonly Entry[]>{
		setTimeout( ()=>{this.setPage();}, 1 );
		return this.subject.asObservable();
	}

	disconnect(collectionViewer: CollectionViewer){
		//this.entries.length=0;
	}
	getVisibleEntries( startIndex:number=0 ):Entry[]{
		let entries = [];
		let visibleIndex = 0;
		for( let entry of this.allEntries ){
			if( entry.hidden )
				continue;
			if( visibleIndex++>=startIndex )
				entries.push( entry );
			if( entries.length==this.view().limit )
				break;
		}
		return entries;
	}
	/**
	* @returns true if end of data is reached
	*/
	setPage( start=0 ):boolean{
		this.#startIndex.set( start );
		let pageSize = this.view().limit;
		if( start>this.allEntries.length )
			return true;
		let entries = this.getVisibleEntries( start );
		this.subject.next( entries );
		return entries.length<pageSize;
	}
	/**
	* The pager's turn.  A page not all loaded yet is not shown until the read lands - LogDetail.load re-shows startIndex - so
	* the table keeps its rows rather than blanking for the round trip.
	* @returns true if the page needs a read
	*/
	turnTo( start:number ):boolean{
		this.#startIndex.set( start );
		const entries = this.getVisibleEntries( start );
		if( entries.length<this.view().limit && this.#length()===undefined )
			return true;
		this.subject.next( entries );
		return false;
	}
	//Insertion index for `entry` in the sorted `data`: the first position holding something that sorts AFTER it, so equal
	//entries keep arrival order.  Plain upper-bound binary search — the previous version branched on `compare(...)` as a
	//boolean, but compare returns -1/0/1 and only 0 is falsy, so both "sorts before" and "sorts after" took the same path:
	//the lower-half recursion was unreachable and inserts landed too far right (15 into [10..80] returned 4, not 1).  It
	//also used Math.round for the pivot, which rounds 0.5 up and could index data[end] === undefined, and its
	//`end-start<=1` case returned pivotIndex-1 for an entry that sorts after the pivot.
	locationOf( data:Entry[], entry:Entry, start:number, end:number ):number{
		while( start<end ){
			const mid = (start+end)>>>1;//floor, so mid<end always — never indexes past the range
			if( this.compare(data[mid], entry)>0 )
				end = mid;
			else
				start = mid+1;
		}
		return start;
	}

	addLoadedEntries( data:LogEntries ){
		//before the push loop, which resolves each entry's text to filter and to sort it - a template that has not
		//been merged yet reads as "".  the dedup below compares only ids and times, so it is unaffected.
		data.strings.forEach( (value,key)=>this.strings.set( key, value ) );
		//A read is skip/limit against a log that grows while it is paged, so a page can repeat rows already loaded.  Every row
		//is checked, not just the page's first ones:  the old loop stopped at the first that matched nothing, so a repeat
		//behind a new row with the same time - tied rows read back in another order - was added twice.  Filtered before any
		//push, so a row is only ever matched against what was loaded before this page.
		data.entries.filter( entry=>!this.isLoaded(entry) ).forEach( entry=>this.push(entry) );
	}
	//scanned from the end, where a repeated row lands.  Sorted by time, the scan stops at the first loaded row that sorts
	//before `entry`:  nothing earlier can be equal to it.
	private isLoaded( entry:Entry ):boolean{
		const byTime = this.sort[0].active=='time';
		for( let i=this.allEntries.length-1; i>=0; --i ){
			const existing = this.allEntries[i];
			if( LogEntries.equals(entry, existing) )
				return true;
			if( byTime && this.compare(existing, entry)<0 )
				return false;
		}
		return false;
	}

	push( entry:Entry ):void{
		//against the current filter, else entries paged in after the level was set come back visible.  a no-op
		//until something filters: the default level is NoLog(-1), which nothing is below.
		entry.hidden = this.isHidden( entry );
		let addToArray = (data:Entry[])=>{
			const location = this.locationOf( data, entry, 0, data.length );
			if( location==data.length )
				data.push( entry );
			else if( location==0 )
				data.unshift( entry );
			else
				data.splice( location, 0, entry );
		}
		addToArray( this.allEntries );
	}
	clear(){
		this.allEntries.length=0;
		this.#length.set( undefined );
		this.setPage();
	}
	//a read came back short:  everything is loaded, so the visible total is known
	endReached(){
		this.#length.set( this.visibleCount() );
	}
	visibleCount():number{
		return this.allEntries.filter( e=>!e.hidden ).length;
	}
	compare( a:Entry, b:Entry ){
		let active = this.sort[0].active;
		let result = 0;
		if( active=='time' ){
			if( a.time<b.time )
				result = -1;
			else if( a.time>b.time )
				result = 1;
			// else if( a.id && b.id )
			// 	result = a.id<b.id ? -1 : 1;
			else
				result = 0;
		}
		else if( active=='level' )
			result = a.level==b.level ? 0 : a.level < b.level ? -1 : 1 ;
		else if( active=='message' )
			result = this.message(a)==this.message(b) ? 0 : this.message(a)<this.message(b) ? -1 : 1;
		else if( active=='file' )
			result = this.file(a)==this.file(b) ? 0 : this.file(a)!<this.file(b)! ? -1 : 1;
		else if( active=='function' )
			result = this.function(a)==this.function(b) ? 0 : this.function(a)!<this.function(b)! ? -1 : 1;
		else
			console.error( `unknown sort '${active}'` );

		return this.sort[0].direction === 'asc' ? result : -result;
	}
	file( entry:Entry ):string|undefined{ return this.strings.get( entry.fileId.toString() ); }
	function( entry:Entry ):string|undefined{ return this.strings.get( entry.functionId.toString() ); }
	message( entry:Entry ):string{
		let template = this.strings.get( entry.templateId.toString() ) ?? "";
		let text = "";
		let argIndex = 0;
		for( let i=0; i<template.length; ++i ){
			let ch = template[i];
			if( ch!='{' || i+1>=template.length || (ch=='{' && template[i+1]=='{') ){
				text += ch;
				if( ch=='{' && i+1<template.length )
					++i;//"{{" escape: emitted one "{" above, skip the second so it collapses to "{" (was `text += template[++i]` → doubled)
				continue;
			}
			let next = template[++i];
			if( next=='}' || i+1==template.length ){
				text += this.strings.get( entry.argIds[argIndex++].toString() );
				verify( next=='}' );
				continue;
			}
			verify( next==':' );
			let type = template[++i];
			if( type=='x' ){
				text += ( +this.strings.get( entry.argIds[argIndex++].toString())! ).toString(16);
				verify( template[++i]=='}' );
			}
			else
				verify( false, `Unknown format type '${type}' in template '${template}'` );
		}
		return text;
	}
	/**
	* Selects the next visible entry of the selected one's template and pages to it.  The selection stays put when none is
	* loaded:  it used to be dropped first, so the read-on in LogDetail.navigateNext had nothing left to look past.
	* @returns false when no later entry of the template is loaded
	*/
	selectNext():boolean{
		const index = this.allEntries.findIndex( e=>e.selected );
		if( index==-1 )
			return false;
		const current = this.allEntries[index];
		const next = this.allEntries.findIndex( (e, i)=>i>index && !e.hidden && e.templateId.equals(current.templateId) );
		if( next==-1 )
			return false;
		current.selected = false;
		this.allEntries[next].selected = true;
		const visibleIndex = this.allEntries.slice( 0, next ).filter( e=>!e.hidden ).length;
		const pageSize = this.view().limit;
		this.setPage( Math.floor(visibleIndex/pageSize)*pageSize );
		return true;
	}

	isHidden( entry:Entry ):boolean{
		let filter = this.filter;
		return filter.messageIds.some( guid=>guid.equals(entry.templateId) ) || entry.level<filter.level;
	}
	/**
	* Re-applies the row filter and pages to wherever the selected entry landed - the first page when nothing visible is
	* selected.  angular-review #44: the old body took the selected index off an array it never pushed to and compared it
	* with -1 after seeding it with 0, so it paged to 0 every time and the selection scrolled out of view.
	* @returns true if end of data is reached
	*/
	filterData( filter: Filter ):boolean{
		this.filter = filter;
		if( this.#length()!==undefined )
			this.#length.set( this.visibleCount() );
		let visibleIndex = 0, selectedVisibleIndex = -1;
		for( const entry of this.allEntries ){
			entry.hidden = this.isHidden( entry );
			if( entry.hidden )
				entry.selected = false;
			else{
				if( entry.selected && selectedVisibleIndex==-1 )
					selectedVisibleIndex = visibleIndex;
				++visibleIndex;
			}
		}
		const pageSize = this.view().limit;
		const start = selectedVisibleIndex==-1 || !pageSize ? 0 : Math.floor( selectedVisibleIndex/pageSize )*pageSize;
		return this.setPage( start );
	}
	filter: Filter = { messageIds: [], level: Log.ELogLevel.NoLog };
	get startIndex(){ return this.#startIndex.asReadonly(); }//the visible index the page starts at - the pager's too
	#startIndex = signal<number>( 0 );
	get length(){ return this.#length.asReadonly(); }//visible entries, once the end has been read - until then Next stays on
	#length = signal<number|undefined>( undefined );
	get sort(){ return this.view().sort; }
  private subject = new BehaviorSubject<Entry[]>([]);

	allEntries:Entry[] = [];
	strings:Map<string,string> = new Map<string,string>();
	view:Signal<LogView>;
}