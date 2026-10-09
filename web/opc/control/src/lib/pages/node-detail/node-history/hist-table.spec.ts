if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { EAccess } from '../../../model/types';
import { HistTable } from './hist-table';

const variable = ( i:number, name:string, level:EAccess )=>new Variable( <any>{ns:2, i, name, browse: {ns:2, name}, historizing: true, userAccessLevel: level} );
const A = variable( 1, 'A', EAccess.Read|EAccess.HistoryRead|EAccess.HistoryWrite ), B = variable( 2, 'B', EAccess.Read|EAccess.HistoryRead );
const at = ( v:Variable, ms:number, value:number, extra:Partial<HistValue>={} ):HistValue=>({ node: v.nodeId, source: new Date(ms), server: null, sourceTime: {seconds: Math.floor(ms/1000), nanos: ms%1000*1_000_000}, status: 0, value, bound: false, heartbeat: false, ...extra });
const render = ( values:HistValue[], inputs:Record<string,unknown>={} )=>{
	const fixture = TestBed.createComponent( HistTable );
	fixture.componentRef.setInput( 'values', values );
	fixture.componentRef.setInput( 'nodes', [A, B] );
	for( const [name, value] of Object.entries( inputs ) )
		fixture.componentRef.setInput( name, value );
	fixture.detectChanges();
	const headers = ()=>[...fixture.nativeElement.querySelectorAll( 'mat-header-cell' )].map( h=>h.textContent.trim() );
	const cells = ( row:number )=>[...fixture.nativeElement.querySelectorAll( 'mat-row' )[row].querySelectorAll( 'mat-cell' )].map( c=>c.textContent.trim() );
	return { fixture, table: fixture.componentInstance, headers, cells };
};

describe( 'HistTable', ()=>{
	it( 'lists the values newest first with the flags column, and no actions without editable nodes', ()=>{
		const t = render( [at(A, 1000, 1), at(B, 2000, 2, {bound: true})] );
		expect( t.headers() ).toEqual( ['Node', 'Source time', 'Server time', 'Status', 'Value', ''] );
		expect( t.cells( 0 )[0] ).toBe( 'B' );
		expect( t.cells( 0 )[5] ).toBe( 'bound' );
		expect( t.fixture.nativeElement.querySelector( '.row-action' ) ).toBeNull();
	} );
	//ModificationInfo in place of the flags:  what the edit did, when, and by whom (plan Phase 4)
	it( 'shows each modification\'s type, time and user in modified mode, with no row actions', ()=>{
		const edited = new Date( 2026, 9, 9, 12, 0, 0, 0 );
		const t = render( [at(A, 1000, 1, {modification: {time: edited, type: 'Replace', user: 'jde'}})], {modified: true, editable: [A]} );
		expect( t.headers() ).toEqual( ['Node', 'Source time', 'Server time', 'Status', 'Value', 'Edit', 'Edited', 'By'] );
		expect( t.cells( 0 ).slice( 5 ) ).toEqual( ['Replace', '2026-10-09 12:00:00.000', 'jde'] );
		expect( t.fixture.nativeElement.querySelector( '.row-action' ) ).toBeNull();
	} );
	//a Replace and a Delete on a value of a node this user may write, and none on another node's, a bound's or a timeless one's,
	//nor on a push's, whose time a Date cut to the millisecond (historian-web-edits #1)
	it( 'offers a Replace and a Delete on the values of the editable nodes alone', ()=>{
		const t = render( [at(A, 500, 5, {sourceTime: undefined}), at(A, 1000, 1), at(B, 2000, 2), at(A, 3000, 3, {bound: true}), {...at(A, 0, 4), source: null, sourceTime: undefined}], {editable: [A]} );
		expect( t.headers().at( -1 ) ).toBe( '' );
		expect( t.headers() ).toHaveLength( 7 );
		const actions = ( row:number )=>[...t.fixture.nativeElement.querySelectorAll( 'mat-row' )[row].querySelectorAll( '.row-action' )].map( b=>b.getAttribute( 'aria-label' ) );
		expect( actions( 0 ) ).toEqual( [] );//the bound at 3000
		expect( actions( 1 ) ).toEqual( [] );//B's
		expect( actions( 2 ) ).toEqual( ['Replace this value', 'Delete this value'] );//A at 1000
		expect( actions( 3 ) ).toEqual( [] );//a push
		expect( actions( 4 ) ).toEqual( [] );//no time
		const replaced:HistValue[] = [], removed:HistValue[] = [];
		t.table.replace.subscribe( v=>replaced.push( v ) );
		t.table.remove.subscribe( v=>removed.push( v ) );
		const [replace, remove] = t.fixture.nativeElement.querySelectorAll( 'mat-row' )[2].querySelectorAll( '.row-action' );
		replace.click();
		remove.click();
		expect( replaced.map( v=>v.source?.getTime() ) ).toEqual( [1000] );
		expect( removed.map( v=>v.source?.getTime() ) ).toEqual( [1000] );
	} );
} );