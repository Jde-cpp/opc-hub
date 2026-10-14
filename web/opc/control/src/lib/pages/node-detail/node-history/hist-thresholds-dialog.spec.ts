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
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { HistMemberThresholds, HistThresholdOverride, HistThresholds } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { EAccess, ETypes } from '../../../model/types';
import { HistThresholdsDialog, HistThresholdsDialogData, ownFields, thresholdChanges, thresholdProblem } from './hist-thresholds-dialog';

const settle = ()=>new Promise( r=>setTimeout(r) );
const Rpm = new Variable( <any>{ns:2, i:6022, name: 'rpm', browse: {ns:2, name: 'rpm'}, dataType: {i: ETypes.Double}, historizing: true, userAccessLevel: EAccess.All} );
//Pump2's rpm, from the template:  its own deviation and format, the template's min interval, the group's max interval, the
//default stepped.  And the server's State, added by hand, an enumeration.
const rpm = ():HistMemberThresholds=>({ id: 7, node: Rpm.nodeId, templateNode: 3, path: [{nsu: 'urn:jde:pumps', name: 'motorRpm'}], enumeration: false, thresholds: [
	{ name: 'exceptionDeviation', value: 5, source: 'node' }, { name: 'exceptionDeviationFormat', value: 1, source: 'node' }, { name: 'minTimeInterval', value: 1000, source: 'template' },
	{ name: 'maxTimeInterval', value: 60000, source: 'group' }, { name: 'stepped', value: true, source: 'default' }
] });
const state = ():HistMemberThresholds=>({ id: 8, node: new NodeId( {ns: 0, i: 2259} ), enumeration: true, thresholds: [
	{ name: 'exceptionDeviation', value: 2, source: 'group' }, { name: 'exceptionDeviationFormat', value: 0, source: 'default' }, { name: 'minTimeInterval', value: 0, source: 'default' },
	{ name: 'maxTimeInterval', value: 60000, source: 'group' }, { name: 'stepped', value: true, source: 'default' }
] });
//The dialog over a stub gateway:  `lists` is what each load answered, `saves` what was sent, and the next load's or save's
//refusal is `fail`.
const open = ( list:HistThresholds, data:Partial<HistThresholdsDialogData>={} )=>{
	const saves:{id:number; changes:HistThresholdOverride}[] = [];
	let loads = 0;
	const y = {
		list, fail: <Error|undefined>undefined,
		ref: { disableClose: <boolean|undefined>undefined, close: ()=>{} }
	};
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: y.ref },
		{ provide: MAT_DIALOG_DATA, useValue: <HistThresholdsDialogData>{ group: 3, name: 'Pumps', nodes: [Rpm],
			load: async ()=>{ ++loads; if( y.fail ){ const e = y.fail; y.fail = undefined; throw e; } return y.list; },
			save: async ( id:number, changes:HistThresholdOverride )=>{ saves.push( {id, changes} ); if( y.fail ){ const e = y.fail; y.fail = undefined; throw e; } },
			...data } }
	]});
	const fixture = TestBed.createComponent( HistThresholdsDialog );
	fixture.detectChanges();
	return { fixture, dialog: fixture.componentInstance, saves, loads: ()=>loads, set list( l:HistThresholds ){ y.list = l; }, set fail( e:Error ){ y.fail = e; },
		//each cell's spans - a threshold's value and its source - joined by a space
		cells: ( row:number )=>[...fixture.nativeElement.querySelectorAll( 'mat-row' )[row].querySelectorAll( 'mat-cell' )].map( ( c:Element )=>[...c.querySelectorAll( 'span' )].map( s=>s.textContent!.trim() ).filter( t=>t.length ).join( ' ' ) ),
		settle: async ()=>{ await settle(); fixture.detectChanges(); await settle(); fixture.detectChanges(); } };
};

describe( 'threshold fields', ()=>{
	it( "holds the member's own values and leaves what it inherits blank", ()=>{
		expect( ownFields( rpm() ) ).toEqual( {exceptionDeviation: '5', exceptionDeviationFormat: '1', minTimeInterval: '', maxTimeInterval: '', stepped: ''} );
	} );
	//a column typed overrides, one cleared sends null, and one left as it was isn't sent - a blank that was blank still inherits
	it( 'sends what changed, null for a clear, and nothing for a field left as it was', ()=>{
		const fields = { ...ownFields( rpm() ), exceptionDeviation: '', minTimeInterval: '250', stepped: 'false' };
		expect( thresholdChanges( rpm(), fields ) ).toEqual( {exceptionDeviation: null, minTimeInterval: 250, stepped: false} );
		expect( thresholdChanges( rpm(), ownFields( rpm() ) ) ).toEqual( {} );
		expect( thresholdChanges( rpm(), {...ownFields( rpm() ), exceptionDeviation: ' 5 '} ) ).toEqual( {} );
		expect( thresholdChanges( rpm(), {...ownFields( rpm() ), exceptionDeviationFormat: '3'} ) ).toEqual( {exceptionDeviationFormat: 3} );
	} );
	it( 'refuses a negative, a format out of range, and stepped that is neither word', ()=>{
		expect( thresholdProblem( ownFields( rpm() ) ) ).toBeUndefined();
		expect( thresholdProblem( {...ownFields( rpm() ), minTimeInterval: '-1'} ) ).toBe( 'Min interval is a number, 0 or more.' );
		expect( thresholdProblem( {...ownFields( rpm() ), exceptionDeviationFormat: '4'} ) ).toBe( 'The format is one of the four.' );
		expect( thresholdProblem( {...ownFields( rpm() ), stepped: 'yes'} ) ).toBe( 'Stepped is true or false.' );
		expect( thresholdChanges( rpm(), {...ownFields( rpm() ), minTimeInterval: '-1'} ) ).toEqual( {} );
	} );
} );

