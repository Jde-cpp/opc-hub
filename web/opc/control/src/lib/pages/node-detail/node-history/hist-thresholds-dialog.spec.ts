import { TestBed } from '@angular/core/testing';
import { MAT_DIALOG_DATA, MatDialogRef } from '@angular/material/dialog';
import { HistBand, HistMemberThresholds, HistThresholdOverride, histThresholdProblem, HistThresholds } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { ExNodeId } from '../../../model/ex-node-id';
import { EAccess, ETypes } from '../../../model/types';
import { HistThresholdsDialog, HistThresholdsDialogData, ownFields, thresholdChanges } from './hist-thresholds-dialog';
import { flush } from 'jde-spa/testing';
import { variable } from 'jde-opc/testing';

const Rpm = variable( 6022, 'rpm', {dataType: {i: ETypes.Double}, historizing: true, userAccessLevel: EAccess.All} );
//Pump2's rpm, from the template:  its own deviation and format, the template's min interval, the group's max interval, the
//default stepped.  And the server's State, added by hand, an enumeration.
const rpm = ():HistMemberThresholds=>({ id: 7, node: new ExNodeId( {ns: 2, i: 6022, nsu: 'urn:jde:pumps'} ), templateNode: 3, path: [{nsu: 'urn:jde:pumps', name: 'motorRpm'}], enumeration: false, thresholds: [
	{ name: 'exceptionDeviation', value: 5, source: 'node' }, { name: 'exceptionDeviationFormat', value: 1, source: 'node' }, { name: 'minTimeInterval', value: 1000, source: 'template' },
	{ name: 'maxTimeInterval', value: 60000, source: 'group' }, { name: 'stepped', value: true, source: 'default' }
] });
const state = ():HistMemberThresholds=>({ id: 8, node: new ExNodeId( {i: 2259, nsu: 'http://opcfoundation.org/UA/'} ), enumeration: true, thresholds: [
	{ name: 'exceptionDeviation', value: 2, source: 'group' }, { name: 'exceptionDeviationFormat', value: 0, source: 'default' }, { name: 'minTimeInterval', value: 0, source: 'default' },
	{ name: 'maxTimeInterval', value: 60000, source: 'group' }, { name: 'stepped', value: true, source: 'default' }
] });
//The dialog over a stub gateway:  `list` is what each load answers, `save` takes what was sent, and the next load's or save's
//refusal is `fail`.
const open = ( list:HistThresholds, data:Partial<HistThresholdsDialogData>={} )=>{
	const y = {
		list, fail: <Error|undefined>undefined,
		ref: { disableClose: <boolean|undefined>undefined, close: ()=>{} }
	};
	const refusal = ()=>{ if( y.fail ){ const e = y.fail; y.fail = undefined; throw e; } };
	const load = vi.fn( async ()=>{ refusal(); return y.list; } );
	const save = vi.fn( async ( _id:number, _changes:HistThresholdOverride )=>refusal() );
	TestBed.configureTestingModule({ providers: [
		{ provide: MatDialogRef, useValue: y.ref },
		{ provide: MAT_DIALOG_DATA, useValue: <HistThresholdsDialogData>{ group: 3, name: 'Pumps', nodes: [Rpm], load, save, ...data } }
	]});
	const fixture = TestBed.createComponent( HistThresholdsDialog );
	fixture.detectChanges();
	return { fixture, dialog: fixture.componentInstance, load, save, set list( l:HistThresholds ){ y.list = l; }, set fail( e:Error ){ y.fail = e; },
		//each cell's spans - a threshold's value and its source - joined by a space
		cells: ( row:number )=>[...fixture.nativeElement.querySelectorAll( 'mat-row' )[row].querySelectorAll( 'mat-cell' )].map( ( c:Element )=>[...c.querySelectorAll( 'span' )].map( s=>s.textContent!.trim() ).filter( t=>t.length ).join( ' ' ) ),
		settle: async ()=>{ await flush(); fixture.detectChanges(); await flush(); fixture.detectChanges(); } };
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
		expect( histThresholdProblem( ownFields( rpm() ) ) ).toBeUndefined();
		expect( histThresholdProblem( {...ownFields( rpm() ), minTimeInterval: '-1'} ) ).toBe( 'Min interval is a number, 0 or more.' );
		expect( histThresholdProblem( {...ownFields( rpm() ), exceptionDeviationFormat: '4'} ) ).toBe( 'The format is one of the four.' );
		expect( histThresholdProblem( {...ownFields( rpm() ), stepped: 'yes'} ) ).toBe( 'Stepped is true or false.' );
		expect( thresholdChanges( rpm(), {...ownFields( rpm() ), minTimeInterval: '-1'} ) ).toEqual( {} );
	} );
} );

