import { signal } from '@angular/core';
import { ELogLevel } from 'jde-proto/Log';
import { Guid } from '../../model/guid';
import { LogDataSource } from './log-data-source';
import { Entry, LogEntries, LogView } from './log-entry';

const guid = ( n:number )=>new Guid( n.toString(16).padStart(32, '0') );
const entry = ( line:number, template:number ):Entry=>( {templateId: guid(template), argIds: [], level: ELogLevel.Information, tags: [], line, time: new Date(0), userId: 0, fileId: guid(1), functionId: guid(1), hidden: false} );
const noFilter = { messageIds: [], message: undefined, level: ELogLevel.NoLog };

//angular-review #44: filterData's selection-follow was dead code - it paged to 0 whatever was selected, so hiding a message
//template scrolled the selected row out of view.  Six entries, two per page; the pages the source publishes are captured.
describe( 'LogDataSource.filterData', ()=>{
	const create = ( count:number, limit:number )=>{
		const ds = new LogDataSource( signal({limit} as unknown as LogView) );
		for( let i=0; i<count; ++i )
			ds.allEntries.push( entry(i, 10+i) );
		const pages:number[][] = [];
		ds.connect( null as any ).subscribe( p=>pages.push(p.map(e=>e.line)) );
		return { ds, pages };
	};

	it( 'pages to the selected entry after the filter hides rows above it', ()=>{
		const { ds, pages } = create( 6, 2 );
		ds.allEntries[4].selected = true;//visible index 4 today; hiding entry 0 makes it visible index 3, which is page 1 (start 2)
		ds.filterData( {...noFilter, messageIds: [guid(10)]} );
		expect( pages.at(-1) ).toEqual( [3, 4] );
		expect( ds.allEntries[4].selected ).toBe( true );
	} );

	it( 'pages to the first page and drops the selection when the filter hides the selected entry', ()=>{
		const { ds, pages } = create( 6, 2 );
		ds.allEntries[4].selected = true;
		ds.filterData( {...noFilter, messageIds: [guid(14)]} );
		expect( pages.at(-1) ).toEqual( [0, 1] );
		expect( ds.allEntries[4].selected ).toBeFalsy();
	} );

	it( 'pages to the first page when nothing is selected', ()=>{
		const { ds, pages } = create( 6, 2 );
		ds.allEntries[1].hidden = true;//a stale flag from an earlier filter is recomputed, not trusted
		ds.filterData( noFilter );
		expect( pages.at(-1) ).toEqual( [0, 1] );
	} );
} );

//A read is skip/limit against a log that grows while it is paged, so the next page can repeat the tail of the last one -
//by as many rows as were written in between.  Each read is a fresh copy off the wire: equal entries, never the same object.
describe( 'LogDataSource.addLoadedEntries', ()=>{
	const at = ( line:number, second:number ):Entry=>( {...entry(line, 10), time: new Date(second*1000)} );
	const read = ( rows:[number,number][], strings:[string,string][]=[] ):LogEntries=>( {entries: rows.map( ([line, second])=>at(line, second) ), strings: new Map(strings)} );
	const create = ( direction:'asc'|'desc', ...first:[number,number][] )=>{
		const ds = new LogDataSource( signal({limit: 10, sort: [{active: 'time', direction}]} as unknown as LogView) );
		ds.addLoadedEntries( read(first) );
		return ds;
	};
	const lines = ( ds:LogDataSource )=>ds.allEntries.map( e=>e.line );

	it( 'drops the rows a descending page repeats', ()=>{
		const ds = create( 'desc', [5,50], [4,40], [3,30] );
		ds.addLoadedEntries( read([[4,40], [3,30], [2,20], [1,10]]) );//two rows written since the last read
		expect( lines(ds) ).toEqual( [5, 4, 3, 2, 1] );
	} );

	it( 'drops the rows an ascending page repeats', ()=>{
		const ds = create( 'asc', [1,10], [2,20], [3,30] );
		ds.addLoadedEntries( read([[3,30], [4,40], [5,50]]) );
		expect( lines(ds) ).toEqual( [1, 2, 3, 4, 5] );
	} );

	it( 'keeps a row that shares a time with a loaded one but is not it', ()=>{
		const ds = create( 'desc', [5,50], [4,40], [3,30] );
		ds.addLoadedEntries( read([[3,30], [9,30], [2,20]]) );
		expect( lines(ds) ).toEqual( [5, 4, 3, 9, 2] );
	} );

	it( 'drops a repeat that comes behind a new row with the same time', ()=>{//tied rows read back in another order
		const desc = create( 'desc', [5,50], [4,40], [3,30] );
		desc.addLoadedEntries( read([[9,30], [3,30], [2,20]]) );
		expect( lines(desc) ).toEqual( [5, 4, 3, 9, 2] );
		const asc = create( 'asc', [1,10], [2,20], [3,30] );
		asc.addLoadedEntries( read([[9,30], [3,30], [4,40]]) );
		expect( lines(asc) ).toEqual( [1, 2, 3, 9, 4] );
	} );

	it( 'keeps rows one line logs in the same millisecond with other arguments', ()=>{
		const withArg = ( line:number, second:number, arg:number ):Entry=>( {...at(line, second), argIds: [guid(arg)]} );
		const ds = create( 'desc' );
		ds.addLoadedEntries( {entries: [withArg(1, 30, 100)], strings: new Map()} );
		ds.addLoadedEntries( {entries: [withArg(1, 30, 100), withArg(1, 30, 101), withArg(1, 30, 102)], strings: new Map()} );
		expect( ds.allEntries.map(e=>e.argIds[0].toString()) ).toEqual( [100, 101, 102].map(n=>guid(n).toString()) );
	} );

	it( 'drops repeats under a sort other than time', ()=>{
		const ds = new LogDataSource( signal({limit: 10, sort: [{active: 'level', direction: 'asc'}]} as unknown as LogView) );
		ds.addLoadedEntries( read([[1,10], [2,20], [3,30]]) );
		ds.addLoadedEntries( read([[2,20], [4,40]]) );
		expect( lines(ds) ).toEqual( [1, 2, 3, 4] );
	} );

	it( 'adds a page with no overlap whole', ()=>{
		const ds = create( 'desc', [5,50], [4,40] );
		ds.addLoadedEntries( read([[3,30], [2,20]]) );
		expect( lines(ds) ).toEqual( [5, 4, 3, 2] );
	} );

	it( 'takes an empty page, keeping its strings', ()=>{
		const ds = create( 'desc', [5,50], [4,40] );
		ds.addLoadedEntries( read([], [["a", "disk full"]]) );
		expect( lines(ds) ).toEqual( [5, 4] );
		expect( ds.strings.get("a") ).toBe( "disk full" );
	} );

	it( 'adds nothing from a page that is all duplicates', ()=>{
		for( const direction of ['asc', 'desc'] as const ){
			const ds = create( direction, [1,10], [2,20], [3,30] );
			ds.addLoadedEntries( read(direction=='asc' ? [[2,20], [3,30]] : [[2,20], [1,10]]) );
			expect( lines(ds) ).toEqual( direction=='asc' ? [1, 2, 3] : [3, 2, 1] );
		}
	} );

	it( 'merges the page\'s strings before it filters the page', ()=>{
		const ds = new LogDataSource( signal({limit: 10, sort: [{active: 'time', direction: 'desc'}]} as unknown as LogView) );
		ds.filter = {...noFilter, message: "disk"};
		ds.addLoadedEntries( read([[1,10]], [[guid(10).toString(), "disk full"]]) );
		expect( ds.allEntries[0].hidden ).toBe( false );
	} );
} );