describe( 'HistThresholdsDialog', ()=>{
	//each member's effective values, each marked with its source;  the page's variable names the member it knows, the path the
	//rest;  an enumeration's deviation cells say n/a
	it( 'lists the members with each threshold and its source', async ()=>{
		const d = open( {update: true, nodes: [rpm(), state()]} );
		await d.settle();
		expect( d.loads() ).toBe( 1 );
		expect( d.dialog.canUpdate() ).toBe( true );
		expect( d.cells( 0 ) ).toEqual( ['rpm', '5 node', 'PercentOfValue node', '1000 template', '60000 group', 'true default', ''] );
		expect( d.cells( 1 ).slice( 1, 3 ) ).toEqual( ['n/a group', 'n/a default'] );
		expect( d.dialog.memberName( state() ) ).toBe( state().node.toString() );
		expect( d.dialog.cellTooltip( state(), 'exceptionDeviation' ) ).toContain( 'not applied' );
	} );
	it( 'offers no edit to a user who may not update the rows', async ()=>{
		const d = open( {update: false, nodes: [rpm()]} );
		await d.settle();
		expect( d.fixture.nativeElement.querySelector( 'button[aria-label="Edit this member\'s thresholds"]' ) ).toBeNull();
		expect( d.dialog.displayedColumns() ).not.toContain( 'actions' );
	} );
	//an edit sends the row's id with the columns changed, null for one cleared, then asks for the list again, so the cells show
	//what the gateway resolved
	it( 'saves an override and a clear, and reads the list again', async ()=>{
		const d = open( {update: true, nodes: [rpm()]} );
		await d.settle();
		d.dialog.edit( rpm() );
		expect( d.dialog.changed() ).toBe( false );
		d.dialog.setField( 'exceptionDeviation', '' );
		d.dialog.setField( 'maxTimeInterval', '30000' );
		expect( d.dialog.changes() ).toEqual( {exceptionDeviation: null, maxTimeInterval: 30000} );
		expect( d.dialog.inheritsHint( 'maxTimeInterval' ) ).toBe( "Inherits 60000 from the group's." );
		d.list = {update: true, nodes: [{...rpm(), thresholds: rpm().thresholds.map( t=>t.name=='maxTimeInterval' ? {...t, value: 30000, source: 'node'} : t.name=='exceptionDeviation' ? {...t, value: 2, source: 'group'} : t )}]};
		await d.dialog.apply();
		await d.settle();
		expect( d.saves ).toEqual( [{id: 7, changes: {exceptionDeviation: null, maxTimeInterval: 30000}}] );
		expect( d.loads() ).toBe( 2 );
		expect( d.dialog.editing() ).toBeUndefined();
		expect( d.cells( 0 ).slice( 1, 5 ) ).toEqual( ['2 group', 'PercentOfValue node', '1000 template', '30000 node'] );
	} );
	it( "shows the gateway's refusal and keeps the edit for another go", async ()=>{
		const d = open( {update: true, nodes: [rpm()]} );
		await d.settle();
		d.dialog.edit( rpm() );
		d.dialog.setField( 'minTimeInterval', '250' );
		d.fail = new Error( 'minTimeInterval must be milliseconds, 0 or more, not -1.' );
		await d.dialog.apply();
		await d.settle();
		expect( d.saves ).toHaveLength( 1 );
		expect( d.dialog.error() ).toContain( 'minTimeInterval must be' );
		expect( d.dialog.editing()?.id ).toBe( 7 );
		expect( d.dialog.fields().minTimeInterval ).toBe( '250' );
		expect( d.loads() ).toBe( 1 );
	} );
	it( 'shows why the list could not be read', async ()=>{
		const d = open( {update: true, nodes: []}, { load: async ()=>{ throw new Error( "Group 3 isn't one this gateway historizes." ); } } );
		await d.settle();
		expect( d.dialog.error() ).toContain( "isn't one this gateway historizes" );
		expect( d.dialog.rows() ).toEqual( [] );
	} );
} );