describe( 'HistThresholdsDialog', ()=>{
	//each member's effective values, each marked with its source;  the page's variable names the member it knows, the path the
	//rest;  an enumeration's deviation cells say n/a
	it( 'lists the members with each threshold and its source', async ()=>{
		const d = open( {update: true, nodes: [rpm(), state()]} );
		await d.settle();
		expect( d.load ).toHaveBeenCalledTimes( 1 );
		expect( d.dialog.canUpdate() ).toBe( true );
		expect( d.cells( 0 ) ).toEqual( ['rpm', '5 node', 'PercentOfValue node', '1000 template', '60000 group', 'true default', ''] );
		expect( d.cells( 1 ).slice( 1, 3 ) ).toEqual( ['n/a group', 'n/a default'] );
		expect( d.dialog.memberName( state() ) ).toBe( state().node.toString() );
		expect( d.dialog.cellTooltip( state(), 'exceptionDeviation' ) ).toContain( 'not applied' );
	} );
	//historian-threshold-overrides #2:  before the collector has a member's index the gateway sends its URI alone, which kept as a
	//NodeId was namespace 0's:  shown without the URI, and named after the page's namespace-0 variable with the same number
	it( "names a member the collector hasn't indexed by its URI, not by a namespace-0 variable", async ()=>{
		const State = new Variable( <any>{ns: 0, i: 2259, name: 'State', browse: {ns: 0, name: 'State'}, dataType: {i: ETypes.Int32}, historizing: true, userAccessLevel: EAccess.All} );
		const unindexed = {...state(), id: 9, node: new ExNodeId( {i: 2259, nsu: 'urn:jde:pumps'} )};
		const d = open( {update: true, nodes: [state(), unindexed]}, {nodes: [Rpm, State]} );
		await d.settle();
		expect( d.dialog.memberName( state() ) ).toBe( 'State' );//namespace 0's index needs no server
		expect( d.dialog.memberName( unindexed ) ).toBe( '{"nsu":"urn:jde:pumps","i":2259}' );
		expect( d.dialog.memberDetail( unindexed ) ).toContain( 'Node {"nsu":"urn:jde:pumps","i":2259}.' );
		expect( d.cells( 1 )[0] ).toBe( '{"nsu":"urn:jde:pumps","i":2259}' );
	} );
	//historian-threshold-overrides #4:  a percent band the node has no range for isn't applied, though its thresholds look like any
	//other's;  the gateway's band says so, as it says a band waits for the server, where there is one
	it( "marks a band the group doesn't apply, and one that waits", async ()=>{
		const percent = ( band:HistBand, format:number ):HistMemberThresholds=>({...rpm(), band, enumeration: band=='waiting' ? undefined : false, thresholds: rpm().thresholds.map( t=>t.name=='exceptionDeviationFormat' ? {...t, value: format} : t )});
		const eu = percent( 'noRange', 3 );
		const d = open( {update: true, nodes: [eu]} );
		await d.settle();
		expect( d.dialog.unapplied( eu, 'exceptionDeviation' ) ).toBe( true );
		expect( d.dialog.unapplied( eu, 'exceptionDeviationFormat' ) ).toBe( true );
		expect( d.dialog.unapplied( eu, 'minTimeInterval' ) ).toBe( false );
		expect( d.dialog.moot( eu, 'exceptionDeviation' ) ).toBe( false );//still editable:  a format or a deviation can put it right
		expect( d.cells( 0 ).slice( 1, 3 ) ).toEqual( ['5 node', 'PercentOfEURange node'] );
		expect( d.fixture.nativeElement.querySelectorAll( 'mat-row' )[0].querySelectorAll( '.cell-value.moot' ) ).toHaveLength( 2 );
		expect( d.fixture.nativeElement.querySelector( 'mat-icon.member-flag' )?.textContent ).toContain( 'warning' );
		expect( d.dialog.cellTooltip( eu, 'exceptionDeviation' ) ).toBe( "From this member's own.  Not applied - the node has no usable EURange, so every change is stored." );
		expect( d.dialog.cellTooltip( percent( 'noRange', 2 ), 'exceptionDeviationFormat' ) ).toContain( 'no usable InstrumentRange' );
		expect( d.dialog.cellTooltip( percent( 'applied', 1 ), 'exceptionDeviation' ) ).toBe( "From this member's own." );
		expect( d.dialog.cellTooltip( percent( 'waiting', 3 ), 'exceptionDeviation' ) ).toContain( 'The band waits' );
		expect( d.dialog.cellTooltip( {...percent( 'none', 1 ), enumeration: undefined}, 'exceptionDeviation' ) ).not.toContain( 'waits' );
	} );
	//the editor is the pages' shared one (historian-group-template-admin #8), given the member's moot fields and what each blank one
	//inherits:  an enumeration's deviation and its format are off, and say why
	it( "edits through the shared editor, an enumeration's deviation off", async ()=>{
		const d = open( {update: true, nodes: [state()]} );
		await d.settle();
		d.dialog.edit( state() );
		d.fixture.detectChanges();
		expect( d.dialog.mootFields() ).toEqual( ['exceptionDeviation', 'exceptionDeviationFormat'] );
		const editor = <HTMLElement>d.fixture.nativeElement.querySelector( 'section.editor hist-threshold-fields' );
		expect( editor ).not.toBeNull();
		const inputs = [...editor.querySelectorAll( 'input' )];//the deviation and the two intervals
		expect( inputs.map( i=>i.disabled ) ).toEqual( [true, false, false] );
		expect( editor.querySelector( 'mat-hint' )?.textContent ).toContain( 'Not applied to an enumeration.' );
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
		d.dialog.fields.update( f=>({...f, exceptionDeviation: '', maxTimeInterval: '30000'}) );
		expect( d.dialog.changes() ).toEqual( {exceptionDeviation: null, maxTimeInterval: 30000} );
		expect( d.dialog.inheritsHint( 'maxTimeInterval' ) ).toBe( "Inherits 60000 from the group's." );
		d.list = {update: true, nodes: [{...rpm(), thresholds: rpm().thresholds.map( t=>t.name=='maxTimeInterval' ? {...t, value: 30000, source: 'node'} : t.name=='exceptionDeviation' ? {...t, value: 2, source: 'group'} : t )}]};
		await d.dialog.apply();
		await d.settle();
		expect( d.save.mock.calls ).toEqual( [[7, {exceptionDeviation: null, maxTimeInterval: 30000}]] );
		expect( d.load ).toHaveBeenCalledTimes( 2 );
		expect( d.dialog.editing() ).toBeUndefined();
		expect( d.cells( 0 ).slice( 1, 5 ) ).toEqual( ['2 group', 'PercentOfValue node', '1000 template', '30000 node'] );
	} );
	//historian-threshold-overrides #6:  a number input reports text it can't parse as '', which the binding wrote back over what was
	//typed - '.' on the way to '.5' wiped, and the column cleared meanwhile
	it( 'keeps a partial number as typed, and sends nothing until it is one', async ()=>{
		const d = open( {update: true, nodes: [rpm()]} );
		await d.settle();
		d.dialog.edit( rpm() );
		d.fixture.detectChanges();
		const input = <HTMLInputElement>d.fixture.nativeElement.querySelector( 'section.editor input' );
		expect( input.value ).toBe( '5' );
		const type = async ( text:string )=>{ input.value = text; input.dispatchEvent( new Event('input') ); d.fixture.detectChanges(); await flush(); };
		await type( '.' );
		expect( input.value ).toBe( '.' );
		expect( d.dialog.problem() ).toBe( 'Deviation is a number, 0 or more.' );
		expect( d.dialog.changes() ).toEqual( {} );
		await type( '.5' );
		expect( input.value ).toBe( '.5' );
		expect( d.dialog.changes() ).toEqual( {exceptionDeviation: 0.5} );
	} );
	it( "shows the gateway's refusal and keeps the edit for another go", async ()=>{
		const d = open( {update: true, nodes: [rpm()]} );
		await d.settle();
		d.dialog.edit( rpm() );
		d.dialog.fields.update( f=>({...f, minTimeInterval: '250'}) );
		d.fail = new Error( 'minTimeInterval must be milliseconds, 0 or more, not -1.' );
		await d.dialog.apply();
		await d.settle();
		expect( d.save ).toHaveBeenCalledTimes( 1 );
		expect( d.dialog.error() ).toContain( 'minTimeInterval must be' );
		expect( d.dialog.editing()?.id ).toBe( 7 );
		expect( d.dialog.fields().minTimeInterval ).toBe( '250' );
		expect( d.load ).toHaveBeenCalledTimes( 1 );
	} );
	it( 'shows why the list could not be read', async ()=>{
		const d = open( {update: true, nodes: []}, { load: async ()=>{ throw new Error( "Group 3 isn't one this gateway historizes." ); } } );
		await d.settle();
		expect( d.dialog.error() ).toContain( "isn't one this gateway historizes" );
		expect( d.dialog.rows() ).toEqual( [] );
	} );
} );